#pragma once

#include <Arduino.h>

class DisplaySsd1309 {
 public:
  bool begin();
  void setSampleSelection(int currentSampleNumber, int totalSamples, const String &sampleName);
  void update();

 private:
  void render();

  bool ready_ = false;
  bool dirty_ = true;
  int currentSampleNumber_ = 0;
  int totalSamples_ = 0;
  String currentSampleName_ = "-";
};
