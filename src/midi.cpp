#include "midi.h"

#include <Arduino.h>

#include "pins.h"
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
  runningStatus_ = 0;
  dataCount_ = 0;
  Serial2.begin(31250, SERIAL_8N1, Pins::MIDI_IN, -1);
  
}

void Midi::update() {
  while (Serial2.available() > 0) {
    const int raw = Serial2.read();
    if (raw >= 0) {
      handleMidiByte(static_cast<uint8_t>(raw));
    }
  }
}

void Midi::setAssignedNoteOnCallback(OnAssignedNoteOnCallback callback, void *context) {
  onAssignedNoteOn_ = callback;
  assignedNoteOnContext_ = context;
}

void Midi::handleNoteOn(int midiNote) {
  const int note = clampMidiNote(midiNote);
  if (!ui_) return;

  ui_->handleEvent({Ui::EventType::MidiNoteOn, note});

  if (onAssignedNoteOn_) {
    onAssignedNoteOn_(note, assignedNoteOnContext_);
  }
}

uint8_t Midi::expectedDataBytes(uint8_t status) const {
  if (status >= 0xF8) return 0;
  switch (status & 0xF0) {
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
      return 2;
    case 0xC0:
    case 0xD0:
      return 1;
    default:
      break;
  }

  switch (status) {
    case 0xF1:
    case 0xF3:
      return 1;
    case 0xF2:
      return 2;
    default:
      return 0;
  }
}

void Midi::processShortMessage(uint8_t status, const uint8_t *data) {
  const uint8_t type = status & 0xF0;
  if (type == 0x90 && data[1] > 0) {
    handleNoteOn(static_cast<int>(data[0]));
  }
}

void Midi::handleMidiByte(uint8_t b) {
  if (b >= 0xF8) {
    return;
  }

  if (b & 0x80) {
    runningStatus_ = b;
    dataCount_ = 0;
    return;
  }

  if (runningStatus_ == 0) return;

  const uint8_t needed = expectedDataBytes(runningStatus_);
  if (needed == 0) {
    if (runningStatus_ >= 0xF0) {
      runningStatus_ = 0;
    }
    return;
  }

  dataBytes_[dataCount_++] = b;
  if (dataCount_ < needed) return;

  processShortMessage(runningStatus_, dataBytes_);
  dataCount_ = 0;

  if (runningStatus_ >= 0xF0) {
    runningStatus_ = 0;
  }
}
