#pragma once
#include "AudioFileSource.h"
#include "wav_validation.h"

// Presents cached, validated PCM as a canonical WAV without rereading its header.
// The underlying file is opened/owned by the stream manager.
class ValidatedWavSource : public AudioFileSource {
 public:
  bool attach(AudioFileSource *source, const WavValidation::Result &info);
  uint32_t read(void *data, uint32_t len) override;
  bool seek(int32_t pos, int dir) override;
  bool close() override;
  bool isOpen() override;
  uint32_t getSize() override { return size_; }
  uint32_t getPos() override { return pos_; }
 private:
  AudioFileSource *source_ = nullptr;
  uint8_t header_[44] = {};
  uint32_t offset_ = 0, size_ = 0, pos_ = 0;
};
