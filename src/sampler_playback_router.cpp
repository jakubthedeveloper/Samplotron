#include "sampler_playback_router.h"

#include "active_sample_registry.h"
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
  if (!catalog_->playable(sampleIndex)) return;

  TriggerEvent event;
  event.source = TriggerSourceType::StreamPath;
  event.isPreview = true;
  event.volume = static_cast<uint8_t>(ui_->sampleVolumeForSample(sampleIndex));
  event.retriggerGroupId = static_cast<int16_t>(sampleIndex);
  event.loopEnabled = ui_->sampleLoopPlaybackEnabled(sampleIndex);
  const String &path = catalog_->paths[sampleIndex];
  path.toCharArray(event.path, sizeof(event.path));

  triggerEngine_->enqueue(event);
}

void SamplerPlaybackRouter::onAssignedMidiNoteOn(int midiNote) const {
  if (!ui_ || !catalog_ || !runtime_ || !triggerEngine_) return;

  if (ui_->panicMidiNote() == midiNote) {
    triggerEngine_->panicAll();
    return;
  }

  const int sampleIndex = ui_->assignedSampleForMidiNote(midiNote);
  if (sampleIndex < 0 || sampleIndex >= catalog_->count) {
    ui_->clearTriggeredSample();
    return;
  }

  if (!catalog_->playable(sampleIndex)) return;
  ui_->reportTriggeredSample(sampleIndex);
  const String &assignedPath = catalog_->paths[sampleIndex];
  const uint8_t assignedVolume = static_cast<uint8_t>(ui_->sampleVolumeForSample(sampleIndex));

  const ActiveSampleRegistry::Entry *entry = runtime_->findRegistryEntryForNote(midiNote);
  const bool hasPreparedEntry = (entry && entry->path == assignedPath);

  if (hasPreparedEntry &&
      entry->effectiveMode == ActiveSampleRegistry::EffectiveStorageMode::Unavailable) {
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
      event.retriggerGroupId = static_cast<int16_t>(sampleIndex);
      event.loopEnabled = ui_->sampleLoopPlaybackEnabled(sampleIndex);
      event.ramData = loadedData.data;
      event.ramDataBytes = loadedData.dataBytes;
      event.channelCount = classified->channelCount;
      event.sampleRate = classified->sampleRate;
      event.bitsPerSample = classified->bitsPerSample;
      const bool played = triggerEngine_->enqueue(event);
      if (played) {
        return;
      }
    }
  }

  TriggerEvent event;
  event.source = TriggerSourceType::StreamPath;
  event.volume = assignedVolume;
  event.retriggerGroupId = static_cast<int16_t>(sampleIndex);
  event.loopEnabled = ui_->sampleLoopPlaybackEnabled(sampleIndex);
  assignedPath.toCharArray(event.path, sizeof(event.path));
  triggerEngine_->enqueue(event);
}

void SamplerPlaybackRouter::onPlaybackModeChanged(Ui::PlaybackMode mode, int sampleIndex) const {
  if (!triggerEngine_) return;
  if (sampleIndex < 0) return;
  const int16_t groupId = static_cast<int16_t>(sampleIndex);
  if (mode == Ui::PlaybackMode::Loop) {
    triggerEngine_->setLoopEnabledForGroup(groupId, true);
    return;
  }
  triggerEngine_->stopLoopingVoicesForGroup(groupId);
}
