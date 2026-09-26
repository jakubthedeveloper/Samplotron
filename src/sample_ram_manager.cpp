#include "sample_ram_manager.h"

#include <SD.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>

namespace {

// Reads larger than the 4 KiB stdio buffer go straight to FATFS as
// multi-sector transfers, which matters at the 4 MHz fallback SD clock.
constexpr size_t kReadChunkBytes = 32 * 1024;
// PSRAM left for everything else (JSON documents, FATFS state, buffers).
constexpr uint32_t kPsramReserveBytes = 512UL * 1024UL;

struct LoadedEntry {
  String path;
  uint32_t dataBytes = 0;
  uint32_t poolOffset = 0;
  bool valid = false;
  bool head = false;  // Only the start of a streamed sample.
};

uint8_t *gPool = nullptr;
uint32_t gPoolCapacity = 0;
LoadedEntry gLoadedEntries[SettingsStore::SamplerSettings::kMaxAssignments];
bool gPoolBudgetLocked = false;
uint32_t gFixedPoolBudget = 0;

void clearLoadedEntries() {
  for (int i = 0; i < SettingsStore::SamplerSettings::kMaxAssignments; i++) {
    gLoadedEntries[i].path = "";
    gLoadedEntries[i].dataBytes = 0;
    gLoadedEntries[i].poolOffset = 0;
    gLoadedEntries[i].valid = false;
    gLoadedEntries[i].head = false;
  }
}

void freePool() {
  if (gPool) {
    free(gPool);
    gPool = nullptr;
  }
  gPoolCapacity = 0;
}

bool allocatePool(uint32_t budgetBytes) {
  if (budgetBytes == 0) return true;
  if (gPool && gPoolCapacity == budgetBytes) return true;

  gPool = static_cast<uint8_t *>(heap_caps_malloc(budgetBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gPool) {
    gPool = static_cast<uint8_t *>(malloc(budgetBytes));
  }
  if (!gPool) {
    return false;
  }

  gPoolCapacity = budgetBytes;
  return true;
}

int findLoadedEntryByPath(const String &path, bool head) {
  for (int i = 0; i < SettingsStore::SamplerSettings::kMaxAssignments; i++) {
    if (gLoadedEntries[i].valid && gLoadedEntries[i].head == head && gLoadedEntries[i].path == path) {
      return i;
    }
  }
  return -1;
}

bool loadedDataAt(int idx, SampleRamManager::LoadedSampleData &data) {
  data = SampleRamManager::LoadedSampleData{};
  if (idx < 0 || !gPool) return false;
  const uint32_t offset = gLoadedEntries[idx].poolOffset;
  const uint32_t bytes = gLoadedEntries[idx].dataBytes;
  if (bytes == 0 || offset > gPoolCapacity || (gPoolCapacity - offset) < bytes) {
    return false;
  }
  data.data = gPool + offset;
  data.dataBytes = bytes;
  return true;
}

int findFreeLoadedEntrySlot() {
  for (int i = 0; i < SettingsStore::SamplerSettings::kMaxAssignments; i++) {
    if (!gLoadedEntries[i].valid) return i;
  }
  return -1;
}

bool readFileRangeToBuffer(const String &path, uint32_t offset, uint32_t size, uint8_t *dst) {
  File file = SD.open(path, FILE_READ);
  if (!file) return false;
  if (!file.seek(offset)) {
    file.close();
    return false;
  }

  uint32_t totalRead = 0;
  while (totalRead < size) {
    const uint32_t remaining = size - totalRead;
    const size_t toRead = (remaining < kReadChunkBytes) ? remaining : kReadChunkBytes;
    const int readNow = file.read(dst + totalRead, toRead);
    if (readNow <= 0) {
      file.close();
      return false;
    }
    totalRead += static_cast<uint32_t>(readNow);
    // SPI SD transfers busy-wait. Loading several MiB would otherwise keep
    // this core from IDLE long enough to trip the task watchdog.
    vTaskDelay(1);
  }

  file.close();
  return true;
}

}  // namespace

namespace SampleRamManager {

uint32_t budgetBytes() {
  if (!gPoolBudgetLocked) {
    // The pool is allocated once and never resized, so size it from PSRAM
    // available at the first preparation rather than a fixed setting.
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    gFixedPoolBudget = largest > kPsramReserveBytes
                           ? static_cast<uint32_t>(largest - kPsramReserveBytes)
                           : SettingsStore::SamplerSettings::kDefaultSampleRamBudgetBytes;
    gPoolBudgetLocked = true;
  }
  return gFixedPoolBudget;
}

bool prepare(const SettingsStore::SamplerSettings &settings,
             const SampleClassifier::ClassificationReport &classification,
             LoadReport &report) {
  report = LoadReport{};
  report.budgetBytes = settings.sampleRamBudgetBytes;
  report.effectiveBudgetBytes = budgetBytes();
  if (settings.sampleRamBudgetBytes != gFixedPoolBudget) {
    report.fixedBudgetMismatch = true;
  }

  for (int i = 0; i < classification.itemCount; i++) {
    const SampleClassifier::AssignedSampleClassification &item = classification.items[i];
    if (item.mode == SampleClassifier::StorageMode::Ram) {
      report.requestedRamCount++;
    } else if (item.mode == SampleClassifier::StorageMode::Stream && item.headBytes > 0) {
      report.requestedHeadCount++;
    }
  }

  if (report.requestedRamCount == 0 && report.requestedHeadCount == 0) {
    clearLoadedEntries();
    report.allocatedBytes = gPoolCapacity;
    return true;
  }

  clearLoadedEntries();
  if (!allocatePool(gFixedPoolBudget)) {
    report.allocatedBytes = 0;
    report.fallbackToStreamCount = report.requestedRamCount;
    return false;
  }
  report.allocatedBytes = gPoolCapacity;

  uint32_t used = 0;

  for (int i = 0; i < classification.itemCount; i++) {
    const SampleClassifier::AssignedSampleClassification &item = classification.items[i];
    const bool head = item.mode == SampleClassifier::StorageMode::Stream && item.headBytes > 0;
    if (item.mode != SampleClassifier::StorageMode::Ram && !head) {
      continue;
    }
    const uint32_t bytes = head ? item.headBytes : item.dataBytes;

    if (findLoadedEntryByPath(item.path, head) >= 0) {
      if (head) report.loadedHeadCount++;
      else report.loadedRamCount++;
      continue;
    }

    // A missing head only delays that sample's start; it still streams.
    if (bytes == 0 || bytes > gPoolCapacity || (gPoolCapacity - used) < bytes) {
      if (!head) report.fallbackToStreamCount++;
      continue;
    }

    if (!readFileRangeToBuffer(item.path, item.dataOffset, bytes, gPool + used)) {
      report.readErrorCount++;
      if (!head) report.fallbackToStreamCount++;
      continue;
    }

    const int slot = findFreeLoadedEntrySlot();
    if (slot < 0) {
      if (!head) report.fallbackToStreamCount++;
      continue;
    }

    gLoadedEntries[slot].path = item.path;
    gLoadedEntries[slot].dataBytes = bytes;
    gLoadedEntries[slot].poolOffset = used;
    gLoadedEntries[slot].valid = true;
    gLoadedEntries[slot].head = head;

    used += bytes;
    if (head) report.loadedHeadCount++;
    else report.loadedRamCount++;
  }

  report.usedBytes = used;
  return true;
}

void release() {
  clearLoadedEntries();
  freePool();
  gPoolBudgetLocked = false;
  gFixedPoolBudget = 0;
}

bool getLoadedSampleByPath(const String &path, LoadedSampleInfo &info) {
  const int idx = findLoadedEntryByPath(path, false);
  if (idx < 0) return false;
  info.dataBytes = gLoadedEntries[idx].dataBytes;
  info.poolOffset = gLoadedEntries[idx].poolOffset;
  return true;
}

bool getLoadedSampleDataByPath(const String &path, LoadedSampleData &data) {
  return loadedDataAt(findLoadedEntryByPath(path, false), data);
}

bool getLoadedHeadByPath(const String &path, LoadedSampleData &data) {
  return loadedDataAt(findLoadedEntryByPath(path, true), data);
}

}  // namespace SampleRamManager
