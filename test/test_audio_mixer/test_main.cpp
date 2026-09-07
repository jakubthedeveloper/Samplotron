#include <unity.h>
#include "AudioOutputMixer.h"

// Compile the actual mixer without the library's hardware-specific units,
// following the other native suites' selected-production-unit convention.
#include "AudioOutputMixer.cpp"

namespace {

class CountingSink : public AudioOutput {
 public:
  int starts = 0;
  int stops = 0;
  int capacity = 0;
  int nonzero = 0;
  bool begin() override { ++starts; return true; }
  bool stop() override { ++stops; return true; }
  bool ConsumeSample(int16_t sample[2]) override {
    if (!capacity) return false;
    --capacity;
    if (sample[0] || sample[1]) ++nonzero;
    return true;
  }
};

void test_startup_and_repeated_playback_keep_one_sink_and_idle_silence() {
  CountingSink sink;
  AudioOutputMixer mixer(512, &sink);
  auto *voice = mixer.NewInput();
  TEST_ASSERT_NOT_NULL(voice);
  voice->SetGain(1.0f);
  voice->SetChannels(2);

  // Boot: start through a mixer input, then leave all voices inactive.
  TEST_ASSERT_TRUE(voice->begin());
  TEST_ASSERT_TRUE(voice->stop());
  sink.capacity = 1024;
  mixer.loop();
  TEST_ASSERT_EQUAL_INT(0, sink.capacity);
  TEST_ASSERT_EQUAL_INT(0, sink.nonzero);
  TEST_ASSERT_EQUAL_INT(1, sink.starts);
  TEST_ASSERT_EQUAL_INT(0, sink.stops);

  for (int replay = 0; replay < 3; ++replay) {
    TEST_ASSERT_TRUE(voice->begin());
    TEST_ASSERT_EQUAL_INT(1, sink.starts);  // Playback must not restart I2S.
    int16_t sample[2] = {1000, -1000};
    TEST_ASSERT_TRUE(voice->ConsumeSample(sample));
    sink.capacity = 1;
    mixer.loop();
    TEST_ASSERT_EQUAL_INT(0, sink.capacity);
    TEST_ASSERT_EQUAL_INT(replay + 1, sink.nonzero);
    TEST_ASSERT_TRUE(voice->stop());
    sink.capacity = 1024;
    mixer.loop();
    TEST_ASSERT_EQUAL_INT(0, sink.capacity);
    TEST_ASSERT_EQUAL_INT(replay + 1, sink.nonzero);
    TEST_ASSERT_EQUAL_INT(1, sink.starts);
    TEST_ASSERT_EQUAL_INT(0, sink.stops);
  }
  delete voice;
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_startup_and_repeated_playback_keep_one_sink_and_idle_silence);
  return UNITY_END();
}
