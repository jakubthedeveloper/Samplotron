#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "pins.h"

class InputKeys {
 public:
  using OnPressCallback = void (*)(int keyIndex, void *context);

  void begin();
  void update(OnPressCallback callback, void *context);

 private:
  static constexpr int kNumKeys = 2;
  static constexpr uint8_t kDebounceMs = 35;

  const int pins_[kNumKeys] = {Pins::KEY1, Pins::KEY3};
  int lastReadState_[kNumKeys] = {HIGH, HIGH};
  int stableState_[kNumKeys] = {HIGH, HIGH};
  unsigned long lastDebounceMs_[kNumKeys] = {0, 0};
};
