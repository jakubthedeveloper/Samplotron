#include "trigger_engine.h"

namespace {

constexpr TickType_t kIdleWaitTick = pdMS_TO_TICKS(1);
constexpr size_t kQueueFilterCapacity = 32;
constexpr uint32_t kPanicFadeOutUs = 12000;

bool isPlaybackEvent(const TriggerEvent &event) {
  return event.source == TriggerSourceType::StreamPath || event.source == TriggerSourceType::RamData;
}

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
    
    return false;
  }

  return true;
}

bool TriggerEngine::enqueue(const TriggerEvent &event) {
  if (!triggerQueue_) return false;

  if (event.isPreview) {
    // Keep control events, drop stale playback events, then prioritize newest preview.
    TriggerEvent preserved[kQueueFilterCapacity];
    size_t preservedCount = 0;
    TriggerEvent queued;
    while (xQueueReceive(triggerQueue_, &queued, 0) == pdTRUE) {
      if (isPlaybackEvent(queued)) continue;
      if (preservedCount < kQueueFilterCapacity) {
        preserved[preservedCount++] = queued;
      }
    }
    for (size_t i = 0; i < preservedCount; i++) {
      xQueueSend(triggerQueue_, &preserved[i], 0);
    }
    return xQueueSendToFront(triggerQueue_, &event, 0) == pdTRUE;
  }

  return xQueueSend(triggerQueue_, &event, 0) == pdTRUE;
}

bool TriggerEngine::panicAll() {
  TriggerEvent event;
  event.source = TriggerSourceType::PanicAll;
  return enqueue(event);
}

bool TriggerEngine::stopLoopingVoicesForGroup(int16_t retriggerGroupId) {
  if (retriggerGroupId < 0) return false;
  TriggerEvent event;
  event.source = TriggerSourceType::StopLoopingVoicesForGroup;
  event.retriggerGroupId = retriggerGroupId;
  return enqueue(event);
}

bool TriggerEngine::setLoopEnabledForGroup(int16_t retriggerGroupId, bool loopEnabled) {
  if (retriggerGroupId < 0) return false;
  TriggerEvent event;
  event.source = TriggerSourceType::SetLoopEnabledForGroup;
  event.retriggerGroupId = retriggerGroupId;
  event.loopEnabled = loopEnabled;
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
    if (triggerQueue_) {
      // Panic semantics: clear pending trigger backlog and fade active voices quickly.
      xQueueReset(triggerQueue_);
    }
    audio_->fadeOutAllVoices(kPanicFadeOutUs);
    return;
  }

  if (event.source == TriggerSourceType::StopLoopingVoicesForGroup) {
    audio_->stopLoopingVoicesForGroup(event.retriggerGroupId);
    return;
  }

  if (event.source == TriggerSourceType::SetLoopEnabledForGroup) {
    audio_->setLoopEnabledForGroup(event.retriggerGroupId, event.loopEnabled);
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
                            event.retriggerGroupId,
                            event.loopEnabled);
    }
    return;
  }

  if (event.path[0] != '\0') {
    audio_->playSamplePath(String(event.path), event.volume, event.retriggerGroupId, event.loopEnabled);
  }
}
