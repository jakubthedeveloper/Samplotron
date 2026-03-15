#include "audio.h"

#include <Arduino.h>
#include <limits.h>
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

constexpr uint8_t kVolumeScaleMax = 100;
constexpr int kMixerBufferSamples = 512;

float gainFromVolume(uint8_t volume) {
  const uint8_t clamped = (volume > kVolumeScaleMax) ? kVolumeScaleMax : volume;
  return static_cast<float>(clamped) / static_cast<float>(kVolumeScaleMax);
}

// Keep enough headroom so 8 simultaneous full-scale voices do not clip in the mixer.
constexpr float kPerVoiceMixHeadroomGain = 0.125f;

float voiceGainFromVolume(uint8_t volume) {
  return gainFromVolume(volume) * kPerVoiceMixHeadroomGain;
}

// Keep disabled by default to preserve percussive transients.
constexpr uint32_t kVoiceFadeInUs = 0;

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
    AudioGeneratorWAV *wav = nullptr;
    AudioFileSourceRamWav *ramSource = nullptr;
    AudioFileSource *activeSource = nullptr;
    AudioOutputMixerStub *stub = nullptr;
    float targetGain = 0.0f;
    float currentGain = 0.0f;
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
};

namespace {

void stopVoice(Audio::Impl::Voice &voice) {
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

void updateVoiceFadeIn(Audio::Impl::Voice &voice, uint32_t nowUs) {
  if (!voice.active || !voice.stub) return;
  if (voice.currentGain >= voice.targetGain) return;

  const uint32_t elapsedUs = nowUs - voice.startUs;
  float ratio = 1.0f;
  if (kVoiceFadeInUs > 0) {
    ratio = static_cast<float>(elapsedUs) / static_cast<float>(kVoiceFadeInUs);
    if (ratio > 1.0f) ratio = 1.0f;
  }

  const float newGain = voice.targetGain * ratio;
  if (newGain <= voice.currentGain) return;
  voice.currentGain = newGain;
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
  Serial.printf("AUDIO_DIAG active=%u peak=%u steals=%lu loop_gap_max_us=%lu late_loops=%lu "
                "play_max_us=%lu slow_plays=%lu play_count=%lu "
                "rate_set_calls=%lu rate_set_skipped=%lu rate_set_applied=%lu "
                "sd_reads=%lu sd_slow_reads=%lu sd_read_max_us=%lu sd_read_max_bytes=%lu "
                "stream_refills=%lu sd_bytes=%lu\n",
                static_cast<unsigned>(impl->stats.activeVoices),
                static_cast<unsigned>(impl->stats.activeVoicePeak),
                static_cast<unsigned long>(impl->stats.voiceStealCount),
                static_cast<unsigned long>(impl->maxUpdateGapUs),
                static_cast<unsigned long>(impl->lateUpdateCount),
                static_cast<unsigned long>(impl->maxPlayUs),
                static_cast<unsigned long>(impl->slowPlayCount),
                static_cast<unsigned long>(impl->playCount),
                static_cast<unsigned long>(impl->out ? impl->out->rateSetCalls() : 0),
                static_cast<unsigned long>(impl->out ? impl->out->skippedRateSetCalls() : 0),
                static_cast<unsigned long>(impl->out ? impl->out->appliedRateSetCalls() : 0),
                static_cast<unsigned long>(streamDiag.sourceReadCount),
                static_cast<unsigned long>(streamDiag.sourceSlowReadCount),
                static_cast<unsigned long>(streamDiag.sourceMaxReadUs),
                static_cast<unsigned long>(streamDiag.sourceMaxReadBytes),
                static_cast<unsigned long>(streamDiag.bufferRefillCount),
                static_cast<unsigned long>(streamDiag.sourceBytesRead));
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

int allocateVoiceSlot(Audio::Impl *impl, int16_t retriggerGroupId, bool &voiceWasStolen) {
  voiceWasStolen = false;
  if (!impl) return -1;

  if (retriggerGroupId >= 0) {
    for (int i = 0; i < Audio::kVoiceCount; i++) {
      const Audio::Impl::Voice &voice = impl->voices[i];
      if (voice.active && voice.retriggerGroupId == retriggerGroupId) {
        return i;
      }
    }
  }

  for (int i = 0; i < Audio::kVoiceCount; i++) {
    if (!impl->voices[i].active) {
      return i;
    }
  }

  int oldestIndex = -1;
  uint32_t oldestStartOrder = UINT32_MAX;
  for (int i = 0; i < Audio::kVoiceCount; i++) {
    const Audio::Impl::Voice &voice = impl->voices[i];
    if (!voice.active) continue;
    if (voice.startOrder < oldestStartOrder) {
      oldestStartOrder = voice.startOrder;
      oldestIndex = i;
    }
  }

  if (oldestIndex >= 0) {
    voiceWasStolen = true;
    impl->stats.voiceStealCount++;
  }
  return oldestIndex;
}

bool beginVoiceFromPath(Audio::Impl *impl,
                        int voiceIndex,
                        const String &samplePath,
                        uint8_t volume,
                        int16_t retriggerGroupId,
                        bool loopEnabled) {
  if (!impl || voiceIndex < 0 || voiceIndex >= Audio::kVoiceCount) return false;

  Audio::Impl::Voice &voice = impl->voices[voiceIndex];
  if (!voice.wav || !voice.stub) return false;
  if (!impl->streamManager.openStream(static_cast<uint8_t>(voiceIndex), samplePath.c_str())) {
    return false;
  }

  voice.activeSource = impl->streamManager.sourceForStream(static_cast<uint8_t>(voiceIndex));
  if (!voice.activeSource) {
    return false;
  }
  voice.targetGain = voiceGainFromVolume(volume);
  voice.currentGain = voice.targetGain;
  voice.stub->SetGain(voice.currentGain);
  if (!voice.wav->begin(voice.activeSource, voice.stub)) {
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
                       bool loopEnabled) {
  if (!impl || voiceIndex < 0 || voiceIndex >= Audio::kVoiceCount) return false;

  Audio::Impl::Voice &voice = impl->voices[voiceIndex];
  if (!voice.wav || !voice.ramSource || !voice.stub) return false;
  if (!voice.ramSource->open(pcmData, dataBytes, channelCount, sampleRate, bitsPerSample)) {
    return false;
  }

  voice.activeSource = voice.ramSource;
  voice.targetGain = voiceGainFromVolume(volume);
  voice.currentGain = voice.targetGain;
  voice.stub->SetGain(voice.currentGain);
  if (!voice.wav->begin(voice.activeSource, voice.stub)) {
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
  if (!voice.loopEnabled) return false;

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
    return beginVoiceFromPath(impl, voiceIndex, path, volume, retriggerGroupId, loopEnabled);
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
                             loopEnabled);
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
    delete impl_->voices[i].ramSource;
    impl_->voices[i].ramSource = nullptr;
    impl_->voices[i].activeSource = nullptr;
  }

  delete impl_->mixer;
  impl_->mixer = nullptr;
  delete impl_->out;
  impl_->out = nullptr;

  delete impl_;
  impl_ = nullptr;
}

void Audio::begin() {
  if (impl_) return;

  impl_ = new Impl();
  if (!impl_) {
    Serial.println("Audio begin failed: no memory for state");
    return;
  }

  impl_->out =
      new StableAudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_ENABLE);
  if (!impl_->out) {
    Serial.println("Audio begin failed: no memory for I2S output");
    delete impl_;
    impl_ = nullptr;
    return;
  }
  impl_->out->SetPinout(Pins::I2S_BCLK, Pins::I2S_LRC, Pins::I2S_DOUT);
  impl_->out->SetGain(1.0f);

  impl_->mixer = new AudioOutputMixer(kMixerBufferSamples, impl_->out);
  if (!impl_->mixer) {
    Serial.println("Audio begin failed: no memory for mixer");
    delete impl_->out;
    impl_->out = nullptr;
    delete impl_;
    impl_ = nullptr;
    return;
  }

  if (!impl_->streamManager.begin(kVoiceCount)) {
    Serial.println("Audio begin failed: stream manager init failed");
    delete impl_->mixer;
    impl_->mixer = nullptr;
    delete impl_->out;
    impl_->out = nullptr;
    delete impl_;
    impl_ = nullptr;
    return;
  }

  int readyVoices = 0;
  for (int i = 0; i < kVoiceCount; i++) {
    Impl::Voice &voice = impl_->voices[i];
    voice.wav = new AudioGeneratorWAV();
    voice.ramSource = new AudioFileSourceRamWav();
    voice.stub = impl_->mixer->NewInput();

    const bool ready = voice.wav && voice.ramSource && voice.stub;
    if (ready) {
      voice.stub->SetGain(1.0f);
      readyVoices++;
    } else {
      if (DebugFlags::kEnableDebugLogs) {
        Serial.printf("Audio voice %d init failed\n", i);
      }
    }
  }

  refreshStats(impl_);
  Serial.printf("Audio voices ready: %d/%d\n", readyVoices, kVoiceCount);
}

void Audio::update() {
  if (!impl_) return;
  updateSchedulingStats(impl_);
  const uint32_t nowUs = micros();

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

    updateVoiceFadeIn(voice, nowUs);

    if (!voice.wav->loop()) {
      if (!voice.loopEnabled || !restartVoiceLoop(impl_, i)) {
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

  bool voiceWasStolen = false;
  const int voiceIndex = allocateVoiceSlot(impl_, retriggerGroupId, voiceWasStolen);
  if (voiceIndex < 0) {
    Serial.println("No voice available");
    recordPlayCost(impl_, micros() - playStartUs);
    return;
  }

  Impl::Voice &voice = impl_->voices[voiceIndex];
  if (voiceWasStolen && DebugFlags::kEnableDebugLogs) {
    Serial.printf("VOICE_STEAL count=%lu slot=%d\n",
                  static_cast<unsigned long>(impl_->stats.voiceStealCount),
                  voiceIndex);
  }

  stopVoice(voice);
  if (!beginVoiceFromPath(impl_, voiceIndex, samplePath, volume, retriggerGroupId, loopEnabled)) {
    Serial.println("Sample open/start failed");
    refreshStats(impl_);
    recordPlayCost(impl_, micros() - playStartUs);
    return;
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

  bool voiceWasStolen = false;
  const int voiceIndex = allocateVoiceSlot(impl_, retriggerGroupId, voiceWasStolen);
  if (voiceIndex < 0) {
    recordPlayCost(impl_, micros() - playStartUs);
    return false;
  }

  Impl::Voice &voice = impl_->voices[voiceIndex];
  if (voiceWasStolen && DebugFlags::kEnableDebugLogs) {
    Serial.printf("VOICE_STEAL count=%lu slot=%d\n",
                  static_cast<unsigned long>(impl_->stats.voiceStealCount),
                  voiceIndex);
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
                                         loopEnabled);
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
