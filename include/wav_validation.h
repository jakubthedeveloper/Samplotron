#pragma once
#include <cstddef>
#include <cstdint>

namespace WavValidation {
enum class Status : uint8_t { Unchecked, Valid, Unsupported, Invalid, ReadError };
struct Result {
  Status status = Status::Unchecked;
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;
  bool playable() const { return status == Status::Valid; }
};
class Reader {
 public:
  virtual ~Reader() = default;
  virtual uint32_t size() const = 0;
  virtual bool readAt(uint32_t offset, uint8_t *dst, size_t bytes) = 0;
};
Result validate(Reader &reader);
const char *label(Status status);
void pcmHeader(uint8_t *header, uint32_t dataBytes);
}  // namespace WavValidation
