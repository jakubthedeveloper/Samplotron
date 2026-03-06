#include <Arduino.h>
#include <SD.h>

#include "codec_es8388.h"
#include "display_ssd1309.h"
#include "input.h"
#include "pins.h"
#include "sampler_audio.h"
#include "storage_sd.h"

namespace {

SamplerAudio gAudio;
Input gKeys;
DisplaySsd1309 gDisplay;

struct AppContext {
  SamplerAudio *audio;
  DisplaySsd1309 *display;
};

AppContext gContext = {
    &gAudio,
    &gDisplay,
};

constexpr int kMaxSamples = 32;
String gSamplePaths[kMaxSamples];
String gSampleNames[kMaxSamples];
int gSampleCount = 0;
int gCurrentSampleIndex = 0;

bool isWavFile(const String &name) {
  return name.endsWith(".wav") || name.endsWith(".WAV");
}

void sortSamplesByName() {
  for (int i = 0; i < gSampleCount - 1; i++) {
    for (int j = i + 1; j < gSampleCount; j++) {
      if (gSampleNames[j] < gSampleNames[i]) {
        String n = gSampleNames[i];
        String p = gSamplePaths[i];
        gSampleNames[i] = gSampleNames[j];
        gSamplePaths[i] = gSamplePaths[j];
        gSampleNames[j] = n;
        gSamplePaths[j] = p;
      }
    }
  }
}

void loadSamplesFromSd() {
  gSampleCount = 0;
  gCurrentSampleIndex = 0;

  File dir = SD.open("/samples");
  if (!dir || !dir.isDirectory()) {
    Serial.println("Missing /samples directory");
    return;
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      String entryName = entry.name();
      int slash = entryName.lastIndexOf('/');
      String fileName = (slash >= 0) ? entryName.substring(slash + 1) : entryName;
      if (isWavFile(fileName) && gSampleCount < kMaxSamples) {
        gSamplePaths[gSampleCount] = "/samples/" + fileName;
        gSampleNames[gSampleCount] = fileName;
        gSampleCount++;
      }
    }
    entry.close();
  }
  dir.close();

  sortSamplesByName();
  Serial.printf("Found %d sample(s)\n", gSampleCount);
}

void onKeyPressed(int keyIndex, void *context) {
  auto *app = static_cast<AppContext *>(context);
  if (gSampleCount <= 0) return;

  if (keyIndex == 0) {  // Encoder switch: play current sample
    Serial.printf("PLAY sample: %s\n", gSampleNames[gCurrentSampleIndex].c_str());
    app->audio->playSamplePath(gSamplePaths[gCurrentSampleIndex]);
  } else if (keyIndex == 1) {  // Encoder CW: select next sample
    gCurrentSampleIndex = (gCurrentSampleIndex + 1) % gSampleCount;
    app->display->setSampleSelection(
        gCurrentSampleIndex + 1, gSampleCount, gSampleNames[gCurrentSampleIndex]);
    Serial.printf("Selected sample: %s\n", gSampleNames[gCurrentSampleIndex].c_str());
  } else if (keyIndex == 2) {  // Encoder CCW: select previous sample
    gCurrentSampleIndex = (gCurrentSampleIndex + gSampleCount - 1) % gSampleCount;
    app->display->setSampleSelection(
        gCurrentSampleIndex + 1, gSampleCount, gSampleNames[gCurrentSampleIndex]);
    Serial.printf("Selected sample: %s\n", gSampleNames[gCurrentSampleIndex].c_str());
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Booting Samplotron...");

  const bool sdReady = StorageSD::init();
  if (sdReady) {
    loadSamplesFromSd();
  } else {
    Serial.println("Continuing without SD (input/display debug still active).");
    gSampleCount = 0;
    gCurrentSampleIndex = 0;
  }

  if (!CodecES8388::init()) {
    Serial.println("Codec init failed");
  } else {
    Serial.println("Codec OK");
  }

  if (!gDisplay.begin()) {
    Serial.println("Display init failed");
  } else {
    Serial.println("Display OK");
  }
  if (gSampleCount > 0) {
    gDisplay.setSampleSelection(
        gCurrentSampleIndex + 1, gSampleCount, gSampleNames[gCurrentSampleIndex]);
  } else {
    gDisplay.setSampleSelection(0, 0, "no samples");
  }

  gKeys.begin();
  gAudio.begin();
  Serial.println("Sampler ready");
}

void loop() {
  gAudio.update();
  gKeys.update(onKeyPressed, &gContext);
  gDisplay.update();
}
