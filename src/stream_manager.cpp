#include "stream_manager.h"

#include "sample_library.h"
#include "wav_validation.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP32
#include <esp_heap_caps.h>
#endif

namespace {

// A voice update decodes up to kVoiceLoopSampleBudget frames, which is at
// most two 128-byte decoder reads.
constexpr uint32_t kReadyBytes = 256;
// Small first reads start playback quickly; larger ones amortize SD command
// overhead once a stream has some margin.
constexpr uint32_t kUrgentBufferedBytes = 4 * 1024;
constexpr uint32_t kUrgentReadBytes = 2 * 1024;
constexpr uint32_t kReadBytes = 8 * 1024;
constexpr uint32_t kMinReadBytes = 2 * 1024;
// SPI SD transfers busy-wait; yield so lower-priority tasks and IDLE run.
constexpr uint32_t kReaderBusySliceMs = 10;
constexpr uint32_t kReaderIdleWaitMs = 5;
// ESP-IDF task stacks are sized in bytes; FATFS keeps long names on the stack.
constexpr uint32_t kReaderStackBytes = 8192;

uint32_t minU32(uint32_t a, uint32_t b) { return a < b ? a : b; }

}  // namespace

uint32_t StreamManager::SdStream::read(void *data, uint32_t len) {
  if (!data || len == 0) return 0;
  auto *out = static_cast<uint8_t *>(data);
  uint32_t copied = 0;
  if (pos_ < kHeaderBytes) {
    copied = minU32(len, kHeaderBytes - pos_);
    memcpy(out, header_ + pos_, copied);
    pos_ += copied;
    if (pos_ < kHeaderBytes) return copied;
  }

  const uint32_t remaining = dataBytes_ - (pos_ - kHeaderBytes);
  const uint32_t consumed = consumed_.load();
  const uint32_t available = written_.load() - consumed;
  uint32_t bytes = minU32(minU32(len - copied, remaining), available);
  uint32_t index = consumed % kRingBytes;
  const uint32_t first = minU32(bytes, kRingBytes - index);
  memcpy(out + copied, ring_ + index, first);
  memcpy(out + copied + first, ring_, bytes - first);
  consumed_.store(consumed + bytes);
  pos_ += bytes;
  return copied + bytes;
}

bool StreamManager::SdStream::seek(int32_t pos, int dir) {
  int64_t target = pos;
  if (dir == SEEK_CUR) target += pos_;
  else if (dir == SEEK_END) target += getSize();
  else if (dir != SEEK_SET) return false;
  if (target == pos_) return true;
  // PCM arrives sequentially; only the in-memory header can be revisited.
  if (target < 0 || target > kHeaderBytes || pos_ > kHeaderBytes) return false;
  pos_ = static_cast<uint32_t>(target);
  return true;
}

bool StreamManager::SdStream::ready() const {
  if (failed_.load()) return false;
  const uint32_t dataPos = pos_ > kHeaderBytes ? pos_ - kHeaderBytes : 0;
  const uint32_t needed = minU32(kReadyBytes, dataBytes_ - dataPos);
  return written_.load() - consumed_.load() >= needed;
}

void StreamManager::SdStream::release() {
  State expected = State::OpenRequested;
  if (!state_.compare_exchange_strong(expected, State::CloseRequested)) {
    expected = State::Streaming;
    if (!state_.compare_exchange_strong(expected, State::CloseRequested)) return;
  }
  if (owner_ && owner_->readerTask_) xTaskNotifyGive(static_cast<TaskHandle_t>(owner_->readerTask_));
}

StreamManager::~StreamManager() { shutdown(); }

bool StreamManager::begin(const SampleLibrary::Catalog *catalog) {
  shutdown();
  catalog_ = catalog;
  diagnostics_ = Diagnostics{};
  const size_t bytes = static_cast<size_t>(kMaxStreams) * kRingBytes;
#ifdef ESP32
  // Allocated before the RAM sample pool, which is sized from what remains.
  ringMemory_ = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
  if (!ringMemory_) ringMemory_ = static_cast<uint8_t *>(malloc(bytes));
  if (!ringMemory_) return false;
  for (uint8_t i = 0; i < kMaxStreams; i++) {
    streams_[i].ring_ = ringMemory_ + static_cast<size_t>(i) * kRingBytes;
    streams_[i].owner_ = this;
  }
  return true;
}

void StreamManager::shutdown() {
  // The reader task is never stopped; shutdown() only runs when the audio
  // engine is destroyed, which the firmware does not do after startup.
  for (SdStream &stream : streams_) {
    closeFile(stream);
    stream.state_.store(SdStream::State::Free);
    stream.ring_ = nullptr;
  }
  free(ringMemory_);
  ringMemory_ = nullptr;
}

bool StreamManager::startReaderTask(uint8_t priority, int core) {
  if (readerTask_ || !ringMemory_) return readerTask_ != nullptr;
  TaskHandle_t handle = nullptr;
  if (xTaskCreatePinnedToCore(readerTaskEntry, "sd_reader", kReaderStackBytes, this, priority,
                              &handle, core) != pdPASS) {
    return false;
  }
  readerTask_ = handle;
  return true;
}

StreamManager::SdStream *StreamManager::openStream(const char *path, bool loop) {
  if (!catalog_ || !ringMemory_ || !path) return nullptr;
  const int index = SampleLibrary::findIndexByPath(*catalog_, path);
  if (!catalog_->playable(index)) return nullptr;
  const WavValidation::Result &info = catalog_->validation[index];
  if (strlen(path) >= sizeof(streams_[0].path_)) return nullptr;
  // Host tests have no reader task: reclaim released streams here.
  if (!readerTask_) serviceAll();

  for (SdStream &stream : streams_) {
    if (stream.state_.load() != SdStream::State::Free) continue;
    strcpy(stream.path_, path);
    stream.fileDataOffset_ = info.dataOffset;
    stream.dataBytes_ = info.dataBytes;
    WavValidation::pcmHeader(stream.header_, info.dataBytes);
    stream.pos_ = 0;
    stream.loop_.store(loop);
    stream.failed_.store(false);
    stream.written_.store(0);
    stream.consumed_.store(0);
    stream.state_.store(SdStream::State::OpenRequested);
    if (readerTask_) xTaskNotifyGive(static_cast<TaskHandle_t>(readerTask_));
    return &stream;
  }
  diagnostics_.noFreeStreamCount++;
  return nullptr;
}

void StreamManager::serviceAll() {
  while (serviceOnce()) {
  }
}

void StreamManager::readerTaskEntry(void *context) {
  static_cast<StreamManager *>(context)->runReader();
}

void StreamManager::runReader() {
  TickType_t busySince = xTaskGetTickCount();
  for (;;) {
    if (serviceOnce()) {
      if (xTaskGetTickCount() - busySince >= pdMS_TO_TICKS(kReaderBusySliceMs)) {
        vTaskDelay(1);
        busySince = xTaskGetTickCount();
      }
      continue;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kReaderIdleWaitMs));
    busySince = xTaskGetTickCount();
  }
}

bool StreamManager::serviceOnce() {
  bool worked = false;
  for (SdStream &stream : streams_) {
    if (stream.state_.load() != SdStream::State::CloseRequested) continue;
    closeFile(stream);
    stream.state_.store(SdStream::State::Free);
    worked = true;
  }
  // Open one file per pass so a burst of triggers cannot starve running streams.
  for (SdStream &stream : streams_) {
    if (stream.state_.load() != SdStream::State::OpenRequested) continue;
    if (!openFile(stream)) {
      stream.failed_.store(true);
      diagnostics_.openFailureCount++;
      closeFile(stream);
    }
    SdStream::State expected = SdStream::State::OpenRequested;
    stream.state_.compare_exchange_strong(expected, SdStream::State::Streaming);
    worked = true;
    break;
  }
  return fillOnce() || worked;
}

bool StreamManager::openFile(SdStream &stream) {
  stream.file_ = SD.open(stream.path_);
  stream.readerDataPos_ = 0;
  if (!stream.file_) return false;
  // The validation cache may be stale if the card changed since boot.
  if (stream.file_.size() < stream.fileDataOffset_ + stream.dataBytes_) return false;
  return stream.file_.seek(stream.fileDataOffset_);
}

void StreamManager::closeFile(SdStream &stream) {
  if (stream.file_) stream.file_.close();
  stream.file_ = File();
}

bool StreamManager::fillOnce() {
  // Serve the stream closest to running dry.
  SdStream *target = nullptr;
  uint32_t targetBuffered = 0;
  bool worked = false;
  for (SdStream &stream : streams_) {
    if (stream.state_.load() != SdStream::State::Streaming || stream.failed_.load()) continue;
    if (stream.readerDataPos_ >= stream.dataBytes_) {
      if (!stream.loop_.load()) continue;
      // Loops continue straight into the next iteration, so a restart
      // finds PCM already buffered.
      if (!stream.file_.seek(stream.fileDataOffset_)) {
        stream.failed_.store(true);
        continue;
      }
      stream.readerDataPos_ = 0;
      worked = true;
    }
    const uint32_t buffered = stream.written_.load() - stream.consumed_.load();
    const uint32_t remaining = stream.dataBytes_ - stream.readerDataPos_;
    if (kRingBytes - buffered < minU32(kMinReadBytes, remaining)) continue;
    if (!target || buffered < targetBuffered) {
      target = &stream;
      targetBuffered = buffered;
    }
  }
  if (!target) return worked;

  const uint32_t written = target->written_.load();
  const uint32_t index = written % kRingBytes;
  uint32_t bytes = targetBuffered < kUrgentBufferedBytes ? kUrgentReadBytes : kReadBytes;
  bytes = minU32(bytes, kRingBytes - targetBuffered);
  bytes = minU32(bytes, kRingBytes - index);
  bytes = minU32(bytes, target->dataBytes_ - target->readerDataPos_);

  const uint32_t startUs = micros();
  const int got = static_cast<int>(target->file_.read(target->ring_ + index, bytes));
  const uint32_t elapsedUs = micros() - startUs;
  diagnostics_.readCount++;
  if (elapsedUs > diagnostics_.maxReadUs) {
    diagnostics_.maxReadUs = elapsedUs;
    diagnostics_.maxReadBytes = bytes;
  }
  if (got <= 0) {
    target->failed_.store(true);
    return true;
  }
  diagnostics_.bytesRead += static_cast<uint32_t>(got);
  target->readerDataPos_ += static_cast<uint32_t>(got);
  target->written_.store(written + static_cast<uint32_t>(got));
  return true;
}
