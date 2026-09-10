#pragma once
#include "FreeRTOS.h"
using SemaphoreHandle_t = int *;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new int(0); }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline void vSemaphoreDelete(SemaphoreHandle_t mutex) { delete mutex; }
