#include "audio_internal.h"

#include <math.h>
#include <string.h>

namespace AudioInternal {

bool AudioFileSourceRamWav::open(const uint8_t *pcmData,
                                 uint32_t pcmBytes,
                                 uint16_t channelCount,
                                 uint32_t sampleRate,
                                 uint16_t bitsPerSample) {
  if (!pcmData || pcmBytes == 0 || channelCount == 0 || sampleRate == 0 || bitsPerSample == 0) {
    return false;
  }

  const uint16_t blockAlign = static_cast<uint16_t>(channelCount * (bitsPerSample / 8U));
  if (blockAlign == 0) return false;

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

uint32_t AudioFileSourceRamWav::read(void *data, uint32_t len) {
  if (!open_ || !data || len == 0) return 0;

  const uint32_t total = getSize();
  if (pos_ >= total) return 0;

  uint8_t *out = static_cast<uint8_t *>(data);
  uint32_t remaining = len;
  uint32_t copied = 0;

  while (remaining > 0 && pos_ < total) {
    if (pos_ < kHeaderBytes) {
      const uint32_t headerOffset = pos_;
      const uint32_t available = kHeaderBytes - headerOffset;
      const uint32_t chunk = (remaining < available) ? remaining : available;
      memcpy(out + copied, header_ + headerOffset, chunk);
      pos_ += chunk;
      copied += chunk;
      remaining -= chunk;
    } else {
      const uint32_t dataOffset = pos_ - kHeaderBytes;
      const uint32_t available = dataBytes_ - dataOffset;
      const uint32_t chunk = (remaining < available) ? remaining : available;
      memcpy(out + copied, data_ + dataOffset, chunk);
      pos_ += chunk;
      copied += chunk;
      remaining -= chunk;
    }
  }

  return copied;
}

bool AudioFileSourceRamWav::seek(int32_t pos, int dir) {
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

bool AudioFileSourceRamWav::close() {
  data_ = nullptr;
  dataBytes_ = 0;
  pos_ = 0;
  open_ = false;
  return true;
}

bool AudioFileSourceRamWav::isOpen() { return open_; }

uint32_t AudioFileSourceRamWav::getSize() { return kHeaderBytes + dataBytes_; }

uint32_t AudioFileSourceRamWav::getPos() { return pos_; }

void AudioFileSourceRamWav::writeLe16(uint8_t *dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFU);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void AudioFileSourceRamWav::writeLe32(uint8_t *dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFU);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

bool FreshStartAudioGeneratorWAV::begin(AudioFileSource *source, AudioOutput *output) {
  // AudioGenerator keeps lastSample between runs; clear it to avoid start transient on retrigger.
  lastSample[0] = 0;
  lastSample[1] = 0;
  return AudioGeneratorWAV::begin(source, output);
}

bool FreshStartAudioGeneratorWAV::stop() {
  const bool ok = AudioGeneratorWAV::stop();
  lastSample[0] = 0;
  lastSample[1] = 0;
  return ok;
}

StableAudioOutputI2S::StableAudioOutputI2S(int port, int outputMode, int dmaCount, int useApll)
    : AudioOutputI2S(port, outputMode, dmaCount, useApll) {}

bool StableAudioOutputI2S::SetRate(int hz) {
  rateSetCalls_++;
  if (lastRateHz_ == hz) {
    skippedRateSetCalls_++;
    return true;
  }
  lastRateHz_ = hz;
  appliedRateSetCalls_++;
  return AudioOutputI2S::SetRate(hz);
}

uint32_t StableAudioOutputI2S::rateSetCalls() const { return rateSetCalls_; }

uint32_t StableAudioOutputI2S::skippedRateSetCalls() const { return skippedRateSetCalls_; }

uint32_t StableAudioOutputI2S::appliedRateSetCalls() const { return appliedRateSetCalls_; }

WaveformAudioOutput::WaveformAudioOutput(AudioOutput *sink,
                                                   WaveformCaptureState *waveformCapture)
    : sink_(sink), waveformCapture_(waveformCapture) {}

bool WaveformAudioOutput::SetRate(int hz) {
  return sink_ && sink_->SetRate(hz);
}

bool WaveformAudioOutput::SetChannels(int channels) {
  return sink_ && sink_->SetChannels(channels);
}

bool WaveformAudioOutput::begin() { return sink_ && sink_->begin(); }

bool WaveformAudioOutput::stop() {
  return sink_ && sink_->stop();
}

bool WaveformAudioOutput::loop() { return sink_ && sink_->loop(); }

bool WaveformAudioOutput::ConsumeSample(int16_t sample[2]) {
  if (!sink_) return false;
  int16_t output[2] = {sample[0], sample[1]};
  if (!sink_->ConsumeSample(output)) return false;
  captureWaveformSample(sample);
  return true;
}

void WaveformAudioOutput::captureWaveformSample(const int16_t sample[2]) {
  if (!waveformCapture_) return;

  waveformCapture_->decimationCounter++;
  if (waveformCapture_->decimationCounter < kWaveformDecimation) {
    return;
  }
  waveformCapture_->decimationCounter = 0;

  const int32_t mono = (static_cast<int32_t>(sample[0]) + static_cast<int32_t>(sample[1])) / 2;
  int32_t quantized = mono / 256;
  if (quantized > 127) quantized = 127;
  if (quantized < -128) quantized = -128;

  portENTER_CRITICAL(&waveformCapture_->lock);
  waveformCapture_->ring[waveformCapture_->head] = static_cast<int8_t>(quantized);
  waveformCapture_->head = (waveformCapture_->head + 1U) % Audio::kWaveformPointCount;
  if (waveformCapture_->count < Audio::kWaveformPointCount) {
    waveformCapture_->count++;
  }
  portEXIT_CRITICAL(&waveformCapture_->lock);
}

bool copyWaveformSnapshot(const WaveformCaptureState &capture, Audio::WaveformSnapshot &snapshot) {
  snapshot.validPoints = 0;

  portENTER_CRITICAL(&capture.lock);
  const uint16_t count = capture.count;
  const uint16_t head = capture.head;
  const uint16_t start = (count == Audio::kWaveformPointCount) ? head : 0;
  for (uint16_t i = 0; i < count; i++) {
    const uint16_t index = (start + i) % Audio::kWaveformPointCount;
    snapshot.points[i] = capture.ring[index];
  }
  portEXIT_CRITICAL(&capture.lock);

  snapshot.validPoints = count;
  return count > 0;
}

}  // namespace AudioInternal
