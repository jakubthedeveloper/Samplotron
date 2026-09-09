#pragma once

#include <Arduino.h>

#include "display_ssd1309.h"
#include "input.h"
#include "ui.h"

class BootScreenFlow {
 public:
  static constexpr unsigned long kDefaultDismissTimeoutMs = 5000;

  void begin(DisplaySsd1309 *display, Input *input, Ui *ui);
  void render(bool loading, int totalSamples, int assignedSamples, int ramUsagePercent,
              int checkedSamples = 0, int rejectedSamples = 0);
  void waitForDismissOrTimeout(unsigned long timeoutMs = kDefaultDismissTimeoutMs);

 private:
  static void onInputEvent(const Input::Event &event, void *context);

  DisplaySsd1309 *display_ = nullptr;
  Input *input_ = nullptr;
  Ui *ui_ = nullptr;
  DisplaySsd1309::BootScreenModel model_;
  bool dismissRequested_ = false;
};
