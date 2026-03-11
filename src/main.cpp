#include <Arduino.h>

#include "sampler_app.h"

namespace {

SamplerApp gSamplerApp;

}  // namespace

void setup() {
  gSamplerApp.setup();
}

void loop() {
  gSamplerApp.loop();
}
