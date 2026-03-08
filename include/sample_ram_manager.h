#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "sample_classifier.h"
#include "settings_store.h"

namespace SampleRamManager {

struct LoadReport {
  uint32_t budgetBytes = 0;
  uint32_t allocatedBytes = 0;
  uint32_t usedBytes = 0;
  int requestedRamCount = 0;
  int loadedRamCount = 0;
  int fallbackToStreamCount = 0;
  int readErrorCount = 0;
};

bool prepare(const SettingsStore::SamplerSettings &settings,
             const SampleClassifier::ClassificationReport &classification,
             LoadReport &report);

void release();

}  // namespace SampleRamManager
