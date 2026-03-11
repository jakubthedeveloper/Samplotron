#include "sampler_playback_router.h"

#include "active_sample_registry.h"
#include "debug_flags.h"
#include "sample_ram_manager.h"

void SamplerPlaybackRouter::begin(Ui *ui,
                                  const SampleLibrary::Catalog *catalog,
                                  const SamplerRuntime *runtime,
                                  TriggerEngine *triggerEngine) {
  ui_ = ui;
  catalog_ = catalog;
  runtime_ = runtime;
  triggerEngine_ = triggerEngine;
}

void SamplerPlaybackRouter::onPreviewSample(int sampleIndex) const {
  if (!ui_ || !catalog_ || !triggerEngine_) return;
  if (sampleIndex < 0 || sampleIndex >= catalog_->count) return;

  if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
    Serial.printf("PLAY sample: %s\n", catalog_->names[sampleIndex].c_str());
  }

  TriggerEvent event;
  event.source = TriggerSourceType::StreamPath;
  event.volume = static_cast<uint8_t>(ui_->sampleVolumeForSample(sampleIndex));
  const String &path = catalog_->paths[sampleIndex];
  path.toCharArray(event.path, sizeof(event.path));

  if (!triggerEngine_->enqueue(event) && DebugFlags::kEnableDebugLogs) {
    Serial.println("Trigger queue full (preview dropped)");
  }
}

void SamplerPlaybackRouter::onAssignedMidiNoteOn(int midiNote) const {
  if (!ui_ || !catalog_ || !runtime_ || !triggerEngine_) return;

  const int sampleIndex = ui_->assignedSampleForMidiNote(midiNote);
  if (sampleIndex < 0 || sampleIndex >= catalog_->count) {
    ui_->clearTriggeredSample();
    if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
      Serial.printf("No assignment for MIDI note %d\n", midiNote);
    }
    return;
  }

  ui_->reportTriggeredSample(sampleIndex);
  const String &assignedPath = catalog_->paths[sampleIndex];
  const uint8_t assignedVolume = static_cast<uint8_t>(ui_->sampleVolumeForSample(sampleIndex));

  const ActiveSampleRegistry::Entry *entry = runtime_->findRegistryEntryForNote(midiNote);
  const bool hasPreparedEntry = (entry && entry->path == assignedPath);

  if (hasPreparedEntry &&
      entry->effectiveMode == ActiveSampleRegistry::EffectiveStorageMode::Unavailable) {
    if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
      Serial.printf("Playback blocked for note=%d: unsupported or missing sample path=%s\n",
                    midiNote,
                    assignedPath.c_str());
    }
    return;
  }

  if (hasPreparedEntry && entry->effectiveMode == ActiveSampleRegistry::EffectiveStorageMode::Ram) {
    SampleRamManager::LoadedSampleData loadedData;
    const SampleClassifier::AssignedSampleClassification *classified =
        runtime_->findClassificationByPath(entry->path);
    if (classified && SampleRamManager::getLoadedSampleDataByPath(entry->path, loadedData)) {
      TriggerEvent event;
      event.source = TriggerSourceType::RamData;
      event.volume = assignedVolume;
      event.ramData = loadedData.data;
      event.ramDataBytes = loadedData.dataBytes;
      event.channelCount = classified->channelCount;
      event.sampleRate = classified->sampleRate;
      event.bitsPerSample = classified->bitsPerSample;
      const bool played = triggerEngine_->enqueue(event);
      if (played) {
        if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
          Serial.printf("PLAY note=%d via RAM path=%s bytes=%lu\n",
                        midiNote,
                        entry->path.c_str(),
                        static_cast<unsigned long>(loadedData.dataBytes));
        }
        return;
      }
      if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
        Serial.printf("RAM playback failed for note=%d, fallback to stream path=%s\n",
                      midiNote,
                      assignedPath.c_str());
      }
    }
  }

  if (DebugFlags::kEnableDebugLogs && DebugFlags::kEnablePerTriggerPlaybackLogs) {
    if (hasPreparedEntry) {
      Serial.printf("PLAY note=%d via registry mode=%s path=%s\n",
                    midiNote,
                    ActiveSampleRegistry::effectiveStorageModeLabel(entry->effectiveMode),
                    assignedPath.c_str());
    } else {
      Serial.printf("PLAY note=%d via UI assignment (unprepared), stream path=%s\n",
                    midiNote,
                    assignedPath.c_str());
    }
  }

  TriggerEvent event;
  event.source = TriggerSourceType::StreamPath;
  event.volume = assignedVolume;
  assignedPath.toCharArray(event.path, sizeof(event.path));
  if (!triggerEngine_->enqueue(event) && DebugFlags::kEnableDebugLogs) {
    Serial.println("Trigger queue full (assigned trigger dropped)");
  }
}
