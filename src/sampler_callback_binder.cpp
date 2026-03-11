#include "sampler_callback_binder.h"

#include "input_ui_bridge.h"

void SamplerCallbackBinder::begin(Ui *ui,
                                  Midi *midi,
                                  SamplerPlaybackRouter *playbackRouter,
                                  SamplerSaveService *saveService) {
  ui_ = ui;
  midi_ = midi;
  playbackRouter_ = playbackRouter;
  saveService_ = saveService;
}

void SamplerCallbackBinder::bindUiAndMidiCallbacks() const {
  if (!ui_ || !midi_) {
    return;
  }

  ui_->setPreviewCallback(onPreviewSample, const_cast<SamplerCallbackBinder *>(this));
  ui_->setSaveCallback(onSaveConfiguration, const_cast<SamplerCallbackBinder *>(this));
  midi_->setAssignedNoteOnCallback(onAssignedMidiNoteOn,
                                   const_cast<SamplerCallbackBinder *>(this));
}

void SamplerCallbackBinder::pollInput(Input &input) const {
  input.update(onInputEvent, const_cast<SamplerCallbackBinder *>(this));
}

void SamplerCallbackBinder::onInputEvent(const Input::Event &event, void *context) {
  auto *self = static_cast<SamplerCallbackBinder *>(context);
  if (!self || !self->ui_) {
    return;
  }
  InputUiBridge::routeToUi(event, *self->ui_);
}

void SamplerCallbackBinder::onPreviewSample(int sampleIndex, void *context) {
  auto *self = static_cast<SamplerCallbackBinder *>(context);
  if (!self || !self->playbackRouter_) {
    return;
  }
  self->playbackRouter_->onPreviewSample(sampleIndex);
}

void SamplerCallbackBinder::onAssignedMidiNoteOn(int midiNote, void *context) {
  auto *self = static_cast<SamplerCallbackBinder *>(context);
  if (!self || !self->playbackRouter_) {
    return;
  }
  self->playbackRouter_->onAssignedMidiNoteOn(midiNote);
}

bool SamplerCallbackBinder::onSaveConfiguration(void *context) {
  auto *self = static_cast<SamplerCallbackBinder *>(context);
  if (!self || !self->saveService_) {
    return false;
  }
  return self->saveService_->saveConfiguration();
}
