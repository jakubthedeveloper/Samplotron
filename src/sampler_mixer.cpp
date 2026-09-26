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
  // Exponential decay of a peak envelope means multiplicative gain recovery.
  // Interpolating the gain toward 1 recovers far too fast after deep limiting
  // (e.g. 32 aligned voices), flattening the final peaks when look-ahead ends.
  const float nextGain = std::min(oldest.bound, std::min(1.0f, gain_ / releaseCoeff_));
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
  // before a later peak. Reach the bound halfway through the look-ahead,
  // then hold it, so a rising waveform is not limited one point at a time.
  // Overlapping ramps take their minimum.
  // Each frame also retains its own required bound until it reaches the sink.
  if (required < 1.0f) {
    const float slope = (required - gain_) / kAttackSamples;
    for (int i = 1; i < kLookaheadSamples; ++i) {
      Frame &future = delay_[(delayHead_ + i) % kLookaheadSamples];
      future.bound = std::min(future.bound, gain_ + slope * std::min(i, kAttackSamples));
    }
  }
  delay_[delayHead_] = {left, right, required};
  delayHead_ = (delayHead_ + 1) % kLookaheadSamples;
  return true;
}

int SamplerMixer::queued(int id) const {
  // Wrap-safe: positions are absolute frame counters.
  const int32_t ahead = static_cast<int32_t>(written_[id] - emitted_);
  return ahead > 0 ? ahead : 0;
}

bool SamplerMixer::loop() {
  if (!mix_ || !sinkStarted_) return false;
  // A frame is final once every running input has written it. Scan inputs once
  // per call, not once per consumed sample.
  int ready = -1;
  for (int i = 0; i < kMaxInputs; ++i) {
    if (!running_[i]) continue;
    const int count = queued(i);
    if (ready < 0 || count < ready) ready = count;
  }
  // With no running writers, continue flushing tails and then digital silence.
  while (ready != 0) {
    if (!emit(mix_[read_].left, mix_[read_].right)) return true;
    mix_[read_] = {};
    read_ = (read_ + 1) % capacity_;
    ++emitted_;
    if (ready > 0) --ready;
  }
  return true;
}
bool SamplerMixer::consume(int id, float left, float right) {
  if (!running_[id]) return false;
  int count = queued(id);
  // Emission normally happens once per Audio::update(); only a full queue
  // forces it here.
  if (count >= capacity_ - 1) {
    loop();
    count = queued(id);
    if (count >= capacity_ - 1) return false;
  }
  Frame &frame = mix_[(read_ + count) % capacity_];
  frame.left += left;
  frame.right += right;
  written_[id] = emitted_ + count + 1;
  return true;
}

}  // namespace AudioInternal
