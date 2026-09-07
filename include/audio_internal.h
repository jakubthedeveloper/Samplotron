#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <stdint.h>

#include "AudioFileSource.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"
#include "AudioOutputMixer.h"
#include "audio.h"
#include "stream_manager.h"

namespace AudioInternal {

constexpr uint8_t kVolumeScaleMax = 100;
constexpr int kMixerBufferSamples = 512;
constexpr float kPerVoiceVolumeScale = 0.65f;

// Dynamic headroom: keep polyphony safe while allowing much louder single-voice playback.
constexpr float kDynamicMixMinGain = 0.125f;
constexpr float kDynamicMixMaxGain = 0.35f;
constexpr uint32_t kDynamicMixAttackUs = 120000;
constexpr uint32_t kDynamicMixReleaseUs = 20000;

// Limit per-update work so SD streamed voices don't monopolize audio task cycles.
constexpr uint16_t kVoiceLoopSampleBudget = 96;
// Short anti-click fade only when retriggering an active group.
constexpr uint32_t kRetriggerFadeInUs = 800;
// Retriggered voices from the same group are softly cut to avoid clicks.
// Keep this short to avoid audible comb/distortion from long overlap of the same sample.
constexpr uint32_t kRetriggerFadeOutUs = 6000;
// Default soft stop time used by control-path stop commands.
constexpr uint32_t kDefaultStopFadeOutUs = 9000;

enum class VoiceSourceType : uint8_t {
  None,
  StreamPath,
  RamData,
};

class AudioFileSourceRamWav : public AudioFileSource {
 public:
  AudioFileSourceRamWav() = default;

  bool open(const uint8_t *pcmData,
            uint32_t pcmBytes,
            uint16_t channelCount,
            uint32_t sampleRate,
            uint16_t bitsPerSample);

  uint32_t read(void *data, uint32_t len) override;
  bool seek(int32_t pos, int dir) override;
  bool close() override;
  bool isOpen() override;
  uint32_t getSize() override;
  uint32_t getPos() override;

 private:
  static constexpr uint32_t kHeaderBytes = 44U;

  static void writeLe16(uint8_t *dst, uint16_t value);
  static void writeLe32(uint8_t *dst, uint32_t value);

  uint8_t header_[kHeaderBytes] = {0};
  const uint8_t *data_ = nullptr;
  uint32_t dataBytes_ = 0;
  uint32_t pos_ = 0;
  bool open_ = false;
};

class FreshStartAudioGeneratorWAV : public AudioGeneratorWAV {
 public:
  bool begin(AudioFileSource *source, AudioOutput *output) override;
  bool stop() override;
};

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

struct WaveformCaptureState {
  mutable portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  int8_t ring[Audio::kWaveformPointCount] = {0};
  uint16_t head = 0;
  uint16_t count = 0;
  uint16_t decimationCounter = 0;
};

class StableAudioOutputI2S : public AudioOutputI2S {
 public:
  StableAudioOutputI2S(int port, int outputMode, int dmaCount, int useApll);

  bool SetRate(int hz) override;

  uint32_t rateSetCalls() const;
  uint32_t skippedRateSetCalls() const;
  uint32_t appliedRateSetCalls() const;

 private:
  int lastRateHz_ = -1;
  uint32_t rateSetCalls_ = 0;
  uint32_t skippedRateSetCalls_ = 0;
  uint32_t appliedRateSetCalls_ = 0;
};

class SimpleLimiterAudioOutput : public AudioOutput {
 public:
  explicit SimpleLimiterAudioOutput(AudioOutput *sink, WaveformCaptureState *waveformCapture);

  bool SetRate(int hz) override;
  bool SetChannels(int channels) override;
  bool begin() override;
  bool stop() override;
  bool loop() override;
  bool ConsumeSample(int16_t sample[2]) override;

 private:
  void captureWaveformSample(const int16_t sample[2]);
  void updateSmoothing();

  static constexpr uint16_t kWaveformDecimation = 32;
  static constexpr float kLimiterThreshold = 0.995f;
  static constexpr float kLimiterMakeupGain = 1.0f;
  static constexpr float kLimiterReleaseSeconds = 0.030f;

  AudioOutput *sink_ = nullptr;
  WaveformCaptureState *waveformCapture_ = nullptr;
  int sampleRateHz_ = 44100;
  float limiterGain_ = 1.0f;
  float releaseCoeff_ = 0.0f;
};

struct VoiceState {
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

struct EngineState {
  bool ready = false;
  StableAudioOutputI2S *out = nullptr;
  SimpleLimiterAudioOutput *limitedOut = nullptr;
  AudioOutputMixer *mixer = nullptr;
  VoiceState voices[Audio::kVoiceCount];
  StreamManager streamManager;
  uint32_t nextStartOrder = 1;
  Audio::RuntimeStats stats;
  float dynamicMixGain = kDynamicMixMaxGain;
  uint32_t dynamicMixLastUs = 0;
  WaveformCaptureState waveformCapture;
};

float gainFromVolume(uint8_t volume);

void stopVoice(VoiceState &voice);
void requestVoiceStop(VoiceState &voice, uint32_t fadeOutUs);
void refreshStats(EngineState *impl);
void updateDynamicMixGain(EngineState *impl, uint32_t nowUs);
void updateVoiceGain(VoiceState &voice, float dynamicMixGain, uint32_t nowUs);

bool requestStopVoicesForGroup(EngineState *impl,
                               int16_t retriggerGroupId,
                               uint32_t fadeOutUs,
                               int excludeVoiceIndex = -1);
bool requestStopAllVoices(EngineState *impl, uint32_t fadeOutUs);
bool hasActiveVoiceForGroup(EngineState *impl, int16_t retriggerGroupId);
int allocateVoiceSlot(EngineState *impl, int16_t retriggerGroupId, bool &voiceWasStolen);

bool beginVoiceFromPath(EngineState *impl,
                        int voiceIndex,
                        const String &samplePath,
                        uint8_t volume,
                        int16_t retriggerGroupId,
                        bool loopEnabled,
                        uint32_t fadeInUs);

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
                       uint32_t fadeInUs);

bool restartVoiceLoop(EngineState *impl, int voiceIndex);

bool copyWaveformSnapshot(const WaveformCaptureState &capture, Audio::WaveformSnapshot &snapshot);

}  // namespace AudioInternal
