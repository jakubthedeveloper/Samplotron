#pragma once

#include <Arduino.h>

class Audio {
 public:
  Audio() = default;
  ~Audio();

  void begin();
  void update();
  void playSamplePath(const String &samplePath);

 private:
  void resetI2SIfNeeded();

  class AudioGeneratorWAV *wav_ = nullptr;
  class AudioFileSourceSD *file_ = nullptr;
  class AudioOutputI2S *out_ = nullptr;
  bool i2sWasStarted_ = false;
};
