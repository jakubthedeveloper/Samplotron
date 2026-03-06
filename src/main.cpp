#include <Arduino.h>
#include <SD.h>

#include "codec_es8388.h"
#include "display_ssd1309.h"
#include "input.h"
#include "pins.h"
#include "audio.h"
#include "ui.h"
#include "storage_sd.h"

namespace {

Audio gAudio;
Input gInput;
Ui gUi;
DisplaySsd1309 gDisplay;

constexpr int kMaxSamples = Ui::kMaxSamples;
String gSamplePaths[kMaxSamples];
String gSampleNames[kMaxSamples];
int gSampleCount = 0;

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

void onPreviewSample(int sampleIndex, void * /*context*/) {
  if (sampleIndex < 0 || sampleIndex >= gSampleCount) return;
  Serial.printf("PLAY sample: %s\n", gSampleNames[sampleIndex].c_str());
  gAudio.playSamplePath(gSamplePaths[sampleIndex]);
}

void onInputEvent(const Input::Event &event, void * /*context*/) {
  Ui::Event uiEvent;
  uiEvent.value = event.value;

  switch (event.type) {
    case Input::EventType::LeftRotate:
      uiEvent.type = Ui::EventType::LeftRotate;
      break;
    case Input::EventType::LeftClick:
      uiEvent.type = Ui::EventType::LeftClick;
      break;
    case Input::EventType::RightRotate:
      uiEvent.type = Ui::EventType::RightRotate;
      break;
    case Input::EventType::RightClick:
      uiEvent.type = Ui::EventType::RightClick;
      break;
    case Input::EventType::RightLongPress:
      uiEvent.type = Ui::EventType::RightLongPress;
      break;
  }

  gUi.handleEvent(uiEvent);
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

  gUi.begin(gSampleNames, gSamplePaths, gSampleCount);
  gUi.setPreviewCallback(onPreviewSample, nullptr);
  gDisplay.renderUi(gUi);

  gInput.begin();
  gAudio.begin();
  Serial.println("Sampler ready");
}

void loop() {
  gAudio.update();
  gInput.update(onInputEvent, nullptr);
  gUi.update();
  gDisplay.update();
}
