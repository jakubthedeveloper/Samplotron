#pragma once
#include "AudioOutput.h"
#include <array>
#include <vector>
// Hardware boundary only. Production decoder, voice engine and mixer run unchanged.
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef ESP_OK
#define ESP_OK 0
#endif
using i2s_chan_handle_t = void *;
struct i2s_event_data_t { void *dma_buf; size_t size; };
using i2s_isr_callback_t = bool (*)(i2s_chan_handle_t, i2s_event_data_t *, void *);
struct i2s_event_callbacks_t {
  i2s_isr_callback_t on_recv, on_recv_q_ovf, on_sent, on_send_q_ovf;
};
namespace FakeI2S {
inline int capacity = 0, starts = 0, stops = 0, driverCalls = 0, enabled = 0;
inline std::vector<std::array<int16_t, 2>> frames;
inline i2s_isr_callback_t onSendQueueOverflow = nullptr;
inline void *callbackContext = nullptr;
inline void reset() {
  capacity = starts = stops = driverCalls = enabled = 0;
  frames.clear();
  onSendQueueOverflow = nullptr;
  callbackContext = nullptr;
}
// Simulate the DMA running out of queued blocks.
inline void underrun() {
  i2s_event_data_t event{nullptr, 512};
  if (onSendQueueOverflow) onSendQueueOverflow(nullptr, &event, callbackContext);
}
}
class AudioOutputI2S : public AudioOutput {
 public:
  enum { EXTERNAL_I2S = 0, APLL_ENABLE = 1 };
  AudioOutputI2S(int, int, int, int) {}
  bool SetPinout(int, int, int) { return true; }
  bool begin() override {
    ++FakeI2S::starts;
    FakeI2S::enabled = 1;
    i2sOn = true;
    return true;
  }
  bool stop() override { ++FakeI2S::stops; i2sOn = false; return true; }
  bool ConsumeSample(int16_t sample[2]) override {
    if (FakeI2S::capacity == 0) return false;
    --FakeI2S::capacity;
    FakeI2S::frames.push_back({sample[0], sample[1]});
    return true;
  }
 protected:
  bool i2sOn = false;
  bool mono = false;
  void *_tx_handle = this;
};
inline int i2s_channel_disable(void *) {
  if (!FakeI2S::enabled) return -1;
  FakeI2S::enabled = 0;
  return ESP_OK;
}
inline int i2s_channel_enable(void *) {
  if (FakeI2S::enabled) return -1;
  FakeI2S::enabled = 1;
  return ESP_OK;
}
inline int i2s_channel_register_event_callback(void *, const i2s_event_callbacks_t *callbacks,
                                               void *context) {
  if (FakeI2S::enabled) return -1;  // The driver rejects registration while running.
  FakeI2S::onSendQueueOverflow = callbacks->on_send_q_ovf;
  FakeI2S::callbackContext = context;
  return ESP_OK;
}
inline int i2s_channel_write(void *, const void *src, size_t bytes, size_t *written, uint32_t) {
  ++FakeI2S::driverCalls;
  const auto *data = static_cast<const uint8_t *>(src);
  *written = 0;
  while (*written + 4 <= bytes && FakeI2S::capacity > 0) {
    const uint8_t *p = data + *written;
    FakeI2S::frames.push_back({static_cast<int16_t>(p[0] | uint16_t(p[1]) << 8),
                               static_cast<int16_t>(p[2] | uint16_t(p[3]) << 8)});
    --FakeI2S::capacity; *written += 4;
  }
  return *written == bytes ? 0 : -1;
}
