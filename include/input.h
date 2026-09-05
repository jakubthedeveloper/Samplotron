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
    KeypadNoteOn,
  };

  struct Event {
    EventType type;
    int value;  // Rotation delta (+1/-1), MIDI note for KeypadNoteOn, otherwise 0.
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
  void updateKeypad(OnEventCallback callback, void *context);
  bool keypadReady_ = false;
  unsigned long keypadLastScanMs_ = 0;
  bool keypadStable_[16] = {};
  bool keypadLastRead_[16] = {};
  unsigned long keypadLastChangeMs_[16] = {};
  uint8_t encoderState_[kNumEncoders] = {0, 0};
  int8_t encoderTicks_[kNumEncoders] = {0, 0};
  int switchStableState_[kNumEncoders] = {HIGH, HIGH};
  int switchLastReadState_[kNumEncoders] = {HIGH, HIGH};
  unsigned long switchLastDebounceMs_[kNumEncoders] = {0, 0};
  unsigned long switchPressStartMs_[kNumEncoders] = {0, 0};
  bool longPressFired_[kNumEncoders] = {false, false};
};
