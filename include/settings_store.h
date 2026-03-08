#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace SettingsStore {

struct MidiAssignment {
  uint8_t note = 0;
  String samplePath;
};

struct SamplerSettings {
  static constexpr uint32_t kDefaultSampleRamBudgetBytes = 1024UL * 1024UL;
  static constexpr int kMaxAssignments = 128;

  String version;
  uint32_t sampleRamBudgetBytes = kDefaultSampleRamBudgetBytes;

  int assignmentCount = 0;
  MidiAssignment assignments[kMaxAssignments];
};

void applyDefaults(SamplerSettings &settings);
bool loadFromSd(SamplerSettings &settings);
bool saveToSd(const SamplerSettings &settings);
bool logRawJsonFromSd();

}  // namespace SettingsStore
