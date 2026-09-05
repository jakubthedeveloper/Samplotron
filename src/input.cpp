#include "input.h"

#include <Wire.h>

#include "pins.h"
#include "keypad_mapping.h"

namespace {

constexpr uint8_t kRegIodirA = 0x00;
constexpr uint8_t kRegGppuA = 0x0C;
constexpr uint8_t kRegIntConA = 0x08;
constexpr uint8_t kRegGpIntEnA = 0x04;
constexpr uint8_t kRegIoCon = 0x0A;
constexpr uint8_t kRegGpioA = 0x12;
// Register addresses for IOCON.BANK = 0.
constexpr uint8_t kRegIodirB = 0x01;
constexpr uint8_t kRegIpolB = 0x03;
constexpr uint8_t kRegGpIntEnB = 0x05;
constexpr uint8_t kRegGppuB = 0x0D;
constexpr uint8_t kRegGpioB = 0x13;
constexpr uint8_t kRegOlatB = 0x15;
constexpr uint8_t kKeypadRowBits[] = {0, 1, 2, 3};
constexpr uint8_t kKeypadColumnBits[] = {4, 5, 6, 7};
constexpr unsigned long kKeypadScanMs = 5;

constexpr int kNumEncoders = 2;
constexpr uint8_t kEncABits[kNumEncoders] = {0, 3};
constexpr uint8_t kEncBBits[kNumEncoders] = {1, 4};
constexpr uint8_t kSwitchBits[kNumEncoders] = {2, 5};
constexpr uint8_t kUsedInputMask = 0x3F;

bool mcpWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(Pins::MCP_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mcpReadReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(Pins::MCP_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(static_cast<int>(Pins::MCP_I2C_ADDR), 1) != 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

int8_t quadratureDelta(uint8_t prev, uint8_t current) {
  switch ((prev << 2) | current) {
    case 0b0001:
    case 0b0111:
    case 0b1110:
    case 0b1000:
      return 1;
    case 0b0010:
    case 0b1011:
    case 0b1101:
    case 0b0100:
      return -1;
    default:
      return 0;
  }
}

}  // namespace

void Input::begin() {
  pinMode(Pins::MCP_INTA, INPUT_PULLUP);

  Wire.begin(Pins::MCP_SDA, Pins::MCP_SCL, 400000U);

  uint8_t gpioA = 0;
  if (!mcpReadReg(kRegGpioA, gpioA)) {
    ready_ = false;
    return;
  }

  // INTA/INTB mirrored, open-drain, active-low for reliable pull-up wiring.
  if (!mcpWriteReg(kRegIoCon, 0b01000100)) {
    ready_ = false;
    return;
  }

  // GPA0..GPA5 used: two encoders (A/B + switch).
  if (!mcpWriteReg(kRegIodirA, 0xFF) || !mcpWriteReg(kRegGppuA, 0xFF)) {
    ready_ = false;
    return;
  }

  // Interrupt-on-change against previous pin value.
  if (!mcpWriteReg(kRegIntConA, 0x00) || !mcpWriteReg(kRegGpIntEnA, kUsedInputMask)) {
    ready_ = false;
    return;
  }

  const unsigned long now = millis();
  for (int i = 0; i < kNumEncoders; i++) {
    encoderState_[i] = static_cast<uint8_t>(((gpioA >> kEncABits[i]) & 0x01) |
                                            (((gpioA >> kEncBBits[i]) & 0x01) << 1));
    switchLastReadState_[i] = ((gpioA >> kSwitchBits[i]) & 0x01) ? HIGH : LOW;
    switchStableState_[i] = switchLastReadState_[i];
    switchLastDebounceMs_[i] = now;
    encoderTicks_[i] = 0;
  }
  ready_ = true;
  // Only the selected row drives LOW; all other pins remain inputs.
  // This avoids opposing output levels when several keys are held.
  keypadReady_ = mcpWriteReg(kRegIodirB, 0xFF) &&
                 mcpWriteReg(kRegGpIntEnB, 0x00) &&
                 mcpWriteReg(kRegIpolB, 0x00) &&
                 mcpWriteReg(kRegOlatB, 0x00) &&
                 mcpWriteReg(kRegGppuB, 0xF0);
  Serial.println(keypadReady_ ? "[KEYPAD] ready: 4x4, rows PB0-PB3, columns PB4-PB7"
                             : "[KEYPAD] MCP port B initialization failed");
}

void Input::updateKeypad(OnEventCallback callback, void *context) {
  const unsigned long now = millis();
  if (!keypadReady_ || now - keypadLastScanMs_ < kKeypadScanMs) return;
  keypadLastScanMs_ = now;

  uint16_t pressed = 0;
  bool scanOk = true;
  for (uint8_t row = 0; row < 4; ++row) {
    uint8_t gpioB = 0xFF;
    if (!mcpWriteReg(kRegIodirB, static_cast<uint8_t>(~(1U << kKeypadRowBits[row])))) {
      scanOk = false;
      break;
    }
    delayMicroseconds(5);
    if (!mcpReadReg(kRegGpioB, gpioB)) {
      scanOk = false;
      break;
    }
    for (uint8_t column = 0; column < 4; ++column) {
      if ((gpioB & (1U << kKeypadColumnBits[column])) == 0) {
        pressed |= static_cast<uint16_t>(1U << (row * 4 + column));
      }
    }
  }
  // Release the last row, including after a failed transaction.
  if (!mcpWriteReg(kRegIodirB, 0xFF)) scanOk = false;
  if (!scanOk) {
    for (uint8_t key = 0; key < 16; ++key) keypadLastChangeMs_[key] = now;
    return;
  }

  for (uint8_t key = 0; key < 16; ++key) {
    const bool down = (pressed & (1U << key)) != 0;
    if (down != keypadLastRead_[key]) {
      keypadLastRead_[key] = down;
      keypadLastChangeMs_[key] = now;
    }
    if (down != keypadStable_[key] && now - keypadLastChangeMs_[key] >= kDebounceMs) {
      keypadStable_[key] = down;
      if (down) {
        const int midiNote = KeypadMapping::kMidiNotes[key];
        Serial.printf("[KEYPAD] PRESS key=%u row=%u col=%u note=%d\n",
                      static_cast<unsigned>(key + 1),
                      static_cast<unsigned>(key / 4 + 1),
                      static_cast<unsigned>(key % 4 + 1), midiNote);
        if (callback) {
          callback({EventType::KeypadNoteOn, midiNote}, context);
        }
      }
    }
  }
}

void Input::update(OnEventCallback callback, void *context) {
  if (!ready_) return;
  updateKeypad(callback, context);
  static unsigned long lastPollMs = 0;
  const unsigned long now = millis();
  if (now == lastPollMs) return;
  lastPollMs = now;

  uint8_t gpioA = 0;
  if (!mcpReadReg(kRegGpioA, gpioA)) return;

  for (int i = 0; i < kNumEncoders; i++) {
    const uint8_t currentEncoderState =
        static_cast<uint8_t>(((gpioA >> kEncABits[i]) & 0x01) |
                             (((gpioA >> kEncBBits[i]) & 0x01) << 1));
    const int8_t delta = quadratureDelta(encoderState_[i], currentEncoderState);
    if (delta != 0) {
      encoderTicks_[i] = static_cast<int8_t>(encoderTicks_[i] + delta);
      if (encoderTicks_[i] >= kEncoderDetentTicks) {
        encoderTicks_[i] = 0;
        if (callback) {
          const Event event = {
              (i == 0) ? EventType::LeftRotate : EventType::RightRotate,
              1,
          };
          callback(event, context);
        }
      } else if (encoderTicks_[i] <= -kEncoderDetentTicks) {
        encoderTicks_[i] = 0;
        if (callback) {
          const Event event = {
              (i == 0) ? EventType::LeftRotate : EventType::RightRotate,
              -1,
          };
          callback(event, context);
        }
      }
    }
    encoderState_[i] = currentEncoderState;

    const int switchRaw = ((gpioA >> kSwitchBits[i]) & 0x01) ? HIGH : LOW;
    if (switchRaw != switchLastReadState_[i]) {
      switchLastReadState_[i] = switchRaw;
      switchLastDebounceMs_[i] = millis();
    }

    if ((millis() - switchLastDebounceMs_[i]) >= kDebounceMs &&
        switchRaw != switchStableState_[i]) {
      switchStableState_[i] = switchRaw;
      if (switchStableState_[i] == LOW) {
        switchPressStartMs_[i] = now;
        longPressFired_[i] = false;
      } else if (callback && !longPressFired_[i]) {
        const Event event = {
            (i == 0) ? EventType::LeftClick : EventType::RightClick,
            0,
        };
        callback(event, context);
      }
    }

    if (i == 1 && switchStableState_[i] == LOW && !longPressFired_[i] &&
        (now - switchPressStartMs_[i]) >= kLongPressMs) {
      longPressFired_[i] = true;
      if (callback) {
        const Event event = {EventType::RightLongPress, 0};
        callback(event, context);
      }
    }
  }
}
