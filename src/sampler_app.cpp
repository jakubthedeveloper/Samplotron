#include "sampler_app.h"

#include <Arduino.h>
#include <esp_system.h>
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

const char *startupTitleForResetReason() {
  const esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_SW || reason == ESP_RST_PANIC || reason == ESP_RST_TASK_WDT) {
    return "Updating firmware...";
  }
  return "Starting...";
}

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
    
    return;
  }
  renderBootScreen(false);
  prepareCallbacksAndBootFlow();
  
}

void SamplerApp::initializePlatform() {
  
  delay(200);
  
  
}

void SamplerApp::initializeHardware() {
  if (!CodecES8388::init()) {
    
  } else {
    
  }

  if (!display_.begin()) {
    
  } else {
    
    display_.renderStartupMessage(startupTitleForResetReason(), "Please wait...");
  }
}

void SamplerApp::initializeRuntimeDefaults() {
  runtime_.applyDefaultSettings();
  display_.setAudio(&audio_);
  bootScreenFlow_.begin(&display_, &input_, &ui_);
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
  callbackBinder_.begin(&ui_, &midi_, &playbackRouter_, &saveService_, loaderCommandQueue_);
  bootScreenFlow_.waitForDismissOrTimeout();
  ui_.forceMainScreen();

  callbackBinder_.bindUiAndMidiCallbacks();
  display_.renderUi(ui_);
}

bool SamplerApp::startTasks() {
  loaderCommandQueue_ = xQueueCreate(kLoaderCommandQueueLength, sizeof(LoaderCommand));
  if (!loaderCommandQueue_) {
    
    return false;
  }

  uiStatusQueue_ = xQueueCreate(kUiStatusQueueLength, sizeof(UiStatusEvent));
  if (!uiStatusQueue_) {
    
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
  if (command.type == LoaderCommandType::PreviewSample) {
    if (command.sampleIndex >= 0) {
      playbackRouter_.onPreviewSample(static_cast<int>(command.sampleIndex));
    }
    return;
  }

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

    
  }
  if (xQueueSend(uiStatusQueue_, &status, pdMS_TO_TICKS(20)) != pdTRUE) {
    
  }
}

void SamplerApp::runUiTask() {
  
  uint32_t lastRamDiagMs = 0;
  while (true) {
    if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnableRuntimeRamUsageLogs) {
      const uint32_t nowMs = millis();
      if ((nowMs - lastRamDiagMs) >= DebugFlags::kRuntimeRamUsageLogIntervalMs) {
        lastRamDiagMs = nowMs;
        
      }
    }
    midi_.update();
    callbackBinder_.pollInput(input_);
    ui_.update();
    display_.update();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
