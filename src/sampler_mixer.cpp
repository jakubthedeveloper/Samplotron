#include "sampler_mixer.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace AudioInternal {

SamplerMixerInput::SamplerMixerInput(SamplerMixer *parent, int id) : parent_(parent), id_(id) {}
SamplerMixerInput::~SamplerMixerInput() {
  parent_->allocated_[id_] = false;
  parent_->running_[id_] = false;
}
bool SamplerMixerInput::SetRate(int hz) {
  // The shared output has no resampler. A voice must not retune other voices.
  return hz == 44100;
}
bool SamplerMixerInput::SetChannels(int count) {
  if (count != 1 && count != 2) return false;
  channels_ = count;
  return true;
}
bool SamplerMixerInput::SetGain(float gain) {
  if (!std::isfinite(gain)) return false;
  gain_ = std::max(0.0f, std::min(1.0f, gain));
  return true;
}
bool SamplerMixerInput::begin() { return parent_->start(id_); }
bool SamplerMixerInput::stop() {
  parent_->running_[id_] = false;
  return true;
}
bool SamplerMixerInput::ConsumeSample(int16_t sample[2]) {
  return parent_->consume(id_, sample[0] * gain_,
                         sample[channels_ == 1 ? 0 : 1] * gain_);
}

SamplerMixer::SamplerMixer(int bufferSamples, AudioOutput *sink)
    : sink_(sink), capacity_(bufferSamples),
      releaseCoeff_(std::exp(-1.0f / (44100.0f * kReleaseSeconds))) {
  if (capacity_ > 1) mix_ = new (std::nothrow) Frame[capacity_];
}
SamplerMixer::~SamplerMixer() { delete[] mix_; }
SamplerMixerInput *SamplerMixer::NewInput() {
  if (!mix_ || !sink_) return nullptr;
  for (int i = 0; i < kMaxInputs; ++i) {
    if (allocated_[i]) continue;
    auto *input = new (std::nothrow) SamplerMixerInput(this, i);
    if (!input) return nullptr;
    allocated_[i] = true;
    // Retain already accumulated tails if this slot is reused.
    return input;
  }
  return nullptr;
}
bool SamplerMixer::start(int id) {
  if (!sinkStarted_) {
    if (!sink_->SetRate(44100) || !sink_->SetChannels(2) || !sink_->begin()) return false;
    sinkStarted_ = true;
  }
  running_[id] = true;
  return true;
}

bool SamplerMixer::emit(float left, float right) {
  const Frame &oldest = delay_[delayHead_];
  const float nextGain = std::min(oldest.bound, 1.0f - (1.0f - gain_) * releaseCoeff_);
  // Bounds were computed on the wide sum before narrowing. The clamp only
  // guards floating point rounding at the PCM16 boundary, not mix overloads.
  auto pcm = [](float value) {
    return static_cast<int16_t>(std::lround(std::max(-kCeiling, std::min(kCeiling, value))));
  };
  int16_t out[2] = {pcm(oldest.left * nextGain), pcm(oldest.right * nextGain)};
  if (!sink_->ConsumeSample(out)) return false;

  gain_ = nextGain;
  const float peak = std::max(std::fabs(left), std::fabs(right));
  const float required = peak > kCeiling ? kCeiling / peak : 1.0f;
  // Schedule a gain bound from the current gain to the future peak. This
  // ramps down BEFORE overload and also prevents release from rising too far
  // before a later peak. Overlapping ramps take their minimum.
  // Each frame also retains its own required bound until it reaches the sink.
  if (required < 1.0f) {
    const float slope = (required - gain_) / kLookaheadSamples;
    for (int i = 1; i < kLookaheadSamples; ++i) {
      Frame &future = delay_[(delayHead_ + i) % kLookaheadSamples];
      future.bound = std::min(future.bound, gain_ + slope * i);
    }
  }
  delay_[delayHead_] = {left, right, required};
  delayHead_ = (delayHead_ + 1) % kLookaheadSamples;
  return true;
}

bool SamplerMixer::loop() {
  if (!mix_ || !sinkStarted_) return false;
  for (;;) {
    for (int i = 0; i < kMaxInputs; ++i) {
      if (running_[i] && queued_[i] == 0) return true;
    }
    if (!emit(mix_[read_].left, mix_[read_].right)) return true;
    mix_[read_] = {};
    read_ = (read_ + 1) % capacity_;
    for (int &count : queued_) if (count > 0) --count;
    // With no running writers, continue flushing tails and then digital silence.
  }
}
bool SamplerMixer::consume(int id, float left, float right) {
  if (!running_[id]) return false;
  loop();
  if (queued_[id] >= capacity_ - 1) return false;
  Frame &frame = mix_[(read_ + queued_[id]) % capacity_];
  frame.left += left;
  frame.right += right;
  ++queued_[id];
  return true;
}

}  // namespace AudioInternal
