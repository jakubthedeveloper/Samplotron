#pragma once

#include "FreeRTOS.h"

typedef void *TaskHandle_t;

inline void vTaskDelay(TickType_t) {}
inline void vTaskDelete(void *) {}
inline BaseType_t xPortGetCoreID() { return 0; }
inline UBaseType_t uxTaskPriorityGet(TaskHandle_t) { return 0; }
typedef void (*TaskFunction_t)(void *);
// Host tests run without tasks; creation fails so callers stay synchronous.
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *,
                                          UBaseType_t, TaskHandle_t *, BaseType_t) {
  return 0;
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }
inline TickType_t xTaskGetTickCount() { return 0; }
