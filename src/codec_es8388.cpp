#include "codec_es8388.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace {

constexpr uint8_t kEs8388Addr = 0x10;
constexpr uint32_t kCodecI2cClockHz = 400000U;

// ES8388 analog output volume registers use a 0..0x21 scale.
constexpr uint8_t kAnalogOutputVolumeMaxCode = 0x21;
// ES8388 DAC digital volume uses attenuation codes where 0 is 0 dB and 96 is ~-96 dB.
constexpr uint8_t kDacVolumeMaxAttenuationCode = 96;

// Register addresses used by this board profile.
constexpr uint8_t kRegDacControl3 = 0x19;
// ES8388 datasheet section 6.3.3: reset value 0x22, soft ramp at bit 5.
// Preserve the default control bits, as in RudeBox commit 6be0d1e, to avoid
// the reported pop after sustained digital silence. Mute changes only bit 2.
constexpr uint8_t kDacControl3Playback = 0x22;
constexpr uint8_t kDacMuteMask = 1U << 2;
constexpr uint8_t kDacControl3Muted = kDacControl3Playback | kDacMuteMask;
constexpr uint8_t kRegDacPower = 0x04;
constexpr uint8_t kRegDacVolumeLeft = 0x1A;
constexpr uint8_t kRegDacVolumeRight = 0x1B;
constexpr uint8_t kRegSpeakerOutLeft = 0x2E;
constexpr uint8_t kRegSpeakerOutRight = 0x2F;
constexpr uint8_t kRegHeadphoneOutLeft = 0x30;
constexpr uint8_t kRegHeadphoneOutRight = 0x31;

TwoWire gCodecWire(1);
bool gCodecReady = false;

bool codecWrite(uint8_t reg, uint8_t val) {
  gCodecWire.beginTransmission(kEs8388Addr);
  gCodecWire.write(reg);
  gCodecWire.write(val);
  return gCodecWire.endTransmission() == 0;
}

bool codecRead(uint8_t reg, uint8_t &val) {
  gCodecWire.beginTransmission(kEs8388Addr);
  gCodecWire.write(reg);
  if (gCodecWire.endTransmission(false) != 0) return false;
  if (gCodecWire.requestFrom(static_cast<uint8_t>(kEs8388Addr), static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  val = gCodecWire.read();
  return true;
}

void setHeadphoneVolume(uint8_t percent) {
  const uint8_t clamped = constrain(percent, 0, 100);
  const uint8_t code = static_cast<uint8_t>((kAnalogOutputVolumeMaxCode * clamped) / 100U);
  codecWrite(kRegHeadphoneOutLeft, code);
  codecWrite(kRegHeadphoneOutRight, code);
}

void setSpeakerVolume(uint8_t percent) {
  const uint8_t clamped = constrain(percent, 0, 100);
  const uint8_t code = static_cast<uint8_t>((kAnalogOutputVolumeMaxCode * clamped) / 100U);
  codecWrite(kRegSpeakerOutLeft, code);
  codecWrite(kRegSpeakerOutRight, code);
}

void setMainDacVolume(uint8_t percent) {
  const uint8_t clamped = constrain(percent, 0, 100);
  const uint8_t attenuationCode =
      static_cast<uint8_t>(kDacVolumeMaxAttenuationCode -
                           ((kDacVolumeMaxAttenuationCode * clamped) / 100U));
  codecWrite(kRegDacVolumeLeft, attenuationCode);
  codecWrite(kRegDacVolumeRight, attenuationCode);
}

}  // namespace

namespace CodecES8388 {

bool init() {
  gCodecReady = false;
  pinMode(Pins::PA_EN, OUTPUT);
  digitalWrite(Pins::PA_EN, LOW);
  gCodecWire.begin(Pins::I2C_SDA, Pins::I2C_SCL, kCodecI2cClockHz);
  gCodecWire.beginTransmission(kEs8388Addr);
  if (gCodecWire.endTransmission() != 0) {
    return false;
  }

  // Board-validated ES8388 startup profile.
  const uint8_t initSeq[][2] = {
      {kRegDacControl3, kDacControl3Muted},  // Mute, preserving default control bits.
      {0x01, 0x50},             // Control1: use board MCLK setup profile.
      {0x02, 0x00},             // Chip power: enable core analog/digital blocks.
      {0x08, 0x00},             // Master mode control: codec in slave timing mode.
      {kRegDacPower, 0x3E},     // DAC power/routing pre-configuration.
      {0x00, 0x12},             // Control2: board-specific clock source bits.
      {0x17, 0x18},             // ADC control: input PGA gain/routing profile.
      {0x18, 0x02},             // ADC control: data format and serial clock edge.
      {0x26, 0x1B},             // DAC serial: I2S framing/word length setup.
      {0x27, 0x90},             // DAC control: de-emphasis and soft-ramp profile.
      {0x2A, 0x90},             // DAC mixer: right-channel route profile.
      {0x2B, 0x80},             // DAC mixer: left-channel route profile.
      {0x2D, 0x00},             // DAC limiter/ALC disabled for transparent playback.
      {kRegDacVolumeRight, 0x00},   // Start DAC digital volume at 0 dB.
      {kRegDacVolumeLeft, 0x00},    // Start DAC digital volume at 0 dB.
      {0x03, 0xFF},             // ADC power: temporary reset mask before final enable.
      {0x09, 0x88},             // ADC digital format: I2S 16-bit board profile.
      {0x0A, 0xF0},             // ADC input selection and PGA source mapping.
      {0x0B, 0x80},             // ADC control: enable high-pass/offset cancellation path.
      {0x0C, 0x0E},             // ADC control: gain/boost profile used by this hardware.
      {0x0D, 0x02},             // ADC control: mono/stereo path and clock divider profile.
      {0x10, 0x20},             // ADC digital volume left channel default.
      {0x11, 0x20},             // ADC digital volume right channel default.
      {kRegSpeakerOutLeft, 0x1E},   // Speaker output analog gain baseline.
      {kRegSpeakerOutRight, 0x1E},  // Speaker output analog gain baseline.
      {kRegHeadphoneOutLeft, 0x1E}, // Headphone analog gain baseline.
      {kRegHeadphoneOutRight, 0x1E},// Headphone analog gain baseline.
      {kRegDacPower, 0x3C},     // Final DAC output path enable mask.
      {kRegDacControl3, kDacControl3Muted},  // Keep muted until I2S is running.
      {0x03, 0x00},             // Final ADC power enable state.
  };

  for (size_t i = 0; i < sizeof(initSeq) / sizeof(initSeq[0]); i++) {
    if (!codecWrite(initSeq[i][0], initSeq[i][1])) {
      return false;
    }
  }

#ifdef FUNC_GPIO0_CLK_OUT1
  // Route internal clock to GPIO0; used as MCLK source for some ES8388 board revisions.
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
#endif
  WRITE_PERI_REG(PIN_CTRL, 0xFFF0);

  setMainDacVolume(100);
  setHeadphoneVolume(100);
  setSpeakerVolume(100);

  uint8_t control3 = 0;
  if (!codecRead(kRegDacControl3, control3) || control3 != kDacControl3Muted) {
    return false;
  }
  gCodecReady = true;
  return true;
}

bool unmute() {
  if (!gCodecReady) return false;
  uint8_t value = 0;
  if (!codecRead(kRegDacControl3, value)) return false;
  // Reject an unexpected startup profile instead of silently clearing its mute.
  if ((value & static_cast<uint8_t>(~kDacMuteMask)) != kDacControl3Playback) {
    return false;
  }
  if (!codecWrite(kRegDacControl3, static_cast<uint8_t>(value & ~kDacMuteMask))) {
    return false;
  }
  if (!codecRead(kRegDacControl3, value) || value != kDacControl3Playback) {
    return false;
  }

  if (!codecRead(kRegDacPower, value)) return false;
  if (!codecWrite(kRegDacPower, static_cast<uint8_t>(value | (3U << 4) | (3U << 2)))) {
    return false;
  }

  digitalWrite(Pins::PA_EN, HIGH);
  return true;
}

}  // namespace CodecES8388
