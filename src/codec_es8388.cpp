#include "codec_es8388.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace {

constexpr uint8_t kEs8388Addr = 0x10;
constexpr uint32_t kCodecI2cClockHz = 400000U;

// Use unity analog gain with the full-level digital mixer: 0x1E = 0 dB.
// The previous 0x21 (+4.5 dB) boosted an already attenuated digital signal.
constexpr uint8_t kAnalogOutputPlaybackCode = 0x1E;
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
constexpr uint8_t kRegChipPower = 0x02;
constexpr uint8_t kRegAdcPower = 0x03;
constexpr uint8_t kRegDacPower = 0x04;
// Preserve the original board clock/reference state. On this device, 0xAA
// introduced output noise; restoring 0x00 alone removed it in the hardware test.
// Analog capture is independently powered down by ADCPOWER below.
constexpr uint8_t kChipPowerBoardProfile = 0x00;
// Power down both analog inputs, ADCs, microphone bias and ADC bias generator.
constexpr uint8_t kAdcPowerOff = 0xFC;
// Keep both output pairs available: AudioKit module revisions differ in wiring.
// External L/R speaker amplifiers are disabled independently by PA_EN = LOW.
constexpr uint8_t kDacPowerPlayback = 0x3C;
constexpr uint8_t kRegDacVolumeLeft = 0x1A;
constexpr uint8_t kRegDacVolumeRight = 0x1B;
constexpr uint8_t kRegOut1Left = 0x2E;
constexpr uint8_t kRegOut1Right = 0x2F;
constexpr uint8_t kRegOut2Left = 0x30;
constexpr uint8_t kRegOut2Right = 0x31;

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

void setOut2Volume(uint8_t percent) {
  const uint8_t clamped = constrain(percent, 0, 100);
  const uint8_t code = static_cast<uint8_t>((kAnalogOutputPlaybackCode * clamped) / 100U);
  codecWrite(kRegOut2Left, code);
  codecWrite(kRegOut2Right, code);
}

void setOut1Volume(uint8_t percent) {
  const uint8_t clamped = constrain(percent, 0, 100);
  const uint8_t code = static_cast<uint8_t>((kAnalogOutputPlaybackCode * clamped) / 100U);
  codecWrite(kRegOut1Left, code);
  codecWrite(kRegOut1Right, code);
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

  // Playback-only ES8388 profile; output is one headphones-out channel via 600:600.
  const uint8_t initSeq[][2] = {
      {kRegDacControl3, kDacControl3Muted},  // Mute, preserving default control bits.
      {0x01, 0x50},             // Chip Control 2: retain analog reference/bias profile.
      {kRegAdcPower, kAdcPowerOff}, // Line-in, microphones and ADC remain off.
      {kRegChipPower, kChipPowerBoardProfile}, // Retain original clock/reference state.
      {0x08, 0x00},             // Master mode control: codec in slave timing mode.
      {kRegDacPower, kDacPowerPlayback},     // DAC power/routing pre-configuration.
      {0x00, 0x12},             // Chip Control 1: retain board clock/VMID profile.
      {0x17, 0x18},             // DAC Control 1: I2S, 16-bit words.
      {0x18, 0x02},             // DAC Control 2: MCLK/LRCK ratio.
      {0x26, 0x1B},             // Analog mixer input selection (bypass disabled below).
      {0x27, 0x90},             // Left DAC to mixer on; analog input bypass off.
      {0x2A, 0x90},             // Right DAC to mixer on; analog input bypass off.
      {0x2B, 0x80},             // DAC/ADC share LRCK; use DAC timing.
      {0x2D, 0x00},             // DAC Control 23: output resistance default.
      {kRegDacVolumeRight, 0x00},   // Start DAC digital volume at 0 dB.
      {kRegDacVolumeLeft, 0x00},    // Start DAC digital volume at 0 dB.
      {0x0F, 0x24},             // ADC Control 7: explicitly mute ADC digital output.
      {kRegOut1Left, 0x1E},   // LOUT1/ROUT1 analog gain baseline.
      {kRegOut1Right, 0x1E},  // LOUT1/ROUT1 analog gain baseline.
      {kRegOut2Left, 0x1E}, // LOUT2/ROUT2 analog gain baseline.
      {kRegOut2Right, 0x1E},// LOUT2/ROUT2 analog gain baseline.
      {kRegDacPower, kDacPowerPlayback},     // Final DAC output path enable mask.
      {kRegDacControl3, kDacControl3Muted},  // Keep muted until I2S is running.
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
  setOut2Volume(100);
  setOut1Volume(100);

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

  // Unmuting playback must never enable the unused Class-D speaker outputs.
  digitalWrite(Pins::PA_EN, LOW);
  return true;
}

}  // namespace CodecES8388
