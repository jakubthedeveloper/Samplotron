#pragma once

#include <Arduino.h>
#include <stdint.h>

class Ui;

class Midi {
 public:
  using OnAssignedNoteOnCallback = void (*)(int midiNote, void *context);

  void begin(Ui *ui);
  void update();
  void setAssignedNoteOnCallback(OnAssignedNoteOnCallback callback, void *context);
  void handleNoteOn(int midiNote);

 private:
  void handleMidiByte(uint8_t b);
  uint8_t expectedDataBytes(uint8_t status) const;
  void processShortMessage(uint8_t status, const uint8_t *data);

  Ui *ui_ = nullptr;
  OnAssignedNoteOnCallback onAssignedNoteOn_ = nullptr;
  void *assignedNoteOnContext_ = nullptr;
  uint8_t runningStatus_ = 0;
  uint8_t dataBytes_[2] = {0, 0};
  uint8_t dataCount_ = 0;
};
