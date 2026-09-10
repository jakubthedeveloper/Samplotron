#pragma once
#include "AudioFileSource.h"
#include "SD.h"
#include <cstdio>
class AudioFileSourceSD : public AudioFileSource {
 public:
  bool open(const char *path) override { file_ = SD.open(path); pos_ = 0; return bool(file_); }
  uint32_t read(void *dst, uint32_t n) override {
    if (!file_) return 0;
    const auto bytes = file_.read(static_cast<uint8_t *>(dst), n); pos_ += bytes; return bytes;
  }
  bool seek(int32_t p, int dir) override {
    int64_t next = p;
    if (dir == SEEK_CUR) next += pos_;
    else if (dir == SEEK_END) next += getSize();
    else if (dir != SEEK_SET) return false;
    if (next < 0 || next > getSize() || !file_.seek(next)) return false;
    pos_ = next; return true;
  }
  bool close() override { file_.close(); return true; }
  bool isOpen() override { return bool(file_); }
  uint32_t getSize() override { return file_.size(); }
  uint32_t getPos() override { return pos_; }
 private:
  File file_;
  uint32_t pos_ = 0;
};
