#pragma once

#include "input.h"
#include "midi.h"
#include "sampler_playback_router.h"
#include "sampler_save_service.h"
#include "ui.h"

class SamplerCallbackBinder {
 public:
  void begin(Ui *ui,
             Midi *midi,
             SamplerPlaybackRouter *playbackRouter,
             SamplerSaveService *saveService);
  void bindUiAndMidiCallbacks();
  void pollInput(Input &input);

 private:
  static void onInputEvent(const Input::Event &event, void *context);
  static void onPreviewSample(int sampleIndex, void *context);
  static void onAssignedMidiNoteOn(int midiNote, void *context);
  static bool onSaveConfiguration(void *context);
  static void onPlaybackModeChanged(Ui::PlaybackMode mode, int sampleIndex, void *context);

  Ui *ui_ = nullptr;
  Midi *midi_ = nullptr;
  SamplerPlaybackRouter *playbackRouter_ = nullptr;
  SamplerSaveService *saveService_ = nullptr;
};
