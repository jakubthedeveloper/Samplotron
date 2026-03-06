#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class InputKeys {
 public:
  using OnPressCallback = void (*)(int keyIndex, void *context);

  void begin();
  void update(OnPressCallback callback, void *context);

 private:
  static constexpr int kNumEncoders = 2;
  static constexpr uint8_t kDebounceMs = 35;
  static constexpr int kEncoderDetentTicks = 4;

  bool ready_ = false;
  uint8_t encoderState_[kNumEncoders] = {0, 0};
  int8_t encoderTicks_[kNumEncoders] = {0, 0};
  int switchStableState_[kNumEncoders] = {HIGH, HIGH};
  int switchLastReadState_[kNumEncoders] = {HIGH, HIGH};
  unsigned long switchLastDebounceMs_[kNumEncoders] = {0, 0};
};
