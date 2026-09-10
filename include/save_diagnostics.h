#pragma once

#include <stdint.h>

// Stable codes shown as E00..E17 on the OLED; see docs/save-errors.md.
// Save runs synchronously on the UI task, so no cross-task access is needed.
namespace SaveDiagnostics {
enum class Stage : uint8_t {
  Unknown = 0,
  Callback = 1,
  Initialization = 2,
  Playback = 3,
  LoaderQueue = 4,
  Loader = 5,
  Memory = 6,
  Json = 7,
  OpenWrite = 8,
  WriteBuffer = 9,
  Write = 10,
  OpenRead = 11,
  ReadBuffer = 12,
  FileSize = 13,
  Read = 14,
  DataMismatch = 15,
  Backup = 16,
  Rename = 17,
};

inline Stage &stage() {
  static Stage value = Stage::Unknown;
  return value;
}
inline void setStage(Stage value) { stage() = value; }
}  // namespace SaveDiagnostics
