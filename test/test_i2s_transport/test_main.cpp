#include <unity.h>
#include <vector>
#include <algorithm>
#include <cstring>
// Exercise the ESP32 branch of our actual I2S adapter against the driver fake.
#define ESP32 1
#include "../support/arduino_stubs.cpp"
#include "AudioGeneratorWAV.cpp"
#include "../../src/audio_output_chain.cpp"
#include "../../src/pcm_block_buffer.cpp"
using AudioInternal::PcmBlockBuffer;
namespace {
struct Writer {
  std::vector<uint8_t> bytes;
  size_t allowance = 1000000, calls = 0;
  static size_t write(void *p, const uint8_t *data, size_t length) {
    auto &w = *static_cast<Writer *>(p); ++w.calls;
    const size_t n = std::min(length, w.allowance);
    w.bytes.insert(w.bytes.end(), data, data+n); return n;
  }
};
void test_esp32_adapter_packs_full_level_pcm_without_extra_attenuation() {
  FakeI2S::reset();
  AudioInternal::StableAudioOutputI2S out(0, 0, 8, 1);
  TEST_ASSERT_TRUE(out.SetGain(1)); TEST_ASSERT_TRUE(out.SetChannels(2)); TEST_ASSERT_TRUE(out.begin());
  FakeI2S::capacity = 128;
  for (int i = 0; i < 128; ++i) {
    int16_t frame[2] = {32767, -32767};
    TEST_ASSERT_TRUE(out.ConsumeSample(frame));
  }
  TEST_ASSERT_EQUAL_INT(1, FakeI2S::driverCalls);
  TEST_ASSERT_EQUAL_UINT(128, FakeI2S::frames.size());
  for (auto frame : FakeI2S::frames) {
    TEST_ASSERT_EQUAL_INT(32767, frame[0]); TEST_ASSERT_EQUAL_INT(-32767, frame[1]);
  }
}
void test_one_driver_call_per_dma_block_preserves_signed_stereo_pcm() {
  PcmBlockBuffer block; Writer w; std::vector<uint8_t> expected;
  for (int i = 0; i < 4096; ++i) {
    int16_t s[2] = {static_cast<int16_t>(i*31), static_cast<int16_t>(-i*19)};
    for (auto v:s) { expected.push_back(uint16_t(v)&255); expected.push_back(uint16_t(v)>>8); }
    TEST_ASSERT_TRUE(block.consume(s, Writer::write, &w));
  }
  TEST_ASSERT_EQUAL_UINT(32, w.calls); TEST_ASSERT_EQUAL_UINT(0, block.pendingBytes());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), w.bytes.data(), expected.size());
}
void test_short_writes_and_timeouts_retain_exact_byte_suffix() {
  Writer reference, actual; PcmBlockBuffer first, second;
  for (int i = 0; i < 512; ++i) {
    int16_t s[2] = {static_cast<int16_t>(i*53), static_cast<int16_t>(-i*23)};
    TEST_ASSERT_TRUE(first.consume(s, Writer::write, &reference));
    actual.allowance = 0;
    bool accepted = second.consume(s, Writer::write, &actual);
    if (!accepted) {
      for (int retry=0; retry<8; ++retry) TEST_ASSERT_FALSE(second.consume(s, Writer::write, &actual));
      actual.allowance = 7; // Also test an incomplete stereo frame, not just whole frames.
      int attempts = 0;
      while (!accepted && ++attempts < 100) accepted = second.consume(s, Writer::write, &actual);
      TEST_ASSERT_TRUE(accepted);
    }
  }
  // Continuous idle silence submits the final block while retaining the new frame.
  actual.allowance = 1000000; int16_t silence[2] = {};
  TEST_ASSERT_TRUE(second.consume(silence, Writer::write, &actual));
  TEST_ASSERT_EQUAL_UINT(reference.bytes.size(), actual.bytes.size());
  TEST_ASSERT_EQUAL_MEMORY(reference.bytes.data(), actual.bytes.data(), actual.bytes.size());
}
struct ClockedDma {
  double timeUs = 0, nextFrameUs = 1000000.0/44100;
  size_t queued = 1024, underruns = 0, calls = 0;
  void advance(double us) {
    timeUs += us;
    while (nextFrameUs <= timeUs) {
      if (queued) --queued; else ++underruns;
      nextFrameUs += 1000000.0/44100;
    }
  }
  static size_t write(void *context, const uint8_t *, size_t bytes) {
    auto &dma = *static_cast<ClockedDma *>(context); ++dma.calls; dma.advance(6);
    const size_t n = std::min(bytes/4, 1024-dma.queued); dma.queued += n; return n*4;
  }
};
void test_clock_keeps_running_during_processing_and_driver_calls() {
  // Explicit synthetic timing model, NOT an ESP32 speed measurement:
  // processing 19 us/frame + driver overhead 6 us/call. At 44.1 kHz the
  // old one-call-per-frame path misses deadlines although every write succeeds.
  ClockedDma single, batched; PcmBlockBuffer buffer;
  for (int i = 0; i < 30000; ++i) {
    int16_t s[2] = {1000, -1000};
    single.advance(19); ClockedDma::write(&single, reinterpret_cast<uint8_t *>(s), 4);
    batched.advance(19);
    while (!buffer.consume(s, ClockedDma::write, &batched)) {}
  }
  TEST_ASSERT_GREATER_THAN(1000, single.underruns); // Negative control for the clock-aware oracle.
  TEST_ASSERT_EQUAL_UINT(0, batched.underruns);
}
}
void setUp() {} void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_esp32_adapter_packs_full_level_pcm_without_extra_attenuation);
  RUN_TEST(test_one_driver_call_per_dma_block_preserves_signed_stereo_pcm);
  RUN_TEST(test_short_writes_and_timeouts_retain_exact_byte_suffix);
  RUN_TEST(test_clock_keeps_running_during_processing_and_driver_calls);
  return UNITY_END();
}
