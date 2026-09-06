#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "display_config.h"
#include "pins.h"

namespace {

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C gDisplay(
    DisplayConfig::ROTATE_180 ? U8G2_R2 : U8G2_R0, U8X8_PIN_NONE);
constexpr int kDebugSdaPin = Pins::OLED_MOSI;  // GPIO23
constexpr int kDebugSclPin = Pins::OLED_SCK;   // GPIO18

constexpr uint8_t kRegIodirA = 0x00;
constexpr uint8_t kRegGppuA = 0x0C;
constexpr uint8_t kRegGpioA = 0x12;

constexpr int kNumEncoders = 2;
constexpr uint8_t kEncABits[kNumEncoders] = {0, 3};
constexpr uint8_t kEncBBits[kNumEncoders] = {1, 4};
constexpr uint8_t kSwitchBits[kNumEncoders] = {2, 5};

constexpr uint8_t kDebounceMs = 35;
constexpr int kDetentTicks = 4;

uint8_t gEncoderState[kNumEncoders] = {0, 0};
int8_t gEncoderTicks[kNumEncoders] = {0, 0};
long gEncoderPosition[kNumEncoders] = {0, 0};

int gSwitchStable[kNumEncoders] = {HIGH, HIGH};
int gSwitchLastRead[kNumEncoders] = {HIGH, HIGH};
unsigned long gSwitchLastChangeMs[kNumEncoders] = {0, 0};

String gLastEvent = "startup";
bool gDisplayReady = false;
unsigned long gLastRenderMs = 0;
uint8_t gMcpAddress = Pins::MCP_I2C_ADDR;

bool mcpWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(gMcpAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool mcpReadReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(gMcpAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom(static_cast<int>(gMcpAddress), 1) != 1) return false;

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

void logEvent(const String &eventText) {
  gLastEvent = eventText;

}

void renderDisplay() {
  if (!gDisplayReady) return;

  gDisplay.clearBuffer();
  gDisplay.setFont(u8g2_font_6x12_tf);
  gDisplay.drawStr(0, 10, "MCP23017 debug");

  char line[32];
  snprintf(line, sizeof(line), "ENC1 pos: %ld", gEncoderPosition[0]);
  gDisplay.drawStr(0, 24, line);
  snprintf(line, sizeof(line), "ENC2 pos: %ld", gEncoderPosition[1]);
  gDisplay.drawStr(0, 36, line);
  snprintf(line, sizeof(line), "SW1:%s SW2:%s",
           (gSwitchStable[0] == LOW) ? "ON" : "OFF",
           (gSwitchStable[1] == LOW) ? "ON" : "OFF");
  gDisplay.drawStr(0, 48, line);
  snprintf(line, sizeof(line), "MCP:0x%02X %s", gMcpAddress, gLastEvent.c_str());
  gDisplay.drawStr(0, 60, line);
  gDisplay.sendBuffer();
}

bool initDisplay() {
  Wire.begin(kDebugSdaPin, kDebugSclPin, 400000U);

  uint8_t address = 0;
  for (uint8_t candidate : {static_cast<uint8_t>(0x3C), static_cast<uint8_t>(0x3D)}) {
    Wire.beginTransmission(candidate);
    if (Wire.endTransmission() == 0) {
      address = candidate;
      break;
    }
  }
  if (address == 0) return false;

  gDisplay.setI2CAddress(static_cast<uint8_t>(address << 1));
  return gDisplay.begin();
}

bool initMcp() {

  bool foundAny = false;
  for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {

      foundAny = true;
    }
  }

  bool mcpFound = false;
  for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      gMcpAddress = addr;
      mcpFound = true;
      break;
    }
  }
  if (!mcpFound) {

    return false;
  }

  if (!mcpWriteReg(kRegIodirA, 0xFF)) {

    return false;
  }
  if (!mcpWriteReg(kRegGppuA, 0xFF)) {

    return false;
  }

  uint8_t gpioA = 0;
  if (!mcpReadReg(kRegGpioA, gpioA)) {

    return false;
  }

  const unsigned long now = millis();
  for (int i = 0; i < kNumEncoders; i++) {
    gEncoderState[i] = static_cast<uint8_t>(((gpioA >> kEncABits[i]) & 0x01) |
                                            (((gpioA >> kEncBBits[i]) & 0x01) << 1));
    gSwitchLastRead[i] = ((gpioA >> kSwitchBits[i]) & 0x01) ? HIGH : LOW;
    gSwitchStable[i] = gSwitchLastRead[i];
    gSwitchLastChangeMs[i] = now;
  }
  return true;
}

void processInputs() {
  uint8_t gpioA = 0;
  if (!mcpReadReg(kRegGpioA, gpioA)) return;

  for (int i = 0; i < kNumEncoders; i++) {
    const uint8_t currentState = static_cast<uint8_t>(((gpioA >> kEncABits[i]) & 0x01) |
                                                      (((gpioA >> kEncBBits[i]) & 0x01) << 1));
    const int8_t delta = quadratureDelta(gEncoderState[i], currentState);
    gEncoderState[i] = currentState;
    if (delta != 0) {
      gEncoderTicks[i] = static_cast<int8_t>(gEncoderTicks[i] + delta);
      if (gEncoderTicks[i] >= kDetentTicks) {
        gEncoderTicks[i] = 0;
        gEncoderPosition[i]++;
        logEvent("ENC" + String(i + 1) + " CW");
      } else if (gEncoderTicks[i] <= -kDetentTicks) {
        gEncoderTicks[i] = 0;
        gEncoderPosition[i]--;
        logEvent("ENC" + String(i + 1) + " CCW");
      }
    }

    const int switchRaw = ((gpioA >> kSwitchBits[i]) & 0x01) ? HIGH : LOW;
    if (switchRaw != gSwitchLastRead[i]) {
      gSwitchLastRead[i] = switchRaw;
      gSwitchLastChangeMs[i] = millis();
    }

    if ((millis() - gSwitchLastChangeMs[i]) >= kDebounceMs &&
        switchRaw != gSwitchStable[i]) {
      gSwitchStable[i] = switchRaw;
      if (gSwitchStable[i] == LOW) {
        logEvent("ENC" + String(i + 1) + " PRESS");
      } else {
        logEvent("ENC" + String(i + 1) + " RELEASE");
      }
    }
  }
}

}  // namespace

void setup() {

  delay(200);

  gDisplayReady = initDisplay();

  const bool mcpReady = initMcp();

  if (!mcpReady) {
    logEvent("MCP error");
  } else {
    logEvent("ready");
  }
  renderDisplay();
}

void loop() {
  processInputs();

  const unsigned long now = millis();
  if ((now - gLastRenderMs) >= 80) {
    gLastRenderMs = now;
    renderDisplay();
  }

  delay(2);
}
