#pragma once

#include <Arduino.h>

#include "display_ssd1309.h"
#include "input.h"

class BootScreenFlow {
 public:
  void begin(DisplaySsd1309 *display, Input *input);
  void render(bool loading, int totalSamples, int assignedSamples, int ramUsagePercent);
  void waitForDismissOrTimeout(unsigned long timeoutMs);

 private:
  static void onInputEvent(const Input::Event &event, void *context);

  DisplaySsd1309 *display_ = nullptr;
  Input *input_ = nullptr;
  DisplaySsd1309::BootScreenModel model_;
  bool dismissRequested_ = false;
};
