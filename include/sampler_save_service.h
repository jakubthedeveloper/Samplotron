#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "sample_library.h"
#include "sampler_loader_ipc.h"
#include "sampler_runtime.h"
#include "trigger_engine.h"
#include "ui.h"

class SamplerSaveService {
 public:
  void begin(Ui *ui,
             const SampleLibrary::Catalog *catalog,
             SamplerRuntime *runtime,
             TriggerEngine *triggerEngine,
             QueueHandle_t loaderCommandQueue,
             QueueHandle_t uiStatusQueue);
  bool saveConfiguration() const;

 private:
  bool requestLoaderRebuildAndWait(uint32_t timeoutMs) const;

  Ui *ui_ = nullptr;
  const SampleLibrary::Catalog *catalog_ = nullptr;
  SamplerRuntime *runtime_ = nullptr;
  TriggerEngine *triggerEngine_ = nullptr;
  QueueHandle_t loaderCommandQueue_ = nullptr;
  QueueHandle_t uiStatusQueue_ = nullptr;
};
