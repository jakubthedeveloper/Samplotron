#include "Arduino.h"

namespace {
unsigned long gNowMs = 0;
}  // namespace

HardwareSerial Serial;

unsigned long millis() {
  return gNowMs;
}

void testSetMillis(unsigned long value) {
  gNowMs = value;
}

void testAdvanceMillis(unsigned long delta) {
  gNowMs += delta;
}
