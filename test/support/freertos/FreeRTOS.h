#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#ifndef pdTRUE
#define pdTRUE 1
#endif
#ifndef pdFALSE
#define pdFALSE 0
#endif
#ifndef pdPASS
#define pdPASS 1
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#endif
