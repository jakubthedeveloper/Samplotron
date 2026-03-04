#include <Arduino.h>

#include "codec_es8388.h"
#include "display_ssd1309.h"
#include "input_keys.h"
#include "sampler_audio.h"
#include "storage_sd.h"

namespace {

SamplerAudio gAudio;
InputKeys gKeys;
DisplaySsd1309 gDisplay;

struct AppContext {
  SamplerAudio *audio;
  DisplaySsd1309 *display;
};

AppContext gContext = {
    &gAudio,
    &gDisplay,
};

void onKeyPressed(int keyIndex, void *context) {
  auto *app = static_cast<AppContext *>(context);
  int sampleNumber = keyIndex + 1;
  app->audio->playSample(sampleNumber);
  app->display->setLastSample(sampleNumber);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  gKeys.begin();

  bool sdOk = StorageSD::init();
  if (!sdOk) {
    while (true) delay(1000);
  }

  bool codecOk = CodecES8388::init();
  if (!codecOk) {
    Serial.println("Codec init failed");
  } else {
    Serial.println("Codec OK");
  }

  if (!gDisplay.begin()) {
    Serial.println("Display init failed");
  } else {
    Serial.println("Display OK");
  }
  gDisplay.setBootStatus(sdOk, codecOk);

  gAudio.begin();
  Serial.println("Sampler ready");
}

void loop() {
  gAudio.update();
  gKeys.update(onKeyPressed, &gContext);
  gDisplay.update();
}
