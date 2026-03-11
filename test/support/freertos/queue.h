#pragma once

#include "FreeRTOS.h"

typedef void *QueueHandle_t;

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t) {
  return 0;
}
