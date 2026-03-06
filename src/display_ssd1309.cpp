#include "display_ssd1309.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <stdio.h>

#include "pins.h"

namespace {

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C gDisplay(U8G2_R0, U8X8_PIN_NONE);

bool probeI2cAddress(uint8_t address7bit) {
  Wire.beginTransmission(address7bit);
  return Wire.endTransmission() == 0;
}

bool detectDisplayAddress(uint8_t &address7bit) {
  constexpr uint8_t kCandidates[] = {0x3C, 0x3D};
  for (uint8_t candidate : kCandidates) {
    if (probeI2cAddress(candidate)) {
      address7bit = candidate;
      return true;
    }
  }
  return false;
}

}  // namespace

bool DisplaySsd1309::begin() {
  Wire.begin(Pins::OLED_MOSI, Pins::OLED_SCK, 400000U);

  uint8_t displayAddress = 0;
  if (!detectDisplayAddress(displayAddress)) {
    Serial.println("SSD1309 not found on I2C (tried 0x3C/0x3D)");
    ready_ = false;
    return false;
  }

  gDisplay.setI2CAddress(static_cast<uint8_t>(displayAddress << 1));
  if (!gDisplay.begin()) {
    ready_ = false;
    return false;
  }

  ready_ = true;
  dirty_ = false;
  return true;
}

void DisplaySsd1309::renderUi(Ui &ui) {
  ui_ = &ui;
  dirty_ = true;
}

void DisplaySsd1309::update() {
  if (!ready_ || !ui_) return;

  if (!ui_->consumeDirty() && !dirty_) return;
  const Ui::RenderModel &model = ui_->model();

  gDisplay.clearBuffer();
  switch (model.state) {
    case Ui::State::Main:
      renderMain(model, *ui_);
      break;
    case Ui::State::Library:
      renderLibrary(model, *ui_);
      break;
    case Ui::State::AssignNote:
      renderAssign(model, *ui_);
      break;
    case Ui::State::Saving:
      renderSaving();
      break;
  }
  gDisplay.sendBuffer();
  dirty_ = false;
}

String DisplaySsd1309::midiNoteLabel(int note) {
  static const char *kNotes[12] = {"C",  "C#", "D",  "D#", "E", "F",
                                   "F#", "G",  "G#", "A",  "A#", "B"};
  if (note < 0 || note > 127) return "--";
  String label = kNotes[note % 12];
  label += String((note / 12) - 1);
  return label;
}

String DisplaySsd1309::sampleLabel(int sampleIndex, const String &sampleName) {
  char prefix[8];
  if (sampleIndex < 0) {
    snprintf(prefix, sizeof(prefix), "---");
  } else {
    snprintf(prefix, sizeof(prefix), "%03d", sampleIndex + 1);
  }
  return String(prefix) + " " + sampleName;
}

void DisplaySsd1309::renderMain(const Ui::RenderModel &model, const Ui &ui) {
  gDisplay.setFont(u8g2_font_5x8_tf);

  String lastSample = "Last: ";
  if (model.lastTriggeredSampleIndex >= 0) {
    lastSample += sampleLabel(model.lastTriggeredSampleIndex, model.lastTriggeredSampleName);
  } else {
    lastSample += "---";
  }
  gDisplay.drawStr(0, 8, lastSample.c_str());

  String midi = "MIDI: ";
  if (model.lastMidiNote >= 0) {
    midi += midiNoteLabel(model.lastMidiNote) + " (" + String(model.lastMidiNote) + ")";
  } else {
    midi += "--";
  }
  gDisplay.drawStr(0, 16, midi.c_str());

  char volLine[24];
  snprintf(volLine, sizeof(volLine), "Vol:%3d", model.currentVolume);
  gDisplay.drawStr(0, 26, volLine);

  char pitchLine[24];
  snprintf(pitchLine, sizeof(pitchLine), "Pitch:%+3d", model.currentPitch);
  gDisplay.drawStr(68, 26, pitchLine);

  if (model.showSavedFeedback) {
    gDisplay.drawStr(0, 36, "Saved");
  }

  static const char *kItems[4] = {"LIB", "VOL", "PITCH", "SAVE"};
  const int y = 48;
  const int itemW = 32;
  for (int i = 0; i < 4; i++) {
    const int x = i * itemW;
    if (i == model.mainSelection) {
      gDisplay.setDrawColor(1);
      gDisplay.drawBox(x, y - 7, itemW, 9);
      gDisplay.setDrawColor(0);
      gDisplay.drawStr(x + 2, y, kItems[i]);
      gDisplay.setDrawColor(1);
    } else {
      gDisplay.drawStr(x + 2, y, kItems[i]);
    }
  }

  gDisplay.drawHLine(0, 54, 128);
  gDisplay.drawStr(0, 62, "L:select   R:enter/adj");
}

void DisplaySsd1309::renderLibrary(const Ui::RenderModel &model, const Ui &ui) {
  gDisplay.setFont(u8g2_font_5x8_tf);
  gDisplay.drawStr(0, 8, "LIBRARY");

  const int start = model.libraryWindowStart;
  for (int row = 0; row < 3; row++) {
    const int sampleIndex = start + row;
    const int y = 20 + row * 9;
    if (sampleIndex >= model.sampleCount) break;

    const bool selected = (sampleIndex == model.currentSampleIndex);
    String line = (selected ? "> " : "  ");
    line += sampleLabel(sampleIndex, ui.sampleNameAt(sampleIndex));
    if (selected) {
      gDisplay.drawBox(0, y - 7, 128, 9);
      gDisplay.setDrawColor(0);
      gDisplay.drawStr(0, y, line.c_str());
      gDisplay.setDrawColor(1);
    } else {
      gDisplay.drawStr(0, y, line.c_str());
    }
  }

  String assigned = "Assigned: ";
  if (model.assignedNoteForSelectedSample >= 0) {
    assigned += midiNoteLabel(model.assignedNoteForSelectedSample);
    assigned += " (";
    assigned += String(model.assignedNoteForSelectedSample);
    assigned += ")";
  } else {
    assigned += "--";
  }
  gDisplay.drawStr(0, 47, assigned.c_str());

  gDisplay.setFont(u8g2_font_4x6_tf);
  gDisplay.drawStr(0, 57, "L: back        R: browse");
  gDisplay.drawStr(0, 63, "R click: play  R hold: assign");
}

void DisplaySsd1309::renderAssign(const Ui::RenderModel &model, const Ui &ui) {
  gDisplay.setFont(u8g2_font_5x8_tf);
  gDisplay.drawStr(0, 8, "ASSIGN NOTE");

  String sample = "Sample: ";
  if (model.currentSampleIndex >= 0) {
    sample += sampleLabel(model.currentSampleIndex, ui.sampleNameAt(model.currentSampleIndex));
  } else {
    sample += "---";
  }
  gDisplay.drawStr(0, 20, sample.c_str());
  gDisplay.drawStr(0, 31, "Play note to assign");

  String current = "Current: ";
  if (model.assignedNoteForSelectedSample >= 0) {
    current += midiNoteLabel(model.assignedNoteForSelectedSample);
    current += " (";
    current += String(model.assignedNoteForSelectedSample);
    current += ")";
  } else {
    current += "--";
  }
  gDisplay.drawStr(0, 42, current.c_str());

  gDisplay.drawHLine(0, 54, 128);
  gDisplay.drawStr(0, 62, "L:cancel  Waiting for MIDI...");
}

void DisplaySsd1309::renderSaving() {
  gDisplay.setFont(u8g2_font_6x12_tf);
  gDisplay.drawStr(0, 22, "SAVING...");
  gDisplay.setFont(u8g2_font_5x8_tf);
  gDisplay.drawStr(0, 38, "Persisting configuration");
  gDisplay.drawStr(0, 62, "No encoder action during save");
}
