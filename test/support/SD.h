#pragma once
#include "Arduino.h"
#include <algorithm>
#include <map>
#include <memory>
#include <vector>
#include <string>

constexpr int FILE_READ = 0;
namespace FakeSD {
inline std::map<std::string, std::vector<uint8_t>> files;
inline int opens = 0, reads = 0, seeks = 0;
inline size_t bytesRead = 0;
}
class File {
 public:
  File() = default;
  explicit File(std::string name, bool directory = false) : name_(name), valid_(true), directory_(directory) {
    if (directory) for (const auto &entry : FakeSD::files) entries_.push_back(entry.first);
  }
  explicit operator bool() const { return valid_; }
  bool isDirectory() const { return directory_; }
  const char *name() const { return name_.c_str(); }
  uint32_t size() const { return valid_ && !directory_ ? FakeSD::files.at(name_).size() : 0; }
  bool seek(uint32_t offset) { ++FakeSD::seeks; if (offset > size()) return false; pos_ = offset; return true; }
  int read(uint8_t *dst, size_t bytes) {
    ++FakeSD::reads;
    bytes = std::min(bytes, static_cast<size_t>(size() - pos_));
    std::memcpy(dst, FakeSD::files.at(name_).data() + pos_, bytes);
    pos_ += bytes; FakeSD::bytesRead += bytes;
    return bytes;
  }
  File openNextFile() { return next_ < entries_.size() ? File(entries_[next_++]) : File(); }
  void close() { valid_ = false; }
 private:
  std::string name_;
  bool valid_ = false, directory_ = false;
  uint32_t pos_ = 0;
  size_t next_ = 0;
  std::vector<std::string> entries_;
};
struct SDClass {
  File open(const String &path, int = FILE_READ) {
    ++FakeSD::opens;
    if (path == "/samples") return File("/samples", true);
    return FakeSD::files.count(path.c_str()) ? File(path.c_str()) : File();
  }
};
inline SDClass SD;
