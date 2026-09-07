#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

// Register-level I2C fake for the codec suite; no hardware driver is linked.
class TwoWire {
 public:
  explicit TwoWire(int) {}
  std::array<uint8_t, 256> registers{};
  std::vector<std::pair<uint8_t, uint8_t>> writes;
  int ignoredWriteRegister = -1;
  bool failRead = false;
  void begin(int, int, uint32_t) {}
  void beginTransmission(uint8_t) { bytes_.clear(); }
  void write(uint8_t value) { bytes_.push_back(value); }
  uint8_t endTransmission(bool = true) {
    if (!bytes_.empty()) selected_ = bytes_[0];
    if (bytes_.size() == 2) {
      writes.emplace_back(selected_, bytes_[1]);
      if (selected_ != ignoredWriteRegister) registers[selected_] = bytes_[1];
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t, uint8_t count) { return failRead ? 0 : count; }
  int read() { return registers[selected_]; }
 private:
  uint8_t selected_ = 0;
  std::vector<uint8_t> bytes_;
};
