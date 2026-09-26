#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "settings_store.h"

namespace SampleLibrary { struct Catalog; }

namespace SampleClassifier {

// Start of each streamed sample kept in RAM (~185 ms), so playback begins
// at once while the SD reader opens the file and catches up.
constexpr uint32_t kStreamHeadBytes = 16 * 1024;
constexpr uint32_t kRequiredSampleRate = 44100;
constexpr uint16_t kRequiredChannelCount = 1;
constexpr uint16_t kRequiredBitsPerSample = 16;
constexpr uint16_t kRequiredAudioFormatPcm = 1;

enum class StorageMode : uint8_t {
  Ram,
  Stream,
  MissingFile,
  InvalidFormat,
  ReadError,
};

struct AssignedSampleClassification {
  uint8_t note = 0;
  String path;
  uint16_t channelCount = 0;
  uint16_t bitsPerSample = 0;
  uint32_t sampleRate = 0;
  uint32_t dataBytes = 0;
  uint32_t dataOffset = 0;
  float durationSeconds = 0.0f;
  StorageMode mode = StorageMode::ReadError;
  uint32_t headBytes = 0;  // Stream only: bytes preloaded from the start.
};

struct ClassificationReport {
  static constexpr int kMaxItems = SettingsStore::SamplerSettings::kMaxAssignments;

  int itemCount = 0;
  uint32_t sampleRamBudgetBytes = 0;
  uint32_t sampleRamUsedBytes = 0;
  int ramSampleCount = 0;
  int streamSampleCount = 0;
  int missingFileCount = 0;
  int invalidFormatCount = 0;
  int readErrorCount = 0;

  AssignedSampleClassification items[kMaxItems];
};

void classifyAssignedSamples(const SettingsStore::SamplerSettings &settings,
                             const SampleLibrary::Catalog &catalog,
                             ClassificationReport &report);
const char *storageModeLabel(StorageMode mode);

}  // namespace SampleClassifier
