#pragma once
#include "AudioOutput.h"
#include <cstdint>

namespace AudioInternal {

class BudgetedAudioOutput : public AudioOutput {
 public:
  explicit BudgetedAudioOutput(AudioOutput *sink);

  void resetBudget(uint16_t sampleCount);
  void resetFadeEnvelope();
  void beginFadeOut(uint32_t fadeOutUs);
  bool isFadeOutComplete() const;

  bool SetRate(int hz) override;
  bool SetChannels(int channels) override;
  bool begin() override;
  bool stop() override;
  bool loop() override;
  bool ConsumeSample(int16_t sample[2]) override;

 private:
  AudioOutput *sink_ = nullptr;
  uint16_t budgetSamples_ = 0;
  int sampleRateHz_ = 44100;
  uint32_t fadeEnvelopeQ15_ = 32768;
  uint32_t fadeStepQ15_ = 0;
  uint32_t fadeSamplesRemaining_ = 0;
  bool fadeActive_ = false;
  bool fadeComplete_ = false;
};

}  // namespace AudioInternal
