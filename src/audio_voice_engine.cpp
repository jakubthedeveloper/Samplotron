#include "audio_internal.h"

#include <Arduino.h>
#include <limits.h>
#include <math.h>
#include <string.h>

namespace AudioInternal {

namespace {

float clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

float dynamicMixGainTarget(uint8_t activeVoices) {
  if (activeVoices == 0) return kDynamicMixMaxGain;

  const float gain = 1.0f / static_cast<float>(activeVoices);
  if (gain < kDynamicMixMinGain) return kDynamicMixMinGain;
  if (gain > kDynamicMixMaxGain) return kDynamicMixMaxGain;
  return gain;
}

float smoothGain(float current, float target, uint32_t dtUs, uint32_t timeConstantUs) {
  if (dtUs == 0 || timeConstantUs == 0) return target;
  const float alpha = clamp01(static_cast<float>(dtUs) / static_cast<float>(timeConstantUs));
  return current + ((target - current) * alpha);
}

float voiceFadeInRatio(const VoiceState &voice, uint32_t nowUs) {
  if (!voice.active) return 0.0f;
  if (voice.fadeInUs == 0) return 1.0f;

  const uint32_t elapsedUs = nowUs - voice.startUs;
  const float ratio = static_cast<float>(elapsedUs) / static_cast<float>(voice.fadeInUs);
  return (ratio > 1.0f) ? 1.0f : ratio;
}

float voiceFadeOutRatio(const VoiceState &voice, uint32_t nowUs) {
  if (!voice.active) return 0.0f;
  if (!voice.stopping) return 1.0f;
  if (voice.fadeOutUs == 0) return 0.0f;

  const uint32_t elapsedUs = nowUs - voice.stopStartUs;
  if (elapsedUs >= voice.fadeOutUs) return 0.0f;

  const float ratio = 1.0f - (static_cast<float>(elapsedUs) / static_cast<float>(voice.fadeOutUs));
  return (ratio < 0.0f) ? 0.0f : ratio;
}

}  // namespace

float gainFromVolume(uint8_t volume) {
  const uint8_t clamped = (volume > kVolumeScaleMax) ? kVolumeScaleMax : volume;
  return (static_cast<float>(clamped) / static_cast<float>(kVolumeScaleMax)) * kPerVoiceVolumeScale;
}

void stopVoice(VoiceState &voice) {
  if (voice.budgetedOut) {
    voice.budgetedOut->resetBudget(0);
    voice.budgetedOut->resetFadeEnvelope();
  }
  if (voice.stub) {
    voice.stub->SetGain(0.0f);
  }
  if (voice.wav && voice.wav->isRunning()) {
    voice.wav->stop();
  }
  if (voice.stub) {
    voice.stub->stop();
  }
  if (voice.activeSource) {
    voice.activeSource->close();
    voice.activeSource = nullptr;
  }
  voice.active = false;
  voice.startOrder = 0;
  voice.startUs = 0;
  voice.retriggerGroupId = -1;
  voice.targetGain = 0.0f;
  voice.currentGain = 0.0f;
  voice.fadeInUs = 0;
  voice.stopping = false;
  voice.stopStartUs = 0;
  voice.fadeOutUs = 0;
  voice.volume = 100;
  voice.loopEnabled = false;
  voice.sourceType = VoiceSourceType::None;
  voice.path[0] = '\0';
  voice.ramData = nullptr;
  voice.ramDataBytes = 0;
  voice.channelCount = 0;
  voice.sampleRate = 0;
  voice.bitsPerSample = 0;
}

void requestVoiceStop(VoiceState &voice, uint32_t fadeOutUs) {
  if (!voice.active) return;
  if (fadeOutUs == 0) {
    stopVoice(voice);
    return;
  }

  if (voice.stopping) {
    if (fadeOutUs < voice.fadeOutUs) {
      voice.fadeOutUs = fadeOutUs;
      voice.stopStartUs = micros();
      if (voice.budgetedOut) {
        voice.budgetedOut->beginFadeOut(fadeOutUs);
      }
    }
    return;
  }

  voice.stopping = true;
  voice.stopStartUs = micros();
  voice.fadeOutUs = fadeOutUs;
  if (voice.budgetedOut) {
    voice.budgetedOut->beginFadeOut(fadeOutUs);
  }
  // Keep stop deterministic: no loop restart while voice is tailing out.
  voice.loopEnabled = false;
}

void refreshStats(EngineState *impl) {
  if (!impl) return;

  uint8_t activeVoices = 0;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    if (impl->voices[i].active) {
      activeVoices++;
    }
  }
  impl->stats.activeVoices = activeVoices;
  if (activeVoices > impl->stats.activeVoicePeak) {
    impl->stats.activeVoicePeak = activeVoices;
  }
}

void updateDynamicMixGain(EngineState *impl, uint32_t nowUs) {
  if (!impl) return;

  const float target = dynamicMixGainTarget(impl->stats.activeVoices);
  const uint32_t dtUs = (impl->dynamicMixLastUs == 0) ? 0 : (nowUs - impl->dynamicMixLastUs);
  impl->dynamicMixLastUs = nowUs;

  if (dtUs == 0) {
    impl->dynamicMixGain = target;
    return;
  }

  const uint32_t timeConstantUs =
      (target < impl->dynamicMixGain) ? kDynamicMixReleaseUs : kDynamicMixAttackUs;
  impl->dynamicMixGain = smoothGain(impl->dynamicMixGain, target, dtUs, timeConstantUs);
}

void updateVoiceGain(VoiceState &voice, float dynamicMixGain, uint32_t nowUs) {
  if (!voice.active || !voice.stub) return;
  if (voice.stopping) {
    // During stop, keep mixer gain fixed and rely only on per-sample fade envelope.
    // This avoids stepwise gain changes from dynamic mix updates ("zipper" artifacts).
    return;
  }

  // Fade-out is handled sample-by-sample in BudgetedAudioOutput.
  const float fadeOutRatio = voiceFadeOutRatio(voice, nowUs);
  const float gain = voice.targetGain * dynamicMixGain * voiceFadeInRatio(voice, nowUs) * fadeOutRatio;
  if (fabsf(gain - voice.currentGain) < 0.0005f) return;

  voice.currentGain = gain;
  voice.stub->SetGain(voice.currentGain);
}

bool requestStopVoicesForGroup(EngineState *impl,
                               int16_t retriggerGroupId,
                               uint32_t fadeOutUs,
                               int excludeVoiceIndex) {
  if (!impl || retriggerGroupId < 0) return false;
  bool changed = false;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    if (i == excludeVoiceIndex) continue;
    VoiceState &voice = impl->voices[i];
    if (!voice.active) continue;
    if (voice.retriggerGroupId != retriggerGroupId) continue;
    requestVoiceStop(voice, fadeOutUs);
    changed = true;
  }
  return changed;
}

bool requestStopAllVoices(EngineState *impl, uint32_t fadeOutUs) {
  if (!impl) return false;
  bool changed = false;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    VoiceState &voice = impl->voices[i];
    if (!voice.active) continue;
    requestVoiceStop(voice, fadeOutUs);
    changed = true;
  }
  return changed;
}

bool hasActiveVoiceForGroup(EngineState *impl, int16_t retriggerGroupId) {
  if (!impl || retriggerGroupId < 0) return false;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    const VoiceState &voice = impl->voices[i];
    if (!voice.active) continue;
    if (voice.retriggerGroupId == retriggerGroupId) return true;
  }
  return false;
}

int allocateVoiceSlot(EngineState *impl, int16_t retriggerGroupId, bool &voiceWasStolen) {
  voiceWasStolen = false;
  if (!impl) return -1;

  for (int i = 0; i < Audio::kVoiceCount; i++) {
    if (!impl->voices[i].active) {
      return i;
    }
  }

  int oldestSameGroupIndex = -1;
  uint32_t oldestSameGroupStartOrder = UINT32_MAX;
  int oldestIndex = -1;
  uint32_t oldestStartOrder = UINT32_MAX;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    const VoiceState &voice = impl->voices[i];
    if (!voice.active) continue;
    if (retriggerGroupId >= 0 && voice.retriggerGroupId == retriggerGroupId &&
        voice.startOrder < oldestSameGroupStartOrder) {
      oldestSameGroupStartOrder = voice.startOrder;
      oldestSameGroupIndex = i;
    }
    if (voice.startOrder < oldestStartOrder) {
      oldestStartOrder = voice.startOrder;
      oldestIndex = i;
    }
  }

  const int stolenIndex = (oldestSameGroupIndex >= 0) ? oldestSameGroupIndex : oldestIndex;
  if (stolenIndex >= 0) {
    voiceWasStolen = true;
    impl->stats.voiceStealCount++;
  }
  return stolenIndex;
}

bool beginVoiceFromPath(EngineState *impl,
                        int voiceIndex,
                        const String &samplePath,
                        uint8_t volume,
                        int16_t retriggerGroupId,
                        bool loopEnabled,
                        uint32_t fadeInUs) {
  if (!impl || voiceIndex < 0 || voiceIndex >= Audio::kVoiceCount) return false;

  VoiceState &voice = impl->voices[voiceIndex];
  if (!voice.wav || !voice.stub || !voice.budgetedOut) return false;
  if (!impl->streamManager.openStream(static_cast<uint8_t>(voiceIndex), samplePath.c_str())) {
    return false;
  }

  voice.activeSource = impl->streamManager.sourceForStream(static_cast<uint8_t>(voiceIndex));
  if (!voice.activeSource) {
    return false;
  }
  voice.targetGain = gainFromVolume(volume);
  voice.fadeInUs = fadeInUs;
  voice.stopping = false;
  voice.stopStartUs = 0;
  voice.fadeOutUs = 0;
  voice.currentGain = (voice.fadeInUs > 0) ? 0.0f : (voice.targetGain * impl->dynamicMixGain);
  voice.budgetedOut->resetFadeEnvelope();
  voice.stub->SetGain(voice.currentGain);
  if (!voice.wav->begin(voice.activeSource, voice.budgetedOut)) {
    voice.activeSource->close();
    voice.activeSource = nullptr;
    voice.stub->stop();
    return false;
  }

  voice.startOrder = impl->nextStartOrder++;
  voice.startUs = micros();
  if (impl->nextStartOrder == 0) {
    impl->nextStartOrder = 1;
  }
  voice.active = true;
  voice.volume = volume;
  voice.loopEnabled = loopEnabled;
  voice.sourceType = VoiceSourceType::StreamPath;
  samplePath.toCharArray(voice.path, sizeof(voice.path));
  voice.ramData = nullptr;
  voice.ramDataBytes = 0;
  voice.channelCount = 0;
  voice.sampleRate = 0;
  voice.bitsPerSample = 0;
  voice.retriggerGroupId = retriggerGroupId;
  return true;
}

bool beginVoiceFromRam(EngineState *impl,
                       int voiceIndex,
                       const uint8_t *pcmData,
                       uint32_t dataBytes,
                       uint16_t channelCount,
                       uint32_t sampleRate,
                       uint16_t bitsPerSample,
                       uint8_t volume,
                       int16_t retriggerGroupId,
                       bool loopEnabled,
                       uint32_t fadeInUs) {
  if (!impl || voiceIndex < 0 || voiceIndex >= Audio::kVoiceCount) return false;

  VoiceState &voice = impl->voices[voiceIndex];
  if (!voice.wav || !voice.ramSource || !voice.stub || !voice.budgetedOut) return false;
  if (!voice.ramSource->open(pcmData, dataBytes, channelCount, sampleRate, bitsPerSample)) {
    return false;
  }

  voice.activeSource = voice.ramSource;
  voice.targetGain = gainFromVolume(volume);
  voice.fadeInUs = fadeInUs;
  voice.stopping = false;
  voice.stopStartUs = 0;
  voice.fadeOutUs = 0;
  voice.currentGain = (voice.fadeInUs > 0) ? 0.0f : (voice.targetGain * impl->dynamicMixGain);
  voice.budgetedOut->resetFadeEnvelope();
  voice.stub->SetGain(voice.currentGain);
  if (!voice.wav->begin(voice.activeSource, voice.budgetedOut)) {
    voice.activeSource->close();
    voice.activeSource = nullptr;
    voice.stub->stop();
    return false;
  }

  voice.startOrder = impl->nextStartOrder++;
  voice.startUs = micros();
  if (impl->nextStartOrder == 0) {
    impl->nextStartOrder = 1;
  }
  voice.active = true;
  voice.volume = volume;
  voice.loopEnabled = loopEnabled;
  voice.sourceType = VoiceSourceType::RamData;
  voice.path[0] = '\0';
  voice.ramData = pcmData;
  voice.ramDataBytes = dataBytes;
  voice.channelCount = channelCount;
  voice.sampleRate = sampleRate;
  voice.bitsPerSample = bitsPerSample;
  voice.retriggerGroupId = retriggerGroupId;
  return true;
}

bool restartVoiceLoop(EngineState *impl, int voiceIndex) {
  if (!impl || voiceIndex < 0 || voiceIndex >= Audio::kVoiceCount) return false;

  const VoiceState &voice = impl->voices[voiceIndex];
  if (!voice.loopEnabled || voice.stopping) return false;

  const VoiceSourceType sourceType = voice.sourceType;
  const String path(voice.path);
  const uint8_t volume = voice.volume;
  const int16_t retriggerGroupId = voice.retriggerGroupId;
  const bool loopEnabled = voice.loopEnabled;
  const uint8_t *ramData = voice.ramData;
  const uint32_t ramDataBytes = voice.ramDataBytes;
  const uint16_t channelCount = voice.channelCount;
  const uint32_t sampleRate = voice.sampleRate;
  const uint16_t bitsPerSample = voice.bitsPerSample;

  stopVoice(impl->voices[voiceIndex]);

  if (sourceType == VoiceSourceType::StreamPath && path.length() > 0) {
    return beginVoiceFromPath(impl, voiceIndex, path, volume, retriggerGroupId, loopEnabled, 0);
  }
  if (sourceType == VoiceSourceType::RamData && ramData && ramDataBytes > 0) {
    return beginVoiceFromRam(impl,
                             voiceIndex,
                             ramData,
                             ramDataBytes,
                             channelCount,
                             sampleRate,
                             bitsPerSample,
                             volume,
                             retriggerGroupId,
                             loopEnabled,
                             0);
  }
  return false;
}

}  // namespace AudioInternal
