#include <Arduino.h>

#include "codec_es8388.h"
#include "input_keys.h"
#include "sampler_audio.h"
#include "storage_sd.h"

namespace {

SamplerAudio gAudio;
InputKeys gKeys;

void onKeyPressed(int keyIndex, void *context) {
  auto *audio = static_cast<SamplerAudio *>(context);
  audio->playSample(keyIndex + 1);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  gKeys.begin();

  if (!StorageSD::init()) {
    while (true) delay(1000);
  }

  if (!CodecES8388::init()) {
    Serial.println("Codec init failed");
  } else {
    Serial.println("Codec OK");
  }

  gAudio.begin();
  Serial.println("Sampler ready");
}

void loop() {
  gAudio.update();
  gKeys.update(onKeyPressed, &gAudio);
}
