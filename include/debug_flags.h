#pragma once

#include <stdint.h>

namespace DebugFlags {

constexpr bool kEnableDebugLogs = true;
constexpr bool kEnablePerTriggerPlaybackLogs = false;
constexpr bool kEnableInputEventLogs = false;
constexpr bool kEnableRuntimeAudioDiagLogs = false;
constexpr bool kEnableRuntimeRamUsageLogs = true;
constexpr uint32_t kRuntimeRamUsageLogIntervalMs = 5000;

}  // namespace DebugFlags
