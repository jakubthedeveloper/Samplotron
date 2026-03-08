#include <Arduino.h>
#include <SD.h>

#include "codec_es8388.h"
#include "display_ssd1309.h"
#include "input.h"
#include "pins.h"
#include "audio.h"
#include "ui.h"
#include "storage_sd.h"
#include "settings_store.h"
#include "sample_classifier.h"

namespace {

Audio gAudio;
Input gInput;
Ui gUi;
DisplaySsd1309 gDisplay;

constexpr int kMaxSamples = Ui::kMaxSamples;
String gSamplePaths[kMaxSamples];
String gSampleNames[kMaxSamples];
int gSampleCount = 0;
SettingsStore::SamplerSettings gSettings;
SampleClassifier::ClassificationReport gClassificationReport;

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
  Serial.printf("Assignments applied: %d, missing: %d\n", applied, missing);
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

bool onSaveConfiguration(void * /*context*/) {
  collectSettingsAssignmentsFromUi();
  classifyAssignedSamplesAndLog();
  const bool ok = SettingsStore::saveToSd(gSettings);
  Serial.printf("Settings save: %s, assignments=%d\n",
                ok ? "OK" : "FAILED",
                gSettings.assignmentCount);
  return ok;
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

  const bool sdReady = StorageSD::init();
  if (sdReady) {
    loadSamplesFromSd();
    const bool loaded = SettingsStore::loadFromSd(gSettings);
    Serial.printf("Settings load: %s, assignments=%d, ram_budget=%lu, preload=%.2f\n",
                  loaded ? "OK" : "DEFAULT",
                  gSettings.assignmentCount,
                  static_cast<unsigned long>(gSettings.sampleRamBudgetBytes),
                  static_cast<double>(gSettings.preloadThresholdSeconds));
  } else {
    Serial.println("Continuing without SD (input/display debug still active).");
    gSampleCount = 0;
    SettingsStore::applyDefaults(gSettings);
  }

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

  gUi.begin(gSampleNames, gSamplePaths, gSampleCount);
  applySettingsAssignmentsToUi();
  classifyAssignedSamplesAndLog();
  gUi.setPreviewCallback(onPreviewSample, nullptr);
  gUi.setSaveCallback(onSaveConfiguration, nullptr);
  gDisplay.renderUi(gUi);

  gInput.begin();
  gAudio.begin();
  Serial.println("Sampler ready");
}

void loop() {
  gAudio.update();
  gInput.update(onInputEvent, nullptr);
  gUi.update();
  gDisplay.update();
}
