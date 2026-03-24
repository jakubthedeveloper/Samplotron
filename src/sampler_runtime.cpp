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
  
  SettingsStore::logRawJsonFromSd();
  return loaded;
}

void SamplerRuntime::applyAssignmentsToUi(Ui &ui, const SampleLibrary::Catalog &catalog) const {
  if (settings_.panicNote >= 0 && settings_.panicNote <= 127) {
    ui.setPanicMidiNote(settings_.panicNote);
  }

  int playbackModesApplied = 0;
  int playbackModesMissing = 0;
  for (int i = 0; i < settings_.playbackModeCount; i++) {
    const SettingsStore::SamplePlaybackMode &mode = settings_.playbackModes[i];
    const int sampleIndex = SampleLibrary::findIndexByPath(catalog, mode.samplePath);
    if (sampleIndex < 0) {
      playbackModesMissing++;
      continue;
    }
    ui.setSamplePlaybackMode(sampleIndex,
                             mode.loopPlaybackEnabled ? Ui::PlaybackMode::Loop
                                                      : Ui::PlaybackMode::OneShot);
    playbackModesApplied++;
  }

  int applied = 0;
  int missing = 0;
  for (int i = 0; i < settings_.assignmentCount; i++) {
    const SettingsStore::MidiAssignment &assignment = settings_.assignments[i];
    const int sampleIndex = SampleLibrary::findIndexByPath(catalog, assignment.samplePath);
    if (sampleIndex < 0) {
      
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
    
  }
}

void SamplerRuntime::collectAssignmentsFromUi(const Ui &ui, const SampleLibrary::Catalog &catalog) {
  settings_.panicNote = static_cast<int16_t>(ui.panicMidiNote());

  settings_.playbackModeCount = 0;
  for (int sampleIndex = 0; sampleIndex < catalog.count; sampleIndex++) {
    if (settings_.playbackModeCount >= SettingsStore::SamplerSettings::kMaxPlaybackModes) {
      
      break;
    }

    if (!ui.sampleLoopPlaybackEnabled(sampleIndex)) {
      continue;
    }

    const String &samplePath = catalog.paths[sampleIndex];
    if (samplePath.length() == 0) {
      continue;
    }

    SettingsStore::SamplePlaybackMode &mode = settings_.playbackModes[settings_.playbackModeCount];
    mode.samplePath = samplePath;
    mode.loopPlaybackEnabled = true;
    settings_.playbackModeCount++;
  }

  settings_.assignmentCount = 0;
  for (int note = 0; note < 128; note++) {
    if (settings_.assignmentCount >= SettingsStore::SamplerSettings::kMaxAssignments) {
      
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
    
  }

  if (DebugFlags::kEnableDebugLogs) {
    for (int i = 0; i < classificationReport_.itemCount; i++) {
      const SampleClassifier::AssignedSampleClassification &item = classificationReport_.items[i];
      
    }
  }
}

void SamplerRuntime::loadClassifiedRamSamplesAndLog() {
  const bool ok = SampleRamManager::prepare(settings_, classificationReport_, ramLoadReport_);
  
  if (ramLoadReport_.fixedBudgetMismatch) {
    
  }
  
}

void SamplerRuntime::buildActiveRegistryAndLog() {
  ActiveSampleRegistry::build(classificationReport_, activeRegistryReport_);
  if (DebugFlags::kEnableDebugLogs) {
    
    for (int i = 0; i < activeRegistryReport_.itemCount; i++) {
      const ActiveSampleRegistry::Entry &entry = activeRegistryReport_.items[i];
      
    }
  }
}
