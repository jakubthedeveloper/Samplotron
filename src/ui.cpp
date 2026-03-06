#include "ui.h"

#include <Arduino.h>

namespace {

constexpr int kMainLib = 0;
constexpr int kMainVol = 1;
constexpr int kMainPitch = 2;
constexpr int kMainSave = 3;
constexpr int kLibraryWindowSize = 3;

int clampValue(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

}  // namespace

void Ui::begin(const String *sampleNames, const String *samplePaths, int sampleCount) {
  sampleNames_ = sampleNames;
  samplePaths_ = samplePaths;
  sampleCount_ = clampValue(sampleCount, 0, kMaxSamples);

  for (int i = 0; i < sampleCount_; i++) {
    sampleVolumes_[i] = 100;
    samplePitches_[i] = 0;
  }
  for (int i = sampleCount_; i < kMaxSamples; i++) {
    sampleVolumes_[i] = 0;
    samplePitches_[i] = 0;
  }
  for (int i = 0; i < 128; i++) {
    sampleForMidiNote_[i] = -1;
  }

  currentSampleIndex_ = (sampleCount_ > 0) ? 0 : -1;
  lastTriggeredSampleIndex_ = -1;
  lastMidiNote_ = -1;
  mainSelection_ = 0;
  libraryWindowStart_ = 0;
  state_ = State::Main;
  saveCompletedPending_ = false;
  saveFeedbackUntilMs_ = 0;

  markDirty();
}

void Ui::setPreviewCallback(OnPreviewSampleCallback callback, void *context) {
  onPreview_ = callback;
  previewContext_ = context;
}

void Ui::handleEvent(const Event &event) {
  if (state_ == State::Saving) {
    return;
  }

  if (event.type == EventType::MidiNoteOn) {
    lastMidiNote_ = clampValue(event.value, 0, 127);
    if (state_ == State::AssignNote) {
      logNotImplemented("assign_selected_sample_to_received_note");
      transitionTo(State::Library);
    } else {
      markDirty();
    }
    return;
  }

  switch (state_) {
    case State::Main:
      handleMainEvent(event);
      break;
    case State::Library:
      handleLibraryEvent(event);
      break;
    case State::AssignNote:
      handleAssignEvent(event);
      break;
    case State::Saving:
      break;
  }
}

void Ui::update() {
  if (saveCompletedPending_) {
    saveCompletedPending_ = false;
    completeSave();
  }

  const unsigned long now = millis();
  if (saveFeedbackUntilMs_ != 0 && now >= saveFeedbackUntilMs_) {
    saveFeedbackUntilMs_ = 0;
    markDirty();
  }
}

bool Ui::consumeDirty() {
  const bool wasDirty = model_.dirty;
  model_.dirty = false;
  return wasDirty;
}

const Ui::RenderModel &Ui::model() const {
  return model_;
}

const String &Ui::samplePathAt(int sampleIndex) const {
  static const String kEmpty = "";
  if (!samplePaths_ || sampleIndex < 0 || sampleIndex >= sampleCount_) {
    return kEmpty;
  }
  return samplePaths_[sampleIndex];
}

const String &Ui::sampleNameAt(int sampleIndex) const {
  static const String kEmpty = "";
  if (!sampleNames_ || sampleIndex < 0 || sampleIndex >= sampleCount_) {
    return kEmpty;
  }
  return sampleNames_[sampleIndex];
}

bool Ui::hasSamples() const {
  return sampleCount_ > 0;
}

void Ui::markDirty() {
  updateDerivedModel();
  model_.dirty = true;
}

void Ui::updateDerivedModel() {
  model_.state = state_;
  model_.mainSelection = mainSelection_;
  model_.currentSampleIndex = currentSampleIndex_;
  model_.sampleCount = sampleCount_;
  model_.lastTriggeredSampleIndex = lastTriggeredSampleIndex_;
  model_.lastTriggeredSampleName =
      (lastTriggeredSampleIndex_ >= 0) ? sampleNameAt(lastTriggeredSampleIndex_) : "-";
  model_.lastMidiNote = lastMidiNote_;
  model_.libraryWindowStart = libraryWindowStart_;
  model_.assignedNoteForSelectedSample = findAssignedNoteForSample(currentSampleIndex_);
  model_.showSavedFeedback = (saveFeedbackUntilMs_ != 0);
  if (currentSampleIndex_ >= 0 && currentSampleIndex_ < sampleCount_) {
    model_.currentVolume = sampleVolumes_[currentSampleIndex_];
    model_.currentPitch = samplePitches_[currentSampleIndex_];
  } else {
    model_.currentVolume = 0;
    model_.currentPitch = 0;
  }
}

void Ui::handleMainEvent(const Event &event) {
  switch (event.type) {
    case EventType::LeftRotate:
      mainSelection_ = wrapIndex(mainSelection_ + event.value, kMenuItems);
      markDirty();
      return;
    case EventType::RightRotate:
      if (currentSampleIndex_ < 0 || currentSampleIndex_ >= sampleCount_) return;
      if (mainSelection_ == kMainVol) {
        sampleVolumes_[currentSampleIndex_] =
            clampValue(sampleVolumes_[currentSampleIndex_] + event.value, 0, 127);
        markDirty();
      } else if (mainSelection_ == kMainPitch) {
        samplePitches_[currentSampleIndex_] =
            clampValue(samplePitches_[currentSampleIndex_] + event.value, -12, 12);
        markDirty();
      }
      return;
    case EventType::RightClick:
      if (mainSelection_ == kMainLib) {
        transitionTo(State::Library);
      } else if (mainSelection_ == kMainSave) {
        transitionTo(State::Saving);
      }
      return;
    case EventType::LeftClick:
    case EventType::RightLongPress:
    case EventType::MidiNoteOn:
      return;
  }
}

void Ui::handleLibraryEvent(const Event &event) {
  switch (event.type) {
    case EventType::LeftClick:
      transitionTo(State::Main);
      return;
    case EventType::RightRotate:
      if (sampleCount_ <= 0) return;
      currentSampleIndex_ = wrapIndex(currentSampleIndex_ + event.value, sampleCount_);
      if (currentSampleIndex_ < libraryWindowStart_) {
        libraryWindowStart_ = currentSampleIndex_;
      } else if (currentSampleIndex_ >= libraryWindowStart_ + kLibraryWindowSize) {
        libraryWindowStart_ = currentSampleIndex_ - (kLibraryWindowSize - 1);
      }
      markDirty();
      return;
    case EventType::RightClick:
      triggerPreview(currentSampleIndex_);
      return;
    case EventType::RightLongPress:
      transitionTo(State::AssignNote);
      return;
    case EventType::LeftRotate:
    case EventType::MidiNoteOn:
      return;
  }
}

void Ui::handleAssignEvent(const Event &event) {
  switch (event.type) {
    case EventType::LeftClick:
      transitionTo(State::Library);
      return;
    case EventType::LeftRotate:
    case EventType::RightRotate:
    case EventType::RightClick:
    case EventType::RightLongPress:
    case EventType::MidiNoteOn:
      return;
  }
}

void Ui::transitionTo(State state) {
  state_ = state;
  if (state_ == State::Saving) {
    startSave();
    return;
  }
  markDirty();
}

void Ui::startSave() {
  logNotImplemented("save_configuration");
  saveCompletedPending_ = true;
  markDirty();
}

void Ui::completeSave() {
  state_ = State::Main;
  saveFeedbackUntilMs_ = millis() + kSaveFeedbackMs;
  markDirty();
}

void Ui::logNotImplemented(const char *functionName) const {
  Serial.printf("Not implemented %s\n", functionName);
}

void Ui::triggerPreview(int sampleIndex) {
  if (sampleIndex < 0 || sampleIndex >= sampleCount_) return;
  if (onPreview_) onPreview_(sampleIndex, previewContext_);
  lastTriggeredSampleIndex_ = sampleIndex;
  markDirty();
}

int Ui::wrapIndex(int value, int size) {
  if (size <= 0) return 0;
  int wrapped = value % size;
  if (wrapped < 0) wrapped += size;
  return wrapped;
}

int Ui::findAssignedNoteForSample(int sampleIndex) const {
  if (sampleIndex < 0 || sampleIndex >= sampleCount_) return -1;
  for (int note = 0; note < 128; note++) {
    if (sampleForMidiNote_[note] == sampleIndex) return note;
  }
  return -1;
}
