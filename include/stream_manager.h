#pragma once

#include <Arduino.h>
#include <SD.h>
#include <atomic>
#include <stdint.h>

#include "AudioFileSource.h"

namespace SampleLibrary { struct Catalog; }

// Streams SD samples through per-stream ring buffers. A reader task performs
// all SD access (open, seek, read, close); the audio task only copies
// buffered PCM and never waits for the card. Without a reader task (host
// tests), Audio::update() services streams synchronously before decoding.
class StreamManager {
 public:
  // SD streams that can play at once. At the 4 MHz fallback clock the card
  // sustains only a few, but retriggers briefly hold two streams per sample.
  static constexpr uint8_t kMaxStreams = 16;
  static constexpr uint32_t kRingBytes = 16 * 1024;  // ~185 ms of mono PCM16.

  struct Diagnostics {
    uint32_t readCount = 0;
    uint32_t bytesRead = 0;
    uint32_t maxReadUs = 0;
    uint32_t maxReadBytes = 0;
    uint32_t openFailureCount = 0;
    uint32_t noFreeStreamCount = 0;
    uint32_t starvedUpdateCount = 0;
  };

  // Decoder-facing view of one stream: a canonical 44-byte WAV header from
  // memory, then an optional preloaded head of the PCM from RAM, then PCM
  // from the ring. Owned by StreamManager, borrowed by one voice from
  // openStream() until release().
  class SdStream : public AudioFileSource {
   public:
    uint32_t read(void *data, uint32_t len) override;
    bool seek(int32_t pos, int dir) override;
    // The decoder closes its source at EOF. Keep the stream: a loop restart
    // continues from PCM the reader has already buffered.
    bool close() override { return true; }
    bool isOpen() override { return true; }
    uint32_t getSize() override { return kHeaderBytes + dataBytes_; }
    uint32_t getPos() override { return pos_; }

    // True when one voice update can decode without running dry.
    bool ready() const;
    bool failed() const { return failed_.load(); }
    // Next loop iteration: header again, PCM continues from the ring.
    void rewind() { pos_ = 0; }
    void setLoop(bool loop) { loop_.store(loop); }
    void release();

   private:
    friend class StreamManager;
    enum class State : uint8_t { Free, OpenRequested, Streaming, CloseRequested };
    static constexpr uint32_t kHeaderBytes = 44;

    // Written by the audio task before OpenRequested, then read-only.
    char path_[128] = {0};
    uint32_t fileDataOffset_ = 0;
    uint32_t dataBytes_ = 0;
    const uint8_t *head_ = nullptr;  // First headBytes_ of PCM, in RAM.
    uint32_t headBytes_ = 0;
    uint8_t header_[kHeaderBytes] = {0};
    StreamManager *owner_ = nullptr;
    // Shared between tasks.
    std::atomic<State> state_{State::Free};
    std::atomic<bool> loop_{false};
    std::atomic<bool> failed_{false};
    std::atomic<uint32_t> written_{0};   // Advanced by the reader.
    std::atomic<uint32_t> consumed_{0};  // Advanced by the audio task.
    uint8_t *ring_ = nullptr;
    // Audio task only: position in the virtual WAV file.
    uint32_t pos_ = 0;
    // Reader task only.
    File file_;
    uint32_t readerDataPos_ = 0;
  };

  StreamManager() = default;
  ~StreamManager();

  bool begin(const SampleLibrary::Catalog *catalog);
  void shutdown();
  // Moves SD access to a task on another core. Before this, serviceAll()
  // must be called by whoever reads the streams.
  bool startReaderTask(uint8_t priority, int core);
  bool hasReaderTask() const { return readerTask_ != nullptr; }

  // Claims a stream for a validated catalog sample; the reader opens it.
  // With a preloaded head, the reader starts after it and playback can
  // begin before any SD data arrives; loops replay the head from RAM.
  SdStream *openStream(const char *path,
                       bool loop,
                       const uint8_t *head = nullptr,
                       uint32_t headBytes = 0);
  // Performs all pending reader work (host tests / no reader task).
  void serviceAll();
  void noteStarvedUpdate() { diagnostics_.starvedUpdateCount++; }

  const Diagnostics &diagnostics() const { return diagnostics_; }

 private:
  static void readerTaskEntry(void *context);
  void runReader();
  // One unit of reader work; false when nothing needed doing.
  bool serviceOnce();
  bool openFile(SdStream &stream);
  bool fillOnce();
  void closeFile(SdStream &stream);

  SdStream streams_[kMaxStreams];
  uint8_t *ringMemory_ = nullptr;
  const SampleLibrary::Catalog *catalog_ = nullptr;
  void *readerTask_ = nullptr;
  Diagnostics diagnostics_;
};
