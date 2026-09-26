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
    uint32_t sdReadCount = 0;
    uint32_t sdSlowReadCount = 0;
    uint32_t sdMaxReadUs = 0;
    uint32_t sdMaxReadBytes = 0;
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
  void update();
  void playSamplePath(const String &samplePath,
                      uint8_t volume = 100,
                      int16_t retriggerGroupId = -1,
                      bool loopEnabled = false);
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
