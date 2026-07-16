#include <Arduino.h>
#include <Wire.h>

#ifndef KEYPAD_MCP_ADDRESS
#define KEYPAD_MCP_ADDRESS 0x26
#endif

namespace {

constexpr int kSdaPin = 23;
constexpr int kSclPin = 18;
constexpr uint32_t kI2cFrequency = 400000U;
constexpr uint32_t kSerialBaud = 115200U;
constexpr uint8_t kMcpAddress = KEYPAD_MCP_ADDRESS;

constexpr uint8_t kRegIodirA = 0x00;
constexpr uint8_t kRegIodirB = 0x01;
constexpr uint8_t kRegIoconBank0 = 0x0A;
constexpr uint8_t kRegIoconBank1 = 0x05;
constexpr uint8_t kRegGppuA = 0x0C;
constexpr uint8_t kRegGppuB = 0x0D;
constexpr uint8_t kRegGpioA = 0x12;
constexpr uint8_t kRegGpioB = 0x13;
constexpr uint8_t kRegOlatA = 0x14;
constexpr uint8_t kRegOlatB = 0x15;

constexpr uint8_t kLineCount = 10;
constexpr uint8_t kMaximumPairCount = (kLineCount * (kLineCount - 1)) / 2;
constexpr uint16_t kSettleTimeUs = 75;
constexpr uint16_t kPollIntervalMs = 3;  // About 200--300 complete scans per second.
constexpr uint16_t kDebounceMs = 25;
constexpr uint16_t kRetryIntervalMs = 1000;

struct KeypadLine {
  char port;
  uint8_t bit;
};

constexpr KeypadLine kLines[kLineCount] = {
    {'A', 0}, {'A', 1}, {'A', 2}, {'A', 3}, {'A', 4},
    {'A', 5}, {'A', 6}, {'A', 7}, {'B', 0}, {'B', 1},
};

struct Pair {
  uint8_t first;
  uint8_t second;
};

struct PairDetection {
  Pair pair;
  uint8_t directionCount;
};

enum class ObservationKind : uint8_t {
  None,
  Single,
  Multiple,
};

struct Observation {
  ObservationKind kind;
  Pair pair;
};

Pair gStoredPairs[kMaximumPairCount];
uint8_t gStoredPairCount = 0;
uint32_t gMappingCounter = 0;

Observation gCandidate = {ObservationKind::None, {0, 0}};
Observation gStable = {ObservationKind::None, {0, 0}};
uint32_t gCandidateSinceMs = 0;
uint32_t gLastPollMs = 0;
uint32_t gLastRetryMs = 0;
bool gMcpReady = false;
bool gCommunicationErrorReported = false;

bool mcpWriteRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kMcpAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mcpReadPorts(uint8_t &portA, uint8_t &portB) {
  Wire.beginTransmission(kMcpAddress);
  Wire.write(kRegGpioA);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom(static_cast<int>(kMcpAddress), 2) != 2) return false;
  portA = Wire.read();
  portB = Wire.read();
  return true;
}

bool addressResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void printAddress(uint8_t address) {
  Serial.print(F("0x"));
  if (address < 0x10) Serial.print('0');
  Serial.print(address, HEX);
}

void scanI2cBus() {
  Serial.println(F("I2C scan (SDA=GPIO23, SCL=GPIO18):"));
  uint8_t found = 0;
  for (uint8_t address = 0x03; address <= 0x77; ++address) {
    if (!addressResponds(address)) continue;
    Serial.print(F("  found device at "));
    printAddress(address);
    if (address == kMcpAddress) Serial.print(F(" (configured keypad MCP23017)"));
    Serial.println();
    ++found;
  }
  if (found == 0) Serial.println(F("  no I2C devices found"));
}

bool restoreAllInputs() {
  // These two writes can only remove an output; they can never create a second one.
  // Always attempt both writes, even if communication with one register fails.
  const bool portAOk = mcpWriteRegister(kRegIodirA, 0xFF);
  const bool portBOk = mcpWriteRegister(kRegIodirB, 0xFF);
  return portAOk && portBOk;
}

bool initializeMcp() {
  if (!addressResponds(kMcpAddress)) {
    Serial.print(F("ERROR: keypad MCP23017 not detected at "));
    printAddress(kMcpAddress);
    Serial.println(F(". Check power, address straps, SDA and SCL."));
    return false;
  }

  // Cope with either IOCON.BANK setting left by an earlier program. Writing
  // 0x05 first selects BANK=0 when the chip is currently in BANK=1.
  if (!mcpWriteRegister(kRegIoconBank1, 0x00) ||
      !mcpWriteRegister(kRegIoconBank0, 0x00) ||
      !restoreAllInputs() ||
      !mcpWriteRegister(kRegOlatA, 0x00) ||
      !mcpWriteRegister(kRegOlatB, 0x00) ||
      !mcpWriteRegister(kRegGppuA, 0xFF) ||
      !mcpWriteRegister(kRegGppuB, 0x03)) {
    Serial.println(F("ERROR: MCP23017 responded but register setup failed."));
    return false;
  }

  Serial.print(F("Keypad MCP23017 ready at "));
  printAddress(kMcpAddress);
  Serial.println('.');
  return true;
}

void printPair(const Pair &pair) {
  const KeypadLine &first = kLines[pair.first];
  const KeypadLine &second = kLines[pair.second];
  Serial.print('P');
  Serial.print(first.port);
  Serial.print(first.bit);
  Serial.print(F(" <-> P"));
  Serial.print(second.port);
  Serial.print(second.bit);
}

bool samePair(const Pair &left, const Pair &right) {
  return left.first == right.first && left.second == right.second;
}

bool sameObservation(const Observation &left, const Observation &right) {
  if (left.kind != right.kind) return false;
  return left.kind != ObservationKind::Single || samePair(left.pair, right.pair);
}

bool containsPair(const Pair *pairs, uint8_t count, const Pair &pair) {
  for (uint8_t i = 0; i < count; ++i) {
    if (samePair(pairs[i], pair)) return true;
  }
  return false;
}

bool scanKeypad(Observation &observation) {
  PairDetection detected[kMaximumPairCount];
  uint8_t detectedCount = 0;

  if (!restoreAllInputs()) return false;

  for (uint8_t drivenIndex = 0; drivenIndex < kLineCount; ++drivenIndex) {
    const KeypadLine &driven = kLines[drivenIndex];
    const uint8_t direction = static_cast<uint8_t>(~(1U << driven.bit));

    // OLAT was set LOW before scanning. Changing exactly one IODIR bit is the
    // only operation that enables an output.
    const bool configured =
        (driven.port == 'A')
            ? mcpWriteRegister(kRegIodirA, direction)
            : mcpWriteRegister(kRegIodirB, direction);
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
      const uint8_t port = (readLine.port == 'A') ? portA : portB;
      if ((port & (1U << readLine.bit)) != 0) continue;

      Pair pair = {min(drivenIndex, readIndex), max(drivenIndex, readIndex)};
      bool alreadyDetected = false;
      for (uint8_t i = 0; i < detectedCount; ++i) {
        if (!samePair(detected[i].pair, pair)) continue;
        ++detected[i].directionCount;
        alreadyDetected = true;
        break;
      }
      if (!alreadyDetected) {
        detected[detectedCount++] = {pair, 1};
      }
    }

    // Return to all inputs before selecting the next driven line.
    if (!restoreAllInputs()) return false;
  }

  // A real key is a passive short, so it must be visible in both directions:
  // first while one endpoint is LOW, then while the other endpoint is LOW.
  // Reject one-sided LOW readings caused by noise, settling or a grounded line.
  uint8_t reciprocalCount = 0;
  Pair reciprocalPair = {0, 0};
  for (uint8_t i = 0; i < detectedCount; ++i) {
    if (detected[i].directionCount < 2) continue;
    reciprocalPair = detected[i].pair;
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

void storePairIfUnique(const Pair &pair) {
  if (containsPair(gStoredPairs, gStoredPairCount, pair)) return;
  if (gStoredPairCount < kMaximumPairCount) {
    gStoredPairs[gStoredPairCount++] = pair;
    Serial.print(F("STORED: "));
    printPair(pair);
    Serial.print(F(" (unique pairs: "));
    Serial.print(gStoredPairCount);
    Serial.println(')');
  }
}

void applyStableObservation(const Observation &next) {
  if (gStable.kind == ObservationKind::Single &&
      (next.kind != ObservationKind::Single || !samePair(gStable.pair, next.pair))) {
    Serial.print(F("KEY UP:   "));
    printPair(gStable.pair);
    Serial.println();
  }

  if (next.kind == ObservationKind::Multiple &&
      gStable.kind != ObservationKind::Multiple) {
    Serial.println(F("WARNING: multiple connections detected; press one key at a time"));
  }

  if (next.kind == ObservationKind::Single &&
      (gStable.kind != ObservationKind::Single || !samePair(gStable.pair, next.pair))) {
    ++gMappingCounter;
    Serial.print(F("KEY DOWN: "));
    printPair(next.pair);
    Serial.println();
    Serial.print(F("MAPPING #"));
    Serial.print(gMappingCounter);
    Serial.print(F(": "));
    printPair(next.pair);
    Serial.println();
    storePairIfUnique(next.pair);
  }

  gStable = next;
}

void updateDebounce(const Observation &observation, uint32_t now) {
  if (!sameObservation(observation, gCandidate)) {
    gCandidate = observation;
    gCandidateSinceMs = now;
    return;
  }

  if (!sameObservation(gCandidate, gStable) &&
      static_cast<uint32_t>(now - gCandidateSinceMs) >= kDebounceMs) {
    applyStableObservation(gCandidate);
  }
}

void dumpCurrentKeypadState() {
  Observation observation = {ObservationKind::None, {0, 0}};
  if (!gMcpReady) {
    Serial.println(F("SNAPSHOT ERROR: keypad MCP23017 is not ready."));
    return;
  }

  if (!scanKeypad(observation)) {
    Serial.println(F("SNAPSHOT ERROR: could not read keypad MCP23017."));
    gMcpReady = false;
    gLastRetryMs = millis();
    return;
  }

  Serial.print(F("SNAPSHOT: "));
  switch (observation.kind) {
    case ObservationKind::None:
      Serial.println(F("no connection"));
      break;
    case ObservationKind::Single:
      printPair(observation.pair);
      Serial.println();
      storePairIfUnique(observation.pair);
      break;
    case ObservationKind::Multiple:
      Serial.println(F("multiple connections detected; press one key at a time"));
      break;
  }
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  h  show this help"));
  Serial.println(F("  s  scan the I2C bus"));
  Serial.println(F("  d  dump the currently held keypad connection"));
  Serial.println(F("  r  reset mapping counter"));
  Serial.println(F("  c  clear stored unique pairs"));
  Serial.println(F("  l  list stored unique pairs"));
}

void listStoredPairs() {
  Serial.print(F("Stored unique pairs: "));
  Serial.println(gStoredPairCount);
  for (uint8_t i = 0; i < gStoredPairCount; ++i) {
    Serial.print(i + 1);
    Serial.print(F(": "));
    printPair(gStoredPairs[i]);
    Serial.println();
  }
}

void handleCommand(char command) {
  switch (command) {
    case 'h':
    case 'H':
      printHelp();
      break;
    case 's':
    case 'S':
      scanI2cBus();
      break;
    case 'd':
    case 'D':
      dumpCurrentKeypadState();
      break;
    case 'r':
    case 'R':
      gMappingCounter = 0;
      Serial.println(F("Mapping counter reset."));
      break;
    case 'c':
    case 'C':
      gStoredPairCount = 0;
      Serial.println(F("Stored pairs cleared."));
      break;
    case 'l':
    case 'L':
      listStoredPairs();
      break;
    case '\r':
    case '\n':
    case ' ':
    case '\t':
      break;
    default:
      Serial.print(F("Unknown command: "));
      Serial.println(command);
      printHelp();
      break;
  }
}

void processSerial() {
  while (Serial.available() > 0) {
    handleCommand(static_cast<char>(Serial.read()));
  }
}

void resetObservedState() {
  gCandidate = {ObservationKind::None, {0, 0}};
  gStable = {ObservationKind::None, {0, 0}};
  gCandidateSinceMs = millis();
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(300);
  Serial.println();
  Serial.println(F("Calculator keypad diagnostic"));
  Wire.begin(kSdaPin, kSclPin, kI2cFrequency);
  scanI2cBus();
  gMcpReady = initializeMcp();
  resetObservedState();
  printHelp();
}

void loop() {
  processSerial();

  const uint32_t now = millis();
  if (!gMcpReady) {
    if (static_cast<uint32_t>(now - gLastRetryMs) >= kRetryIntervalMs) {
      gLastRetryMs = now;
      gMcpReady = initializeMcp();
      if (gMcpReady) resetObservedState();
    }
    return;
  }

  if (static_cast<uint32_t>(now - gLastPollMs) < kPollIntervalMs) return;
  gLastPollMs = now;

  Observation observation = {ObservationKind::None, {0, 0}};
  if (!scanKeypad(observation)) {
    if (!gCommunicationErrorReported) {
      Serial.println(F("ERROR: lost communication with keypad MCP23017; retrying."));
      gCommunicationErrorReported = true;
    }
    restoreAllInputs();
    gMcpReady = false;
    gLastRetryMs = now;
    resetObservedState();
    return;
  }

  gCommunicationErrorReported = false;
  updateDebounce(observation, now);
}
