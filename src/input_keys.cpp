#include "input_keys.h"

void InputKeys::begin() {
  for (int i = 0; i < kNumKeys; i++) {
    pinMode(pins_[i], INPUT_PULLUP);
    int state = digitalRead(pins_[i]);
    lastReadState_[i] = state;
    stableState_[i] = state;
    lastDebounceMs_[i] = millis();
  }
}

void InputKeys::update(OnPressCallback callback, void *context) {
  for (int i = 0; i < kNumKeys; i++) {
    int raw = digitalRead(pins_[i]);

    if (raw != lastReadState_[i]) {
      lastReadState_[i] = raw;
      lastDebounceMs_[i] = millis();
    }

    if ((millis() - lastDebounceMs_[i]) >= kDebounceMs && raw != stableState_[i]) {
      stableState_[i] = raw;
      if (stableState_[i] == LOW && callback) {
        callback(i, context);
      }
    }
  }
}
