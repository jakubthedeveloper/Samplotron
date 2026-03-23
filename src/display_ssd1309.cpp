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

void DisplaySsd1309::setAudio(Audio *audio) {
  audio_ = audio;
}

void DisplaySsd1309::renderUi(Ui &ui) {
  ui_ = &ui;
  dirty_ = true;
}

void DisplaySsd1309::renderBootScreen(const BootScreenModel &model) {
  if (!ready_) return;

  gDisplay.clearBuffer();

  gDisplay.setFont(u8g2_font_10x20_tf);
  gDisplay.drawStr(12, 20, "Samplotron");

  gDisplay.setFont(u8g2_font_5x8_tf);
  char line1[28];
  char line2[28];
  char line3[28];
  snprintf(line1, sizeof(line1), "Samples total: %d", model.totalSamples);
  snprintf(line2, sizeof(line2), "Assigned:      %d", model.assignedSamples);
  snprintf(line3, sizeof(line3), "RAM used:      %d%%", model.ramUsagePercent);
  gDisplay.drawStr(0, 34, line1);
  gDisplay.drawStr(0, 44, line2);
  gDisplay.drawStr(0, 54, line3);
  gDisplay.drawStr(0, 63, model.loading ? "Loading..." : "Ready");

  gDisplay.sendBuffer();
  dirty_ = false;
}

void DisplaySsd1309::update() {
  if (!ready_ || !ui_) return;

  const Ui::RenderModel &model = ui_->model();
  const bool visualizerActive = (model.state == Ui::State::Visualizer);
  const bool modelDirty = ui_->consumeDirty();
  if (!visualizerActive && !modelDirty && !dirty_) return;

  if (visualizerActive && !modelDirty && !dirty_) {
    const uint32_t nowMs = millis();
    if ((nowMs - lastVisualizerFrameMs_) < 33) {
      return;
    }
  }

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
    case Ui::State::Visualizer:
      renderVisualizer();
      break;
  }
  if (model.state != Ui::State::Visualizer) {
    renderTopRightIndicators(model);
  }
  gDisplay.sendBuffer();
  if (visualizerActive) {
    lastVisualizerFrameMs_ = millis();
  }
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
  const bool hasLastSample = (model.lastTriggeredSampleIndex >= 0);
  const bool showSave = model.hasUnsavedChanges;
  const char *playbackModeLabel =
      (model.playbackMode == Ui::PlaybackMode::Loop) ? "LOOP" : "SHOT";

  String currentSample = "----";
  if (model.lastTriggeredSampleIndex >= 0) {
    currentSample = sampleLabel(model.lastTriggeredSampleIndex, model.lastTriggeredSampleName);
  }
  gDisplay.drawStr(0, 8, currentSample.c_str());

  String midi = "MIDI: ";
  if (model.lastMidiNote >= 0) {
    midi += midiNoteLabel(model.lastMidiNote) + " (" + String(model.lastMidiNote) + ")";
  } else {
    midi += "--";
  }
  gDisplay.drawStr(0, 16, midi.c_str());

  if (hasLastSample) {
    char volLine[24];
    snprintf(volLine, sizeof(volLine), "Vol:%3d", model.currentVolume);
    gDisplay.drawStr(0, 26, volLine);
  }

  if (model.showSavedFeedback) {
    gDisplay.drawStr(0, 36, model.lastSaveSucceeded ? "Saved" : "Save ERR");
  }

  const char *kItemsLibOnly[1] = {"LIB"};
  const char *kItemsLibSave[2] = {"LIB", "SAVE"};
  const char *kItemsLibVolShot[3] = {"LIB", "VOL", playbackModeLabel};
  const char *kItemsLibVolShotSave[4] = {"LIB", "VOL", playbackModeLabel, "SAVE"};

  const char **items = nullptr;
  int itemCount = 0;
  if (hasLastSample && showSave) {
    items = kItemsLibVolShotSave;
    itemCount = 4;
  } else if (hasLastSample && !showSave) {
    items = kItemsLibVolShot;
    itemCount = 3;
  } else if (!hasLastSample && showSave) {
    items = kItemsLibSave;
    itemCount = 2;
  } else {
    items = kItemsLibOnly;
    itemCount = 1;
  }
  const int y = 48;
  const int itemW = 32;
  for (int i = 0; i < itemCount; i++) {
    const int x = i * itemW;
    if (i == model.mainSelection) {
      gDisplay.setDrawColor(1);
      gDisplay.drawBox(x, y - 7, itemW, 9);
      gDisplay.setDrawColor(0);
      gDisplay.drawStr(x + 2, y, items[i]);
      gDisplay.setDrawColor(1);
    } else {
      gDisplay.drawStr(x + 2, y, items[i]);
    }
  }

  gDisplay.drawHLine(0, 54, 128);
  gDisplay.drawStr(0, 62, "L:select   R:enter/adj");
}

void DisplaySsd1309::renderLibrary(const Ui::RenderModel &model, const Ui &ui) {
  gDisplay.setFont(u8g2_font_5x8_tf);
  gDisplay.drawStr(0, 8, "LIBRARY");

  if (!model.libraryAssignsPanic) {
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
  }

  if (model.libraryAssignsPanic) {
    gDisplay.drawStr(0, 24, "PANIC MODE");
    String panic = "Trigger: ";
    if (model.panicNote >= 0) {
      panic += midiNoteLabel(model.panicNote);
      panic += " (";
      panic += String(model.panicNote);
      panic += ")";
    } else {
      panic += "--";
    }
    gDisplay.drawStr(0, 35, panic.c_str());
  } else {
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
  }

  gDisplay.setFont(u8g2_font_4x6_tf);
  gDisplay.drawStr(0, 57, model.libraryAssignsPanic ? "L clk:back L rot:mode"
                                                     : "L clk:back L rot:mode R:browse");
  gDisplay.drawStr(0, 63, "R click: play  R hold: assign");
}

void DisplaySsd1309::renderAssign(const Ui::RenderModel &model, const Ui &ui) {
  gDisplay.setFont(u8g2_font_5x8_tf);
  gDisplay.drawStr(0, 8, model.assigningPanic ? "ASSIGN PANIC" : "ASSIGN NOTE");

  String sample = "Sample: ";
  if (model.currentSampleIndex >= 0) {
    sample += sampleLabel(model.currentSampleIndex, ui.sampleNameAt(model.currentSampleIndex));
  } else {
    sample += "---";
  }
  gDisplay.drawStr(0, 20, sample.c_str());
  gDisplay.drawStr(0, 31, model.assigningPanic ? "Play note for panic" : "Play note to assign");

  String current = "Current: ";
  if (model.assigningPanic) {
    if (model.panicNote >= 0) {
      current += midiNoteLabel(model.panicNote);
      current += " (";
      current += String(model.panicNote);
      current += ")";
    } else {
      current += "--";
    }
  } else {
    if (model.assignedNoteForSelectedSample >= 0) {
      current += midiNoteLabel(model.assignedNoteForSelectedSample);
      current += " (";
      current += String(model.assignedNoteForSelectedSample);
      current += ")";
    } else {
      current += "--";
    }
  }
  gDisplay.drawStr(0, 42, current.c_str());

  gDisplay.drawHLine(0, 54, 128);
  gDisplay.drawStr(0, 62, "L:cancel  Waiting for MIDI...");
}

void DisplaySsd1309::renderSaving() {
  gDisplay.setFont(u8g2_font_6x12_tf);
  gDisplay.drawStr(0, 22, "Saving...");
  gDisplay.setFont(u8g2_font_5x8_tf);
  gDisplay.drawStr(0, 38, "Please wait");
}

void DisplaySsd1309::renderVisualizer() {
  constexpr int x0 = 0;
  constexpr int y0 = 0;
  constexpr int width = 128;
  constexpr int height = 64;
  constexpr int midY = y0 + (height / 2);

  gDisplay.drawFrame(x0, y0, width, height);
  gDisplay.drawHLine(x0 + 1, midY, width - 2);

  Audio::WaveformSnapshot snapshot;
  if (!audio_ || !audio_->waveformSnapshot(snapshot) || snapshot.validPoints < 2) {
    return;
  }

  const int pointCount = static_cast<int>(snapshot.validPoints);
  const int drawableW = width - 2;
  const int drawableH = height - 2;
  const int halfH = drawableH / 2;
  constexpr int kVisualizerAmplitudeScale = 4;

  int prevX = x0 + 1;
  int prevY = midY;
  for (int x = 0; x < drawableW; x++) {
    const int index = (x * pointCount) / drawableW;
    const int8_t sample = snapshot.points[index];
    const int scaledSample = static_cast<int>(sample) * kVisualizerAmplitudeScale;
    int y = midY - ((scaledSample * halfH) / 128);
    if (y < y0 + 1) y = y0 + 1;
    if (y > y0 + height - 2) y = y0 + height - 2;
    const int drawX = x0 + 1 + x;
    if (x > 0) {
      gDisplay.drawLine(prevX, prevY, drawX, y);
    }
    prevX = drawX;
    prevY = y;
  }
}

void DisplaySsd1309::renderTopRightIndicators(const Ui::RenderModel &model) {
  gDisplay.setFont(u8g2_font_5x8_tf);
  if (model.hasUnsavedChanges) {
    gDisplay.drawStr(116, 8, "*");
  }
  if (model.midiPulseActive) {
    gDisplay.drawDisc(124, 4, 2, U8G2_DRAW_ALL);
  }
}
