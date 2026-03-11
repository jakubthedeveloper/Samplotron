#include "sampler_app.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "codec_es8388.h"
#include "storage_sd.h"

namespace {

constexpr UBaseType_t kAudioTaskPriority = 6;
constexpr UBaseType_t kUiTaskPriority = 2;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr BaseType_t kUiTaskCore = 0;
constexpr uint16_t kTriggerQueueLength = 32;
constexpr uint16_t kAudioTaskStackWords = 6144;
constexpr uint16_t kUiTaskStackWords = 8192;

}  // namespace

void SamplerApp::setup() {
  initializePlatform();
  initializeHardware();
  initializeRuntimeDefaults();
  loadStorageAndSettings();
  initializeInteractiveModules();
  prepareCallbacksAndBootFlow();
  if (!startTasks()) {
    return;
  }
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
  runtime_.rebuildPreparedSamples();
  renderBootScreen(false);
}

void SamplerApp::prepareCallbacksAndBootFlow() {
  playbackRouter_.begin(&ui_, &catalog_, &runtime_, &triggerEngine_);
  saveService_.begin(&ui_, &catalog_, &runtime_, &triggerEngine_);
  callbackBinder_.begin(&ui_, &midi_, &playbackRouter_, &saveService_);
  bootScreenFlow_.waitForDismissOrTimeout(5000UL);

  callbackBinder_.bindUiAndMidiCallbacks();
  display_.renderUi(ui_);
}

bool SamplerApp::startTasks() {
  if (!triggerEngine_.begin(
          &audio_, kAudioTaskPriority, kAudioTaskCore, kTriggerQueueLength, kAudioTaskStackWords)) {
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

void SamplerApp::runUiTask() {
  Serial.printf("ui_task started core=%d prio=%u\n",
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)));
  while (true) {
    midi_.update();
    callbackBinder_.pollInput(input_);
    ui_.update();
    display_.update();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
