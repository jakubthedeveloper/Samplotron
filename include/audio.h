#pragma once

#include <Arduino.h>
#include <stdint.h>

class Audio {
 public:
  Audio() = default;
  ~Audio();

  void begin();
  void update();
  void playSamplePath(const String &samplePath, uint8_t volume = 127);
  bool playSampleRam(const uint8_t *pcmData,
                     uint32_t dataBytes,
                     uint16_t channelCount,
                     uint32_t sampleRate,
                     uint16_t bitsPerSample,
                     uint8_t volume = 127);

 private:
  void applyVolume(uint8_t volume);
  void resetI2SIfNeeded();

  class AudioGeneratorWAV *wav_ = nullptr;
  class AudioFileSource *file_ = nullptr;
  class AudioOutputI2S *out_ = nullptr;
  bool i2sWasStarted_ = false;
};
