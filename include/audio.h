#pragma once

#include <Arduino.h>
#include <stdint.h>

class Audio {
 public:
  static constexpr uint8_t kVoiceCount = 8;

  struct RuntimeStats {
    uint8_t activeVoices = 0;
    uint8_t activeVoicePeak = 0;
    uint32_t voiceStealCount = 0;
  };

  struct Impl;

  Audio();
  ~Audio();

  void begin();
  void update();
  void playSamplePath(const String &samplePath, uint8_t volume = 100, int16_t retriggerGroupId = -1);
  bool playSampleRam(const uint8_t *pcmData,
                     uint32_t dataBytes,
                     uint16_t channelCount,
                     uint32_t sampleRate,
                     uint16_t bitsPerSample,
                     uint8_t volume = 100,
                     int16_t retriggerGroupId = -1);
  RuntimeStats runtimeStats() const;
  uint32_t voiceStealCount() const;

 private:
  Impl *impl_ = nullptr;
};
