#include "sampler_save_service.h"

void SamplerSaveService::begin(Ui *ui,
                               const SampleLibrary::Catalog *catalog,
                               SamplerRuntime *runtime,
                               TriggerEngine *triggerEngine) {
  ui_ = ui;
  catalog_ = catalog;
  runtime_ = runtime;
  triggerEngine_ = triggerEngine;
}

bool SamplerSaveService::saveConfiguration() const {
  if (!ui_ || !catalog_ || !runtime_ || !triggerEngine_) {
    return false;
  }

  if (!triggerEngine_->waitForIdle(3000)) {
    Serial.println("Settings save deferred: audio still active");
    return false;
  }

  runtime_->collectAssignmentsFromUi(*ui_, *catalog_);
  runtime_->rebuildPreparedSamples();

  const bool ok = runtime_->saveSettingsToSd();
  Serial.printf("Settings save: %s, assignments=%d\n",
                ok ? "OK" : "FAILED",
                runtime_->assignedSamplesCount());
  return ok;
}
