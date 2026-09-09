#include <unity.h>
#include <array>
#include <cmath>
#include <vector>
#include "sampler_mixer.h"
#include "../../src/sampler_mixer.cpp"
#include "../../src/budgeted_audio_output.cpp"

using namespace AudioInternal;
using Stereo = std::array<int16_t, 2>;
namespace {
class Sink : public AudioOutput {
 public:
  int starts = 0;
  int capacity = 0;
  bool failStart = false;
  std::vector<Stereo> samples;
  bool begin() override { ++starts; return !failStart; }
  bool ConsumeSample(int16_t sample[2]) override {
    if (!capacity) return false;
    --capacity;
    samples.push_back({sample[0], sample[1]});
    return true;
  }
};

std::vector<Stereo> render(const std::vector<std::vector<Stereo>> &voices,
                           bool blocked = false, float volume = 1.0f) {
  Sink sink;
  SamplerMixer mixer(512, &sink);
  std::vector<SamplerMixerInput *> inputs;
  for (size_t v = 0; v < voices.size(); ++v) {
    auto *input = mixer.NewInput();
    TEST_ASSERT_NOT_NULL(input);
    TEST_ASSERT_TRUE(input->SetGain(volume));
    TEST_ASSERT_TRUE(input->begin());
    inputs.push_back(input);
  }
  const size_t count = voices[0].size();
  for (size_t t = 0; t < count; ++t) {
    sink.capacity = blocked && t % 11 < 5 ? 0 : 1000;
    for (size_t v = 0; v < voices.size(); ++v) {
      Stereo sample = voices[v][t];
      TEST_ASSERT_TRUE(inputs[v]->ConsumeSample(sample.data()));
      TEST_ASSERT_EQUAL_INT(voices[v][t][0], sample[0]);
      TEST_ASSERT_EQUAL_INT(voices[v][t][1], sample[1]);
    }
    mixer.loop();
  }
  sink.capacity = 0;
  for (auto *input : inputs) input->stop();
  sink.capacity = static_cast<int>(count + SamplerMixer::kLookaheadSamples - sink.samples.size());
  mixer.loop();
  TEST_ASSERT_EQUAL_UINT(count + SamplerMixer::kLookaheadSamples, sink.samples.size());
  TEST_ASSERT_EQUAL_INT(1, sink.starts);
  for (auto *input : inputs) delete input;
  return {sink.samples.begin() + SamplerMixer::kLookaheadSamples, sink.samples.end()};
}

void test_single_voice_unity_and_low_volume() {
  std::vector<Stereo> x;
  for (int i = 0; i < 1024; ++i) {
    const int16_t value = static_cast<int16_t>(32767 * std::sin(i * 0.11));
    x.push_back({value, static_cast<int16_t>(-value)});
  }
  x[300] = {32767, -32767};
  auto y = render({x});
  for (size_t i = 0; i < x.size(); ++i) {
    TEST_ASSERT_EQUAL_INT(x[i][0], y[i][0]);
    TEST_ASSERT_EQUAL_INT(x[i][1], y[i][1]);
  }
  y = render({std::vector<Stereo>(200, {10000, -10000})}, false, 0.05f);
  TEST_ASSERT_EQUAL_INT(500, y[100][0]);
  TEST_ASSERT_EQUAL_INT(-500, y[100][1]);
}

void test_sum_below_ceiling_is_unchanged_and_silence_does_not_duck() {
  auto y = render({std::vector<Stereo>(200, {10000, -5000}),
                   std::vector<Stereo>(200, {5000, -10000}),
                   std::vector<Stereo>(200, {0, 0})});
  TEST_ASSERT_EQUAL_INT(15000, y[100][0]);
  TEST_ASSERT_EQUAL_INT(-15000, y[100][1]);
}

void test_wide_sum_limits_before_pcm_and_links_channels() {
  auto y = render({std::vector<Stereo>(200, {20000, 5000}),
                   std::vector<Stereo>(200, {20000, 5000})});
  TEST_ASSERT_INT_WITHIN(1, 32767, y[100][0]);
  TEST_ASSERT_INT_WITHIN(1, 8192, y[100][1]);
  y = render({std::vector<Stereo>(200, {30000, -30000}),
              std::vector<Stereo>(200, {-30000, 30000})});
  TEST_ASSERT_EQUAL_INT(0, y[100][0]);
  TEST_ASSERT_EQUAL_INT(0, y[100][1]);
}

void test_32_full_scale_voices_and_rejected_output_are_identical() {
  std::vector<std::vector<Stereo>> voices(32);
  uint32_t random = 12345;
  for (int v = 0; v < 32; ++v) {
    for (int t = 0; t < 2048; ++t) {
      random = random * 1664525U + 1013904223U;
      const int16_t value = t < 512 ? 32767 : t < 1024 ? -32768 : static_cast<int16_t>(random >> 16);
      voices[v].push_back({value, static_cast<int16_t>(value / 2)});
    }
  }
  auto normal = render(voices);
  auto blocked = render(voices, true);
  for (size_t i = 0; i < normal.size(); ++i) {
    TEST_ASSERT_INT_WITHIN(32767, 0, normal[i][0]);
    TEST_ASSERT_EQUAL_INT(normal[i][0], blocked[i][0]);
    TEST_ASSERT_EQUAL_INT(normal[i][1], blocked[i][1]);
  }
  TEST_ASSERT_INT_WITHIN(1, 32767, normal[300][0]);
  TEST_ASSERT_INT_WITHIN(1, -32767, normal[800][0]);
}

void test_aligned_full_scale_sines_remain_bounded_without_flat_tops() {
  std::vector<std::vector<Stereo>> voices(32);
  for (int t = 0; t < 4096; ++t) {
    const int16_t value = static_cast<int16_t>(32767 * std::sin(t * 0.14247586));
    for (auto &voice : voices) voice.push_back({value, value});
  }
  const auto y = render(voices);
  int nearPeakSamples = 0;
  double squaredError = 0;
  double clippedSquaredError = 0;
  // Include the first attack and the final delayed frames: excluding either
  // hides limiter errors at start-up or when future input becomes silence.
  for (size_t i = 0; i < y.size(); ++i) {
    const int reference = voices[0][i][0];  // Ideal gain for 32 aligned voices: 1/32.
    const int error = y[i][0] - reference;
    squaredError += static_cast<double>(error) * error;
    TEST_ASSERT_INT_WITHIN(128, reference, y[i][0]);
    TEST_ASSERT_EQUAL_INT(y[i][0], y[i][1]);
    if (std::abs(y[i][0]) >= 32760) ++nearPeakSamples;
    if (i > 0) {
      TEST_ASSERT_FALSE(std::abs(y[i][0]) == 32767 && y[i][0] == y[i-1][0]);
    }
    // Negative control: a prematurely clipped 32-voice sum must fail the same
    // shape-error criterion even though all its samples fit within PCM16.
    const int clipped = std::max(-32767, std::min(32767, reference * 32));
    const int clippedError = clipped - reference;
    clippedSquaredError += static_cast<double>(clippedError) * clippedError;
  }
  TEST_ASSERT_LESS_THAN(400, nearPeakSamples);
  TEST_ASSERT_TRUE(std::sqrt(squaredError / y.size()) < 64.0);
  TEST_ASSERT_TRUE(std::sqrt(clippedSquaredError / y.size()) > 10000.0);
}

void test_lookahead_and_release_on_sudden_overlap() {
  std::vector<Stereo> a(5000, {10000, 10000}), b(5000, {0, 0});
  b[200] = {30000, 30000};
  auto y = render({a, b});
  TEST_ASSERT_EQUAL_INT(10000, y[100][0]);
  TEST_ASSERT_LESS_THAN(y[150][0], y[190][0]);
  TEST_ASSERT_INT_WITHIN(1, 32767, y[200][0]);
  TEST_ASSERT_LESS_THAN(9000, y[201][0]);
  TEST_ASSERT_GREATER_THAN(y[201][0], y[4000][0]);
  TEST_ASSERT_LESS_THAN(10001, y[4000][0]);
}

void test_idle_reuse_and_tail_keep_one_sink() {
  Sink sink;
  SamplerMixer mixer(512, &sink);
  auto *input = mixer.NewInput();
  TEST_ASSERT_TRUE(input->begin());
  input->stop();
  sink.capacity = 1200;
  mixer.loop();
  for (int replay = 0; replay < 3; ++replay) {
    sink.samples.clear();
    TEST_ASSERT_TRUE(input->begin());
    int16_t sample[2] = {1234, -1234};
    TEST_ASSERT_TRUE(input->ConsumeSample(sample));
    input->stop();
    sink.capacity = 100;
    mixer.loop();
    TEST_ASSERT_EQUAL_INT(1234, sink.samples[64][0]);
    TEST_ASSERT_EQUAL_INT(-1234, sink.samples[64][1]);
    for (int i = 0; i < 100; ++i) if (i != 64) TEST_ASSERT_EQUAL_INT(0, sink.samples[i][0]);
  }
  TEST_ASSERT_EQUAL_INT(1, sink.starts);
  TEST_ASSERT_FALSE(input->SetRate(48000));
  TEST_ASSERT_TRUE(input->SetRate(44100));
  delete input;
}

void test_rejected_fade_sample_preserves_input_budget_and_envelope() {
  Sink normal, blocked;
  BudgetedAudioOutput first(&normal), second(&blocked);
  first.resetBudget(45); second.resetBudget(45);
  first.beginFadeOut(1000); second.beginFadeOut(1000);
  for (int i = 0; i < 45; ++i) {
    int16_t sample[2] = {30000, -30000};
    normal.capacity = 1;
    TEST_ASSERT_TRUE(first.ConsumeSample(sample));
    blocked.capacity = 0;
    for (int retry = 0; retry < 10; ++retry) {
      TEST_ASSERT_FALSE(second.ConsumeSample(sample));
      TEST_ASSERT_FALSE(second.isFadeOutComplete());
      TEST_ASSERT_EQUAL_INT(30000, sample[0]);
      TEST_ASSERT_EQUAL_INT(-30000, sample[1]);
    }
    blocked.capacity = 1;
    TEST_ASSERT_TRUE(second.ConsumeSample(sample));
    TEST_ASSERT_EQUAL_INT(normal.samples.back()[0], blocked.samples.back()[0]);
    TEST_ASSERT_EQUAL_INT(normal.samples.back()[1], blocked.samples.back()[1]);
  }
  TEST_ASSERT_TRUE(first.isFadeOutComplete());
  TEST_ASSERT_TRUE(second.isFadeOutComplete());
}

void test_capacity_and_failed_start() {
  Sink sink;
  SamplerMixer mixer(512, &sink);
  SamplerMixerInput *inputs[32];
  for (auto &input : inputs) { input = mixer.NewInput(); TEST_ASSERT_NOT_NULL(input); }
  TEST_ASSERT_NULL(mixer.NewInput());
  sink.failStart = true;
  TEST_ASSERT_FALSE(inputs[0]->begin());
  sink.failStart = false;
  TEST_ASSERT_TRUE(inputs[0]->begin());
  TEST_ASSERT_EQUAL_INT(2, sink.starts);
  for (auto *input : inputs) delete input;
}
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single_voice_unity_and_low_volume);
  RUN_TEST(test_sum_below_ceiling_is_unchanged_and_silence_does_not_duck);
  RUN_TEST(test_wide_sum_limits_before_pcm_and_links_channels);
  RUN_TEST(test_32_full_scale_voices_and_rejected_output_are_identical);
  RUN_TEST(test_aligned_full_scale_sines_remain_bounded_without_flat_tops);
  RUN_TEST(test_lookahead_and_release_on_sudden_overlap);
  RUN_TEST(test_idle_reuse_and_tail_keep_one_sink);
  RUN_TEST(test_capacity_and_failed_start);
  RUN_TEST(test_rejected_fade_sample_preserves_input_budget_and_envelope);
  return UNITY_END();
}
