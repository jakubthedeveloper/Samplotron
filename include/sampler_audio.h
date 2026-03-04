#pragma once

class SamplerAudio {
 public:
  SamplerAudio() = default;
  ~SamplerAudio();

  void begin();
  void update();
  void playSample(int sampleNumber);

 private:
  void resetI2SIfNeeded();

  class AudioGeneratorWAV *wav_ = nullptr;
  class AudioFileSourceSD *file_ = nullptr;
  class AudioOutputI2S *out_ = nullptr;
  bool i2sWasStarted_ = false;
};
