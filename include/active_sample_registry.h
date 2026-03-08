#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "sample_classifier.h"

namespace ActiveSampleRegistry {

enum class EffectiveStorageMode : uint8_t {
  Ram,
  Stream,
  Unavailable,
};

struct Entry {
  uint8_t note = 0;
  String path;
  SampleClassifier::StorageMode classifiedMode = SampleClassifier::StorageMode::ReadError;
  EffectiveStorageMode effectiveMode = EffectiveStorageMode::Unavailable;
  bool fallbackFromRamToStream = false;
  uint32_t dataBytes = 0;
  float durationSeconds = 0.0f;
  uint32_t ramOffsetBytes = 0;
};

struct RegistryReport {
  static constexpr int kMaxItems = SampleClassifier::ClassificationReport::kMaxItems;

  int itemCount = 0;
  int effectiveRamCount = 0;
  int effectiveStreamCount = 0;
  int unavailableCount = 0;
  int fallbackFromRamToStreamCount = 0;
  Entry items[kMaxItems];
};

void build(const SampleClassifier::ClassificationReport &classification, RegistryReport &report);
const char *effectiveStorageModeLabel(EffectiveStorageMode mode);

}  // namespace ActiveSampleRegistry
