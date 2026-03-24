#include "audio.h"
#include "audio_internal.h"

#include <Arduino.h>

#include "pins.h"

struct Audio::Impl : AudioInternal::EngineState {};

Audio::Audio() = default;

Audio::~Audio() {
  if (!impl_) return;

  for (int i = 0; i < kVoiceCount; i++) {
    AudioInternal::stopVoice(impl_->voices[i]);
  }

  impl_->streamManager.shutdown();

  for (int i = 0; i < kVoiceCount; i++) {
    delete impl_->voices[i].wav;
    impl_->voices[i].wav = nullptr;
    delete impl_->voices[i].stub;
    impl_->voices[i].stub = nullptr;
    delete impl_->voices[i].budgetedOut;
    impl_->voices[i].budgetedOut = nullptr;
    delete impl_->voices[i].ramSource;
    impl_->voices[i].ramSource = nullptr;
    impl_->voices[i].activeSource = nullptr;
  }

  delete impl_->mixer;
  impl_->mixer = nullptr;
  delete impl_->limitedOut;
  impl_->limitedOut = nullptr;
  delete impl_->out;
  impl_->out = nullptr;

  delete impl_;
  impl_ = nullptr;
}

void Audio::begin() {
  if (impl_) return;

  impl_ = new Impl();
  if (!impl_) {
    return;
  }

  impl_->out = new AudioInternal::StableAudioOutputI2S(
      0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_ENABLE);
  if (!impl_->out) {
    delete impl_;
    impl_ = nullptr;
    return;
  }
  impl_->out->SetPinout(Pins::I2S_BCLK, Pins::I2S_LRC, Pins::I2S_DOUT);
  impl_->out->SetGain(1.0f);

  impl_->limitedOut =
      new AudioInternal::SimpleLimiterAudioOutput(impl_->out, &impl_->waveformCapture);
  if (!impl_->limitedOut) {
    delete impl_->limitedOut;
    impl_->limitedOut = nullptr;
    delete impl_->out;
    impl_->out = nullptr;
    delete impl_;
    impl_ = nullptr;
    return;
  }

  impl_->mixer = new AudioOutputMixer(AudioInternal::kMixerBufferSamples, impl_->limitedOut);
  if (!impl_->mixer) {
    delete impl_->limitedOut;
    impl_->limitedOut = nullptr;
    delete impl_->out;
    impl_->out = nullptr;
    delete impl_;
    impl_ = nullptr;
    return;
  }

  if (!impl_->streamManager.begin(kVoiceCount)) {
    delete impl_->mixer;
    impl_->mixer = nullptr;
    delete impl_->limitedOut;
    impl_->limitedOut = nullptr;
    delete impl_->out;
    impl_->out = nullptr;
    delete impl_;
    impl_ = nullptr;
    return;
  }

  for (int i = 0; i < kVoiceCount; i++) {
    AudioInternal::VoiceState &voice = impl_->voices[i];
    voice.wav = new AudioInternal::FreshStartAudioGeneratorWAV();
    voice.ramSource = new AudioInternal::AudioFileSourceRamWav();
    voice.stub = impl_->mixer->NewInput();
    voice.budgetedOut = voice.stub ? new AudioInternal::BudgetedAudioOutput(voice.stub) : nullptr;

    if (voice.wav && voice.ramSource && voice.stub && voice.budgetedOut) {
      voice.stub->SetGain(1.0f);
    }
  }

  AudioInternal::refreshStats(impl_);
}

void Audio::update() {
  if (!impl_) return;
  const uint32_t nowUs = micros();

  AudioInternal::refreshStats(impl_);
  AudioInternal::updateDynamicMixGain(impl_, nowUs);

  bool stateChanged = false;

  for (int i = 0; i < kVoiceCount; i++) {
    AudioInternal::VoiceState &voice = impl_->voices[i];
    if (!voice.active) {
      continue;
    }

    if (!voice.wav || !voice.wav->isRunning()) {
      AudioInternal::stopVoice(voice);
      stateChanged = true;
      continue;
    }

    if (voice.stopping && voice.fadeOutUs > 0) {
      const uint32_t fadeElapsedUs = nowUs - voice.stopStartUs;
      if (fadeElapsedUs >= voice.fadeOutUs) {
        AudioInternal::stopVoice(voice);
        stateChanged = true;
        continue;
      }
    }

    if (voice.budgetedOut) {
      voice.budgetedOut->resetBudget(AudioInternal::kVoiceLoopSampleBudget);
    }

    AudioInternal::updateVoiceGain(voice, impl_->dynamicMixGain, nowUs);

    if (!voice.wav->loop()) {
      if (voice.stopping || !voice.loopEnabled || !AudioInternal::restartVoiceLoop(impl_, i)) {
        AudioInternal::stopVoice(voice);
        stateChanged = true;
      }
    }
  }

  if (impl_->mixer) {
    impl_->mixer->loop();
  }

  if (stateChanged) {
    AudioInternal::refreshStats(impl_);
  }
}

void Audio::playSamplePath(const String &samplePath,
                           uint8_t volume,
                           int16_t retriggerGroupId,
                           bool loopEnabled) {
  if (!impl_ || samplePath.length() == 0) return;
  const uint32_t fadeInUs = 0;

  bool voiceWasStolen = false;
  const int voiceIndex = AudioInternal::allocateVoiceSlot(impl_, retriggerGroupId, voiceWasStolen);
  (void)voiceWasStolen;
  if (voiceIndex < 0) {
    return;
  }

  AudioInternal::VoiceState &voice = impl_->voices[voiceIndex];

  AudioInternal::stopVoice(voice);
  if (!AudioInternal::beginVoiceFromPath(
          impl_, voiceIndex, samplePath, volume, retriggerGroupId, loopEnabled, fadeInUs)) {
    AudioInternal::refreshStats(impl_);
    return;
  }

  AudioInternal::refreshStats(impl_);
}

void Audio::stopAllVoices() {
  if (!impl_) return;
  for (int i = 0; i < kVoiceCount; i++) {
    AudioInternal::stopVoice(impl_->voices[i]);
  }
  AudioInternal::refreshStats(impl_);
}

void Audio::fadeOutAllVoices(uint32_t fadeOutUs) {
  if (!impl_) return;
  if (fadeOutUs == 0) {
    stopAllVoices();
    return;
  }
  if (AudioInternal::requestStopAllVoices(impl_, fadeOutUs)) {
    AudioInternal::refreshStats(impl_);
  }
}

void Audio::stopLoopingVoicesForGroup(int16_t retriggerGroupId) {
  if (!impl_ || retriggerGroupId < 0) return;
  bool changed = false;
  for (int i = 0; i < kVoiceCount; i++) {
    AudioInternal::VoiceState &voice = impl_->voices[i];
    if (!voice.active || !voice.loopEnabled) continue;
    if (voice.retriggerGroupId != retriggerGroupId) continue;
    AudioInternal::stopVoice(voice);
    changed = true;
  }
  if (changed) {
    AudioInternal::refreshStats(impl_);
  }
}

void Audio::setLoopEnabledForGroup(int16_t retriggerGroupId, bool loopEnabled) {
  if (!impl_ || retriggerGroupId < 0) return;
  for (int i = 0; i < kVoiceCount; i++) {
    AudioInternal::VoiceState &voice = impl_->voices[i];
    if (!voice.active) continue;
    if (voice.retriggerGroupId != retriggerGroupId) continue;
    voice.loopEnabled = loopEnabled;
  }
}

bool Audio::playSampleRam(const uint8_t *pcmData,
                          uint32_t dataBytes,
                          uint16_t channelCount,
                          uint32_t sampleRate,
                          uint16_t bitsPerSample,
                          uint8_t volume,
                          int16_t retriggerGroupId,
                          bool loopEnabled) {
  if (!impl_ || !pcmData || dataBytes == 0) return false;
  const uint32_t fadeInUs = 0;

  bool voiceWasStolen = false;
  const int voiceIndex = AudioInternal::allocateVoiceSlot(impl_, retriggerGroupId, voiceWasStolen);
  (void)voiceWasStolen;
  if (voiceIndex < 0) {
    return false;
  }

  AudioInternal::VoiceState &voice = impl_->voices[voiceIndex];

  AudioInternal::stopVoice(voice);
  const bool started = AudioInternal::beginVoiceFromRam(impl_,
                                                        voiceIndex,
                                                        pcmData,
                                                        dataBytes,
                                                        channelCount,
                                                        sampleRate,
                                                        bitsPerSample,
                                                        volume,
                                                        retriggerGroupId,
                                                        loopEnabled,
                                                        fadeInUs);
  AudioInternal::refreshStats(impl_);
  return started;
}

Audio::RuntimeStats Audio::runtimeStats() const {
  if (!impl_) return RuntimeStats{};
  return impl_->stats;
}

uint32_t Audio::voiceStealCount() const {
  if (!impl_) return 0;
  return impl_->stats.voiceStealCount;
}

bool Audio::waveformSnapshot(WaveformSnapshot &snapshot) const {
  if (!impl_) {
    snapshot.validPoints = 0;
    return false;
  }
  return AudioInternal::copyWaveformSnapshot(impl_->waveformCapture, snapshot);
}
