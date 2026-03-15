#include "boot_screen_flow.h"

void BootScreenFlow::begin(DisplaySsd1309 *display, Input *input, Ui *ui) {
  display_ = display;
  input_ = input;
  ui_ = ui;
  dismissRequested_ = false;
}

void BootScreenFlow::render(bool loading,
                            int totalSamples,
                            int assignedSamples,
                            int ramUsagePercent) {
  if (!display_) return;

  model_.loading = loading;
  model_.totalSamples = totalSamples;
  model_.assignedSamples = assignedSamples;
  model_.ramUsagePercent = ramUsagePercent;
  display_->renderBootScreen(model_);
}

void BootScreenFlow::waitForDismissOrTimeout(unsigned long timeoutMs) {
  if (!input_ || !ui_) return;

  dismissRequested_ = false;
  const unsigned long untilMs = millis() + timeoutMs;
  while (!dismissRequested_ && millis() < untilMs) {
    input_->update(onInputEvent, this);
    if (ui_->model().lastMidiNote >= 0) {
      dismissRequested_ = true;
      break;
    }
    delay(5);
  }
}

void BootScreenFlow::onInputEvent(const Input::Event & /*event*/, void *context) {
  auto *self = static_cast<BootScreenFlow *>(context);
  self->dismissRequested_ = true;
}
