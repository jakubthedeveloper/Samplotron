#include "wav_validation.h"
#include <cstring>

namespace WavValidation {
namespace {
uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
}
Result validate(Reader &reader) {
  Result result;
  result.status = Status::Invalid;
  const uint32_t size = reader.size();
  // Playback sources use signed 32-bit seek positions, including a virtual header.
  if (size < 44 || size > 0x7FFFFFD3U) return result;
  uint8_t riff[12];
  if (!reader.readAt(0, riff, 12)) { result.status = Status::ReadError; return result; }
  if (std::memcmp(riff, "RIFF", 4) || std::memcmp(riff + 8, "WAVE", 4) ||
      le32(riff + 4) != size - 8) return result;
  bool fmt = false, data = false, supported = false;
  uint32_t pos = 12;
  while (pos < size) {
    if (size - pos < 8) return result;
    uint8_t chunk[8];
    if (!reader.readAt(pos, chunk, 8)) { result.status = Status::ReadError; return result; }
    const uint32_t bytes = le32(chunk + 4);
    pos += 8;
    // Subtraction-based bounds checks also reject malicious size overflows.
    if (bytes > size - pos || (bytes & 1U) > size - pos - bytes) return result;
    if (std::memcmp(chunk, "fmt ", 4) == 0) {
      if (fmt || data || bytes < 16 || bytes == 17) return result;
      uint8_t core[18];
      const size_t coreBytes = bytes == 16 ? 16 : 18;
      if (!reader.readAt(pos, core, coreBytes)) { result.status = Status::ReadError; return result; }
      if (bytes >= 18 && le16(core + 16) != bytes - 18) return result;
      fmt = true;
      supported = le16(core) == 1 && le16(core + 2) == 1 &&
                  le32(core + 4) == 44100 && le16(core + 14) == 16;
      if (supported && (le16(core + 12) != 2 || le32(core + 8) != 88200)) return result;
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      if (!fmt || data || bytes == 0 || (supported && bytes % 2 != 0)) return result;
      data = true;
      result.dataOffset = pos;
      result.dataBytes = bytes;
    }
    pos += bytes + (bytes & 1U);
  }
  if (fmt && data) result.status = supported ? Status::Valid : Status::Unsupported;
  return result;
}
const char *label(Status status) {
  switch (status) {
    case Status::Valid: return "OK";
    case Status::Unsupported: return "BAD FORMAT";
    case Status::Invalid: return "BAD WAV";
    case Status::ReadError: return "READ ERROR";
    case Status::Unchecked: return "UNCHECKED";
  }
  return "UNCHECKED";
}
void pcmHeader(uint8_t *header, uint32_t dataBytes) {
  std::memset(header, 0, 44);
  std::memcpy(header, "RIFF", 4); put32(header + 4, dataBytes + 36);
  std::memcpy(header + 8, "WAVEfmt ", 8); put32(header + 16, 16);
  header[20] = 1; header[22] = 1; put32(header + 24, 44100);
  put32(header + 28, 88200); header[32] = 2; header[34] = 16;
  std::memcpy(header + 36, "data", 4); put32(header + 40, dataBytes);
}
}  // namespace WavValidation
