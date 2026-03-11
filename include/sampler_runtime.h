#pragma once

#include <Arduino.h>

#include "active_sample_registry.h"
#include "sample_classifier.h"
#include "sample_library.h"
#include "sample_ram_manager.h"
#include "settings_store.h"
#include "ui.h"

class SamplerRuntime {
 public:
  void applyDefaultSettings();
  bool loadSettingsFromSd();

  void applyAssignmentsToUi(Ui &ui, const SampleLibrary::Catalog &catalog) const;
  void collectAssignmentsFromUi(const Ui &ui, const SampleLibrary::Catalog &catalog);
  void rebuildPreparedSamples();
  bool saveSettingsToSd() const;

  const SettingsStore::SamplerSettings &settings() const;
  int assignedSamplesCount() const;
  int ramUsagePercent() const;

  const ActiveSampleRegistry::Entry *findRegistryEntryForNote(int note) const;
  const SampleClassifier::AssignedSampleClassification *findClassificationByPath(
      const String &path) const;

 private:
  void classifyAssignedSamplesAndLog();
  void loadClassifiedRamSamplesAndLog();
  void buildActiveRegistryAndLog();

  SettingsStore::SamplerSettings settings_;
  SampleClassifier::ClassificationReport classificationReport_;
  SampleRamManager::LoadReport ramLoadReport_;
  ActiveSampleRegistry::RegistryReport activeRegistryReport_;
};
