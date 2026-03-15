#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace SettingsStore {

struct MidiAssignment {
  uint8_t note = 0;
  String samplePath;
  uint8_t volume = 100;
  bool loopPlaybackEnabled = false;
};

struct SamplerSettings {
  static constexpr uint32_t kDefaultSampleRamBudgetBytes = 1024UL * 1024UL;
  static constexpr int kMaxAssignments = 128;

  String version;
  uint32_t sampleRamBudgetBytes = kDefaultSampleRamBudgetBytes;
  int16_t panicNote = -1;

  int assignmentCount = 0;
  MidiAssignment assignments[kMaxAssignments];
};

void applyDefaults(SamplerSettings &settings);
bool loadFromSd(SamplerSettings &settings);
bool saveToSd(const SamplerSettings &settings);
bool logRawJsonFromSd();

}  // namespace SettingsStore
