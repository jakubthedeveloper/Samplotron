#include "sampler_app.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "codec_es8388.h"
#include "debug_flags.h"
#include "storage_sd.h"

namespace {

constexpr UBaseType_t kAudioTaskPriority = 6;
constexpr UBaseType_t kLoaderTaskPriority = 4;
constexpr UBaseType_t kUiTaskPriority = 2;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr BaseType_t kLoaderTaskCore = 0;
constexpr BaseType_t kUiTaskCore = 0;
constexpr uint16_t kTriggerQueueLength = 32;
constexpr uint16_t kLoaderCommandQueueLength = 12;
constexpr uint16_t kUiStatusQueueLength = 16;
constexpr uint16_t kAudioTaskStackWords = 6144;
constexpr uint16_t kLoaderTaskStackWords = 6144;
constexpr uint16_t kUiTaskStackWords = 8192;

}  // namespace

void SamplerApp::setup() {
  initializePlatform();
  initializeHardware();
  initializeRuntimeDefaults();
  loadStorageAndSettings();
  initializeInteractiveModules();
  if (!startTasks()) {
    return;
  }
  if (!requestLoaderRebuildAndWait(8000)) {
    Serial.println("Initial sample_loader rebuild failed");
    return;
  }
  renderBootScreen(false);
  prepareCallbacksAndBootFlow();
  Serial.println("Sampler ready");
}

void SamplerApp::initializePlatform() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Booting Samplotron...");
  Serial.printf("PSRAM: size=%u free=%u\n",
                static_cast<unsigned int>(ESP.getPsramSize()),
                static_cast<unsigned int>(ESP.getFreePsram()));
}

void SamplerApp::initializeHardware() {
  if (!CodecES8388::init()) {
    Serial.println("Codec init failed");
  } else {
    Serial.println("Codec OK");
  }

  if (!display_.begin()) {
    Serial.println("Display init failed");
  } else {
    Serial.println("Display OK");
  }
}

void SamplerApp::initializeRuntimeDefaults() {
  runtime_.applyDefaultSettings();
  bootScreenFlow_.begin(&display_, &input_);
  renderBootScreen(true);
}

void SamplerApp::loadStorageAndSettings() {
  const bool sdReady = StorageSD::init();
  if (sdReady) {
    SampleLibrary::loadFromSd(catalog_);
    renderBootScreen(true);
    runtime_.loadSettingsFromSd();
    renderBootScreen(true);
  } else {
    Serial.println("Continuing without SD (input/display debug still active).");
    SampleLibrary::clear(catalog_);
    runtime_.applyDefaultSettings();
    renderBootScreen(true);
  }
}

void SamplerApp::initializeInteractiveModules() {
  input_.begin();
  ui_.begin(catalog_.names, catalog_.paths, catalog_.count);
  midi_.begin(&ui_);
  runtime_.applyAssignmentsToUi(ui_, catalog_);
  ui_.clearUnsavedChanges();
}

void SamplerApp::prepareCallbacksAndBootFlow() {
  playbackRouter_.begin(&ui_, &catalog_, &runtime_, &triggerEngine_);
  saveService_.begin(
      &ui_, &catalog_, &runtime_, &triggerEngine_, loaderCommandQueue_, uiStatusQueue_);
  callbackBinder_.begin(&ui_, &midi_, &playbackRouter_, &saveService_);
  bootScreenFlow_.waitForDismissOrTimeout();

  callbackBinder_.bindUiAndMidiCallbacks();
  display_.renderUi(ui_);
}

bool SamplerApp::startTasks() {
  loaderCommandQueue_ = xQueueCreate(kLoaderCommandQueueLength, sizeof(LoaderCommand));
  if (!loaderCommandQueue_) {
    Serial.println("sample_loader command queue creation failed");
    return false;
  }

  uiStatusQueue_ = xQueueCreate(kUiStatusQueueLength, sizeof(UiStatusEvent));
  if (!uiStatusQueue_) {
    Serial.println("UI status queue creation failed");
    return false;
  }

  if (!triggerEngine_.begin(
          &audio_,
          kAudioTaskPriority,
          kAudioTaskCore,
          kTriggerQueueLength,
          kAudioTaskStackWords,
          uiStatusQueue_)) {
    return false;
  }

  const BaseType_t loaderTaskOk = xTaskCreatePinnedToCore(loaderTaskEntry,
                                                           "sample_loader",
                                                           kLoaderTaskStackWords,
                                                           this,
                                                           kLoaderTaskPriority,
                                                           &loaderTaskHandle_,
                                                           kLoaderTaskCore);
  if (loaderTaskOk != pdPASS) {
    Serial.println("sample_loader task creation failed");
    return false;
  }

  const BaseType_t uiTaskOk = xTaskCreatePinnedToCore(uiTaskEntry,
                                                       "ui_task",
                                                       kUiTaskStackWords,
                                                       this,
                                                       kUiTaskPriority,
                                                       &uiTaskHandle_,
                                                       kUiTaskCore);
  if (uiTaskOk != pdPASS) {
    Serial.println("UI task creation failed");
    return false;
  }
  return true;
}

void SamplerApp::renderBootScreen(bool loading) {
  bootScreenFlow_.render(loading,
                         catalog_.count,
                         runtime_.assignedSamplesCount(),
                         runtime_.ramUsagePercent());
}

void SamplerApp::loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void SamplerApp::uiTaskEntry(void *param) {
  auto *self = static_cast<SamplerApp *>(param);
  self->runUiTask();
}

void SamplerApp::loaderTaskEntry(void *param) {
  auto *self = static_cast<SamplerApp *>(param);
  self->runLoaderTask();
}

void SamplerApp::runLoaderTask() {
  Serial.printf("sample_loader started core=%d prio=%u\n",
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)));
  if (!loaderCommandQueue_ || !uiStatusQueue_) {
    vTaskDelete(nullptr);
    return;
  }

  LoaderCommand command;
  while (true) {
    if (xQueueReceive(loaderCommandQueue_, &command, portMAX_DELAY) == pdTRUE) {
      processLoaderCommand(command);
    }
  }
}

bool SamplerApp::requestLoaderRebuildAndWait(uint32_t timeoutMs) {
  if (!loaderCommandQueue_ || !uiStatusQueue_) return false;

  LoaderCommand command;
  command.type = LoaderCommandType::RebuildPreparedSamples;
  if (xQueueSend(loaderCommandQueue_, &command, pdMS_TO_TICKS(200)) != pdTRUE) {
    return false;
  }

  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
  while (xTaskGetTickCount() < deadline) {
    UiStatusEvent event;
    if (xQueueReceive(uiStatusQueue_, &event, pdMS_TO_TICKS(20)) != pdTRUE) {
      continue;
    }
    if (event.source == UiStatusSource::SampleLoader &&
        event.type == UiStatusType::LoaderRebuildCompleted) {
      return event.success;
    }
  }
  return false;
}

void SamplerApp::processLoaderCommand(const LoaderCommand &command) {
  UiStatusEvent status;
  status.source = UiStatusSource::SampleLoader;
  status.type = UiStatusType::LoaderRebuildCompleted;
  status.success = false;

  if (command.type == LoaderCommandType::RebuildPreparedSamples) {
    runtime_.rebuildPreparedSamples();
    status.success = true;
    status.assignedSamples = static_cast<uint32_t>(runtime_.assignedSamplesCount());
    status.ramSampleCount = static_cast<uint32_t>(runtime_.ramSampleCount());
    status.streamSampleCount = static_cast<uint32_t>(runtime_.streamSampleCount());
    status.sampleRamUsedBytes = runtime_.sampleRamUsedBytes();

    Serial.printf("sample_loader rebuild done assigned=%lu ram=%lu stream=%lu ram_used=%lu\n",
                  static_cast<unsigned long>(status.assignedSamples),
                  static_cast<unsigned long>(status.ramSampleCount),
                  static_cast<unsigned long>(status.streamSampleCount),
                  static_cast<unsigned long>(status.sampleRamUsedBytes));
  }

  if (xQueueSend(uiStatusQueue_, &status, pdMS_TO_TICKS(20)) != pdTRUE) {
    Serial.println("ui_status_queue full (loader status dropped)");
  }
}

void SamplerApp::runUiTask() {
  Serial.printf("ui_task started core=%d prio=%u\n",
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)));
  uint32_t lastRamDiagMs = 0;
  while (true) {
    if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnableRuntimeRamUsageLogs) {
      const uint32_t nowMs = millis();
      if ((nowMs - lastRamDiagMs) >= DebugFlags::kRuntimeRamUsageLogIntervalMs) {
        lastRamDiagMs = nowMs;
        Serial.printf("RAM_DIAG heap_free=%u heap_min=%u heap_size=%u psram_free=%u psram_size=%u\n",
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getMinFreeHeap()),
                      static_cast<unsigned>(ESP.getHeapSize()),
                      static_cast<unsigned>(ESP.getFreePsram()),
                      static_cast<unsigned>(ESP.getPsramSize()));
      }
    }
    midi_.update();
    callbackBinder_.pollInput(input_);
    ui_.update();
    display_.update();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
