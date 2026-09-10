#include <unity.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "../support/arduino_stubs.cpp"
#include "AudioGeneratorWAV.cpp"
#include "../../src/wav_validation.cpp"
#include "../../src/validated_wav_source.cpp"
#include "../../src/sample_library.cpp"
#include "../../src/stream_manager.cpp"
#include "../../src/sampler_mixer.cpp"
#include "../../src/budgeted_audio_output.cpp"
#include "../../src/audio_output_chain.cpp"
#include "../../src/audio_voice_engine.cpp"
#include "../../src/audio.cpp"

namespace {
using Pcm = std::vector<int16_t>;
constexpr int kDelay = AudioInternal::SamplerMixer::kLookaheadSamples + 1; // Decoder's initial pending zero.
Pcm tone(int n, double hz, double amplitude) {
  Pcm pcm(n);
  for (int i = 0; i < n; ++i) {
    // Smooth edges remove clicks already present in the fixture itself.
    const double edge = std::min(1.0, std::max(0.0, std::min((i - 64) / 256.0, (n - 65 - i) / 256.0)));
    const double envelope = 0.5 - 0.5 * std::cos(3.141592653589793 * edge);
    pcm[i] = std::lround(amplitude * envelope * std::sin(i * 6.283185307179586 * hz / 44100));
  }
  return pcm;
}
void saveWav(const char *path, const Pcm &pcm) {
  auto &bytes = FakeSD::files[path]; bytes.resize(44 + pcm.size() * 2);
  WavValidation::pcmHeader(bytes.data(), pcm.size() * 2);
  for (size_t i = 0; i < pcm.size(); ++i) { bytes[44+2*i] = pcm[i]; bytes[45+2*i] = uint16_t(pcm[i]) >> 8; }
}
struct Rig {
  SampleLibrary::Catalog catalog;
  Audio audio;
  Rig() { FakeI2S::reset(); testSetMillis(0); }
  void begin() { SampleLibrary::loadFromSd(catalog); audio.setSampleCatalog(&catalog); TEST_ASSERT_TRUE(audio.begin()); }
  void start(const Pcm &pcm, bool sd, const char *path, int group = -1) {
    if (sd) audio.playSamplePath(path, 100, group);
    else TEST_ASSERT_TRUE(audio.playSampleRam(reinterpret_cast<const uint8_t *>(pcm.data()), pcm.size()*2, 1, 44100, 16, 100, group));
  }
  void advance(int frames, bool blocked = false) {
    const size_t end = FakeI2S::frames.size() + frames;
    int calls = 0;
    while (FakeI2S::frames.size() < end && calls < frames * 20 + 100) {
      const int batch = blocked ? (calls % 5 == 0 ? 0 : 1 + (calls * 17) % 97) : 1;
      FakeI2S::capacity = std::min(size_t(batch), end - FakeI2S::frames.size());
      audio.update();
      testSetMillis(FakeI2S::frames.size() * 1000 / 44100);
      ++calls;
    }
    TEST_ASSERT_EQUAL_UINT(end, FakeI2S::frames.size());
  }
};
int maximumPcmError(const Pcm &reference, const Pcm &candidate) {
  if (reference.size() != candidate.size()) return 65536;
  int worst = 0;
  for (size_t i = 0; i < reference.size(); ++i)
    worst = std::max(worst, std::abs(int(candidate[i]) - reference[i]));
  return worst;
}
void assertReference(const Pcm &reference) {
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(reference.size() + kDelay, FakeI2S::frames.size());
  Pcm actual;
  for (size_t i = 0; i < reference.size(); ++i) actual.push_back(FakeI2S::frames[i+kDelay][0]);
  const bool matches = maximumPcmError(reference, actual) <= 1;
  for (size_t i = 0; i < reference.size(); ++i) {
    if (!matches && std::abs(actual[i] - reference[i]) > 1) {
      char message[160]; std::snprintf(message, sizeof(message), "PCM differs at frame %zu: expected %d, got %d", i, reference[i], FakeI2S::frames[i+kDelay][0]);
      TEST_FAIL_MESSAGE(message);
    }
    TEST_ASSERT_EQUAL_INT(FakeI2S::frames[i+kDelay][0], FakeI2S::frames[i+kDelay][1]);
  }
  TEST_ASSERT_EQUAL_INT(1, FakeI2S::starts); TEST_ASSERT_EQUAL_INT(0, FakeI2S::stops);
}
void test_single_and_simultaneous_voices_match_pcm_through_eof() {
  for (bool sd : {false, true}) for (bool blocked : {false, true}) for (int voices : {1, 2, 8}) {
    FakeSD::files.clear(); std::vector<Pcm> pcm; Pcm sum(6000, 0);
    for (int v = 0; v < voices; ++v) {
      pcm.push_back(tone(4097 + v*113, 137 + v*73, 2500));
      for (size_t i = 0; i < pcm.back().size(); ++i) sum[i] += pcm.back()[i];
      saveWav(("/samples/"+std::to_string(v)+".wav").c_str(), pcm.back());
    }
    Rig rig; rig.begin();
    for (int v = 0; v < voices; ++v) rig.start(pcm[v], sd, ("/samples/"+std::to_string(v)+".wav").c_str());
    rig.advance(sum.size()+kDelay, blocked); assertReference(sum);
    TEST_ASSERT_EQUAL_INT(0, rig.audio.runtimeStats().activeVoices);
  }
}
void test_staggered_mixed_ram_sd_voices_preserve_timeline() {
  for (bool blocked : {false, true}) for (int offset : {1, 63, 96, 511, 977, 3000}) {
    FakeSD::files.clear(); const auto a = tone(4201, 173, 9000), b = tone(2707, 431, 7000);
    saveWav("/samples/b.wav", b); Rig rig; rig.begin();
    rig.start(a, false, ""); rig.advance(offset, blocked); rig.start(b, true, "/samples/b.wav");
    Pcm expected(8000, 0);
    for (size_t i = 0; i < a.size(); ++i) expected[i] += a[i];
    for (size_t i = 0; i < b.size(); ++i) expected[i+offset] += b[i];
    rig.advance(expected.size()+kDelay-offset, blocked); assertReference(expected);
  }
}
void test_retrigger_matches_independent_fade_and_new_voice() {
  for (bool sd : {false, true}) for (bool blocked : {false, true}) {
    FakeSD::files.clear(); const auto pcm = tone(7003, 197, 10000); saveWav("/samples/a.wav", pcm);
    constexpr int trigger = 2003, length = 11000;
    Pcm faded;
    { // Render the old occurrence's explicitly requested 6 ms fade in isolation.
      Rig rig; rig.begin(); rig.start(pcm, sd, "/samples/a.wav"); rig.advance(trigger, blocked);
      rig.audio.fadeOutAllVoices(6000); rig.advance(length+kDelay-trigger, blocked);
      for (int i = 0; i < length; ++i) faded.push_back(FakeI2S::frames[i+kDelay][0]);
    }
    Rig rig; rig.begin(); rig.start(pcm, sd, "/samples/a.wav", 12); rig.advance(trigger, blocked);
    rig.start(pcm, sd, "/samples/a.wav", 12);
    TEST_ASSERT_EQUAL_INT(2, rig.audio.runtimeStats().activeVoices);
    rig.advance(length+kDelay-trigger, blocked);
    for (size_t i = 0; i < pcm.size(); ++i) faded[i+trigger] += pcm[i];
    assertReference(faded); TEST_ASSERT_EQUAL_INT(0, rig.audio.runtimeStats().activeVoices);
  }
}
void test_slot_reuse_after_natural_end_has_no_stale_pcm() {
  FakeSD::files.clear(); const auto a = tone(911, 233, 11000), b = tone(1407, 317, 8000);
  saveWav("/samples/b.wav", b); Rig rig; rig.begin(); Pcm expected(12000, 0);
  for (int t : {0, 2200, 4400, 6600, 8800}) {
    if (t > 0) rig.advance(t - FakeI2S::frames.size(), true);
    rig.start(a, false, "", 10); rig.start(b, true, "/samples/b.wav", 11);
    for (size_t i = 0; i < a.size(); ++i) expected[t+i] += a[i];
    for (size_t i = 0; i < b.size(); ++i) expected[t+i] += b[i];
  }
  rig.advance(expected.size()+kDelay-FakeI2S::frames.size(), true); assertReference(expected);
  TEST_ASSERT_EQUAL_UINT(0, rig.audio.voiceStealCount());
}

int largestStep(size_t first, size_t end) {
  int step = 0;
  for (size_t i = std::max(size_t(1), first); i < end; ++i)
    step = std::max(step, std::abs(int(FakeI2S::frames[i][0])-FakeI2S::frames[i-1][0]));
  return step;
}
void test_nonzero_start_is_ramped_without_reducing_sustain() {
  FakeSD::files.clear(); Pcm pcm(1103, 12000); Rig rig; rig.begin(); rig.start(pcm, false, "");
  rig.advance(1600, true);
  TEST_ASSERT_LESS_THAN(900, largestStep(0, 150));
  TEST_ASSERT_EQUAL_INT(12000, FakeI2S::frames[600][0]);
}
void test_nonzero_eof_is_ramped_without_cutting_other_voice() {
  FakeSD::files.clear(); Pcm pcm(1103, 12000); auto background = tone(3001, 173, 4000);
  saveWav("/samples/a.wav", pcm); Rig rig; rig.begin();
  rig.start(background, false, ""); rig.start(pcm, true, "/samples/a.wav"); rig.advance(3400, true);
  TEST_ASSERT_LESS_THAN(1000, largestStep(1000, 1250));
  for (int i = 1200; i < 3000; ++i) TEST_ASSERT_EQUAL_INT(background[i], FakeI2S::frames[i+kDelay][0]);
}
void test_nonzero_retrigger_start_does_not_click_over_old_tail() {
  FakeSD::files.clear(); Pcm pcm(3001, 12000); Rig rig; rig.begin(); rig.start(pcm, false, "", 7);
  rig.advance(1103, true); rig.start(pcm, false, "", 7); rig.advance(3500, true);
  TEST_ASSERT_LESS_THAN(900, largestStep(1000, 2000));
}

std::vector<std::array<int16_t, 2>> renderOverload(bool blocked, int voices) {
  FakeSD::files.clear(); std::vector<Pcm> pcm;
  for (int v = 0; v < voices; ++v) {
    pcm.push_back(tone(4801 + v * 31, 137 + (v % 5) * 53, 29000));
    saveWav(("/samples/"+std::to_string(v)+".wav").c_str(), pcm.back());
  }
  Rig rig; rig.begin();
  for (int v = 0; v < voices; ++v) {
    const int when = v * 79;
    rig.advance(when - FakeI2S::frames.size(), blocked);
    rig.start(pcm[v], v % 2, ("/samples/"+std::to_string(v)+".wav").c_str());
  }
  rig.advance(12000 + kDelay - FakeI2S::frames.size(), blocked);
  TEST_ASSERT_EQUAL_INT(0, rig.audio.runtimeStats().activeVoices);
  TEST_ASSERT_EQUAL_INT(0, FakeI2S::stops);
  double previousGain = 1; bool previousKnown = false; int limited = 0;
  for (int i = 0; i < 12000; ++i) {
    int wide = 0;
    for (int v = 0; v < voices; ++v) { const int at = i - v * 79; if (at >= 0 && at < int(pcm[v].size())) wide += pcm[v][at]; }
    const int out = FakeI2S::frames[i+kDelay][0];
    TEST_ASSERT_EQUAL_INT(out, FakeI2S::frames[i+kDelay][1]);
    TEST_ASSERT_INT_WITHIN(32767, 0, out);
    TEST_ASSERT_TRUE(std::abs(out) <= std::abs(wide)+1);
    TEST_ASSERT_TRUE(wide >= 0 ? out >= 0 : out <= 0);
    if (std::abs(wide) > 32767) ++limited;
    // Infer the common gain only away from zero crossings, where division is stable.
    // A gain jump larger than the limiter's 32-frame attack cannot be valid;
    // rounding error is at most 1/2000 here. This catches dropouts/spikes under limiting.
    const bool known = std::abs(wide) >= 2000;
    if (known) {
      const double gain = double(out) / wide;
      if (previousKnown) TEST_ASSERT_TRUE(std::abs(gain - previousGain) < 0.033);
      previousGain = gain;
    }
    previousKnown = known;
  }
  TEST_ASSERT_GREATER_THAN(100, limited);
  return FakeI2S::frames;
}
void test_overloaded_staggered_voices_have_continuous_gain_and_survive_backpressure() {
  for (int voices : {2, 8, 32}) {
    const auto reference = renderOverload(false, voices), blocked = renderOverload(true, voices);
    TEST_ASSERT_EQUAL_UINT(reference.size(), blocked.size());
    for (size_t i = 0; i < reference.size(); ++i) TEST_ASSERT_EQUAL_MEMORY(reference[i].data(), blocked[i].data(), 4);
  }
}
void test_continuity_oracle_rejects_drop_repeat_zero_and_spike() {
  // Negative controls: a broken transport can stay below full scale and still click.
  const auto pcm = tone(3001, 317, 12000);
  auto error = [&](const Pcm &candidate) { return maximumPcmError(pcm, candidate); };
  TEST_ASSERT_EQUAL_INT(0, error(pcm));
  auto broken = pcm; broken[1001] = 0; TEST_ASSERT_GREATER_THAN(1, error(broken));
  broken = pcm; broken[1001] += 500; TEST_ASSERT_GREATER_THAN(1, error(broken));
  broken = pcm; broken.insert(broken.begin()+1001, broken[1000]); broken.pop_back(); TEST_ASSERT_GREATER_THAN(1, error(broken));
  broken = pcm; broken.erase(broken.begin()+1001); broken.push_back(0); TEST_ASSERT_GREATER_THAN(1, error(broken));
}

void test_short_and_full_scale_edges_are_identical_under_retries() {
  for (int length : {1, 2, 17, 34, 35, 69, 70, 97, 511, 1501}) for (int level : {-32767, 32767}) {
    std::vector<std::array<int16_t, 2>> reference;
    for (bool blocked : {false, true}) {
      FakeSD::files.clear(); Pcm pcm(length, level); saveWav("/samples/edge.wav", pcm);
      Rig rig; rig.begin(); rig.start(pcm, true, "/samples/edge.wav"); rig.advance(length+kDelay+200, blocked);
      TEST_ASSERT_LESS_THAN(1500, largestStep(0, FakeI2S::frames.size()));
      TEST_ASSERT_EQUAL_INT(0, rig.audio.runtimeStats().activeVoices);
      for (size_t i = length+kDelay; i < FakeI2S::frames.size(); ++i) TEST_ASSERT_EQUAL_INT(0, FakeI2S::frames[i][0]);
      if (!blocked) reference = FakeI2S::frames;
      else for (size_t i = 0; i < reference.size(); ++i) TEST_ASSERT_EQUAL_MEMORY(reference[i].data(), FakeI2S::frames[i].data(), 4);
    }
  }
}
void test_retrigger_fade_lasts_six_ms_after_queued_audio() {
  // Constant PCM exposes the envelope directly, without dividing around tone zero crossings.
  FakeSD::files.clear(); Pcm pcm(4001, 12000); Rig rig; rig.begin(); rig.start(pcm, false, "", 4);
  rig.advance(1301, true); rig.audio.fadeOutAllVoices(AudioInternal::kRetriggerFadeOutUs); rig.advance(3000, true);
  size_t start = 1301;
  while (start < FakeI2S::frames.size() && FakeI2S::frames[start][0] == 12000) ++start;
  // The already queued PCM is preserved; the fade begins at the writer frontier.
  TEST_ASSERT_LESS_THAN(1301 + 512 + kDelay, start);
  size_t end = start;
  while (end < FakeI2S::frames.size() && FakeI2S::frames[end][0] != 0) {
    TEST_ASSERT_TRUE(FakeI2S::frames[end][0] <= FakeI2S::frames[end-1][0]); ++end;
  }
  TEST_ASSERT_INT_WITHIN(1, 265, end-start); // ceil(44100 * 0.006)
  TEST_ASSERT_LESS_THAN(48, largestStep(start, end+1)); // ceil(12000/265) plus PCM rounding.
}

}
void setUp() {} void tearDown() {}
int main() { UNITY_BEGIN(); 
 RUN_TEST(test_single_and_simultaneous_voices_match_pcm_through_eof);
 RUN_TEST(test_staggered_mixed_ram_sd_voices_preserve_timeline);
 RUN_TEST(test_retrigger_matches_independent_fade_and_new_voice);
 RUN_TEST(test_slot_reuse_after_natural_end_has_no_stale_pcm);

 RUN_TEST(test_nonzero_start_is_ramped_without_reducing_sustain);
 RUN_TEST(test_nonzero_eof_is_ramped_without_cutting_other_voice);
 RUN_TEST(test_nonzero_retrigger_start_does_not_click_over_old_tail);

 RUN_TEST(test_overloaded_staggered_voices_have_continuous_gain_and_survive_backpressure);
 RUN_TEST(test_continuity_oracle_rejects_drop_repeat_zero_and_spike);

 RUN_TEST(test_short_and_full_scale_edges_are_identical_under_retries);
 RUN_TEST(test_retrigger_fade_lasts_six_ms_after_queued_audio);
 return UNITY_END(); }
