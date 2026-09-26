#include "sampler_app.h"

#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "codec_es8388.h"
#include "storage_sd.h"

namespace {

constexpr UBaseType_t kAudioTaskPriority = 6;
constexpr UBaseType_t kLoaderTaskPriority = 4;
constexpr UBaseType_t kUiTaskPriority = 2;
// Above the UI so streams stay fed; the reader yields every 10 ms of SD work.
constexpr UBaseType_t kSdReaderTaskPriority = 3;
constexpr BaseType_t kSdReaderTaskCore = 0;
constexpr uint32_t kDiagnosticsIntervalMs = 1000;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr BaseType_t kLoaderTaskCore = 0;
constexpr BaseType_t kUiTaskCore = 0;
constexpr uint16_t kTriggerQueueLength = 32;
constexpr uint16_t kLoaderCommandQueueLength = 12;
constexpr uint16_t kUiStatusQueueLength = 16;
constexpr uint16_t kAudioTaskStackWords = 6144;
constexpr uint16_t kLoaderTaskStackWords = 6144;
constexpr uint16_t kUiTaskStackWords = 8192;
// Preloading several MiB of samples at the 4 MHz SD fallback takes seconds.
constexpr uint32_t kBootRebuildTimeoutMs = 60000;

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
  if (!requestLoaderRebuildAndWait(kBootRebuildTimeoutMs)) {
    return;
  }
  if (!CodecES8388::unmute()) {
    Serial.println("Audio: codec unmute failed");
    return;
  }
  renderBootScreen(false);
  prepareCallbacksAndBootFlow();
}

void SamplerApp::initializePlatform() {
  Serial.begin(115200);
  delay(200);
}

void SamplerApp::initializeHardware() {
  CodecES8388::init();

  if (display_.begin()) {
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
    SampleLibrary::loadFromSd(catalog_, [](void *context) {
      static_cast<SamplerApp *>(context)->renderBootScreen(true);
    }, this);
    renderBootScreen(true);
    runtime_.loadSettingsFromSd();
  } else {
    SampleLibrary::clear(catalog_);
    runtime_.applyDefaultSettings();
  }
  renderBootScreen(true);
}

void SamplerApp::initializeInteractiveModules() {
  input_.begin();
  runtime_.setCatalog(&catalog_);
  audio_.setSampleCatalog(&catalog_);
  ui_.begin(catalog_.names, catalog_.paths, catalog_.count, catalog_.validation);
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

  // Start I2S while the codec is still soft-muted. The audio task calls
  // begin() too, but Audio::begin() is idempotent.
  if (!audio_.begin()) {
    Serial.println("Audio: initialization failed; outputs remain muted");
    return false;
  }

  // SD reads for streamed samples run on core 0, so the audio task on core 1
  // never waits for the card. Without the task, playback still works but
  // reads block the audio task.
  if (!audio_.startStreamReader(kSdReaderTaskPriority, kSdReaderTaskCore)) {
    Serial.println("Audio: SD reader task failed; streaming from the audio task");
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
                         runtime_.ramUsagePercent(), catalog_.checkedCount, catalog_.rejectedCount);
}

void SamplerApp::loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void SamplerApp::logStreamingDiagnostics() {
  // Runs once per second from the UI task on core 0: the audio task keeps
  // core 1 busy while voices play. Report only seconds with new problems, so
  // a quiet log means playback kept up.
  const Audio::StreamingDiagnostics now = audio_.streamingDiagnostics();
  const Audio::StreamingDiagnostics &last = loggedDiagnostics_;
  const uint32_t sdBytesPerSecond = now.sdBytesRead - lastSdBytesRead_;
  lastSdBytesRead_ = now.sdBytesRead;
  if (now.i2sUnderrunCount == last.i2sUnderrunCount &&
      now.starvedUpdateCount == last.starvedUpdateCount &&
      now.sdNoFreeStreamCount == last.sdNoFreeStreamCount &&
      now.sdOpenFailureCount == last.sdOpenFailureCount) {
    return;
  }
  Serial.printf("Audio: +%lu I2S underruns, +%lu starved SD voice updates, +%lu SD triggers "
                "without a free stream, +%lu SD open failures; SD %lu KiB/s at %lu Hz, "
                "max read %lu us for %lu B\n",
                static_cast<unsigned long>(now.i2sUnderrunCount - last.i2sUnderrunCount),
                static_cast<unsigned long>(now.starvedUpdateCount - last.starvedUpdateCount),
                static_cast<unsigned long>(now.sdNoFreeStreamCount - last.sdNoFreeStreamCount),
                static_cast<unsigned long>(now.sdOpenFailureCount - last.sdOpenFailureCount),
                static_cast<unsigned long>(sdBytesPerSecond / 1024U),
                static_cast<unsigned long>(StorageSD::spiFrequencyHz()),
                static_cast<unsigned long>(now.sdMaxReadUs),
                static_cast<unsigned long>(now.sdMaxReadBytes));
  loggedDiagnostics_ = now;
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
  (void)xQueueSend(uiStatusQueue_, &status, pdMS_TO_TICKS(20));
}

void SamplerApp::runUiTask() {
  uint32_t lastDiagnosticsMs = millis();
  while (true) {
    if (millis() - lastDiagnosticsMs >= kDiagnosticsIntervalMs) {
      lastDiagnosticsMs = millis();
      logStreamingDiagnostics();
    }
    midi_.update();
    callbackBinder_.pollInput(input_);
    ui_.update();
    display_.update();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
