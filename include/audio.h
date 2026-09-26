#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace SampleLibrary { struct Catalog; }

class Audio {
 public:
  static constexpr uint8_t kVoiceCount = 32;
  static constexpr uint16_t kWaveformPointCount = 128;

  struct RuntimeStats {
    uint8_t activeVoices = 0;
    uint8_t activeVoicePeak = 0;
    uint32_t voiceStealCount = 0;
  };

  // Cumulative counters for diagnosing playback that cannot keep up. Safe to
  // poll from another task; values may be a few updates stale.
  struct StreamingDiagnostics {
    uint32_t i2sUnderrunCount = 0;
    // Voice updates that got silence because the SD reader fell behind.
    uint32_t starvedUpdateCount = 0;
    uint32_t sdReadCount = 0;
    uint32_t sdBytesRead = 0;
    uint32_t sdMaxReadUs = 0;
    uint32_t sdMaxReadBytes = 0;
    uint32_t sdOpenFailureCount = 0;
    // SD triggers dropped because every stream buffer was in use.
    uint32_t sdNoFreeStreamCount = 0;
  };

  struct WaveformSnapshot {
    int8_t points[kWaveformPointCount] = {0};
    uint16_t validPoints = 0;
  };

  struct Impl;

  Audio();
  ~Audio();

  void setSampleCatalog(const SampleLibrary::Catalog *catalog) { catalog_ = catalog; }
  bool begin();
  // Moves SD streaming reads to their own task. Until then, update() reads.
  bool startStreamReader(uint8_t priority, int core);
  void update();
  // head/headBytes: optional preloaded start of the file's PCM, played from
  // RAM while the SD reader catches up.
  void playSamplePath(const String &samplePath,
                      uint8_t volume = 100,
                      int16_t retriggerGroupId = -1,
                      bool loopEnabled = false,
                      const uint8_t *head = nullptr,
                      uint32_t headBytes = 0);
  void stopAllVoices();
  void fadeOutAllVoices(uint32_t fadeOutUs);
  void stopLoopingVoicesForGroup(int16_t retriggerGroupId);
  void setLoopEnabledForGroup(int16_t retriggerGroupId, bool loopEnabled);
  bool playSampleRam(const uint8_t *pcmData,
                     uint32_t dataBytes,
                     uint16_t channelCount,
                     uint32_t sampleRate,
                     uint16_t bitsPerSample,
                     uint8_t volume = 100,
                     int16_t retriggerGroupId = -1,
                     bool loopEnabled = false);
  RuntimeStats runtimeStats() const;
  uint32_t voiceStealCount() const;
  StreamingDiagnostics streamingDiagnostics() const;
  bool waveformSnapshot(WaveformSnapshot &snapshot) const;

 private:
  const SampleLibrary::Catalog *catalog_ = nullptr;
  Impl *impl_ = nullptr;
};
