#include "codec_es8388.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace {

constexpr uint8_t ES8388_ADDR = 0x10;
TwoWire gCodecWire(1);

bool codecWrite(uint8_t reg, uint8_t val) {
  gCodecWire.beginTransmission(ES8388_ADDR);
  gCodecWire.write(reg);
  gCodecWire.write(val);
  return gCodecWire.endTransmission() == 0;
}

bool codecRead(uint8_t reg, uint8_t &val) {
  gCodecWire.beginTransmission(ES8388_ADDR);
  gCodecWire.write(reg);
  if (gCodecWire.endTransmission(false) != 0) return false;
  if (gCodecWire.requestFrom((uint8_t)ES8388_ADDR, (uint8_t)1) != 1) return false;
  val = gCodecWire.read();
  return true;
}

void setHeadphoneVolume(uint8_t percent) {
  uint8_t p = constrain(percent, 0, 100);
  uint8_t v = (uint8_t)((0x21UL * p) / 100UL);
  codecWrite(0x30, v);
  codecWrite(0x31, v);
}

void setSpeakerVolume(uint8_t percent) {
  uint8_t p = constrain(percent, 0, 100);
  uint8_t v = (uint8_t)((0x21UL * p) / 100UL);
  codecWrite(0x2E, v);
  codecWrite(0x2F, v);
}

void setMainDacVolume(uint8_t percent) {
  uint8_t p = constrain(percent, 0, 100);
  uint8_t v = (uint8_t)(96UL - ((96UL * p) / 100UL));
  codecWrite(0x1A, v);
  codecWrite(0x1B, v);
}

void unmuteOutputs() {
  uint8_t v = 0;
  if (codecRead(0x19, v)) codecWrite(0x19, (uint8_t)(v & ~(1 << 2)));
  if (codecRead(0x04, v)) codecWrite(0x04, (uint8_t)(v | (3 << 4) | (3 << 2)));
}

}  // namespace

namespace CodecES8388 {

bool init() {
  gCodecWire.begin(Pins::I2C_SDA, Pins::I2C_SCL, 400000U);
  gCodecWire.beginTransmission(ES8388_ADDR);
  if (gCodecWire.endTransmission() != 0) {
    Serial.println("ES8388 not found on I2C");
    return false;
  }

  const uint8_t initSeq[][2] = {
      {0x19, 0x04}, {0x01, 0x50}, {0x02, 0x00}, {0x08, 0x00}, {0x04, 0x3e},
      {0x00, 0x12}, {0x17, 0x18}, {0x18, 0x02}, {0x26, 0x1B}, {0x27, 0x90},
      {0x2A, 0x90}, {0x2B, 0x80}, {0x2D, 0x00}, {0x1B, 0x00}, {0x1A, 0x00},
      {0x03, 0xff}, {0x09, 0x88}, {0x0a, 0xf0}, {0x0b, 0x80}, {0x0c, 0x0e},
      {0x0d, 0x02}, {0x10, 0x20}, {0x11, 0x20}, {0x2e, 0x1e}, {0x2f, 0x1e},
      {0x30, 0x1e}, {0x31, 0x1e}, {0x04, 0x3c}, {0x19, 0x00}, {0x03, 0x00}};

  for (size_t i = 0; i < sizeof(initSeq) / sizeof(initSeq[0]); i++) {
    if (!codecWrite(initSeq[i][0], initSeq[i][1])) {
      Serial.printf("ES8388 init failed at reg 0x%02X\n", initSeq[i][0]);
      return false;
    }
  }

#ifdef FUNC_GPIO0_CLK_OUT1
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
#endif
  WRITE_PERI_REG(PIN_CTRL, 0xFFF0);

  pinMode(Pins::PA_EN, OUTPUT);
  digitalWrite(Pins::PA_EN, HIGH);

  unmuteOutputs();
  setMainDacVolume(100);
  setHeadphoneVolume(100);
  setSpeakerVolume(100);

  uint8_t hpL = 0;
  uint8_t hpR = 0;
  uint8_t mainL = 0;
  if (codecRead(0x30, hpL) && codecRead(0x31, hpR) && codecRead(0x1A, mainL)) {
    Serial.printf("Codec volume HP(L/R)=0x%02X/0x%02X MAIN=0x%02X\n", hpL, hpR, mainL);
  }

  return true;
}

}  // namespace CodecES8388
