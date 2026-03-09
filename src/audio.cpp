#include "audio.h"

#include <Arduino.h>
#include <string.h>
#include "driver/i2s.h"

#include "AudioFileSourceSD.h"
#include "AudioFileSource.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"

#include "pins.h"

namespace {

class AudioFileSourceRamWav : public AudioFileSource {
 public:
  AudioFileSourceRamWav() = default;

  bool open(const uint8_t *pcmData,
            uint32_t pcmBytes,
            uint16_t channelCount,
            uint32_t sampleRate,
            uint16_t bitsPerSample) {
    if (!pcmData || pcmBytes == 0 || channelCount == 0 || sampleRate == 0 || bitsPerSample == 0) {
      return false;
    }

    const uint16_t blockAlign = static_cast<uint16_t>(channelCount * (bitsPerSample / 8U));
    if (blockAlign == 0) {
      return false;
    }
    const uint32_t byteRate = sampleRate * static_cast<uint32_t>(blockAlign);
    const uint32_t riffSize = 36U + pcmBytes;

    memset(header_, 0, sizeof(header_));
    memcpy(&header_[0], "RIFF", 4);
    writeLe32(&header_[4], riffSize);
    memcpy(&header_[8], "WAVE", 4);
    memcpy(&header_[12], "fmt ", 4);
    writeLe32(&header_[16], 16U);
    writeLe16(&header_[20], 1U);
    writeLe16(&header_[22], channelCount);
    writeLe32(&header_[24], sampleRate);
    writeLe32(&header_[28], byteRate);
    writeLe16(&header_[32], blockAlign);
    writeLe16(&header_[34], bitsPerSample);
    memcpy(&header_[36], "data", 4);
    writeLe32(&header_[40], pcmBytes);

    data_ = pcmData;
    dataBytes_ = pcmBytes;
    pos_ = 0;
    open_ = true;
    return true;
  }

  uint32_t read(void *data, uint32_t len) override {
    if (!open_ || !data || len == 0) return 0;
    const uint32_t total = getSize();
    if (pos_ >= total) return 0;

    uint8_t *out = static_cast<uint8_t *>(data);
    uint32_t remaining = len;
    uint32_t copied = 0;
    while (remaining > 0 && pos_ < total) {
      if (pos_ < kHeaderBytes) {
        const uint32_t headerOffset = pos_;
        const uint32_t avail = kHeaderBytes - headerOffset;
        const uint32_t chunk = (remaining < avail) ? remaining : avail;
        memcpy(out + copied, header_ + headerOffset, chunk);
        pos_ += chunk;
        copied += chunk;
        remaining -= chunk;
      } else {
        const uint32_t dataOffset = pos_ - kHeaderBytes;
        const uint32_t avail = dataBytes_ - dataOffset;
        const uint32_t chunk = (remaining < avail) ? remaining : avail;
        memcpy(out + copied, data_ + dataOffset, chunk);
        pos_ += chunk;
        copied += chunk;
        remaining -= chunk;
      }
    }
    return copied;
  }

  bool seek(int32_t pos, int dir) override {
    if (!open_) return false;
    int64_t target = 0;
    if (dir == SEEK_SET) {
      target = pos;
    } else if (dir == SEEK_CUR) {
      target = static_cast<int64_t>(pos_) + pos;
    } else if (dir == SEEK_END) {
      target = static_cast<int64_t>(getSize()) + pos;
    } else {
      return false;
    }
    if (target < 0 || static_cast<uint64_t>(target) > static_cast<uint64_t>(getSize())) {
      return false;
    }
    pos_ = static_cast<uint32_t>(target);
    return true;
  }

  bool close() override {
    data_ = nullptr;
    dataBytes_ = 0;
    pos_ = 0;
    open_ = false;
    return true;
  }

  bool isOpen() override { return open_; }
  uint32_t getSize() override { return kHeaderBytes + dataBytes_; }
  uint32_t getPos() override { return pos_; }

 private:
  static constexpr uint32_t kHeaderBytes = 44U;

  static void writeLe16(uint8_t *dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFFU);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
  }

  static void writeLe32(uint8_t *dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFFU);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
  }

  uint8_t header_[kHeaderBytes] = {0};
  const uint8_t *data_ = nullptr;
  uint32_t dataBytes_ = 0;
  uint32_t pos_ = 0;
  bool open_ = false;
};

AudioFileSourceSD gSdSource;
AudioFileSourceRamWav gRamSource;

}  // namespace

Audio::~Audio() {
  if (wav_ && wav_->isRunning()) {
    wav_->stop();
  }
  if (file_) {
    file_->close();
    file_ = nullptr;
  }
  delete wav_;
  wav_ = nullptr;
  delete out_;
  out_ = nullptr;
  resetI2SIfNeeded();
}

void Audio::begin() {
  out_ = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_ENABLE);
  out_->SetPinout(Pins::I2S_BCLK, Pins::I2S_LRC, Pins::I2S_DOUT);
  out_->SetGain(1.0f);

  wav_ = new AudioGeneratorWAV();
}

void Audio::update() {
  if (wav_ && wav_->isRunning()) {
    if (!wav_->loop()) {
      wav_->stop();
      Serial.println("WAV finished");
    }
  }
}

void Audio::playSamplePath(const String &samplePath, uint8_t volume) {
  Serial.println(samplePath);

  if (wav_ && wav_->isRunning()) {
    wav_->stop();
  }

  if (file_) {
    file_->close();
  }

  file_ = &gSdSource;
  if (file_->open(samplePath.c_str()) && wav_ && file_->isOpen()) {
    applyVolume(volume);
    resetI2SIfNeeded();
    if (!wav_->begin(file_, out_)) {
      Serial.println("WAV start failed");
      file_->close();
      file_ = nullptr;
    } else {
      Serial.println("WAV started");
      i2sWasStarted_ = true;
    }
  } else {
    Serial.println("Sample open failed");
  }
}

bool Audio::playSampleRam(const uint8_t *pcmData,
                          uint32_t dataBytes,
                          uint16_t channelCount,
                          uint32_t sampleRate,
                          uint16_t bitsPerSample,
                          uint8_t volume) {
  if (!pcmData || dataBytes == 0) return false;

  if (wav_ && wav_->isRunning()) {
    wav_->stop();
  }

  if (file_) {
    file_->close();
  }

  file_ = &gRamSource;
  if (!gRamSource.open(pcmData, dataBytes, channelCount, sampleRate, bitsPerSample)) {
    file_ = nullptr;
    return false;
  }

  if (wav_ && file_->isOpen()) {
    applyVolume(volume);
    resetI2SIfNeeded();
    if (!wav_->begin(file_, out_)) {
      Serial.println("RAM WAV start failed");
      file_->close();
      file_ = nullptr;
      return false;
    }
    Serial.println("RAM WAV started");
    i2sWasStarted_ = true;
    return true;
  }

  return false;
}

void Audio::applyVolume(uint8_t volume) {
  if (!out_) return;
  const float gain = static_cast<float>(volume) / 127.0f;
  out_->SetGain(gain);
}

void Audio::resetI2SIfNeeded() {
  if (!i2sWasStarted_) return;

  esp_err_t err = i2s_driver_uninstall((i2s_port_t)0);
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    i2sWasStarted_ = false;
  } else {
    Serial.printf("I2S uninstall error: %d\n", (int)err);
  }
}
