#pragma once

#include <stdint.h>

enum class LoaderCommandType : uint8_t {
  RebuildPreparedSamples,
  PreviewSample,
};

struct LoaderCommand {
  LoaderCommandType type = LoaderCommandType::RebuildPreparedSamples;
  int16_t sampleIndex = -1;
};

enum class UiStatusSource : uint8_t {
  SampleLoader,
  AudioEngine,
};

enum class UiStatusType : uint8_t {
  LoaderRebuildCompleted,
  AudioTaskStarted,
};

struct UiStatusEvent {
  UiStatusSource source = UiStatusSource::SampleLoader;
  UiStatusType type = UiStatusType::LoaderRebuildCompleted;
  bool success = false;

  uint32_t assignedSamples = 0;
  uint32_t ramSampleCount = 0;
  uint32_t streamSampleCount = 0;
  uint32_t sampleRamUsedBytes = 0;
};
