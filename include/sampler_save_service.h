#pragma once

#include "sample_library.h"
#include "sampler_runtime.h"
#include "trigger_engine.h"
#include "ui.h"

class SamplerSaveService {
 public:
  void begin(Ui *ui,
             const SampleLibrary::Catalog *catalog,
             SamplerRuntime *runtime,
             TriggerEngine *triggerEngine);
  bool saveConfiguration() const;

 private:
  Ui *ui_ = nullptr;
  const SampleLibrary::Catalog *catalog_ = nullptr;
  SamplerRuntime *runtime_ = nullptr;
  TriggerEngine *triggerEngine_ = nullptr;
};
