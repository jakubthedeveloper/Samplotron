#include "sampler_audio.h"

#include <Arduino.h>
#include "driver/i2s.h"

#include "AudioFileSourceSD.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"

#include "pins.h"

SamplerAudio::~SamplerAudio() {
  if (wav_ && wav_->isRunning()) {
    wav_->stop();
  }
  if (file_) {
    file_->close();
    delete file_;
    file_ = nullptr;
  }
  delete wav_;
  wav_ = nullptr;
  delete out_;
  out_ = nullptr;
  resetI2SIfNeeded();
}

void SamplerAudio::begin() {
  out_ = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_ENABLE);
  out_->SetPinout(Pins::I2S_BCLK, Pins::I2S_LRC, Pins::I2S_DOUT);
  out_->SetGain(1.0f);

  wav_ = new AudioGeneratorWAV();
}

void SamplerAudio::update() {
  if (wav_ && wav_->isRunning()) {
    if (!wav_->loop()) {
      wav_->stop();
      Serial.println("WAV finished");
    }
  }
}

void SamplerAudio::playSample(int sampleNumber) {
  String path = "/samples/test" + String(sampleNumber) + ".wav";
  Serial.println(path);

  if (wav_ && wav_->isRunning()) {
    wav_->stop();
  }

  if (file_) {
    file_->close();
    delete file_;
    file_ = nullptr;
  }

  file_ = new AudioFileSourceSD(path.c_str());
  if (wav_ && file_ && file_->isOpen()) {
    resetI2SIfNeeded();
    if (!wav_->begin(file_, out_)) {
      Serial.println("WAV start failed");
    } else {
      Serial.println("WAV started");
      i2sWasStarted_ = true;
    }
  } else {
    Serial.println("Sample open failed");
  }
}

void SamplerAudio::resetI2SIfNeeded() {
  if (!i2sWasStarted_) return;

  esp_err_t err = i2s_driver_uninstall((i2s_port_t)0);
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    i2sWasStarted_ = false;
  } else {
    Serial.printf("I2S uninstall error: %d\n", (int)err);
  }
}
