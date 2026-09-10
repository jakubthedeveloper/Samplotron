#include "pcm_block_buffer.h"

namespace AudioInternal {
void PcmBlockBuffer::submit(Writer writer, void *context) {
  if (count_ != kFrames || !writer) return;
  const size_t remaining = sizeof(words_) - sent_;
  const size_t written = writer(context, reinterpret_cast<const uint8_t *>(words_) + sent_, remaining);
  if (written > remaining) return;
  sent_ += written;
  if (sent_ == sizeof(words_)) count_ = sent_ = 0;
}
bool PcmBlockBuffer::consume(const int16_t sample[2], Writer writer, void *context) {
  if (count_ == kFrames) submit(writer, context);
  if (count_ == kFrames) return false;
  words_[count_++] = static_cast<uint16_t>(sample[0]) |
                    (uint32_t(static_cast<uint16_t>(sample[1])) << 16);
  if (count_ == kFrames) submit(writer, context);
  return true;
}
} // namespace AudioInternal
