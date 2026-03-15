#include "trigger_engine.h"

namespace {

constexpr TickType_t kIdleWaitTick = pdMS_TO_TICKS(1);

}  // namespace

bool TriggerEngine::begin(Audio *audio,
                          UBaseType_t taskPriority,
                          BaseType_t taskCore,
                          uint16_t queueLength,
                          uint16_t stackWords,
                          QueueHandle_t uiStatusQueue) {
  audio_ = audio;
  uiStatusQueue_ = uiStatusQueue;
  triggerQueue_ = xQueueCreate(queueLength, sizeof(TriggerEvent));
  if (!triggerQueue_) {
    Serial.println("Failed to create trigger queue");
    return false;
  }

  const BaseType_t ok = xTaskCreatePinnedToCore(audioTaskEntry,
                                                 "audio_task",
                                                 stackWords,
                                                 this,
                                                 taskPriority,
                                                 &taskHandle_,
                                                 taskCore);
  if (ok != pdPASS) {
    Serial.println("Audio task creation failed");
    return false;
  }

  return true;
}

bool TriggerEngine::enqueue(const TriggerEvent &event) {
  if (!triggerQueue_) return false;
  return xQueueSend(triggerQueue_, &event, 0) == pdTRUE;
}

bool TriggerEngine::panicAll() {
  TriggerEvent event;
  event.source = TriggerSourceType::PanicAll;
  return enqueue(event);
}

bool TriggerEngine::waitForIdle(uint32_t timeoutMs) const {
  const uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    const bool noVoices = (audioActiveVoices_ == 0);
    const bool noPendingTriggers = (!triggerQueue_ || uxQueueMessagesWaiting(triggerQueue_) == 0);
    if (noVoices && noPendingTriggers) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

void TriggerEngine::audioTaskEntry(void *param) {
  auto *self = static_cast<TriggerEngine *>(param);
  self->runAudioTask();
}

void TriggerEngine::runAudioTask() {
  if (!audio_ || !triggerQueue_) {
    vTaskDelete(nullptr);
    return;
  }

  audio_->begin();
  Serial.printf("audio_task started core=%d prio=%u\n",
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)));
  if (uiStatusQueue_) {
    UiStatusEvent event;
    event.source = UiStatusSource::AudioEngine;
    event.type = UiStatusType::AudioTaskStarted;
    event.success = true;
    xQueueSend(uiStatusQueue_, &event, 0);
  }

  TriggerEvent event;
  while (true) {
    audio_->update();
    const Audio::RuntimeStats stats = audio_->runtimeStats();
    audioActiveVoices_ = stats.activeVoices;

    // Never drain the entire queue in one go; keep audio update cadence stable.
    if (xQueueReceive(triggerQueue_, &event, 0) == pdTRUE) {
      // Keep output fed around trigger processing to minimize short underruns.
      if (stats.activeVoices > 0) {
        audio_->update();
      }
      processTriggerEvent(event);
      audio_->update();
      audioActiveVoices_ = audio_->runtimeStats().activeVoices;
      continue;
    }

    if (stats.activeVoices == 0) {
      // When idle, block briefly waiting for new triggers instead of spinning.
      if (xQueueReceive(triggerQueue_, &event, kIdleWaitTick) == pdTRUE) {
        processTriggerEvent(event);
      }
    }
  }
}

void TriggerEngine::processTriggerEvent(const TriggerEvent &event) {
  if (!audio_) return;

  if (event.source == TriggerSourceType::PanicAll) {
    audio_->stopAllVoices();
    return;
  }

  if (event.source == TriggerSourceType::RamData) {
    if (event.ramData && event.ramDataBytes > 0) {
      audio_->playSampleRam(event.ramData,
                            event.ramDataBytes,
                            event.channelCount,
                            event.sampleRate,
                            event.bitsPerSample,
                            event.volume,
                            event.retriggerGroupId);
    }
    return;
  }

  if (event.path[0] != '\0') {
    audio_->playSamplePath(String(event.path), event.volume, event.retriggerGroupId);
  }
}
