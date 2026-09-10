#pragma once
#include <cstddef>
#include <cstdint>

namespace AudioInternal {
// One ESP32 DMA descriptor, submitted in a single driver call when possible.
// A short/failed write retains the exact unsubmitted byte suffix.
class PcmBlockBuffer {
 public:
  static constexpr size_t kFrames = 128;
  using Writer = size_t (*)(void *, const uint8_t *, size_t);
  bool consume(const int16_t sample[2], Writer writer, void *context);
  size_t pendingBytes() const { return count_ * 4 - sent_; }
 private:
  void submit(Writer writer, void *context);
  uint32_t words_[kFrames] = {};
  size_t count_ = 0;
  size_t sent_ = 0;
};
} // namespace AudioInternal
