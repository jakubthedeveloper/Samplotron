#pragma once

#include "AudioOutput.h"
#include <cstddef>

namespace AudioInternal {

class SamplerMixer;

class SamplerMixerInput : public AudioOutput {
 public:
  SamplerMixerInput(SamplerMixer *parent, int id);
  ~SamplerMixerInput() override;
  bool SetRate(int hz) override;
  bool SetChannels(int count) override;
  bool SetGain(float gain) override;
  bool begin() override;
  bool stop() override;
  bool ConsumeSample(int16_t sample[2]) override;
 private:
  SamplerMixer *parent_;
  int id_;
  int channels_ = 2;
  float gain_ = 1.0f;
};

// Float accumulation, then linked stereo look-ahead limiting, then PCM16.
// No allocation in the sample path. All state advances only on sink acceptance.
class SamplerMixer {
 public:
  static constexpr int kMaxInputs = 32;
  static constexpr int kLookaheadSamples = 64;  // 1.45 ms at 44.1 kHz.
  static constexpr float kCeiling = 32767.0f;
  static constexpr float kReleaseSeconds = 0.050f;
  SamplerMixer(int bufferSamples, AudioOutput *sink);
  ~SamplerMixer();
  SamplerMixer(const SamplerMixer &) = delete;
  SamplerMixer &operator=(const SamplerMixer &) = delete;
  SamplerMixerInput *NewInput();
  bool loop();
 private:
  friend class SamplerMixerInput;
  struct Frame { float left = 0; float right = 0; float bound = 1; };
  bool start(int id);
  bool consume(int id, float left, float right);
  bool emit(float left, float right);
  AudioOutput *sink_;
  Frame *mix_ = nullptr;
  int capacity_;
  int read_ = 0;
  bool sinkStarted_ = false;
  bool allocated_[kMaxInputs] = {};
  bool running_[kMaxInputs] = {};
  int queued_[kMaxInputs] = {};
  Frame delay_[kLookaheadSamples];
  int delayHead_ = 0;
  float gain_ = 1;
  float releaseCoeff_;
};

}  // namespace AudioInternal
