#include "stream_manager.h"

#include <limits.h>
#include <string.h>

#include "AudioFileSource.h"
#include "AudioFileSourceSD.h"

namespace {

constexpr uint32_t kSlowReadThresholdUs = 3000;

}  // namespace

class StreamManager::BufferedSdSource : public AudioFileSource {
 public:
  explicit BufferedSdSource(Diagnostics *diagnostics) : diagnostics_(diagnostics) {}

  bool open(const char *path) override {
    if (!path || path[0] == '\0') return false;
    close();
    if (!source_.open(path)) return false;

    size_ = source_.getSize();
    pos_ = 0;
    readIndex_ = 0;
    validBytes_ = 0;
    eof_ = (size_ == 0);
    open_ = true;
    fillReadAhead(kInitialReadAheadBytes);
    return true;
  }

  uint32_t read(void *data, uint32_t len) override {
    if (!open_ || !data || len == 0) return 0;

    uint8_t *out = static_cast<uint8_t *>(data);
    uint32_t copied = 0;
    uint32_t remaining = len;

    while (remaining > 0) {
      if (validBytes_ == 0) {
        fillReadAhead(1);
        if (validBytes_ == 0) break;
      }

      uint32_t contiguous = kReadAheadBufferBytes - readIndex_;
      if (contiguous > validBytes_) {
        contiguous = validBytes_;
      }
      const uint32_t chunk = (remaining < contiguous) ? remaining : contiguous;
      memcpy(out + copied, buffer_ + readIndex_, chunk);

      readIndex_ = (readIndex_ + chunk) % kReadAheadBufferBytes;
      validBytes_ -= chunk;
      pos_ += chunk;
      copied += chunk;
      remaining -= chunk;

      fillReadAhead(remaining);
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
      target = static_cast<int64_t>(size_) + pos;
    } else {
      return false;
    }

    if (target < 0 || static_cast<uint64_t>(target) > static_cast<uint64_t>(size_)) {
      return false;
    }

    const uint32_t targetPos = static_cast<uint32_t>(target);
    if (targetPos >= pos_ && targetPos <= (pos_ + validBytes_)) {
      const uint32_t skip = targetPos - pos_;
      readIndex_ = (readIndex_ + skip) % kReadAheadBufferBytes;
      validBytes_ -= skip;
      pos_ = targetPos;
      fillReadAhead(kTargetReadAheadBytes);
      return true;
    }

    if (!source_.seek(static_cast<int32_t>(targetPos), SEEK_SET)) return false;
    pos_ = targetPos;
    readIndex_ = 0;
    validBytes_ = 0;
    eof_ = (pos_ >= size_);
    fillReadAhead(kInitialReadAheadBytes);
    return true;
  }

  bool close() override {
    const bool closed = source_.close();
    open_ = false;
    eof_ = false;
    pos_ = 0;
    size_ = 0;
    readIndex_ = 0;
    validBytes_ = 0;
    return closed;
  }

  bool isOpen() override { return open_; }
  uint32_t getSize() override { return size_; }
  uint32_t getPos() override { return pos_; }

 private:
  static constexpr uint32_t kReadAheadBufferBytes = 8192;
  static constexpr uint32_t kInitialReadAheadBytes = 4096;
  static constexpr uint32_t kTargetReadAheadBytes = 6144;

  void fillReadAhead(uint32_t requestedBytes) {
    if (!open_ || eof_) return;

    uint32_t targetBytes = requestedBytes;
    if (targetBytes < kTargetReadAheadBytes) {
      targetBytes = kTargetReadAheadBytes;
    }
    if (targetBytes > kReadAheadBufferBytes) {
      targetBytes = kReadAheadBufferBytes;
    }

    while (!eof_ && validBytes_ < targetBytes) {
      const uint32_t freeBytes = kReadAheadBufferBytes - validBytes_;
      if (freeBytes == 0) return;

      const uint32_t writeIndex = (readIndex_ + validBytes_) % kReadAheadBufferBytes;
      uint32_t contiguousFree = kReadAheadBufferBytes - writeIndex;
      if (contiguousFree > freeBytes) {
        contiguousFree = freeBytes;
      }

      const uint32_t readStartUs = micros();
      const uint32_t readBytes = source_.read(buffer_ + writeIndex, contiguousFree);
      const uint32_t elapsedUs = micros() - readStartUs;
      updateDiagnostics(readBytes, elapsedUs);

      if (readBytes == 0) {
        eof_ = true;
        return;
      }

      validBytes_ += readBytes;
      if (diagnostics_) {
        diagnostics_->bufferRefillCount++;
      }
    }
  }

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
  Diagnostics *diagnostics_ = nullptr;
  bool open_ = false;
  bool eof_ = false;
  uint32_t pos_ = 0;
  uint32_t size_ = 0;
  uint32_t readIndex_ = 0;
  uint32_t validBytes_ = 0;
  uint8_t buffer_[kReadAheadBufferBytes] = {0};
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

