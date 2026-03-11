#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "audio.h"
#include "sampler_loader_ipc.h"

enum class TriggerSourceType : uint8_t {
  StreamPath,
  RamData,
};

struct TriggerEvent {
  TriggerSourceType source = TriggerSourceType::StreamPath;
  uint8_t volume = 100;
  char path[128] = {0};

  const uint8_t *ramData = nullptr;
  uint32_t ramDataBytes = 0;
  uint16_t channelCount = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
};

class TriggerEngine {
 public:
  bool begin(Audio *audio,
             UBaseType_t taskPriority,
             BaseType_t taskCore,
             uint16_t queueLength = 32,
             uint16_t stackWords = 6144,
             QueueHandle_t uiStatusQueue = nullptr);
  bool enqueue(const TriggerEvent &event);
  bool waitForIdle(uint32_t timeoutMs) const;

 private:
  static void audioTaskEntry(void *param);
  void runAudioTask();
  void processTriggerEvent(const TriggerEvent &event);

  Audio *audio_ = nullptr;
  QueueHandle_t triggerQueue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  volatile uint8_t audioActiveVoices_ = 0;
  QueueHandle_t uiStatusQueue_ = nullptr;
};
