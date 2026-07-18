#include "calculator_keypad.h"

#include <Wire.h>

#include "pins.h"

namespace {

constexpr uint8_t kRegIodirA = 0x00;
constexpr uint8_t kRegIodirB = 0x01;
constexpr uint8_t kRegIoconBank0 = 0x0A;
constexpr uint8_t kRegIoconBank1 = 0x05;
constexpr uint8_t kRegGppuA = 0x0C;
constexpr uint8_t kRegGppuB = 0x0D;
constexpr uint8_t kRegGpioA = 0x12;
constexpr uint8_t kRegOlatA = 0x14;
constexpr uint8_t kRegOlatB = 0x15;

constexpr uint8_t kLineCount = 10;
constexpr uint8_t kMaximumPairCount = (kLineCount * (kLineCount - 1)) / 2;
constexpr uint16_t kSettleTimeUs = 75;
constexpr uint16_t kPollIntervalMs = 3;
constexpr uint16_t kDebounceMs = 25;
constexpr uint16_t kRetryIntervalMs = 1000;
constexpr uint8_t kFirstMidiNote = 60;

enum LineIndex : uint8_t {
  kPa0,
  kPa1,
  kPa2,
  kPa3,
  kPa4,
  kPa5,
  kPa6,
  kPa7,
  kPb0,
  kPb1,
};

struct KeypadLine {
  bool portB;
  uint8_t bit;
};

constexpr KeypadLine kLines[kLineCount] = {
    {false, 0}, {false, 1}, {false, 2}, {false, 3}, {false, 4},
    {false, 5}, {false, 6}, {false, 7}, {true, 0},  {true, 1},
};

struct KeyMapping {
  uint8_t first;
  uint8_t second;
};

// mapping.txt "Key mappings" order: 0, 00, dot, 1, 2, 3, 4, 5, 6, 7, 8, 9.
constexpr KeyMapping kMappings[] = {
    {kPa1, kPb1},
    {kPa1, kPa6},
    {kPa1, kPb0},
    {kPa1, kPa5},
    {kPa1, kPa4},
    {kPa1, kPa7},
    {kPa1, kPa3},
    {kPa1, kPa2},
    {kPa4, kPb0},
    {kPa4, kPa6},
    {kPa4, kPb1},
    {kPa4, kPa5},
};

struct PairDetection {
  uint8_t first;
  uint8_t second;
  uint8_t directionCount;
};

bool mcpWriteRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(Pins::KEYPAD_MCP_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mcpReadPorts(uint8_t &portA, uint8_t &portB) {
  Wire.beginTransmission(Pins::KEYPAD_MCP_I2C_ADDR);
  Wire.write(kRegGpioA);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom(static_cast<int>(Pins::KEYPAD_MCP_I2C_ADDR), 2) != 2) return false;
  portA = Wire.read();
  portB = Wire.read();
  return true;
}

bool addressResponds() {
  Wire.beginTransmission(Pins::KEYPAD_MCP_I2C_ADDR);
  return Wire.endTransmission() == 0;
}

bool samePair(uint8_t firstA, uint8_t secondA, uint8_t firstB, uint8_t secondB) {
  return firstA == firstB && secondA == secondB;
}

}  // namespace

void CalculatorKeypad::begin() {
  ready_ = initializeMcp();
  const uint32_t now = millis();
  lastRetryMs_ = now;
  lastPollMs_ = now;
  resetDebounce(now);
}

void CalculatorKeypad::update(OnNoteOnCallback callback, void *context) {
  const uint32_t now = millis();
  if (!ready_) {
    if (static_cast<uint32_t>(now - lastRetryMs_) < kRetryIntervalMs) return;
    lastRetryMs_ = now;
    ready_ = initializeMcp();
    if (ready_) resetDebounce(now);
    return;
  }

  if (static_cast<uint32_t>(now - lastPollMs_) < kPollIntervalMs) return;
  lastPollMs_ = now;

  Observation observation = {ObservationKind::None, {0, 0}};
  if (!scan(observation)) {
    restoreAllInputs();
    ready_ = false;
    lastRetryMs_ = now;
    resetDebounce(now);
    return;
  }

  updateDebounce(observation, now, callback, context);
}

bool CalculatorKeypad::initializeMcp() {
  if (!addressResponds()) return false;

  // Normalize IOCON.BANK without making any keypad line an output first.
  if (!mcpWriteRegister(kRegIoconBank1, 0x00) ||
      !mcpWriteRegister(kRegIoconBank0, 0x00) ||
      !restoreAllInputs() ||
      !mcpWriteRegister(kRegOlatA, 0x00) ||
      !mcpWriteRegister(kRegOlatB, 0x00) ||
      !mcpWriteRegister(kRegGppuA, 0xFF) ||
      !mcpWriteRegister(kRegGppuB, 0x03)) {
    restoreAllInputs();
    return false;
  }
  return true;
}

bool CalculatorKeypad::restoreAllInputs() {
  const bool portAOk = mcpWriteRegister(kRegIodirA, 0xFF);
  const bool portBOk = mcpWriteRegister(kRegIodirB, 0xFF);
  return portAOk && portBOk;
}

bool CalculatorKeypad::scan(Observation &observation) {
  PairDetection detected[kMaximumPairCount];
  uint8_t detectedCount = 0;

  if (!restoreAllInputs()) return false;

  for (uint8_t drivenIndex = 0; drivenIndex < kLineCount; ++drivenIndex) {
    const KeypadLine &driven = kLines[drivenIndex];
    const uint8_t direction = static_cast<uint8_t>(~(1U << driven.bit));
    const bool configured = driven.portB ? mcpWriteRegister(kRegIodirB, direction)
                                         : mcpWriteRegister(kRegIodirA, direction);
    if (!configured) {
      restoreAllInputs();
      return false;
    }

    delayMicroseconds(kSettleTimeUs);
    uint8_t portA = 0xFF;
    uint8_t portB = 0xFF;
    if (!mcpReadPorts(portA, portB)) {
      restoreAllInputs();
      return false;
    }

    for (uint8_t readIndex = 0; readIndex < kLineCount; ++readIndex) {
      if (readIndex == drivenIndex) continue;
      const KeypadLine &readLine = kLines[readIndex];
      const uint8_t port = readLine.portB ? portB : portA;
      if ((port & (1U << readLine.bit)) != 0) continue;

      const uint8_t first = (drivenIndex < readIndex) ? drivenIndex : readIndex;
      const uint8_t second = (drivenIndex < readIndex) ? readIndex : drivenIndex;
      bool alreadyDetected = false;
      for (uint8_t i = 0; i < detectedCount; ++i) {
        if (!samePair(detected[i].first, detected[i].second, first, second)) continue;
        ++detected[i].directionCount;
        alreadyDetected = true;
        break;
      }
      if (!alreadyDetected && detectedCount < kMaximumPairCount) {
        detected[detectedCount++] = {first, second, 1};
      }
    }

    if (!restoreAllInputs()) return false;
  }

  uint8_t reciprocalCount = 0;
  Pair reciprocalPair = {0, 0};
  for (uint8_t i = 0; i < detectedCount; ++i) {
    if (detected[i].directionCount < 2) continue;
    reciprocalPair = {detected[i].first, detected[i].second};
    ++reciprocalCount;
  }

  if (reciprocalCount == 0) {
    observation = {ObservationKind::None, {0, 0}};
  } else if (reciprocalCount == 1) {
    observation = {ObservationKind::Single, reciprocalPair};
  } else {
    observation = {ObservationKind::Multiple, {0, 0}};
  }
  return true;
}

bool CalculatorKeypad::sameObservation(const Observation &left, const Observation &right) {
  if (left.kind != right.kind) return false;
  return left.kind != ObservationKind::Single ||
         samePair(left.pair.first, left.pair.second, right.pair.first, right.pair.second);
}

int CalculatorKeypad::midiNoteForPair(const Pair &pair) {
  for (uint8_t i = 0; i < sizeof(kMappings) / sizeof(kMappings[0]); ++i) {
    const KeyMapping &mapping = kMappings[i];
    if (samePair(pair.first, pair.second, mapping.first, mapping.second)) {
      return kFirstMidiNote + i;
    }
  }
  return -1;
}

void CalculatorKeypad::resetDebounce(uint32_t now) {
  candidate_ = {ObservationKind::None, {0, 0}};
  stable_ = {ObservationKind::None, {0, 0}};
  candidateSinceMs_ = now;
}

void CalculatorKeypad::updateDebounce(const Observation &observation,
                                      uint32_t now,
                                      OnNoteOnCallback callback,
                                      void *context) {
  if (!sameObservation(observation, candidate_)) {
    candidate_ = observation;
    candidateSinceMs_ = now;
    return;
  }

  if (sameObservation(candidate_, stable_) ||
      static_cast<uint32_t>(now - candidateSinceMs_) < kDebounceMs) {
    return;
  }

  stable_ = candidate_;
  if (stable_.kind != ObservationKind::Single) return;

  const int midiNote = midiNoteForPair(stable_.pair);
  if (midiNote >= 0 && callback) {
    callback(midiNote, context);
  }
}
