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

BudgetedAudioOutput::BudgetedAudioOutput(AudioOutput *sink) : sink_(sink) {}

void BudgetedAudioOutput::resetBudget(uint16_t sampleCount) { budgetSamples_ = sampleCount; }

void BudgetedAudioOutput::resetFadeEnvelope() {
  fadeEnvelopeQ15_ = 32768;
  fadeStepQ15_ = 0;
  fadeSamplesRemaining_ = 0;
  fadeActive_ = false;
  fadeComplete_ = false;
}

void BudgetedAudioOutput::beginFadeOut(uint32_t fadeOutUs) {
  if (fadeOutUs == 0) {
    fadeEnvelopeQ15_ = 0;
    fadeStepQ15_ = 0;
    fadeSamplesRemaining_ = 0;
    fadeActive_ = false;
    fadeComplete_ = true;
    return;
  }

  if (sampleRateHz_ <= 0) {
    sampleRateHz_ = 44100;
  }

  uint64_t fadeSamples =
      (static_cast<uint64_t>(sampleRateHz_) * static_cast<uint64_t>(fadeOutUs) + 999999ULL) /
      1000000ULL;
  if (fadeSamples == 0) {
    fadeSamples = 1;
  }

  fadeSamplesRemaining_ = static_cast<uint32_t>(fadeSamples);
  fadeStepQ15_ = (fadeSamplesRemaining_ > 0) ? (fadeEnvelopeQ15_ / fadeSamplesRemaining_) : fadeEnvelopeQ15_;
  if (fadeStepQ15_ == 0 && fadeEnvelopeQ15_ > 0) {
    fadeStepQ15_ = 1;
  }
  fadeActive_ = true;
  fadeComplete_ = false;
}

bool BudgetedAudioOutput::isFadeOutComplete() const { return fadeComplete_; }

bool BudgetedAudioOutput::SetRate(int hz) {
  if (hz > 0) {
    sampleRateHz_ = hz;
  }
  return sink_ && sink_->SetRate(hz);
}

bool BudgetedAudioOutput::SetChannels(int channels) { return sink_ && sink_->SetChannels(channels); }

bool BudgetedAudioOutput::begin() { return sink_ && sink_->begin(); }

bool BudgetedAudioOutput::stop() {
  budgetSamples_ = 0;
  resetFadeEnvelope();
  return sink_ && sink_->stop();
}

bool BudgetedAudioOutput::loop() { return sink_ && sink_->loop(); }

bool BudgetedAudioOutput::ConsumeSample(int16_t sample[2]) {
  if (!sink_ || budgetSamples_ == 0) return false;

  uint32_t envelopeQ15 = fadeEnvelopeQ15_;
  if (fadeActive_) {
    envelopeQ15 = fadeEnvelopeQ15_;
    if (fadeSamplesRemaining_ > 0) {
      if (fadeEnvelopeQ15_ > fadeStepQ15_) {
        fadeEnvelopeQ15_ -= fadeStepQ15_;
      } else {
        fadeEnvelopeQ15_ = 0;
      }
      fadeSamplesRemaining_--;
    }
    if (fadeSamplesRemaining_ == 0) {
      fadeEnvelopeQ15_ = 0;
      fadeStepQ15_ = 0;
      fadeActive_ = false;
      fadeComplete_ = true;
    }
  }

  if (envelopeQ15 < 32768U) {
    const int32_t left = (static_cast<int32_t>(sample[0]) * static_cast<int32_t>(envelopeQ15) + 16384) >> 15;
    const int32_t right = (static_cast<int32_t>(sample[1]) * static_cast<int32_t>(envelopeQ15) + 16384) >> 15;
    sample[0] = static_cast<int16_t>(left);
    sample[1] = static_cast<int16_t>(right);
  }

  budgetSamples_--;
  return sink_->ConsumeSample(sample);
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

SimpleLimiterAudioOutput::SimpleLimiterAudioOutput(AudioOutput *sink,
                                                   WaveformCaptureState *waveformCapture)
    : sink_(sink), waveformCapture_(waveformCapture) {
  updateSmoothing();
}

bool SimpleLimiterAudioOutput::SetRate(int hz) {
  sampleRateHz_ = (hz > 0) ? hz : sampleRateHz_;
  updateSmoothing();
  return sink_ && sink_->SetRate(hz);
}

bool SimpleLimiterAudioOutput::SetChannels(int channels) {
  return sink_ && sink_->SetChannels(channels);
}

bool SimpleLimiterAudioOutput::begin() { return sink_ && sink_->begin(); }

bool SimpleLimiterAudioOutput::stop() {
  limiterGain_ = 1.0f;
  return sink_ && sink_->stop();
}

bool SimpleLimiterAudioOutput::loop() { return sink_ && sink_->loop(); }

bool SimpleLimiterAudioOutput::ConsumeSample(int16_t sample[2]) {
  if (!sink_) return false;

  float left = static_cast<float>(sample[0]) / 32768.0f;
  float right = static_cast<float>(sample[1]) / 32768.0f;

  // Recover perceived loudness after conservative per-voice headroom.
  left *= kLimiterMakeupGain;
  right *= kLimiterMakeupGain;

  float peak = fabsf(left);
  const float rightAbs = fabsf(right);
  if (rightAbs > peak) peak = rightAbs;

  float desiredGain = 1.0f;
  if (peak > kLimiterThreshold && peak > 0.0f) {
    desiredGain = kLimiterThreshold / peak;
  }

  if (desiredGain < limiterGain_) {
    // Clamp immediately to avoid transient clipping distortion.
    limiterGain_ = desiredGain;
  } else {
    limiterGain_ = desiredGain + (releaseCoeff_ * (limiterGain_ - desiredGain));
  }

  left *= limiterGain_;
  right *= limiterGain_;

  if (left > 1.0f) left = 1.0f;
  if (left < -1.0f) left = -1.0f;
  if (right > 1.0f) right = 1.0f;
  if (right < -1.0f) right = -1.0f;

  sample[0] = static_cast<int16_t>(left * 32767.0f);
  sample[1] = static_cast<int16_t>(right * 32767.0f);
  captureWaveformSample(sample);
  return sink_->ConsumeSample(sample);
}

void SimpleLimiterAudioOutput::captureWaveformSample(const int16_t sample[2]) {
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

void SimpleLimiterAudioOutput::updateSmoothing() {
  const float sampleRate = static_cast<float>((sampleRateHz_ > 0) ? sampleRateHz_ : 44100);
  releaseCoeff_ = expf(-1.0f / (sampleRate * kLimiterReleaseSeconds));
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
