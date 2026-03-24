#include "audio.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "AudioFileSource.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"
#include "AudioOutputMixer.h"

#include "debug_flags.h"
#include "pins.h"
#include "stream_manager.h"

namespace {

class AudioFileSourceRamWav : public AudioFileSource {
 public:
  AudioFileSourceRamWav() = default;

  bool open(const uint8_t *pcmData,
            uint32_t pcmBytes,
            uint16_t channelCount,
            uint32_t sampleRate,
            uint16_t bitsPerSample) {
    if (!pcmData || pcmBytes == 0 || channelCount == 0 || sampleRate == 0 || bitsPerSample == 0) {
      return false;
    }

    const uint16_t blockAlign = static_cast<uint16_t>(channelCount * (bitsPerSample / 8U));
    if (blockAlign == 0) return false;

    const uint32_t byteRate = sampleRate * static_cast<uint32_t>(blockAlign);
    const uint32_t riffSize = 36U + pcmBytes;

    memset(header_, 0, sizeof(header_));
    memcpy(&header_[0], "RIFF", 4);
    writeLe32(&header_[4], riffSize);
    memcpy(&header_[8], "WAVE", 4);
    memcpy(&header_[12], "fmt ", 4);
    writeLe32(&header_[16], 16U);
    writeLe16(&header_[20], 1U);
    writeLe16(&header_[22], channelCount);
    writeLe32(&header_[24], sampleRate);
    writeLe32(&header_[28], byteRate);
    writeLe16(&header_[32], blockAlign);
    writeLe16(&header_[34], bitsPerSample);
    memcpy(&header_[36], "data", 4);
    writeLe32(&header_[40], pcmBytes);

    data_ = pcmData;
    dataBytes_ = pcmBytes;
    pos_ = 0;
    open_ = true;
    return true;
  }

  uint32_t read(void *data, uint32_t len) override {
    if (!open_ || !data || len == 0) return 0;

    const uint32_t total = getSize();
    if (pos_ >= total) return 0;

    uint8_t *out = static_cast<uint8_t *>(data);
    uint32_t remaining = len;
    uint32_t copied = 0;

    while (remaining > 0 && pos_ < total) {
      if (pos_ < kHeaderBytes) {
        const uint32_t headerOffset = pos_;
        const uint32_t available = kHeaderBytes - headerOffset;
        const uint32_t chunk = (remaining < available) ? remaining : available;
        memcpy(out + copied, header_ + headerOffset, chunk);
        pos_ += chunk;
        copied += chunk;
        remaining -= chunk;
      } else {
        const uint32_t dataOffset = pos_ - kHeaderBytes;
        const uint32_t available = dataBytes_ - dataOffset;
        const uint32_t chunk = (remaining < available) ? remaining : available;
        memcpy(out + copied, data_ + dataOffset, chunk);
        pos_ += chunk;
        copied += chunk;
        remaining -= chunk;
      }
    }

    return copied;
  }

  bool seek(int32_t pos, int dir) override {
    if (!open_) return false;

    int64_t target = 0;
    if (dir == SEEK_SET) {
      target = pos;
    } else if (dir == SEEK_CUR) {
      target = static_cast<int64_t>(pos_) + pos;
    } else if (dir == SEEK_END) {
      target = static_cast<int64_t>(getSize()) + pos;
    } else {
      return false;
    }

    if (target < 0 || static_cast<uint64_t>(target) > static_cast<uint64_t>(getSize())) {
      return false;
    }

    pos_ = static_cast<uint32_t>(target);
    return true;
  }

  bool close() override {
    data_ = nullptr;
    dataBytes_ = 0;
    pos_ = 0;
    open_ = false;
    return true;
  }

  bool isOpen() override { return open_; }
  uint32_t getSize() override { return kHeaderBytes + dataBytes_; }
  uint32_t getPos() override { return pos_; }

 private:
  static constexpr uint32_t kHeaderBytes = 44U;

  static void writeLe16(uint8_t *dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFFU);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
  }

  static void writeLe32(uint8_t *dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFFU);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
  }

  uint8_t header_[kHeaderBytes] = {0};
  const uint8_t *data_ = nullptr;
  uint32_t dataBytes_ = 0;
  uint32_t pos_ = 0;
  bool open_ = false;
};

class FreshStartAudioGeneratorWAV : public AudioGeneratorWAV {
 public:
  bool begin(AudioFileSource *source, AudioOutput *output) override {
    // AudioGenerator keeps lastSample between runs; clear it to avoid start transient on retrigger.
    lastSample[0] = 0;
    lastSample[1] = 0;
    return AudioGeneratorWAV::begin(source, output);
  }

  bool stop() override {
    const bool ok = AudioGeneratorWAV::stop();
    lastSample[0] = 0;
    lastSample[1] = 0;
    return ok;
  }
};

class BudgetedAudioOutput : public AudioOutput {
 public:
  explicit BudgetedAudioOutput(AudioOutput *sink) : sink_(sink) {}

  void resetBudget(uint16_t sampleCount) { budgetSamples_ = sampleCount; }

  bool SetRate(int hz) override { return sink_ && sink_->SetRate(hz); }
  bool SetChannels(int channels) override { return sink_ && sink_->SetChannels(channels); }
  bool begin() override { return sink_ && sink_->begin(); }
  bool stop() override {
    budgetSamples_ = 0;
    return sink_ && sink_->stop();
  }
  bool loop() override { return sink_ && sink_->loop(); }

  bool ConsumeSample(int16_t sample[2]) override {
    if (!sink_ || budgetSamples_ == 0) return false;
    budgetSamples_--;
    return sink_->ConsumeSample(sample);
  }

 private:
  AudioOutput *sink_ = nullptr;
  uint16_t budgetSamples_ = 0;
};

struct WaveformCaptureState {
  mutable portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  int8_t ring[Audio::kWaveformPointCount] = {0};
  uint16_t head = 0;
  uint16_t count = 0;
  uint16_t decimationCounter = 0;
};

class StableAudioOutputI2S : public AudioOutputI2S {
 public:
  StableAudioOutputI2S(int port, int outputMode, int dmaCount, int useApll)
      : AudioOutputI2S(port, outputMode, dmaCount, useApll) {}

  bool SetRate(int hz) override {
    rateSetCalls_++;
    if (lastRateHz_ == hz) {
      skippedRateSetCalls_++;
      return true;
    }
    lastRateHz_ = hz;
    appliedRateSetCalls_++;
    return AudioOutputI2S::SetRate(hz);
  }

  uint32_t rateSetCalls() const { return rateSetCalls_; }
  uint32_t skippedRateSetCalls() const { return skippedRateSetCalls_; }
  uint32_t appliedRateSetCalls() const { return appliedRateSetCalls_; }

 private:
  int lastRateHz_ = -1;
  uint32_t rateSetCalls_ = 0;
  uint32_t skippedRateSetCalls_ = 0;
  uint32_t appliedRateSetCalls_ = 0;
};

class SimpleLimiterAudioOutput : public AudioOutput {
 public:
  explicit SimpleLimiterAudioOutput(AudioOutput *sink, WaveformCaptureState *waveformCapture)
      : sink_(sink), waveformCapture_(waveformCapture) {
    updateSmoothing();
  }

  bool SetRate(int hz) override {
    sampleRateHz_ = (hz > 0) ? hz : sampleRateHz_;
    updateSmoothing();
    return sink_ && sink_->SetRate(hz);
  }

  bool SetChannels(int channels) override { return sink_ && sink_->SetChannels(channels); }
  bool begin() override { return sink_ && sink_->begin(); }

  bool stop() override {
    limiterGain_ = 1.0f;
    return sink_ && sink_->stop();
  }

  bool loop() override { return sink_ && sink_->loop(); }

  bool ConsumeSample(int16_t sample[2]) override {
    if (!sink_) return false;

    float left = static_cast<float>(sample[0]) / 32768.0f;
    float right = static_cast<float>(sample[1]) / 32768.0f;

    // Recover perceived loudness after conservative per-voice headroom.
    left *= kLimiterMakeupGain;
    right *= kLimiterMakeupGain;

    float peak = fabsf(left);
    const float rightAbs = fabsf(right);
    if (rightAbs > peak) peak = rightAbs;

    float desiredGain = 1.0f;
    if (peak > kLimiterThreshold && peak > 0.0f) {
      desiredGain = kLimiterThreshold / peak;
    }

    if (desiredGain < limiterGain_) {
      // Clamp immediately to avoid transient clipping distortion.
      limiterGain_ = desiredGain;
    } else {
      limiterGain_ = desiredGain + (releaseCoeff_ * (limiterGain_ - desiredGain));
    }

    left *= limiterGain_;
    right *= limiterGain_;

    if (left > 1.0f) left = 1.0f;
    if (left < -1.0f) left = -1.0f;
    if (right > 1.0f) right = 1.0f;
    if (right < -1.0f) right = -1.0f;

    sample[0] = static_cast<int16_t>(left * 32767.0f);
    sample[1] = static_cast<int16_t>(right * 32767.0f);
    captureWaveformSample(sample);
    return sink_->ConsumeSample(sample);
  }

 private:
  void captureWaveformSample(const int16_t sample[2]) {
    if (!waveformCapture_) return;

    waveformCapture_->decimationCounter++;
    if (waveformCapture_->decimationCounter < kWaveformDecimation) {
      return;
    }
    waveformCapture_->decimationCounter = 0;

    const int32_t mono = (static_cast<int32_t>(sample[0]) + static_cast<int32_t>(sample[1])) / 2;
    int32_t quantized = mono / 256;
    if (quantized > 127) quantized = 127;
    if (quantized < -128) quantized = -128;

    portENTER_CRITICAL(&waveformCapture_->lock);
    waveformCapture_->ring[waveformCapture_->head] = static_cast<int8_t>(quantized);
    waveformCapture_->head = (waveformCapture_->head + 1U) % Audio::kWaveformPointCount;
    if (waveformCapture_->count < Audio::kWaveformPointCount) {
      waveformCapture_->count++;
    }
    portEXIT_CRITICAL(&waveformCapture_->lock);
  }

  static constexpr uint16_t kWaveformDecimation = 32;
  void updateSmoothing() {
    const float sampleRate = static_cast<float>((sampleRateHz_ > 0) ? sampleRateHz_ : 44100);
    releaseCoeff_ = expf(-1.0f / (sampleRate * kLimiterReleaseSeconds));
  }

  static constexpr float kLimiterThreshold = 0.995f;
  static constexpr float kLimiterMakeupGain = 1.0f;
  static constexpr float kLimiterReleaseSeconds = 0.030f;

  AudioOutput *sink_ = nullptr;
  WaveformCaptureState *waveformCapture_ = nullptr;
  int sampleRateHz_ = 44100;
  float limiterGain_ = 1.0f;
  float releaseCoeff_ = 0.0f;
};

constexpr uint8_t kVolumeScaleMax = 100;
constexpr int kMixerBufferSamples = 512;
constexpr float kPerVoiceVolumeScale = 0.65f;

float gainFromVolume(uint8_t volume) {
  const uint8_t clamped = (volume > kVolumeScaleMax) ? kVolumeScaleMax : volume;
  return (static_cast<float>(clamped) / static_cast<float>(kVolumeScaleMax)) * kPerVoiceVolumeScale;
}

// Dynamic headroom: keep polyphony safe while allowing much louder single-voice playback.
constexpr float kDynamicMixMinGain = 0.125f;  // 8x full-scale voices remain bounded pre-limiter.
constexpr float kDynamicMixMaxGain = 0.35f;   // +8.9 dB vs previous fixed 0.125 for single voice.
constexpr uint32_t kDynamicMixAttackUs = 120000;   // Recover loudness slowly to avoid pumping.
constexpr uint32_t kDynamicMixReleaseUs = 20000;   // Clamp quickly when polyphony increases.

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

// Limit per-update work so SD streamed voices don't monopolize audio task cycles.
constexpr uint16_t kVoiceLoopSampleBudget = 96;
// Short anti-click fade only when retriggering an active group.
constexpr uint32_t kRetriggerFadeInUs = 800;
// Retriggered voices from the same group are softly cut to avoid clicks.
constexpr uint32_t kRetriggerFadeOutUs = 1200;

enum class VoiceSourceType : uint8_t {
  None,
  StreamPath,
  RamData,
};

}  // namespace

struct Audio::Impl {
  struct Voice {
    bool active = false;
    uint32_t startOrder = 0;
    uint32_t startUs = 0;
    int16_t retriggerGroupId = -1;
    FreshStartAudioGeneratorWAV *wav = nullptr;
    AudioFileSourceRamWav *ramSource = nullptr;
    AudioFileSource *activeSource = nullptr;
    AudioOutputMixerStub *stub = nullptr;
    BudgetedAudioOutput *budgetedOut = nullptr;
    float targetGain = 0.0f;  // Per-voice gain from sample volume (0..1), before dynamic mix gain.
    float currentGain = 0.0f;
    uint32_t fadeInUs = 0;
    bool stopping = false;
    uint32_t stopStartUs = 0;
    uint32_t fadeOutUs = 0;
    uint8_t volume = 100;
    bool loopEnabled = false;
    VoiceSourceType sourceType = VoiceSourceType::None;
    char path[128] = {0};
    const uint8_t *ramData = nullptr;
    uint32_t ramDataBytes = 0;
    uint16_t channelCount = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
  };

  StableAudioOutputI2S *out = nullptr;
  SimpleLimiterAudioOutput *limitedOut = nullptr;
  AudioOutputMixer *mixer = nullptr;
  Voice voices[kVoiceCount];
  StreamManager streamManager;
  uint32_t nextStartOrder = 1;
  RuntimeStats stats;
  uint32_t lastUpdateUs = 0;
  uint32_t maxUpdateGapUs = 0;
  uint32_t lateUpdateCount = 0;
  uint32_t playCount = 0;
  uint32_t slowPlayCount = 0;
  uint32_t maxPlayUs = 0;
  uint32_t lastDiagLogMs = 0;
  float dynamicMixGain = kDynamicMixMaxGain;
  uint32_t dynamicMixLastUs = 0;
  WaveformCaptureState waveformCapture;
};

namespace {

void stopVoice(Audio::Impl::Voice &voice) {
  if (voice.budgetedOut) {
    voice.budgetedOut->resetBudget(0);
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

void requestVoiceStop(Audio::Impl::Voice &voice, uint32_t fadeOutUs) {
  if (!voice.active) return;
  if (fadeOutUs == 0) {
    stopVoice(voice);
    return;
  }

  if (voice.stopping) {
    if (fadeOutUs < voice.fadeOutUs) {
      voice.fadeOutUs = fadeOutUs;
      voice.stopStartUs = micros();
    }
    return;
  }

  voice.stopping = true;
  voice.stopStartUs = micros();
  voice.fadeOutUs = fadeOutUs;
  // Keep stop deterministic: no loop restart while voice is tailing out.
  voice.loopEnabled = false;
}

void refreshStats(Audio::Impl *impl) {
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

float voiceFadeInRatio(const Audio::Impl::Voice &voice, uint32_t nowUs) {
  if (!voice.active) return 0.0f;
  if (voice.fadeInUs == 0) return 1.0f;

  const uint32_t elapsedUs = nowUs - voice.startUs;
  const float ratio = static_cast<float>(elapsedUs) / static_cast<float>(voice.fadeInUs);
  return (ratio > 1.0f) ? 1.0f : ratio;
}

float voiceFadeOutRatio(const Audio::Impl::Voice &voice, uint32_t nowUs) {
  if (!voice.active) return 0.0f;
  if (!voice.stopping) return 1.0f;
  if (voice.fadeOutUs == 0) return 0.0f;

  const uint32_t elapsedUs = nowUs - voice.stopStartUs;
  if (elapsedUs >= voice.fadeOutUs) return 0.0f;

  const float ratio = 1.0f - (static_cast<float>(elapsedUs) / static_cast<float>(voice.fadeOutUs));
  return (ratio < 0.0f) ? 0.0f : ratio;
}

void updateDynamicMixGain(Audio::Impl *impl, uint32_t nowUs) {
  if (!impl) return;

  const float target = dynamicMixGainTarget(impl->stats.activeVoices);
  const uint32_t dtUs = (impl->dynamicMixLastUs == 0) ? 0 : (nowUs - impl->dynamicMixLastUs);
  impl->dynamicMixLastUs = nowUs;

  if (dtUs == 0) {
    impl->dynamicMixGain = target;
    return;
  }

  const uint32_t timeConstantUs = (target < impl->dynamicMixGain) ? kDynamicMixReleaseUs
                                                                   : kDynamicMixAttackUs;
  impl->dynamicMixGain = smoothGain(impl->dynamicMixGain, target, dtUs, timeConstantUs);
}

void updateVoiceGain(Audio::Impl::Voice &voice, float dynamicMixGain, uint32_t nowUs) {
  if (!voice.active || !voice.stub) return;

  const float gain = voice.targetGain * dynamicMixGain * voiceFadeInRatio(voice, nowUs) *
                     voiceFadeOutRatio(voice, nowUs);
  if (fabsf(gain - voice.currentGain) < 0.0005f) return;

  voice.currentGain = gain;
  voice.stub->SetGain(voice.currentGain);
}

void updateSchedulingStats(Audio::Impl *impl) {
  if (!impl) return;

  const uint32_t nowUs = micros();
  if (impl->lastUpdateUs != 0) {
    const uint32_t gapUs = nowUs - impl->lastUpdateUs;
    if (gapUs > impl->maxUpdateGapUs) {
      impl->maxUpdateGapUs = gapUs;
    }
    if (gapUs >= 6000) {
      impl->lateUpdateCount++;
    }
  }
  impl->lastUpdateUs = nowUs;
}

void maybeLogRuntimeDiagnostics(Audio::Impl *impl) {
  if (!impl || !DebugFlags::kEnableDebugLogs || !DebugFlags::kEnableRuntimeAudioDiagLogs) return;
  if (impl->stats.activeVoices == 0) return;

  const uint32_t nowMs = millis();
  if ((nowMs - impl->lastDiagLogMs) < 2000) return;
  impl->lastDiagLogMs = nowMs;

  const StreamManager::Diagnostics &streamDiag = impl->streamManager.diagnostics();
  
}

void recordPlayCost(Audio::Impl *impl, uint32_t elapsedUs) {
  if (!impl) return;
  impl->playCount++;
  if (elapsedUs > impl->maxPlayUs) {
    impl->maxPlayUs = elapsedUs;
  }
  if (elapsedUs >= 2000) {
    impl->slowPlayCount++;
  }
}

bool requestStopVoicesForGroup(Audio::Impl *impl,
                               int16_t retriggerGroupId,
                               uint32_t fadeOutUs,
                               int excludeVoiceIndex = -1) {
  if (!impl || retriggerGroupId < 0) return false;
  bool changed = false;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    if (i == excludeVoiceIndex) continue;
    Audio::Impl::Voice &voice = impl->voices[i];
    if (!voice.active) continue;
    if (voice.retriggerGroupId != retriggerGroupId) continue;
    requestVoiceStop(voice, fadeOutUs);
    changed = true;
  }
  return changed;
}

bool requestStopAllVoices(Audio::Impl *impl, uint32_t fadeOutUs) {
  if (!impl) return false;
  bool changed = false;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    Audio::Impl::Voice &voice = impl->voices[i];
    if (!voice.active) continue;
    requestVoiceStop(voice, fadeOutUs);
    changed = true;
  }
  return changed;
}

bool hasActiveVoiceForGroup(Audio::Impl *impl, int16_t retriggerGroupId) {
  if (!impl || retriggerGroupId < 0) return false;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    const Audio::Impl::Voice &voice = impl->voices[i];
    if (!voice.active) continue;
    if (voice.retriggerGroupId == retriggerGroupId) return true;
  }
  return false;
}

int allocateVoiceSlot(Audio::Impl *impl, int16_t retriggerGroupId, bool &voiceWasStolen) {
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
    const Audio::Impl::Voice &voice = impl->voices[i];
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

bool beginVoiceFromPath(Audio::Impl *impl,
                        int voiceIndex,
                        const String &samplePath,
                        uint8_t volume,
                        int16_t retriggerGroupId,
                        bool loopEnabled,
                        uint32_t fadeInUs) {
  if (!impl || voiceIndex < 0 || voiceIndex >= Audio::kVoiceCount) return false;

  Audio::Impl::Voice &voice = impl->voices[voiceIndex];
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

bool beginVoiceFromRam(Audio::Impl *impl,
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

  Audio::Impl::Voice &voice = impl->voices[voiceIndex];
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

bool restartVoiceLoop(Audio::Impl *impl, int voiceIndex) {
  if (!impl || voiceIndex < 0 || voiceIndex >= Audio::kVoiceCount) return false;

  const Audio::Impl::Voice &voice = impl->voices[voiceIndex];
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

}  // namespace

Audio::Audio() = default;

Audio::~Audio() {
  if (!impl_) return;

  for (int i = 0; i < kVoiceCount; i++) {
    stopVoice(impl_->voices[i]);
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

  impl_->out =
      new StableAudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_ENABLE);
  if (!impl_->out) {
    
    delete impl_;
    impl_ = nullptr;
    return;
  }
  impl_->out->SetPinout(Pins::I2S_BCLK, Pins::I2S_LRC, Pins::I2S_DOUT);
  impl_->out->SetGain(1.0f);

  impl_->limitedOut = new SimpleLimiterAudioOutput(impl_->out, &impl_->waveformCapture);
  if (!impl_->limitedOut) {
    
    delete impl_->limitedOut;
    impl_->limitedOut = nullptr;
    delete impl_->out;
    impl_->out = nullptr;
    delete impl_;
    impl_ = nullptr;
    return;
  }

  impl_->mixer = new AudioOutputMixer(kMixerBufferSamples, impl_->limitedOut);
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

  int readyVoices = 0;
  for (int i = 0; i < kVoiceCount; i++) {
    Impl::Voice &voice = impl_->voices[i];
    voice.wav = new FreshStartAudioGeneratorWAV();
    voice.ramSource = new AudioFileSourceRamWav();
    voice.stub = impl_->mixer->NewInput();
    voice.budgetedOut = voice.stub ? new BudgetedAudioOutput(voice.stub) : nullptr;

    const bool ready = voice.wav && voice.ramSource && voice.stub && voice.budgetedOut;
    if (ready) {
      voice.stub->SetGain(1.0f);
      readyVoices++;
    } else {
      if (DebugFlags::kEnableDebugLogs) {
        
      }
    }
  }

  refreshStats(impl_);
  
}

void Audio::update() {
  if (!impl_) return;
  updateSchedulingStats(impl_);
  const uint32_t nowUs = micros();

  refreshStats(impl_);
  updateDynamicMixGain(impl_, nowUs);

  bool stateChanged = false;

  for (int i = 0; i < kVoiceCount; i++) {
    Impl::Voice &voice = impl_->voices[i];
    if (!voice.active) {
      continue;
    }

    if (!voice.wav || !voice.wav->isRunning()) {
      stopVoice(voice);
      stateChanged = true;
      continue;
    }

    if (voice.stopping && voice.fadeOutUs > 0) {
      const uint32_t fadeElapsedUs = nowUs - voice.stopStartUs;
      if (fadeElapsedUs >= voice.fadeOutUs) {
        stopVoice(voice);
        stateChanged = true;
        continue;
      }
    }

    if (voice.budgetedOut) {
      voice.budgetedOut->resetBudget(kVoiceLoopSampleBudget);
    }

    updateVoiceGain(voice, impl_->dynamicMixGain, nowUs);

    if (!voice.wav->loop()) {
      if (voice.stopping || !voice.loopEnabled || !restartVoiceLoop(impl_, i)) {
        stopVoice(voice);
        stateChanged = true;
      }
    }
  }

  if (impl_->mixer) {
    impl_->mixer->loop();
  }

  if (stateChanged) {
    refreshStats(impl_);
  }
  maybeLogRuntimeDiagnostics(impl_);
}

void Audio::playSamplePath(const String &samplePath,
                           uint8_t volume,
                           int16_t retriggerGroupId,
                           bool loopEnabled) {
  if (!impl_ || samplePath.length() == 0) return;
  const uint32_t playStartUs = micros();
  const bool retriggeringGroup = hasActiveVoiceForGroup(impl_, retriggerGroupId);
  const uint32_t fadeInUs = retriggeringGroup ? kRetriggerFadeInUs : 0;

  bool voiceWasStolen = false;
  const int voiceIndex = allocateVoiceSlot(impl_, retriggerGroupId, voiceWasStolen);
  if (voiceIndex < 0) {
    
    recordPlayCost(impl_, micros() - playStartUs);
    return;
  }

  Impl::Voice &voice = impl_->voices[voiceIndex];
  if (voiceWasStolen && DebugFlags::kEnableDebugLogs) {
    
  }

  stopVoice(voice);
  if (!beginVoiceFromPath(
          impl_, voiceIndex, samplePath, volume, retriggerGroupId, loopEnabled, fadeInUs)) {
    
    refreshStats(impl_);
    recordPlayCost(impl_, micros() - playStartUs);
    return;
  }

  if (retriggeringGroup) {
    requestStopVoicesForGroup(impl_, retriggerGroupId, kRetriggerFadeOutUs, voiceIndex);
  }

  refreshStats(impl_);
  recordPlayCost(impl_, micros() - playStartUs);
}

void Audio::stopAllVoices() {
  if (!impl_) return;
  for (int i = 0; i < kVoiceCount; i++) {
    stopVoice(impl_->voices[i]);
  }
  refreshStats(impl_);
}

void Audio::fadeOutAllVoices(uint32_t fadeOutUs) {
  if (!impl_) return;
  if (fadeOutUs == 0) {
    stopAllVoices();
    return;
  }
  if (requestStopAllVoices(impl_, fadeOutUs)) {
    refreshStats(impl_);
  }
}

void Audio::stopLoopingVoicesForGroup(int16_t retriggerGroupId) {
  if (!impl_ || retriggerGroupId < 0) return;
  bool changed = false;
  for (int i = 0; i < kVoiceCount; i++) {
    Impl::Voice &voice = impl_->voices[i];
    if (!voice.active || !voice.loopEnabled) continue;
    if (voice.retriggerGroupId != retriggerGroupId) continue;
    stopVoice(voice);
    changed = true;
  }
  if (changed) {
    refreshStats(impl_);
  }
}

void Audio::setLoopEnabledForGroup(int16_t retriggerGroupId, bool loopEnabled) {
  if (!impl_ || retriggerGroupId < 0) return;
  for (int i = 0; i < kVoiceCount; i++) {
    Impl::Voice &voice = impl_->voices[i];
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
  const uint32_t playStartUs = micros();
  const bool retriggeringGroup = hasActiveVoiceForGroup(impl_, retriggerGroupId);
  const uint32_t fadeInUs = retriggeringGroup ? kRetriggerFadeInUs : 0;

  bool voiceWasStolen = false;
  const int voiceIndex = allocateVoiceSlot(impl_, retriggerGroupId, voiceWasStolen);
  if (voiceIndex < 0) {
    recordPlayCost(impl_, micros() - playStartUs);
    return false;
  }

  Impl::Voice &voice = impl_->voices[voiceIndex];
  if (voiceWasStolen && DebugFlags::kEnableDebugLogs) {
    
  }

  stopVoice(voice);
  const bool started = beginVoiceFromRam(impl_,
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
  if (started && retriggeringGroup) {
    requestStopVoicesForGroup(impl_, retriggerGroupId, kRetriggerFadeOutUs, voiceIndex);
  }
  refreshStats(impl_);
  recordPlayCost(impl_, micros() - playStartUs);
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
  snapshot.validPoints = 0;
  if (!impl_) return false;

  const WaveformCaptureState &capture = impl_->waveformCapture;
  portENTER_CRITICAL(&capture.lock);
  const uint16_t count = capture.count;
  const uint16_t head = capture.head;
  const uint16_t start = (count == Audio::kWaveformPointCount) ? head : 0;
  for (uint16_t i = 0; i < count; i++) {
    const uint16_t index = (start + i) % Audio::kWaveformPointCount;
    snapshot.points[i] = capture.ring[index];
  }
  portEXIT_CRITICAL(&capture.lock);

  snapshot.validPoints = count;
  return count > 0;
}
