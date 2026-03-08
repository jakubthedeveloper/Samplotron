#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "settings_store.h"

namespace SampleClassifier {

constexpr float kFixedPreloadThresholdSeconds = 5.0f;

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
  float durationSeconds = 0.0f;
  StorageMode mode = StorageMode::ReadError;
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
                             ClassificationReport &report);
const char *storageModeLabel(StorageMode mode);

}  // namespace SampleClassifier
