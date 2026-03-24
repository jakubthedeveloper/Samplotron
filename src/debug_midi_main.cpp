#include <Arduino.h>

#include "pins.h"

namespace {

constexpr int kMidiRxPin = Pins::MIDI_IN;
constexpr int kMidiTxPin = -1;
constexpr uint32_t kMidiBaud = 31250;
constexpr bool kLogPitchBend = false;

uint8_t gStatus = 0;
uint8_t gData[2] = {0, 0};
uint8_t gDataCount = 0;
int gLastPitchBend[16] = {0};

uint8_t expectedDataBytes(uint8_t status) {
  if (status >= 0xF8) return 0;  // Real-time messages.
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

void printShortMessage(uint8_t status, const uint8_t *data) {
  const uint8_t type = status & 0xF0;
  const int channel = (status & 0x0F) + 1;

  if (type == 0x90 && data[1] > 0) {
    
    return;
  }
  if (type == 0x80 || (type == 0x90 && data[1] == 0)) {
    
    return;
  }
  if (type == 0xB0) {
    
    return;
  }
  if (type == 0xC0) {
    
    return;
  }
  if (type == 0xE0) {
    const int bend = ((static_cast<int>(data[1]) << 7) | data[0]) - 8192;
    if (kLogPitchBend && gLastPitchBend[channel - 1] != bend) {
      
    }
    gLastPitchBend[channel - 1] = bend;
    return;
  }

  const uint8_t count = expectedDataBytes(status);
  if (count == 1) {
    
  } else if (count == 2) {
    
  } else {
    
  }
}

void handleMidiByte(uint8_t b) {
  if (b >= 0xF8) {
    
    return;
  }

  if (b & 0x80) {
    gStatus = b;
    gDataCount = 0;
    return;
  }

  if (gStatus == 0) return;

  const uint8_t needed = expectedDataBytes(gStatus);
  if (needed == 0) {
    if (gStatus >= 0xF0) gStatus = 0;
    return;
  }

  gData[gDataCount++] = b;
  if (gDataCount < needed) return;

  printShortMessage(gStatus, gData);
  gDataCount = 0;

  if (gStatus >= 0xF0) {
    gStatus = 0;  // System Common has no running status.
  }
}

}  // namespace

void setup() {
  
  delay(200);
  
  
  
  

  Serial2.begin(kMidiBaud, SERIAL_8N1, kMidiRxPin, kMidiTxPin);
}

void loop() {
  while (Serial2.available() > 0) {
    const int raw = Serial2.read();
    if (raw >= 0) handleMidiByte(static_cast<uint8_t>(raw));
  }
  delay(1);
}
