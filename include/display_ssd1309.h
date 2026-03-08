#pragma once

#include <Arduino.h>

#include "ui.h"

class DisplaySsd1309 {
 public:
  struct BootScreenModel {
    int totalSamples = 0;
    int assignedSamples = 0;
    int ramUsagePercent = 0;
    bool loading = true;
  };

  bool begin();
  void renderUi(Ui &ui);
  void renderBootScreen(const BootScreenModel &model);
  void update();

 private:
  static String midiNoteLabel(int note);
  static String sampleLabel(int sampleIndex, const String &sampleName);
  void renderMain(const Ui::RenderModel &model, const Ui &ui);
  void renderLibrary(const Ui::RenderModel &model, const Ui &ui);
  void renderAssign(const Ui::RenderModel &model, const Ui &ui);
  void renderSaving();

  bool ready_ = false;
  bool dirty_ = false;
  Ui *ui_ = nullptr;
};
