#include "display_ssd1309.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <stdio.h>

#include "pins.h"

namespace {

U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI gDisplay(
    U8G2_R0,
    Pins::OLED_SCK,
    Pins::OLED_MOSI,
    Pins::OLED_CS,
    Pins::OLED_DC,
    Pins::OLED_RES);

}  // namespace

bool DisplaySsd1309::begin() {
  if (!gDisplay.begin()) {
    ready_ = false;
    return false;
  }

  ready_ = true;
  dirty_ = true;
  render();
  return true;
}

void DisplaySsd1309::setBootStatus(bool sdOk, bool codecOk) {
  sdOk_ = sdOk;
  codecOk_ = codecOk;
  dirty_ = true;
}

void DisplaySsd1309::setLastSample(int sampleNumber) {
  lastSample_ = sampleNumber;
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
  gDisplay.drawStr(0, 30, sdOk_ ? "SD: OK" : "SD: FAIL");
  gDisplay.drawStr(70, 30, codecOk_ ? "Codec: OK" : "Codec: FAIL");

  gDisplay.drawStr(0, 46, "Last sample:");

  char sampleLabel[20];
  if (lastSample_ > 0) {
    snprintf(sampleLabel, sizeof(sampleLabel), "test%d.wav", lastSample_);
  } else {
    snprintf(sampleLabel, sizeof(sampleLabel), "-");
  }
  gDisplay.drawStr(0, 60, sampleLabel);

  gDisplay.sendBuffer();
  dirty_ = false;
}
