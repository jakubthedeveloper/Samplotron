#include "sampler_runtime.h"

#include "debug_flags.h"

void SamplerRuntime::applyDefaultSettings() {
  SettingsStore::applyDefaults(settings_);
  classificationReport_ = SampleClassifier::ClassificationReport{};
  ramLoadReport_ = SampleRamManager::LoadReport{};
  activeRegistryReport_ = ActiveSampleRegistry::RegistryReport{};
}

bool SamplerRuntime::loadSettingsFromSd() {
  const bool loaded = SettingsStore::loadFromSd(settings_);
  Serial.printf("Settings load: %s, assignments=%d, ram_budget=%lu, preload_fixed=%.2f\n",
                loaded ? "OK" : "DEFAULT",
                settings_.assignmentCount,
                static_cast<unsigned long>(settings_.sampleRamBudgetBytes),
                static_cast<double>(SampleClassifier::kFixedPreloadThresholdSeconds));
  SettingsStore::logRawJsonFromSd();
  return loaded;
}

void SamplerRuntime::applyAssignmentsToUi(Ui &ui, const SampleLibrary::Catalog &catalog) const {
  if (settings_.panicNote >= 0 && settings_.panicNote <= 127) {
    ui.setPanicMidiNote(settings_.panicNote);
  }

  int applied = 0;
  int missing = 0;
  for (int i = 0; i < settings_.assignmentCount; i++) {
    const SettingsStore::MidiAssignment &assignment = settings_.assignments[i];
    const int sampleIndex = SampleLibrary::findIndexByPath(catalog, assignment.samplePath);
    if (sampleIndex < 0) {
      Serial.printf("Assignment path missing on SD for note %d: %s\n",
                    static_cast<int>(assignment.note),
                    assignment.samplePath.c_str());
      missing++;
      continue;
    }
    if (ui.setMidiAssignment(assignment.note, sampleIndex)) {
      applied++;
    }
    ui.setSampleVolume(sampleIndex, assignment.volume);
    ui.setSamplePlaybackMode(sampleIndex,
                             assignment.loopPlaybackEnabled ? Ui::PlaybackMode::Loop
                                                            : Ui::PlaybackMode::OneShot);
  }
  if (DebugFlags::kEnableDebugLogs) {
    Serial.printf("Assignments applied: %d, missing: %d\n", applied, missing);
  }
}

void SamplerRuntime::collectAssignmentsFromUi(const Ui &ui, const SampleLibrary::Catalog &catalog) {
  settings_.panicNote = static_cast<int16_t>(ui.panicMidiNote());
  settings_.assignmentCount = 0;
  for (int note = 0; note < 128; note++) {
    if (settings_.assignmentCount >= SettingsStore::SamplerSettings::kMaxAssignments) {
      Serial.println("Assignment export truncated to settings capacity");
      break;
    }

    const int sampleIndex = ui.assignedSampleForMidiNote(note);
    if (sampleIndex < 0 || sampleIndex >= catalog.count) continue;

    SettingsStore::MidiAssignment &entry = settings_.assignments[settings_.assignmentCount];
    entry.note = static_cast<uint8_t>(note);
    entry.samplePath = catalog.paths[sampleIndex];
    entry.volume = static_cast<uint8_t>(ui.sampleVolumeForSample(sampleIndex));
    entry.loopPlaybackEnabled = ui.sampleLoopPlaybackEnabled(sampleIndex);
    settings_.assignmentCount++;
  }
}

void SamplerRuntime::rebuildPreparedSamples() {
  classifyAssignedSamplesAndLog();
  loadClassifiedRamSamplesAndLog();
  buildActiveRegistryAndLog();
}

bool SamplerRuntime::saveSettingsToSd() const {
  return SettingsStore::saveToSd(settings_);
}

const SettingsStore::SamplerSettings &SamplerRuntime::settings() const {
  return settings_;
}

int SamplerRuntime::assignedSamplesCount() const {
  return settings_.assignmentCount;
}

int SamplerRuntime::ramSampleCount() const {
  return classificationReport_.ramSampleCount;
}

int SamplerRuntime::streamSampleCount() const {
  return classificationReport_.streamSampleCount;
}

uint32_t SamplerRuntime::sampleRamUsedBytes() const {
  return ramLoadReport_.usedBytes;
}

int SamplerRuntime::ramUsagePercent() const {
  const uint32_t budgetBytes = settings_.sampleRamBudgetBytes;
  if (budgetBytes == 0) return 0;
  const uint32_t pct = (100UL * ramLoadReport_.usedBytes) / budgetBytes;
  return (pct > 100UL) ? 100 : static_cast<int>(pct);
}

const ActiveSampleRegistry::Entry *SamplerRuntime::findRegistryEntryForNote(int note) const {
  for (int i = 0; i < activeRegistryReport_.itemCount; i++) {
    if (activeRegistryReport_.items[i].note == static_cast<uint8_t>(note)) {
      return &activeRegistryReport_.items[i];
    }
  }
  return nullptr;
}

const SampleClassifier::AssignedSampleClassification *SamplerRuntime::findClassificationByPath(
    const String &path) const {
  for (int i = 0; i < classificationReport_.itemCount; i++) {
    const SampleClassifier::AssignedSampleClassification &item = classificationReport_.items[i];
    if (item.path == path) {
      return &item;
    }
  }
  return nullptr;
}

void SamplerRuntime::classifyAssignedSamplesAndLog() {
  SampleClassifier::classifyAssignedSamples(settings_, classificationReport_);
  if (DebugFlags::kEnableDebugLogs) {
    Serial.printf(
        "Classification: assigned=%d ram=%d stream=%d missing=%d invalid=%d read_err=%d "
        "ram_used=%lu/%lu\n",
        classificationReport_.itemCount,
        classificationReport_.ramSampleCount,
        classificationReport_.streamSampleCount,
        classificationReport_.missingFileCount,
        classificationReport_.invalidFormatCount,
        classificationReport_.readErrorCount,
        static_cast<unsigned long>(classificationReport_.sampleRamUsedBytes),
        static_cast<unsigned long>(classificationReport_.sampleRamBudgetBytes));
  }

  if (DebugFlags::kEnableDebugLogs) {
    for (int i = 0; i < classificationReport_.itemCount; i++) {
      const SampleClassifier::AssignedSampleClassification &item = classificationReport_.items[i];
      Serial.printf("  note=%d mode=%s dur=%.3fs bytes=%lu path=%s\n",
                    static_cast<int>(item.note),
                    SampleClassifier::storageModeLabel(item.mode),
                    static_cast<double>(item.durationSeconds),
                    static_cast<unsigned long>(item.dataBytes),
                    item.path.c_str());
    }
  }
}

void SamplerRuntime::loadClassifiedRamSamplesAndLog() {
  const bool ok = SampleRamManager::prepare(settings_, classificationReport_, ramLoadReport_);
  Serial.printf("RAM preload: %s requested=%d loaded=%d fallback=%d read_err=%d used=%lu/%lu budget=%lu",
                ok ? "OK" : "POOL_ALLOC_FAIL",
                ramLoadReport_.requestedRamCount,
                ramLoadReport_.loadedRamCount,
                ramLoadReport_.fallbackToStreamCount,
                ramLoadReport_.readErrorCount,
                static_cast<unsigned long>(ramLoadReport_.usedBytes),
                static_cast<unsigned long>(ramLoadReport_.allocatedBytes),
                static_cast<unsigned long>(ramLoadReport_.effectiveBudgetBytes));
  if (ramLoadReport_.fixedBudgetMismatch) {
    Serial.print(" (fixed-pool budget mismatch)");
  }
  Serial.println();
}

void SamplerRuntime::buildActiveRegistryAndLog() {
  ActiveSampleRegistry::build(classificationReport_, activeRegistryReport_);
  if (DebugFlags::kEnableDebugLogs) {
    Serial.printf("Active registry: items=%d effective_ram=%d effective_stream=%d unavailable=%d "
                  "ram_fallback_to_stream=%d\n",
                  activeRegistryReport_.itemCount,
                  activeRegistryReport_.effectiveRamCount,
                  activeRegistryReport_.effectiveStreamCount,
                  activeRegistryReport_.unavailableCount,
                  activeRegistryReport_.fallbackFromRamToStreamCount);
    for (int i = 0; i < activeRegistryReport_.itemCount; i++) {
      const ActiveSampleRegistry::Entry &entry = activeRegistryReport_.items[i];
      Serial.printf("  note=%d classified=%s effective=%s bytes=%lu path=%s\n",
                    static_cast<int>(entry.note),
                    SampleClassifier::storageModeLabel(entry.classifiedMode),
                    ActiveSampleRegistry::effectiveStorageModeLabel(entry.effectiveMode),
                    static_cast<unsigned long>(entry.dataBytes),
                    entry.path.c_str());
    }
  }
}
