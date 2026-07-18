#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "audio.h"
#include "boot_screen_flow.h"
#include "calculator_keypad.h"
#include "display_ssd1309.h"
#include "input.h"
#include "midi.h"
#include "sampler_callback_binder.h"
#include "sampler_loader_ipc.h"
#include "sample_library.h"
#include "sampler_playback_router.h"
#include "sampler_runtime.h"
#include "sampler_save_service.h"
#include "trigger_engine.h"
#include "ui.h"

class SamplerApp {
 public:
  void setup();
  void loop();

 private:
  static void uiTaskEntry(void *param);
  static void loaderTaskEntry(void *param);

  void initializePlatform();
  void initializeHardware();
  void initializeRuntimeDefaults();
  void loadStorageAndSettings();
  void initializeInteractiveModules();
  void prepareCallbacksAndBootFlow();
  bool startTasks();
  void renderBootScreen(bool loading);
  bool requestLoaderRebuildAndWait(uint32_t timeoutMs);
  void processLoaderCommand(const LoaderCommand &command);
  void runLoaderTask();
  void runUiTask();

  Audio audio_;
  Input input_;
  CalculatorKeypad calculatorKeypad_;
  Midi midi_;
  Ui ui_;
  DisplaySsd1309 display_;
  SampleLibrary::Catalog catalog_;
  SamplerRuntime runtime_;
  TriggerEngine triggerEngine_;
  SamplerPlaybackRouter playbackRouter_;
  SamplerSaveService saveService_;
  SamplerCallbackBinder callbackBinder_;
  BootScreenFlow bootScreenFlow_;
  QueueHandle_t loaderCommandQueue_ = nullptr;
  QueueHandle_t uiStatusQueue_ = nullptr;
  TaskHandle_t loaderTaskHandle_ = nullptr;
  TaskHandle_t uiTaskHandle_ = nullptr;
};
