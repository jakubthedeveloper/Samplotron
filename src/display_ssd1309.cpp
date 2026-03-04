#include "display_ssd1309.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <stdio.h>

#include "pins.h"

namespace {

U8G2_SSD1309_128X64_NONAME0_F_SW_I2C gDisplay(
    U8G2_R0, Pins::OLED_SCK, Pins::OLED_MOSI, U8X8_PIN_NONE);
TwoWire gOledWire(1);

bool probeI2cAddress(uint8_t address7bit) {
  gOledWire.beginTransmission(address7bit);
  return gOledWire.endTransmission() == 0;
}

bool detectDisplayAddress(uint8_t &address7bit) {
  constexpr uint8_t kCandidates[] = {0x3C, 0x3D};
  for (uint8_t candidate : kCandidates) {
    if (probeI2cAddress(candidate)) {
      address7bit = candidate;
      return true;
    }
  }
  return false;
}

}  // namespace

bool DisplaySsd1309::begin() {
  gOledWire.begin(Pins::OLED_MOSI, Pins::OLED_SCK, 400000U);

  uint8_t displayAddress = 0;
  if (!detectDisplayAddress(displayAddress)) {
    Serial.println("SSD1309 not found on I2C (tried 0x3C/0x3D)");
    ready_ = false;
    return false;
  }

  gDisplay.setI2CAddress(static_cast<uint8_t>(displayAddress << 1));

  if (!gDisplay.begin()) {
    ready_ = false;
    return false;
  }

  ready_ = true;
  dirty_ = true;
  render();
  return true;
}

void DisplaySsd1309::setSampleSelection(int currentSampleNumber, int totalSamples, const String &sampleName) {
  currentSampleNumber_ = currentSampleNumber;
  totalSamples_ = totalSamples;
  currentSampleName_ = sampleName;
  dirty_ = true;
}

void DisplaySsd1309::update() {
  if (!ready_ || !dirty_) return;
  render();
}

void DisplaySsd1309::render() {
  if (!ready_) return;

  gDisplay.clearBuffer();

  gDisplay.setFont(u8g2_font_9x15_tf);
  gDisplay.drawStr(0, 14, "Samplotron");

  gDisplay.setFont(u8g2_font_6x12_tf);
  char header[24];
  snprintf(header, sizeof(header), "Sample %d/%d", currentSampleNumber_, totalSamples_);
  gDisplay.drawStr(0, 46, header);

  gDisplay.drawStr(0, 60, currentSampleName_.c_str());

  gDisplay.sendBuffer();
  dirty_ = false;
}
