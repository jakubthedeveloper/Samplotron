#pragma once

#include <Arduino.h>

class SamplerAudio {
 public:
  SamplerAudio() = default;
  ~SamplerAudio();

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
