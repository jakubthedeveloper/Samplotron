#include "sampler_save_service.h"
#include "save_diagnostics.h"

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
  SaveDiagnostics::setStage(SaveDiagnostics::Stage::Initialization);
  if (!ui_ || !catalog_ || !runtime_ || !triggerEngine_ || !loaderCommandQueue_ || !uiStatusQueue_) {
    Serial.println("Save: service not initialized");
    return false;
  }

  SaveDiagnostics::setStage(SaveDiagnostics::Stage::Playback);
  if (!triggerEngine_->waitForIdle(3000)) {
    // Save should be reliable even if a loop is currently active.
    if (!triggerEngine_->panicAll()) {
      Serial.println("Save: could not enqueue playback stop");
      return false;
    }
    if (!triggerEngine_->waitForIdle(1500)) {
      Serial.println("Save: playback did not stop");
      return false;
    }
  }

  runtime_->collectAssignmentsFromUi(*ui_, *catalog_);
  if (!requestLoaderRebuildAndWait()) {
    return false;
  }

  const bool ok = runtime_->saveSettingsToSd();
  Serial.println(ok ? "Save: configuration saved" : "Save: SD write failed");
  return ok;
}

bool SamplerSaveService::requestLoaderRebuildAndWait() const {
  SaveDiagnostics::setStage(SaveDiagnostics::Stage::LoaderQueue);
  LoaderCommand command;
  command.type = LoaderCommandType::RebuildPreparedSamples;
  if (xQueueSend(loaderCommandQueue_, &command, pdMS_TO_TICKS(200)) != pdTRUE) {
    Serial.println("Save: loader queue full");
    return false;
  }

  // Rebuilding reads samples from SD and has no fixed duration. It cannot be
  // cancelled: returning early would resume playback while its RAM is changing
  // and leave a stale completion event for the next save.
  Serial.println("Save: preparing samples");
  SaveDiagnostics::setStage(SaveDiagnostics::Stage::Loader);
  while (true) {
    UiStatusEvent event;
    if (xQueueReceive(uiStatusQueue_, &event, pdMS_TO_TICKS(20)) != pdTRUE) {
      continue;
    }
    if (event.source == UiStatusSource::SampleLoader &&
        event.type == UiStatusType::LoaderRebuildCompleted) {
      if (!event.success) Serial.println("Save: sample preparation failed");
      return event.success;
    }
  }
}
