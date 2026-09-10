#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <stdint.h>

class AudioFileSource;
namespace SampleLibrary { struct Catalog; }

class StreamManager {
 public:
  static constexpr uint8_t kMaxStreams = 32;

  struct Diagnostics {
    uint32_t sourceReadCount = 0;
    uint32_t sourceSlowReadCount = 0;
    uint32_t sourceMaxReadUs = 0;
    uint32_t sourceMaxReadBytes = 0;
    uint32_t sourceBytesRead = 0;
    uint32_t bufferRefillCount = 0;
  };

  StreamManager() = default;
  ~StreamManager();

  bool begin(uint8_t streamCount = kMaxStreams, const SampleLibrary::Catalog *catalog = nullptr);
  void shutdown();

  bool openStream(uint8_t streamId, const char *path);
  AudioFileSource *sourceForStream(uint8_t streamId);
  void closeStream(uint8_t streamId);
  void closeAll();

  const Diagnostics &diagnostics() const;

 private:
  class BufferedSdSource;

  BufferedSdSource *streams_[kMaxStreams] = {nullptr};
  uint8_t streamCount_ = 0;
  Diagnostics diagnostics_;
  const SampleLibrary::Catalog *catalog_ = nullptr;
};
