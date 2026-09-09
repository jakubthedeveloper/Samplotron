#include "budgeted_audio_output.h"

namespace AudioInternal {

BudgetedAudioOutput::BudgetedAudioOutput(AudioOutput *sink) : sink_(sink) {}

void BudgetedAudioOutput::resetBudget(uint16_t sampleCount) { budgetSamples_ = sampleCount; }

void BudgetedAudioOutput::resetFadeEnvelope() {
  fadeEnvelopeQ15_ = 32768;
  fadeStepQ15_ = 0;
  fadeSamplesRemaining_ = 0;
  fadeActive_ = false;
  fadeComplete_ = false;
}

void BudgetedAudioOutput::beginFadeOut(uint32_t fadeOutUs) {
  if (fadeOutUs == 0) {
    fadeEnvelopeQ15_ = 0;
    fadeStepQ15_ = 0;
    fadeSamplesRemaining_ = 0;
    fadeActive_ = false;
    fadeComplete_ = true;
    return;
  }

  if (sampleRateHz_ <= 0) {
    sampleRateHz_ = 44100;
  }

  uint64_t fadeSamples =
      (static_cast<uint64_t>(sampleRateHz_) * static_cast<uint64_t>(fadeOutUs) + 999999ULL) /
      1000000ULL;
  if (fadeSamples == 0) {
    fadeSamples = 1;
  }

  fadeSamplesRemaining_ = static_cast<uint32_t>(fadeSamples);
  fadeStepQ15_ = (fadeSamplesRemaining_ > 0) ? (fadeEnvelopeQ15_ / fadeSamplesRemaining_) : fadeEnvelopeQ15_;
  if (fadeStepQ15_ == 0 && fadeEnvelopeQ15_ > 0) {
    fadeStepQ15_ = 1;
  }
  fadeActive_ = true;
  fadeComplete_ = false;
}

bool BudgetedAudioOutput::isFadeOutComplete() const { return fadeComplete_; }

bool BudgetedAudioOutput::SetRate(int hz) {
  if (hz > 0) {
    sampleRateHz_ = hz;
  }
  return sink_ && sink_->SetRate(hz);
}

bool BudgetedAudioOutput::SetChannels(int channels) { return sink_ && sink_->SetChannels(channels); }

bool BudgetedAudioOutput::begin() { return sink_ && sink_->begin(); }

bool BudgetedAudioOutput::stop() {
  budgetSamples_ = 0;
  resetFadeEnvelope();
  return sink_ && sink_->stop();
}

bool BudgetedAudioOutput::loop() { return sink_ && sink_->loop(); }

bool BudgetedAudioOutput::ConsumeSample(int16_t sample[2]) {
  if (!sink_ || budgetSamples_ == 0) return false;

  const uint32_t envelopeQ15 = fadeEnvelopeQ15_;
  int16_t output[2] = {sample[0], sample[1]};
  if (envelopeQ15 < 32768U) {
    for (int c = 0; c < 2; ++c) {
      output[c] = static_cast<int16_t>((static_cast<int32_t>(sample[c]) *
                                       static_cast<int32_t>(envelopeQ15) + 16384) >> 15);
    }
  }
  if (!sink_->ConsumeSample(output)) return false;

  if (fadeActive_) {
    if (fadeSamplesRemaining_ > 0) {
      if (fadeEnvelopeQ15_ > fadeStepQ15_) {
        fadeEnvelopeQ15_ -= fadeStepQ15_;
      } else {
        fadeEnvelopeQ15_ = 0;
      }
      fadeSamplesRemaining_--;
    }
    if (fadeSamplesRemaining_ == 0) {
      fadeEnvelopeQ15_ = 0;
      fadeStepQ15_ = 0;
      fadeActive_ = false;
      fadeComplete_ = true;
    }
  }

  budgetSamples_--;
  return true;
}

}  // namespace AudioInternal
