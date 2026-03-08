#include "active_sample_registry.h"

#include "sample_ram_manager.h"

namespace {

void resetReport(ActiveSampleRegistry::RegistryReport &report) {
  report.itemCount = 0;
  report.effectiveRamCount = 0;
  report.effectiveStreamCount = 0;
  report.unavailableCount = 0;
  report.fallbackFromRamToStreamCount = 0;

  for (int i = 0; i < ActiveSampleRegistry::RegistryReport::kMaxItems; i++) {
    report.items[i] = ActiveSampleRegistry::Entry{};
  }
}

bool isUnavailableClassifiedMode(SampleClassifier::StorageMode mode) {
  return mode == SampleClassifier::StorageMode::MissingFile ||
         mode == SampleClassifier::StorageMode::InvalidFormat ||
         mode == SampleClassifier::StorageMode::ReadError;
}

}  // namespace

namespace ActiveSampleRegistry {

void build(const SampleClassifier::ClassificationReport &classification, RegistryReport &report) {
  resetReport(report);

  for (int i = 0; i < classification.itemCount && report.itemCount < RegistryReport::kMaxItems; i++) {
    const SampleClassifier::AssignedSampleClassification &classified = classification.items[i];
    Entry &entry = report.items[report.itemCount++];

    entry.note = classified.note;
    entry.path = classified.path;
    entry.classifiedMode = classified.mode;
    entry.dataBytes = classified.dataBytes;
    entry.durationSeconds = classified.durationSeconds;

    if (isUnavailableClassifiedMode(classified.mode)) {
      entry.effectiveMode = EffectiveStorageMode::Unavailable;
      report.unavailableCount++;
      continue;
    }

    if (classified.mode == SampleClassifier::StorageMode::Ram) {
      SampleRamManager::LoadedSampleInfo loadedInfo;
      if (SampleRamManager::getLoadedSampleByPath(classified.path, loadedInfo)) {
        entry.effectiveMode = EffectiveStorageMode::Ram;
        entry.ramOffsetBytes = loadedInfo.poolOffset;
        report.effectiveRamCount++;
      } else {
        entry.effectiveMode = EffectiveStorageMode::Stream;
        entry.fallbackFromRamToStream = true;
        report.fallbackFromRamToStreamCount++;
        report.effectiveStreamCount++;
      }
      continue;
    }

    entry.effectiveMode = EffectiveStorageMode::Stream;
    report.effectiveStreamCount++;
  }
}

const char *effectiveStorageModeLabel(EffectiveStorageMode mode) {
  switch (mode) {
    case EffectiveStorageMode::Ram:
      return "RAM";
    case EffectiveStorageMode::Stream:
      return "STREAM";
    case EffectiveStorageMode::Unavailable:
      return "UNAVAILABLE";
  }
  return "UNKNOWN";
}

}  // namespace ActiveSampleRegistry
