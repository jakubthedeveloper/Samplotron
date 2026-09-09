#include <unity.h>
#include "Arduino.h"
#include "pins.h"

namespace {
int amplifierState = LOW;
constexpr int OUTPUT = 1;
void pinMode(int, int) {}
void digitalWrite(int pin, int value) {
  if (pin == Pins::PA_EN) amplifierState = value;
}
int constrain(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}
}

// Clock-routing register access has no host equivalent.
#define WRITE_PERI_REG(reg, value) ((void)0)
#include "../../src/codec_es8388.cpp"

void setUp() {
  gCodecWire = TwoWire(1);
  gCodecReady = false;
  amplifierState = LOW;
}
void tearDown() {}

void test_startup_preserves_control_bits_while_muted() {
  TEST_ASSERT_TRUE(CodecES8388::init());
  TEST_ASSERT_EQUAL_HEX8(0x26, gCodecWire.registers[0x19]);
  for (int reg = 0x2E; reg <= 0x31; ++reg) {
    TEST_ASSERT_EQUAL_HEX8(0x1E, gCodecWire.registers[reg]);
  }
  TEST_ASSERT_EQUAL_INT(LOW, amplifierState);
  int controlWrites = 0;
  for (const auto &write : gCodecWire.writes) {
    if (write.first != 0x19) continue;
    ++controlWrites;
    TEST_ASSERT_EQUAL_HEX8(0x26, write.second);
  }
  TEST_ASSERT_GREATER_THAN(0, controlWrites);
}

void test_unmute_preserves_profile_and_can_be_repeated() {
  TEST_ASSERT_TRUE(CodecES8388::init());
  TEST_ASSERT_TRUE(CodecES8388::unmute());
  TEST_ASSERT_EQUAL_HEX8(0x22, gCodecWire.registers[0x19]);
  TEST_ASSERT_EQUAL_INT(LOW, amplifierState);
  TEST_ASSERT_TRUE(CodecES8388::unmute());
  TEST_ASSERT_EQUAL_HEX8(0x22, gCodecWire.registers[0x19]);
}

void test_init_rejects_failed_register_readback() {
  gCodecWire.ignoredWriteRegister = 0x19;
  TEST_ASSERT_FALSE(CodecES8388::init());
  TEST_ASSERT_FALSE(CodecES8388::unmute());
  TEST_ASSERT_EQUAL_INT(LOW, amplifierState);
}

void test_unmute_rejects_lost_control_bits() {
  TEST_ASSERT_TRUE(CodecES8388::init());
  gCodecWire.registers[0x19] = 0x04;
  TEST_ASSERT_FALSE(CodecES8388::unmute());
  TEST_ASSERT_EQUAL_INT(LOW, amplifierState);
}

void test_unmute_rejects_ignored_write() {
  TEST_ASSERT_TRUE(CodecES8388::init());
  gCodecWire.ignoredWriteRegister = 0x19;
  TEST_ASSERT_FALSE(CodecES8388::unmute());
  TEST_ASSERT_EQUAL_INT(LOW, amplifierState);
}

void test_unmute_fails_on_read_error() {
  TEST_ASSERT_TRUE(CodecES8388::init());
  gCodecWire.failRead = true;
  TEST_ASSERT_FALSE(CodecES8388::unmute());
  TEST_ASSERT_EQUAL_INT(LOW, amplifierState);
}

void test_playback_never_enables_inputs_or_speaker_amplifiers() {
  amplifierState = HIGH;
  TEST_ASSERT_TRUE(CodecES8388::init());
  for (int i = 0; i < 2; ++i) {
    TEST_ASSERT_TRUE(CodecES8388::unmute());
    TEST_ASSERT_EQUAL_INT(LOW, amplifierState);
    TEST_ASSERT_EQUAL_HEX8(0x00, gCodecWire.registers[0x02]);
    TEST_ASSERT_EQUAL_HEX8(0xFC, gCodecWire.registers[0x03]);
    TEST_ASSERT_BITS_HIGH(0x04, gCodecWire.registers[0x0F]);
    TEST_ASSERT_BITS_LOW(0x40, gCodecWire.registers[0x27]);
    TEST_ASSERT_BITS_LOW(0x40, gCodecWire.registers[0x2A]);
    TEST_ASSERT_EQUAL_HEX8(0x3C, gCodecWire.registers[0x04]);
  }
  for (const auto &write : gCodecWire.writes) {
    if (write.first == 0x03) TEST_ASSERT_EQUAL_HEX8(0xFC, write.second);
    if (write.first == 0x02) TEST_ASSERT_EQUAL_HEX8(0x00, write.second);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_playback_never_enables_inputs_or_speaker_amplifiers);
  RUN_TEST(test_startup_preserves_control_bits_while_muted);
  RUN_TEST(test_unmute_preserves_profile_and_can_be_repeated);
  RUN_TEST(test_init_rejects_failed_register_readback);
  RUN_TEST(test_unmute_rejects_lost_control_bits);
  RUN_TEST(test_unmute_rejects_ignored_write);
  RUN_TEST(test_unmute_fails_on_read_error);
  return UNITY_END();
}
