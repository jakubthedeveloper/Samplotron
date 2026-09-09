#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

using std::size_t;

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
};

constexpr int HIGH = 1;
constexpr int LOW = 0;

class String {
 public:
  String() = default;
  String(const char *s) : data_(s ? s : "") {}
  String(const std::string &s) : data_(s) {}

  String &operator=(const char *s) {
    data_ = s ? s : "";
    return *this;
  }

  String &operator=(const std::string &s) {
    data_ = s;
    return *this;
  }

  bool operator==(const String &other) const { return data_ == other.data_; }
  bool operator!=(const String &other) const { return data_ != other.data_; }
  bool operator==(const char *other) const { return data_ == (other ? other : ""); }
  bool operator!=(const char *other) const { return !(*this == other); }

  bool endsWith(const char *suffix) const {
    const std::string s(suffix);
    return data_.size() >= s.size() && data_.compare(data_.size() - s.size(), s.size(), s) == 0;
  }
  bool operator<(const String &other) const { return data_ < other.data_; }
  friend String operator+(const char *prefix, const String &s) { return String(std::string(prefix) + s.data_); }

  int length() const { return static_cast<int>(data_.size()); }

  const char *c_str() const { return data_.c_str(); }

  void toCharArray(char *buffer, unsigned int bufferSize) const {
    if (!buffer || bufferSize == 0) return;
    const size_t copyLen = (data_.size() < (bufferSize - 1U)) ? data_.size() : (bufferSize - 1U);
    std::memcpy(buffer, data_.data(), copyLen);
    buffer[copyLen] = '\0';
  }

  int lastIndexOf(char ch) const {
    const size_t pos = data_.find_last_of(ch);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  String substring(int from, int to) const {
    if (from < 0) from = 0;
    if (to < from) to = from;
    if (from >= static_cast<int>(data_.size())) return String("");
    const int len = to - from;
    return String(data_.substr(static_cast<size_t>(from), static_cast<size_t>(len)));
  }

  String substring(int from) const {
    if (from < 0) from = 0;
    if (from >= static_cast<int>(data_.size())) return String("");
    return String(data_.substr(static_cast<size_t>(from)));
  }

 private:
  std::string data_;
};

inline bool operator==(const char *lhs, const String &rhs) {
  return rhs == lhs;
}

class HardwareSerial {
 public:
  template <typename... Args>
  void printf(const char *, Args...) {}

  void println(const char * = "") {}
  void print(const char *) {}
};

extern HardwareSerial Serial;

unsigned long millis();
void testSetMillis(unsigned long value);
void testAdvanceMillis(unsigned long delta);
