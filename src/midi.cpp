#include "midi.h"

#include "ui.h"

namespace {

int clampMidiNote(int note) {
  if (note < 0) return 0;
  if (note > 127) return 127;
  return note;
}

}  // namespace

void Midi::begin(Ui *ui) {
  ui_ = ui;
}

void Midi::setAssignedNoteOnCallback(OnAssignedNoteOnCallback callback, void *context) {
  onAssignedNoteOn_ = callback;
  assignedNoteOnContext_ = context;
}

void Midi::handleNoteOn(int midiNote) {
  const int note = clampMidiNote(midiNote);
  if (!ui_) return;

  const bool isAssignMode = (ui_->model().state == Ui::State::AssignNote);
  ui_->handleEvent({Ui::EventType::MidiNoteOn, note});

  if (!isAssignMode && onAssignedNoteOn_) {
    onAssignedNoteOn_(note, assignedNoteOnContext_);
  }
}
