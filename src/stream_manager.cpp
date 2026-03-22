#include "stream_manager.h"

#include "AudioFileSource.h"
#include "AudioFileSourceSD.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr uint32_t kSlowReadThresholdUs = 3000;

}  // namespace

class StreamManager::BufferedSdSource : public AudioFileSource {
 public:
  explicit BufferedSdSource(Diagnostics *diagnostics) : diagnostics_(diagnostics) {}
  ~BufferedSdSource() override {
    close();
    if (sourceMutex_) {
      vSemaphoreDelete(sourceMutex_);
      sourceMutex_ = nullptr;
    }
  }

  bool open(const char *path) override {
    if (!path || path[0] == '\0') return false;
    close();
    if (!sourceMutex_ || xSemaphoreTake(sourceMutex_, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    const bool opened = source_.open(path);
    if (opened) {
      size_ = source_.getSize();
      pos_ = 0;
      eof_ = (size_ == 0);
      open_ = true;
    }
    xSemaphoreGive(sourceMutex_);
    if (!opened) return false;
    return true;
  }

  uint32_t read(void *data, uint32_t len) override {
    if (!open_ || !data || len == 0) return 0;
    if (!sourceMutex_ || xSemaphoreTake(sourceMutex_, pdMS_TO_TICKS(5)) != pdTRUE) return 0;

    const uint32_t readStartUs = micros();
    const uint32_t readBytes = source_.read(data, len);
    const uint32_t elapsedUs = micros() - readStartUs;
    xSemaphoreGive(sourceMutex_);

    updateDiagnostics(readBytes, elapsedUs);
    if (readBytes == 0) {
      eof_ = true;
      return 0;
    }

    pos_ += readBytes;
    return readBytes;
  }

  bool seek(int32_t pos, int dir) override {
    if (!open_) return false;
    if (!sourceMutex_ || xSemaphoreTake(sourceMutex_, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    const bool ok = source_.seek(pos, dir);
    if (ok) {
      pos_ = source_.getPos();
      eof_ = (pos_ >= size_);
    }
    xSemaphoreGive(sourceMutex_);
    return ok;
  }

  bool close() override {
    bool closed = false;
    if (sourceMutex_ && xSemaphoreTake(sourceMutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
      closed = source_.close();
      xSemaphoreGive(sourceMutex_);
    }
    open_ = false;
    eof_ = false;
    pos_ = 0;
    size_ = 0;
    return closed;
  }

  bool isOpen() override { return open_; }
  uint32_t getSize() override { return size_; }
  uint32_t getPos() override { return pos_; }
  bool serviceRefill() { return false; }

 private:
  void updateDiagnostics(uint32_t readBytes, uint32_t elapsedUs) {
    if (!diagnostics_) return;

    diagnostics_->sourceReadCount++;
    diagnostics_->sourceBytesRead += readBytes;
    if (elapsedUs > diagnostics_->sourceMaxReadUs) {
      diagnostics_->sourceMaxReadUs = elapsedUs;
      diagnostics_->sourceMaxReadBytes = readBytes;
    }
    if (elapsedUs >= kSlowReadThresholdUs) {
      diagnostics_->sourceSlowReadCount++;
    }
  }

  AudioFileSourceSD source_;
  SemaphoreHandle_t sourceMutex_ = xSemaphoreCreateMutex();
  Diagnostics *diagnostics_ = nullptr;
  bool open_ = false;
  bool eof_ = false;
  uint32_t pos_ = 0;
  uint32_t size_ = 0;
};

StreamManager::~StreamManager() {
  shutdown();
}

bool StreamManager::begin(uint8_t streamCount) {
  shutdown();
  if (streamCount == 0 || streamCount > kMaxStreams) {
    return false;
  }

  diagnostics_ = Diagnostics{};
  for (uint8_t i = 0; i < streamCount; i++) {
    streams_[i] = new BufferedSdSource(&diagnostics_);
    if (!streams_[i]) {
      shutdown();
      return false;
    }
  }

  streamCount_ = streamCount;
  return true;
}

void StreamManager::shutdown() {
  for (uint8_t i = 0; i < kMaxStreams; i++) {
    delete streams_[i];
    streams_[i] = nullptr;
  }
  streamCount_ = 0;
}

bool StreamManager::openStream(uint8_t streamId, const char *path) {
  if (streamId >= streamCount_ || !streams_[streamId]) return false;
  return streams_[streamId]->open(path);
}

AudioFileSource *StreamManager::sourceForStream(uint8_t streamId) {
  if (streamId >= streamCount_) return nullptr;
  return streams_[streamId];
}

void StreamManager::closeStream(uint8_t streamId) {
  if (streamId >= streamCount_ || !streams_[streamId]) return;
  streams_[streamId]->close();
}

void StreamManager::closeAll() {
  for (uint8_t i = 0; i < streamCount_; i++) {
    if (streams_[i]) {
      streams_[i]->close();
    }
  }
}

const StreamManager::Diagnostics &StreamManager::diagnostics() const {
  return diagnostics_;
}
