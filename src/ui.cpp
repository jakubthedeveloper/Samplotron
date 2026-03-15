#include "ui.h"

#include <Arduino.h>

namespace {

constexpr int kMainLib = 0;
constexpr int kMainVol = 1;
constexpr int kMainSave = 2;
constexpr int kLibraryWindowSize = 3;

int clampValue(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

int mainMenuItemsCount(bool hasLastSample, bool hasUnsavedChanges) {
  int count = 1;  // LIB
  if (hasLastSample) count++;
  if (hasUnsavedChanges) count++;
  return count;
}

int logicalMainItemForSelection(int selection, bool hasLastSample, bool hasUnsavedChanges) {
  if (selection == 0) return kMainLib;
  int cursor = 1;
  if (hasLastSample) {
    if (selection == cursor) return kMainVol;
    cursor++;
  }
  if (hasUnsavedChanges && selection == cursor) return kMainSave;
  return kMainLib;
}

}  // namespace

void Ui::begin(const String *sampleNames, const String *samplePaths, int sampleCount) {
  sampleNames_ = sampleNames;
  samplePaths_ = samplePaths;
  sampleCount_ = clampValue(sampleCount, 0, kMaxSamples);

  for (int i = 0; i < sampleCount_; i++) {
    sampleVolumes_[i] = 100;
  }
  for (int i = sampleCount_; i < kMaxSamples; i++) {
    sampleVolumes_[i] = 0;
  }
  for (int i = 0; i < 128; i++) {
    sampleForMidiNote_[i] = -1;
  }

  currentSampleIndex_ = (sampleCount_ > 0) ? 0 : -1;
  panicMidiNote_ = -1;
  libraryAssignsPanic_ = false;
  assigningPanic_ = false;
  lastTriggeredSampleIndex_ = -1;
  lastMidiNote_ = -1;
  mainSelection_ = 0;
  libraryWindowStart_ = 0;
  state_ = State::Main;
  saveCompletedPending_ = false;
  saveRunPending_ = false;
  saveExecutionArmed_ = false;
  lastSaveSucceeded_ = true;
  hasUnsavedChanges_ = false;
  saveCompleteAfterMs_ = 0;
  midiPulseUntilMs_ = 0;
  saveFeedbackUntilMs_ = 0;

  markDirty();
}

void Ui::setPreviewCallback(OnPreviewSampleCallback callback, void *context) {
  onPreview_ = callback;
  previewContext_ = context;
}

void Ui::setSaveCallback(OnSaveCallback callback, void *context) {
  onSave_ = callback;
  saveContext_ = context;
}

void Ui::handleEvent(const Event &event) {
  if (state_ == State::Saving) {
    return;
  }

  if (event.type == EventType::MidiNoteOn) {
    lastMidiNote_ = clampValue(event.value, 0, 127);
    midiPulseUntilMs_ = millis() + kMidiPulseMs;
    if (state_ == State::AssignNote) {
      if (assigningPanic_) {
        setPanicMidiNote(lastMidiNote_);
      } else {
        setMidiAssignment(lastMidiNote_, currentSampleIndex_);
      }
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
  if (saveCompletedPending_ && millis() >= saveCompleteAfterMs_) {
    saveCompletedPending_ = false;
    completeSave();
  }

  if (state_ == State::Saving && saveRunPending_) {
    // Let display render "Saving..." first, then execute blocking save on next update cycle.
    if (!saveExecutionArmed_) {
      saveExecutionArmed_ = true;
    } else {
      if (!onSave_) {
        logNotImplemented("save_configuration_callback_missing");
        lastSaveSucceeded_ = false;
      } else {
        lastSaveSucceeded_ = onSave_(saveContext_);
      }
      saveRunPending_ = false;
      saveCompletedPending_ = true;
      saveCompleteAfterMs_ = millis() + kSavingScreenMinMs;
      markDirty();
    }
  }

  const unsigned long now = millis();
  if (midiPulseUntilMs_ != 0 && now >= midiPulseUntilMs_) {
    midiPulseUntilMs_ = 0;
    markDirty();
  }
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

bool Ui::setMidiAssignment(int note, int sampleIndex) {
  if (note < 0 || note > 127) return false;
  if (sampleIndex < -1 || sampleIndex >= sampleCount_) return false;
  bool changed = false;

  if (sampleIndex >= 0) {
    for (int i = 0; i < 128; i++) {
      if (i != note && sampleForMidiNote_[i] == sampleIndex) {
        sampleForMidiNote_[i] = -1;
        changed = true;
      }
    }
  }

  if (sampleForMidiNote_[note] == sampleIndex) {
    if (changed) {
      hasUnsavedChanges_ = true;
    }
    markDirty();
    return true;
  }
  sampleForMidiNote_[note] = sampleIndex;
  hasUnsavedChanges_ = true;
  markDirty();
  return true;
}

int Ui::assignedSampleForMidiNote(int note) const {
  if (note < 0 || note > 127) return -1;
  return sampleForMidiNote_[note];
}

bool Ui::setPanicMidiNote(int note) {
  if (note < 0 || note > 127) return false;
  if (panicMidiNote_ == note) {
    markDirty();
    return true;
  }
  panicMidiNote_ = note;
  hasUnsavedChanges_ = true;
  markDirty();
  return true;
}

int Ui::panicMidiNote() const {
  return panicMidiNote_;
}

bool Ui::setSampleVolume(int sampleIndex, int volume) {
  if (sampleIndex < 0 || sampleIndex >= sampleCount_) return false;
  const int clamped = clampValue(volume, kVolumeMin, kVolumeMax);
  if (sampleVolumes_[sampleIndex] == clamped) return true;
  sampleVolumes_[sampleIndex] = clamped;
  hasUnsavedChanges_ = true;
  markDirty();
  return true;
}

int Ui::sampleVolumeForSample(int sampleIndex) const {
  if (sampleIndex < 0 || sampleIndex >= sampleCount_) return kVolumeMax;
  return sampleVolumes_[sampleIndex];
}

void Ui::markDirty() {
  updateDerivedModel();
  model_.dirty = true;
}

void Ui::updateDerivedModel() {
  const bool hasLastSample =
      (lastTriggeredSampleIndex_ >= 0 && lastTriggeredSampleIndex_ < sampleCount_);
  mainSelection_ =
      wrapIndex(mainSelection_, mainMenuItemsCount(hasLastSample, hasUnsavedChanges_));

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
  model_.panicNote = panicMidiNote_;
  model_.libraryAssignsPanic = libraryAssignsPanic_;
  model_.assigningPanic = assigningPanic_;
  model_.showSavedFeedback = (saveFeedbackUntilMs_ != 0);
  model_.lastSaveSucceeded = lastSaveSucceeded_;
  model_.hasUnsavedChanges = hasUnsavedChanges_;
  model_.midiPulseActive = (midiPulseUntilMs_ != 0);
  if (hasLastSample) {
    model_.currentVolume = sampleVolumes_[lastTriggeredSampleIndex_];
  } else {
    model_.currentVolume = 0;
  }
}

void Ui::handleMainEvent(const Event &event) {
  const bool hasLastSample =
      (lastTriggeredSampleIndex_ >= 0 && lastTriggeredSampleIndex_ < sampleCount_);
  const int menuItems = mainMenuItemsCount(hasLastSample, hasUnsavedChanges_);
  const int logicalSelected =
      logicalMainItemForSelection(mainSelection_, hasLastSample, hasUnsavedChanges_);

  switch (event.type) {
    case EventType::LeftRotate:
      mainSelection_ = wrapIndex(mainSelection_ + event.value, menuItems);
      markDirty();
      return;
    case EventType::RightRotate:
      if (!hasLastSample) return;
      if (logicalSelected == kMainVol) {
        const int previous = sampleVolumes_[lastTriggeredSampleIndex_];
        sampleVolumes_[lastTriggeredSampleIndex_] =
            clampValue(sampleVolumes_[lastTriggeredSampleIndex_] + (event.value * kVolumeStep),
                       kVolumeMin,
                       kVolumeMax);
        if (sampleVolumes_[lastTriggeredSampleIndex_] != previous) {
          hasUnsavedChanges_ = true;
        }
        markDirty();
      }
      return;
    case EventType::RightClick:
      if (logicalSelected == kMainLib) {
        transitionTo(State::Library);
      } else if (logicalSelected == kMainSave) {
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
      if (libraryAssignsPanic_) return;
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
      assigningPanic_ = libraryAssignsPanic_;
      transitionTo(State::AssignNote);
      return;
    case EventType::LeftRotate:
      libraryAssignsPanic_ = (wrapIndex((libraryAssignsPanic_ ? 1 : 0) + event.value, 2) == 1);
      markDirty();
      return;
    case EventType::MidiNoteOn:
      return;
  }
}

void Ui::handleAssignEvent(const Event &event) {
  switch (event.type) {
    case EventType::LeftClick:
      assigningPanic_ = false;
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
  if (state_ != State::AssignNote) {
    assigningPanic_ = false;
  }
  markDirty();
}

void Ui::startSave() {
  saveRunPending_ = true;
  saveExecutionArmed_ = false;
  saveCompletedPending_ = false;
  saveCompleteAfterMs_ = 0;
  markDirty();
}

void Ui::completeSave() {
  state_ = State::Main;
  saveRunPending_ = false;
  saveExecutionArmed_ = false;
  if (lastSaveSucceeded_) {
    hasUnsavedChanges_ = false;
  }
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

void Ui::reportTriggeredSample(int sampleIndex) {
  if (sampleIndex < 0 || sampleIndex >= sampleCount_) return;
  lastTriggeredSampleIndex_ = sampleIndex;
  markDirty();
}

void Ui::clearTriggeredSample() {
  if (lastTriggeredSampleIndex_ < 0) return;
  lastTriggeredSampleIndex_ = -1;
  markDirty();
}

void Ui::clearUnsavedChanges() {
  if (!hasUnsavedChanges_) return;
  hasUnsavedChanges_ = false;
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
