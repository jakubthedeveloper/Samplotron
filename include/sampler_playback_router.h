#pragma once

#include "sample_library.h"
#include "sampler_runtime.h"
#include "trigger_engine.h"
#include "ui.h"

class SamplerPlaybackRouter {
 public:
  void begin(Ui *ui,
             const SampleLibrary::Catalog *catalog,
             const SamplerRuntime *runtime,
             TriggerEngine *triggerEngine);

  void onPreviewSample(int sampleIndex) const;
  void onAssignedMidiNoteOn(int midiNote) const;
  void onPlaybackModeChanged(Ui::PlaybackMode mode, int sampleIndex) const;

 private:
  Ui *ui_ = nullptr;
  const SampleLibrary::Catalog *catalog_ = nullptr;
  const SamplerRuntime *runtime_ = nullptr;
  TriggerEngine *triggerEngine_ = nullptr;
};
