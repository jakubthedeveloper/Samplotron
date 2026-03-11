#pragma once

#include "FreeRTOS.h"

typedef void *TaskHandle_t;

inline void vTaskDelay(TickType_t) {}
inline void vTaskDelete(void *) {}
inline BaseType_t xPortGetCoreID() { return 0; }
inline UBaseType_t uxTaskPriorityGet(TaskHandle_t) { return 0; }
