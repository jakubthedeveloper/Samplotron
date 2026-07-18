#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "calculator_keypad.h"
#include "input.h"
#include "midi.h"
#include "sampler_loader_ipc.h"
#include "sampler_playback_router.h"
#include "sampler_save_service.h"
#include "ui.h"

class SamplerCallbackBinder {
 public:
  void begin(Ui *ui,
             Midi *midi,
             SamplerPlaybackRouter *playbackRouter,
             SamplerSaveService *saveService,
             QueueHandle_t loaderCommandQueue);
  void bindUiAndMidiCallbacks();
  void pollInput(Input &input);
  void pollCalculatorKeypad(CalculatorKeypad &keypad);

 private:
  static void onInputEvent(const Input::Event &event, void *context);
  static void onKeypadNoteOn(int midiNote, void *context);
  static void onPreviewSample(int sampleIndex, void *context);
  static void onAssignedMidiNoteOn(int midiNote, void *context);
  static bool onSaveConfiguration(void *context);
  static void onPlaybackModeChanged(Ui::PlaybackMode mode, int sampleIndex, void *context);

  Ui *ui_ = nullptr;
  Midi *midi_ = nullptr;
  SamplerPlaybackRouter *playbackRouter_ = nullptr;
  SamplerSaveService *saveService_ = nullptr;
  QueueHandle_t loaderCommandQueue_ = nullptr;
};
