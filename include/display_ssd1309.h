#pragma once

#include <Arduino.h>

#include "audio.h"
#include "ui.h"

class DisplaySsd1309 {
 public:
  struct BootScreenModel {
    int totalSamples = 0;
    int assignedSamples = 0;
    int checkedSamples = 0;
    int rejectedSamples = 0;
    int ramUsagePercent = 0;
    bool loading = true;
  };

  bool begin();
  void setAudio(Audio *audio);
  void renderStartupMessage(const char *title, const char *subtitle);
  void renderUi(Ui &ui);
  void renderBootScreen(const BootScreenModel &model);
  void update();

 private:
  static String midiNoteLabel(int note);
  static String sampleLabel(int sampleIndex, const String &sampleName);
  void renderTopRightIndicators(const Ui::RenderModel &model);
  void renderMain(const Ui::RenderModel &model, const Ui &ui);
  void renderLibrary(const Ui::RenderModel &model, const Ui &ui);
  void renderAssign(const Ui::RenderModel &model, const Ui &ui);
  void renderSaving();
  void renderVisualizer();

  bool ready_ = false;
  bool dirty_ = false;
  Ui *ui_ = nullptr;
  Audio *audio_ = nullptr;
  uint32_t lastVisualizerFrameMs_ = 0;
};
