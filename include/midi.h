#pragma once

#include <Arduino.h>
#include <stdint.h>

class Ui;

class Midi {
 public:
  using OnAssignedNoteOnCallback = void (*)(int midiNote, void *context);

  void begin(Ui *ui);
  void setAssignedNoteOnCallback(OnAssignedNoteOnCallback callback, void *context);
  void handleNoteOn(int midiNote);

 private:
  Ui *ui_ = nullptr;
  OnAssignedNoteOnCallback onAssignedNoteOn_ = nullptr;
  void *assignedNoteOnContext_ = nullptr;
};
