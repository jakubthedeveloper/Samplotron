#include "sampler_save_service.h"

void SamplerSaveService::begin(Ui *ui,
                               const SampleLibrary::Catalog *catalog,
                               SamplerRuntime *runtime,
                               TriggerEngine *triggerEngine,
                               QueueHandle_t loaderCommandQueue,
                               QueueHandle_t uiStatusQueue) {
  ui_ = ui;
  catalog_ = catalog;
  runtime_ = runtime;
  triggerEngine_ = triggerEngine;
  loaderCommandQueue_ = loaderCommandQueue;
  uiStatusQueue_ = uiStatusQueue;
}

bool SamplerSaveService::saveConfiguration() const {
  if (!ui_ || !catalog_ || !runtime_ || !triggerEngine_ || !loaderCommandQueue_ || !uiStatusQueue_) {
    return false;
  }

  if (!triggerEngine_->waitForIdle(3000)) {
    // Save should be reliable even if a loop is currently active.
    triggerEngine_->panicAll();
    if (!triggerEngine_->waitForIdle(1500)) {
      Serial.println("Settings save deferred: audio still active");
      return false;
    }
  }

  runtime_->collectAssignmentsFromUi(*ui_, *catalog_);
  if (!requestLoaderRebuildAndWait(5000)) {
    Serial.println("Settings save failed: sample_loader rebuild timeout/error");
    return false;
  }

  const bool ok = runtime_->saveSettingsToSd();
  Serial.printf("Settings save: %s, assignments=%d\n",
                ok ? "OK" : "FAILED",
                runtime_->assignedSamplesCount());
  return ok;
}

bool SamplerSaveService::requestLoaderRebuildAndWait(uint32_t timeoutMs) const {
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
