#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

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

enum class TriggerSourceType : uint8_t {
  StreamPath,
  RamData,
};

struct TriggerEvent {
  TriggerSourceType source = TriggerSourceType::StreamPath;
  uint8_t volume = 100;
  char path[128] = {0};

  const uint8_t *ramData = nullptr;
  uint32_t ramDataBytes = 0;
  uint16_t channelCount = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
};

QueueHandle_t gTriggerQueue = nullptr;
TaskHandle_t gAudioTaskHandle = nullptr;
TaskHandle_t gUiTaskHandle = nullptr;
volatile uint8_t gAudioActiveVoices = 0;

constexpr uint32_t kTriggerQueueLength = 32;
constexpr UBaseType_t kAudioTaskPriority = 6;
constexpr UBaseType_t kUiTaskPriority = 2;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr BaseType_t kUiTaskCore = 0;

void onInputEvent(const Input::Event &event, void *context);

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

bool enqueueTriggerEvent(const TriggerEvent &event) {
  if (!gTriggerQueue) return false;
  return xQueueSend(gTriggerQueue, &event, 0) == pdTRUE;
}

bool waitForAudioIdle(uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    const bool noVoices = (gAudioActiveVoices == 0);
    const bool noPendingTriggers = (!gTriggerQueue || uxQueueMessagesWaiting(gTriggerQueue) == 0);
    if (noVoices && noPendingTriggers) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

void processTriggerEvent(const TriggerEvent &event) {
  if (event.source == TriggerSourceType::RamData) {
    if (event.ramData && event.ramDataBytes > 0) {
      gAudio.playSampleRam(event.ramData,
                           event.ramDataBytes,
                           event.channelCount,
                           event.sampleRate,
                           event.bitsPerSample,
                           event.volume);
    }
    return;
  }

  if (event.path[0] != '\0') {
    gAudio.playSamplePath(String(event.path), event.volume);
  }
}

void audioTaskEntry(void * /*param*/) {
  gAudio.begin();
  Serial.printf("audio_task started core=%d prio=%u\n",
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)));

  TriggerEvent event;
  while (true) {
    gAudio.update();
    const Audio::RuntimeStats stats = gAudio.runtimeStats();
    gAudioActiveVoices = stats.activeVoices;

    // Never drain the entire queue in one go; keep audio update cadence stable.
    if (xQueueReceive(gTriggerQueue, &event, 0) == pdTRUE) {
      // Keep output fed around trigger processing to minimize short underruns
      // when a new voice is started while others are already active.
      if (stats.activeVoices > 0) {
        gAudio.update();
      }
      processTriggerEvent(event);
      gAudio.update();
      gAudioActiveVoices = gAudio.runtimeStats().activeVoices;
      continue;
    }

    if (stats.activeVoices == 0) {
      // When idle, block briefly waiting for new triggers instead of spinning.
      if (xQueueReceive(gTriggerQueue, &event, pdMS_TO_TICKS(1)) == pdTRUE) {
        processTriggerEvent(event);
      }
    }
  }
}

void uiTaskEntry(void * /*param*/) {
  Serial.printf("ui_task started core=%d prio=%u\n",
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)));
  while (true) {
    gMidi.update();
    gInput.update(onInputEvent, nullptr);
    gUi.update();
    gDisplay.update();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void onPreviewSample(int sampleIndex, void * /*context*/) {
  if (sampleIndex < 0 || sampleIndex >= gSampleCount) return;
  if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
    Serial.printf("PLAY sample: %s\n", gSampleNames[sampleIndex].c_str());
  }
  TriggerEvent event;
  event.source = TriggerSourceType::StreamPath;
  event.volume = static_cast<uint8_t>(gUi.sampleVolumeForSample(sampleIndex));
  const String &path = gSamplePaths[sampleIndex];
  path.toCharArray(event.path, sizeof(event.path));
  if (!enqueueTriggerEvent(event) && DebugFlags::kEnableDebugLogs) {
    Serial.println("Trigger queue full (preview dropped)");
  }
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
  const int sampleIndex = gUi.assignedSampleForMidiNote(midiNote);
  if (sampleIndex < 0 || sampleIndex >= gSampleCount) {
    gUi.clearTriggeredSample();
    if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
      Serial.printf("No assignment for MIDI note %d\n", midiNote);
    }
    return;
  }
  gUi.reportTriggeredSample(sampleIndex);
  const String &assignedPath = gSamplePaths[sampleIndex];
  const uint8_t assignedVolume = static_cast<uint8_t>(gUi.sampleVolumeForSample(sampleIndex));

  const ActiveSampleRegistry::Entry *entry = findActiveRegistryEntryForNote(midiNote);
  const bool hasPreparedEntry = (entry && entry->path == assignedPath);

  if (hasPreparedEntry &&
      entry->effectiveMode == ActiveSampleRegistry::EffectiveStorageMode::Unavailable) {
    if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
      Serial.printf("Playback blocked for note=%d: unsupported or missing sample path=%s\n",
                    midiNote,
                    assignedPath.c_str());
    }
    return;
  }

  if (hasPreparedEntry && entry->effectiveMode == ActiveSampleRegistry::EffectiveStorageMode::Ram) {
    SampleRamManager::LoadedSampleData loadedData;
    const SampleClassifier::AssignedSampleClassification *classified =
      findClassificationByPath(entry->path);
    if (classified && SampleRamManager::getLoadedSampleDataByPath(entry->path, loadedData)) {
      TriggerEvent event;
      event.source = TriggerSourceType::RamData;
      event.volume = assignedVolume;
      event.ramData = loadedData.data;
      event.ramDataBytes = loadedData.dataBytes;
      event.channelCount = classified->channelCount;
      event.sampleRate = classified->sampleRate;
      event.bitsPerSample = classified->bitsPerSample;
      const bool played = enqueueTriggerEvent(event);
      if (played) {
        if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
          Serial.printf("PLAY note=%d via RAM path=%s bytes=%lu\n",
                        midiNote,
                        entry->path.c_str(),
                        static_cast<unsigned long>(loadedData.dataBytes));
        }
        return;
      }
      if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
        Serial.printf("RAM playback failed for note=%d, fallback to stream path=%s\n",
                      midiNote,
                      assignedPath.c_str());
      }
    }
  }

  if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
    if (hasPreparedEntry) {
      Serial.printf("PLAY note=%d via registry mode=%s path=%s\n",
                    midiNote,
                    ActiveSampleRegistry::effectiveStorageModeLabel(entry->effectiveMode),
                    assignedPath.c_str());
    } else {
      Serial.printf("PLAY note=%d via UI assignment (unprepared), stream path=%s\n",
                    midiNote,
                    assignedPath.c_str());
    }
  }
  TriggerEvent event;
  event.source = TriggerSourceType::StreamPath;
  event.volume = assignedVolume;
  assignedPath.toCharArray(event.path, sizeof(event.path));
  if (!enqueueTriggerEvent(event) && DebugFlags::kEnableDebugLogs) {
    Serial.println("Trigger queue full (assigned trigger dropped)");
  }
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
    gUi.setSampleVolume(sampleIndex, assignment.volume);
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
    entry.volume = static_cast<uint8_t>(gUi.sampleVolumeForSample(sampleIndex));
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
  if (!waitForAudioIdle(3000)) {
    Serial.println("Settings save deferred: audio still active");
    return false;
  }
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
  gUi.clearUnsavedChanges();
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

  gTriggerQueue = xQueueCreate(kTriggerQueueLength, sizeof(TriggerEvent));
  if (!gTriggerQueue) {
    Serial.println("Failed to create trigger queue");
    return;
  }

  const BaseType_t audioTaskOk = xTaskCreatePinnedToCore(audioTaskEntry,
                                                          "audio_task",
                                                          6144,
                                                          nullptr,
                                                          kAudioTaskPriority,
                                                          &gAudioTaskHandle,
                                                          kAudioTaskCore);
  const BaseType_t uiTaskOk = xTaskCreatePinnedToCore(uiTaskEntry,
                                                       "ui_task",
                                                       8192,
                                                       nullptr,
                                                       kUiTaskPriority,
                                                       &gUiTaskHandle,
                                                       kUiTaskCore);
  if (audioTaskOk != pdPASS || uiTaskOk != pdPASS) {
    Serial.println("Task creation failed");
    return;
  }

  Serial.println("Sampler ready");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
