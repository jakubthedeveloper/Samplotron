#include "validated_wav_source.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

bool ValidatedWavSource::attach(AudioFileSource *source, const WavValidation::Result &info) {
  source_ = nullptr; size_ = pos_ = 0;
  if (!source || !source->isOpen() || !info.playable() || info.dataBytes == 0 ||
      info.dataBytes > 0x7FFFFFD3U || info.dataOffset > source->getSize() ||
      info.dataBytes > source->getSize() - info.dataOffset) return false;
  if (!source->seek(info.dataOffset, SEEK_SET)) return false;
  source_ = source; offset_ = info.dataOffset; size_ = info.dataBytes + 44;
  WavValidation::pcmHeader(header_, info.dataBytes);
  return true;
}
uint32_t ValidatedWavSource::read(void *data, uint32_t len) {
  if (!isOpen() || !data || pos_ >= size_) return 0;
  len = std::min(len, size_ - pos_);
  auto *dst = static_cast<uint8_t *>(data);
  uint32_t copied = 0;
  if (pos_ < 44) {
    copied = std::min(len, 44 - pos_);
    std::memcpy(dst, header_ + pos_, copied);
    pos_ += copied;
  }
  if (len > copied) {
    const uint32_t bytes = source_->read(dst + copied, len - copied);
    pos_ += bytes; copied += bytes;
  }
  return copied;
}
bool ValidatedWavSource::seek(int32_t pos, int dir) {
  if (!isOpen()) return false;
  int64_t target = pos;
  if (dir == SEEK_CUR) target += pos_;
  else if (dir == SEEK_END) target += size_;
  else if (dir != SEEK_SET) return false;
  if (target < 0 || target > size_) return false;
  const uint32_t physical = offset_ + (target > 44 ? static_cast<uint32_t>(target - 44) : 0);
  if (!source_->seek(physical, SEEK_SET)) return false;
  pos_ = static_cast<uint32_t>(target);
  return true;
}
bool ValidatedWavSource::close() {
  const bool ok = source_ ? source_->close() : true;
  source_ = nullptr; size_ = pos_ = 0;
  return ok;
}
bool ValidatedWavSource::isOpen() { return source_ && source_->isOpen(); }
