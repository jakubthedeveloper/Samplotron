#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class Input {
 public:
  enum class EventType : uint8_t {
    LeftRotate,
    LeftClick,
    RightRotate,
    RightClick,
    RightLongPress,
  };

  struct Event {
    EventType type;
    int value;  // Rotation delta for rotate events (+1/-1), otherwise 0.
  };

  using OnEventCallback = void (*)(const Event &event, void *context);

  void begin();
  void update(OnEventCallback callback, void *context);

 private:
  static constexpr int kNumEncoders = 2;
  static constexpr uint8_t kDebounceMs = 35;
  static constexpr int kEncoderDetentTicks = 4;
  static constexpr unsigned long kLongPressMs = 700;

  bool ready_ = false;
  uint8_t encoderState_[kNumEncoders] = {0, 0};
  int8_t encoderTicks_[kNumEncoders] = {0, 0};
  int switchStableState_[kNumEncoders] = {HIGH, HIGH};
  int switchLastReadState_[kNumEncoders] = {HIGH, HIGH};
  unsigned long switchLastDebounceMs_[kNumEncoders] = {0, 0};
  unsigned long switchPressStartMs_[kNumEncoders] = {0, 0};
  bool longPressFired_[kNumEncoders] = {false, false};
};
