#include <Arduino.h>
#include <SD.h>

#include "codec_es8388.h"
#include "debug_flags.h"
#include "display_ssd1309.h"
#include "input.h"
#include "midi.h"
#include "pins.h"
#include "audio.h"
#include "ui.h"
#include "storage_sd.h"
#include "settings_store.h"
#include "sample_classifier.h"
#include "sample_ram_manager.h"
#include "active_sample_registry.h"

namespace {

Audio gAudio;
Input gInput;
Midi gMidi;
Ui gUi;
DisplaySsd1309 gDisplay;

constexpr int kMaxSamples = Ui::kMaxSamples;
String gSamplePaths[kMaxSamples];
String gSampleNames[kMaxSamples];
int gSampleCount = 0;
SettingsStore::SamplerSettings gSettings;
SampleClassifier::ClassificationReport gClassificationReport;
SampleRamManager::LoadReport gRamLoadReport;
ActiveSampleRegistry::RegistryReport gActiveRegistryReport;
DisplaySsd1309::BootScreenModel gBootScreenModel;
bool gBootScreenDismissRequested = false;

bool isWavFile(const String &name) {
  return name.endsWith(".wav") || name.endsWith(".WAV");
}

void sortSamplesByName() {
  for (int i = 0; i < gSampleCount - 1; i++) {
    for (int j = i + 1; j < gSampleCount; j++) {
      if (gSampleNames[j] < gSampleNames[i]) {
        String n = gSampleNames[i];
        String p = gSamplePaths[i];
        gSampleNames[i] = gSampleNames[j];
        gSamplePaths[i] = gSamplePaths[j];
        gSampleNames[j] = n;
        gSamplePaths[j] = p;
      }
    }
  }
}

void loadSamplesFromSd() {
  gSampleCount = 0;

  File dir = SD.open("/samples");
  if (!dir || !dir.isDirectory()) {
    Serial.println("Missing /samples directory");
    return;
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      String entryName = entry.name();
      int slash = entryName.lastIndexOf('/');
      String fileName = (slash >= 0) ? entryName.substring(slash + 1) : entryName;
      if (isWavFile(fileName) && gSampleCount < kMaxSamples) {
        gSamplePaths[gSampleCount] = "/samples/" + fileName;
        gSampleNames[gSampleCount] = fileName;
        gSampleCount++;
      }
    }
    entry.close();
  }
  dir.close();

  sortSamplesByName();
  Serial.printf("Found %d sample(s)\n", gSampleCount);
}

void onPreviewSample(int sampleIndex, void * /*context*/) {
  if (sampleIndex < 0 || sampleIndex >= gSampleCount) return;
  Serial.printf("PLAY sample: %s\n", gSampleNames[sampleIndex].c_str());
  gAudio.playSamplePath(gSamplePaths[sampleIndex]);
}

const ActiveSampleRegistry::Entry *findActiveRegistryEntryForNote(int note) {
  for (int i = 0; i < gActiveRegistryReport.itemCount; i++) {
    if (gActiveRegistryReport.items[i].note == static_cast<uint8_t>(note)) {
      return &gActiveRegistryReport.items[i];
    }
  }
  return nullptr;
}

const SampleClassifier::AssignedSampleClassification *findClassificationByPath(const String &path) {
  for (int i = 0; i < gClassificationReport.itemCount; i++) {
    const SampleClassifier::AssignedSampleClassification &item = gClassificationReport.items[i];
    if (item.path == path) {
      return &item;
    }
  }
  return nullptr;
}

void onAssignedMidiNoteOn(int midiNote, void * /*context*/) {
  const ActiveSampleRegistry::Entry *entry = findActiveRegistryEntryForNote(midiNote);
  if (!entry) {
    Serial.printf("No active assignment for MIDI note %d\n", midiNote);
    return;
  }
  if (entry->effectiveMode == ActiveSampleRegistry::EffectiveStorageMode::Unavailable) {
    Serial.printf("Assignment unavailable for MIDI note %d (%s)\n",
                  midiNote,
                  entry->path.c_str());
    return;
  }

  if (entry->effectiveMode == ActiveSampleRegistry::EffectiveStorageMode::Ram) {
    SampleRamManager::LoadedSampleData loadedData;
    const SampleClassifier::AssignedSampleClassification *classified =
        findClassificationByPath(entry->path);
    if (classified && SampleRamManager::getLoadedSampleDataByPath(entry->path, loadedData)) {
      const bool played = gAudio.playSampleRam(loadedData.data,
                                               loadedData.dataBytes,
                                               classified->channelCount,
                                               classified->sampleRate,
                                               classified->bitsPerSample);
      if (played) {
        if (DebugFlags::kEnableDebugLogs) {
          Serial.printf("PLAY note=%d via RAM path=%s bytes=%lu\n",
                        midiNote,
                        entry->path.c_str(),
                        static_cast<unsigned long>(loadedData.dataBytes));
        }
        return;
      }
      if (DebugFlags::kEnableDebugLogs) {
        Serial.printf("RAM playback failed for note=%d, fallback to stream path=%s\n",
                      midiNote,
                      entry->path.c_str());
      }
    }
  }

  Serial.printf("PLAY note=%d via registry mode=%s path=%s\n",
                midiNote,
                ActiveSampleRegistry::effectiveStorageModeLabel(entry->effectiveMode),
                entry->path.c_str());
  gAudio.playSamplePath(entry->path);
}

int findSampleIndexByPath(const String &path) {
  for (int i = 0; i < gSampleCount; i++) {
    if (gSamplePaths[i] == path) return i;
  }
  return -1;
}

void applySettingsAssignmentsToUi() {
  int applied = 0;
  int missing = 0;
  for (int i = 0; i < gSettings.assignmentCount; i++) {
    const SettingsStore::MidiAssignment &assignment = gSettings.assignments[i];
    const int sampleIndex = findSampleIndexByPath(assignment.samplePath);
    if (sampleIndex < 0) {
      Serial.printf("Assignment path missing on SD for note %d: %s\n",
                    static_cast<int>(assignment.note),
                    assignment.samplePath.c_str());
      missing++;
      continue;
    }
    if (gUi.setMidiAssignment(assignment.note, sampleIndex)) {
      applied++;
    }
  }
  if (DebugFlags::kEnableDebugLogs) {
    Serial.printf("Assignments applied: %d, missing: %d\n", applied, missing);
  }
}

void collectSettingsAssignmentsFromUi() {
  gSettings.assignmentCount = 0;
  for (int note = 0; note < 128; note++) {
    if (gSettings.assignmentCount >= SettingsStore::SamplerSettings::kMaxAssignments) {
      Serial.println("Assignment export truncated to settings capacity");
      break;
    }

    const int sampleIndex = gUi.assignedSampleForMidiNote(note);
    if (sampleIndex < 0 || sampleIndex >= gSampleCount) continue;

    SettingsStore::MidiAssignment &entry = gSettings.assignments[gSettings.assignmentCount];
    entry.note = static_cast<uint8_t>(note);
    entry.samplePath = gSamplePaths[sampleIndex];
    gSettings.assignmentCount++;
  }
}

void classifyAssignedSamplesAndLog() {
  SampleClassifier::classifyAssignedSamples(gSettings, gClassificationReport);
  if (DebugFlags::kEnableDebugLogs) {
    Serial.printf(
        "Classification: assigned=%d ram=%d stream=%d missing=%d invalid=%d read_err=%d "
        "ram_used=%lu/%lu\n",
        gClassificationReport.itemCount,
        gClassificationReport.ramSampleCount,
        gClassificationReport.streamSampleCount,
        gClassificationReport.missingFileCount,
        gClassificationReport.invalidFormatCount,
        gClassificationReport.readErrorCount,
        static_cast<unsigned long>(gClassificationReport.sampleRamUsedBytes),
        static_cast<unsigned long>(gClassificationReport.sampleRamBudgetBytes));
  }

  if (DebugFlags::kEnableDebugLogs) {
    for (int i = 0; i < gClassificationReport.itemCount; i++) {
      const SampleClassifier::AssignedSampleClassification &item = gClassificationReport.items[i];
      Serial.printf("  note=%d mode=%s dur=%.3fs bytes=%lu path=%s\n",
                    static_cast<int>(item.note),
                    SampleClassifier::storageModeLabel(item.mode),
                    static_cast<double>(item.durationSeconds),
                    static_cast<unsigned long>(item.dataBytes),
                    item.path.c_str());
    }
  }
}

void loadClassifiedRamSamplesAndLog() {
  const bool ok = SampleRamManager::prepare(gSettings, gClassificationReport, gRamLoadReport);
  Serial.printf("RAM preload: %s requested=%d loaded=%d fallback=%d read_err=%d used=%lu/%lu budget=%lu",
                ok ? "OK" : "POOL_ALLOC_FAIL",
                gRamLoadReport.requestedRamCount,
                gRamLoadReport.loadedRamCount,
                gRamLoadReport.fallbackToStreamCount,
                gRamLoadReport.readErrorCount,
                static_cast<unsigned long>(gRamLoadReport.usedBytes),
                static_cast<unsigned long>(gRamLoadReport.allocatedBytes),
                static_cast<unsigned long>(gRamLoadReport.effectiveBudgetBytes));
  if (gRamLoadReport.fixedBudgetMismatch) {
    Serial.print(" (fixed-pool budget mismatch)");
  }
  Serial.println();
}

void buildActiveRegistryAndLog() {
  ActiveSampleRegistry::build(gClassificationReport, gActiveRegistryReport);
  if (DebugFlags::kEnableDebugLogs) {
    Serial.printf("Active registry: items=%d effective_ram=%d effective_stream=%d unavailable=%d "
                  "ram_fallback_to_stream=%d\n",
                  gActiveRegistryReport.itemCount,
                  gActiveRegistryReport.effectiveRamCount,
                  gActiveRegistryReport.effectiveStreamCount,
                  gActiveRegistryReport.unavailableCount,
                  gActiveRegistryReport.fallbackFromRamToStreamCount);
    for (int i = 0; i < gActiveRegistryReport.itemCount; i++) {
      const ActiveSampleRegistry::Entry &entry = gActiveRegistryReport.items[i];
      Serial.printf("  note=%d classified=%s effective=%s bytes=%lu path=%s\n",
                    static_cast<int>(entry.note),
                    SampleClassifier::storageModeLabel(entry.classifiedMode),
                    ActiveSampleRegistry::effectiveStorageModeLabel(entry.effectiveMode),
                    static_cast<unsigned long>(entry.dataBytes),
                    entry.path.c_str());
    }
  }
}

void refreshBootScreenMetrics(bool loading) {
  gBootScreenModel.loading = loading;
  gBootScreenModel.totalSamples = gSampleCount;
  gBootScreenModel.assignedSamples = gSettings.assignmentCount;

  const uint32_t budgetBytes = gSettings.sampleRamBudgetBytes;
  if (budgetBytes > 0) {
    const uint32_t pct = (100UL * gRamLoadReport.usedBytes) / budgetBytes;
    gBootScreenModel.ramUsagePercent = (pct > 100UL) ? 100 : static_cast<int>(pct);
  } else {
    gBootScreenModel.ramUsagePercent = 0;
  }

  gDisplay.renderBootScreen(gBootScreenModel);
}

bool onSaveConfiguration(void * /*context*/) {
  collectSettingsAssignmentsFromUi();
  classifyAssignedSamplesAndLog();
  loadClassifiedRamSamplesAndLog();
  buildActiveRegistryAndLog();
  const bool ok = SettingsStore::saveToSd(gSettings);
  Serial.printf("Settings save: %s, assignments=%d\n",
                ok ? "OK" : "FAILED",
                gSettings.assignmentCount);
  return ok;
}

void onBootScreenInputEvent(const Input::Event & /*event*/, void * /*context*/) {
  gBootScreenDismissRequested = true;
}

void onInputEvent(const Input::Event &event, void * /*context*/) {
  Ui::Event uiEvent;
  uiEvent.value = event.value;

  switch (event.type) {
    case Input::EventType::LeftRotate:
      uiEvent.type = Ui::EventType::LeftRotate;
      break;
    case Input::EventType::LeftClick:
      uiEvent.type = Ui::EventType::LeftClick;
      break;
    case Input::EventType::RightRotate:
      uiEvent.type = Ui::EventType::RightRotate;
      break;
    case Input::EventType::RightClick:
      uiEvent.type = Ui::EventType::RightClick;
      break;
    case Input::EventType::RightLongPress:
      uiEvent.type = Ui::EventType::RightLongPress;
      break;
  }

  gUi.handleEvent(uiEvent);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Booting Samplotron...");
  Serial.printf("PSRAM: size=%u free=%u\n",
                static_cast<unsigned int>(ESP.getPsramSize()),
                static_cast<unsigned int>(ESP.getFreePsram()));

  if (!CodecES8388::init()) {
    Serial.println("Codec init failed");
  } else {
    Serial.println("Codec OK");
  }

  if (!gDisplay.begin()) {
    Serial.println("Display init failed");
  } else {
    Serial.println("Display OK");
  }

  SettingsStore::applyDefaults(gSettings);
  gClassificationReport = SampleClassifier::ClassificationReport{};
  refreshBootScreenMetrics(true);

  const bool sdReady = StorageSD::init();
  if (sdReady) {
    loadSamplesFromSd();
    refreshBootScreenMetrics(true);
    const bool loaded = SettingsStore::loadFromSd(gSettings);
    Serial.printf("Settings load: %s, assignments=%d, ram_budget=%lu, preload_fixed=%.2f\n",
                  loaded ? "OK" : "DEFAULT",
                  gSettings.assignmentCount,
                  static_cast<unsigned long>(gSettings.sampleRamBudgetBytes),
                  static_cast<double>(SampleClassifier::kFixedPreloadThresholdSeconds));
    SettingsStore::logRawJsonFromSd();
    refreshBootScreenMetrics(true);
  } else {
    Serial.println("Continuing without SD (input/display debug still active).");
    gSampleCount = 0;
    SettingsStore::applyDefaults(gSettings);
    refreshBootScreenMetrics(true);
  }

  gInput.begin();
  gUi.begin(gSampleNames, gSamplePaths, gSampleCount);
  gMidi.begin(&gUi);
  applySettingsAssignmentsToUi();
  classifyAssignedSamplesAndLog();
  loadClassifiedRamSamplesAndLog();
  buildActiveRegistryAndLog();
  refreshBootScreenMetrics(false);

  const unsigned long bootScreenUntilMs = millis() + 5000UL;
  while (!gBootScreenDismissRequested && millis() < bootScreenUntilMs) {
    gInput.update(onBootScreenInputEvent, nullptr);
    delay(5);
  }

  gUi.setPreviewCallback(onPreviewSample, nullptr);
  gUi.setSaveCallback(onSaveConfiguration, nullptr);
  gMidi.setAssignedNoteOnCallback(onAssignedMidiNoteOn, nullptr);
  gDisplay.renderUi(gUi);

  gAudio.begin();
  Serial.println("Sampler ready");
}

void loop() {
  gAudio.update();
  gInput.update(onInputEvent, nullptr);
  gUi.update();
  gDisplay.update();
}
