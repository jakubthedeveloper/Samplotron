#include "sample_ram_manager.h"

#include <SD.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr size_t kReadChunkBytes = 1024;

struct LoadedEntry {
  String path;
  uint32_t dataBytes = 0;
  uint32_t poolOffset = 0;
  bool valid = false;
};

uint8_t *gPool = nullptr;
uint32_t gPoolCapacity = 0;
LoadedEntry gLoadedEntries[SettingsStore::SamplerSettings::kMaxAssignments];

uint32_t readLe32(const uint8_t *buf) {
  return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
         (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
}

void clearLoadedEntries() {
  for (int i = 0; i < SettingsStore::SamplerSettings::kMaxAssignments; i++) {
    gLoadedEntries[i].path = "";
    gLoadedEntries[i].dataBytes = 0;
    gLoadedEntries[i].poolOffset = 0;
    gLoadedEntries[i].valid = false;
  }
}

void freePool() {
  if (gPool) {
    free(gPool);
    gPool = nullptr;
  }
  gPoolCapacity = 0;
}

bool ensurePool(uint32_t budgetBytes) {
  if (budgetBytes == 0) {
    freePool();
    return true;
  }

  if (gPool && gPoolCapacity == budgetBytes) {
    return true;
  }

  freePool();
  gPool = static_cast<uint8_t *>(
      heap_caps_malloc(budgetBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gPool) {
    gPool = static_cast<uint8_t *>(malloc(budgetBytes));
  }
  if (!gPool) {
    return false;
  }

  gPoolCapacity = budgetBytes;
  return true;
}

int findLoadedEntryByPath(const String &path) {
  for (int i = 0; i < SettingsStore::SamplerSettings::kMaxAssignments; i++) {
    if (gLoadedEntries[i].valid && gLoadedEntries[i].path == path) {
      return i;
    }
  }
  return -1;
}

int findFreeLoadedEntrySlot() {
  for (int i = 0; i < SettingsStore::SamplerSettings::kMaxAssignments; i++) {
    if (!gLoadedEntries[i].valid) return i;
  }
  return -1;
}

bool findWavDataChunk(const String &path, uint32_t &dataOffset, uint32_t &dataSize) {
  File file = SD.open(path, FILE_READ);
  if (!file) return false;

  uint8_t riffHeader[12] = {0};
  if (file.read(riffHeader, sizeof(riffHeader)) != static_cast<int>(sizeof(riffHeader))) {
    file.close();
    return false;
  }

  if (memcmp(riffHeader, "RIFF", 4) != 0 || memcmp(&riffHeader[8], "WAVE", 4) != 0) {
    file.close();
    return false;
  }

  while (file.available()) {
    uint8_t chunkHeader[8] = {0};
    if (file.read(chunkHeader, sizeof(chunkHeader)) != static_cast<int>(sizeof(chunkHeader))) {
      file.close();
      return false;
    }

    const uint32_t chunkSize = readLe32(&chunkHeader[4]);
    const uint32_t chunkDataPos = static_cast<uint32_t>(file.position());

    if (memcmp(chunkHeader, "data", 4) == 0) {
      dataOffset = chunkDataPos;
      dataSize = chunkSize;
      file.close();
      return true;
    }

    uint32_t skipTo = chunkDataPos + chunkSize;
    if ((chunkSize & 1U) != 0) skipTo += 1U;
    if (!file.seek(skipTo)) {
      file.close();
      return false;
    }
  }

  file.close();
  return false;
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
  }

  file.close();
  return true;
}

}  // namespace

namespace SampleRamManager {

bool prepare(const SettingsStore::SamplerSettings &settings,
             const SampleClassifier::ClassificationReport &classification,
             LoadReport &report) {
  report = LoadReport{};
  report.budgetBytes = settings.sampleRamBudgetBytes;
  for (int i = 0; i < classification.itemCount; i++) {
    if (classification.items[i].mode == SampleClassifier::StorageMode::Ram) {
      report.requestedRamCount++;
    }
  }

  if (report.requestedRamCount == 0) {
    freePool();
    clearLoadedEntries();
    return true;
  }

  clearLoadedEntries();
  if (!ensurePool(report.budgetBytes)) {
    report.allocatedBytes = 0;
    report.fallbackToStreamCount = report.requestedRamCount;
    return false;
  }
  report.allocatedBytes = gPoolCapacity;

  uint32_t used = 0;

  for (int i = 0; i < classification.itemCount; i++) {
    const SampleClassifier::AssignedSampleClassification &item = classification.items[i];
    if (item.mode != SampleClassifier::StorageMode::Ram) {
      continue;
    }

    const int existingIndex = findLoadedEntryByPath(item.path);
    if (existingIndex >= 0) {
      report.loadedRamCount++;
      continue;
    }

    if (item.dataBytes == 0 || item.dataBytes > gPoolCapacity || (gPoolCapacity - used) < item.dataBytes) {
      report.fallbackToStreamCount++;
      continue;
    }

    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;
    if (!findWavDataChunk(item.path, dataOffset, dataSize) || dataSize < item.dataBytes) {
      report.readErrorCount++;
      report.fallbackToStreamCount++;
      continue;
    }

    if (!readFileRangeToBuffer(item.path, dataOffset, item.dataBytes, gPool + used)) {
      report.readErrorCount++;
      report.fallbackToStreamCount++;
      continue;
    }

    const int slot = findFreeLoadedEntrySlot();
    if (slot < 0) {
      report.fallbackToStreamCount++;
      continue;
    }

    gLoadedEntries[slot].path = item.path;
    gLoadedEntries[slot].dataBytes = item.dataBytes;
    gLoadedEntries[slot].poolOffset = used;
    gLoadedEntries[slot].valid = true;

    used += item.dataBytes;
    report.loadedRamCount++;
  }

  report.usedBytes = used;
  return true;
}

void release() {
  clearLoadedEntries();
  freePool();
}

}  // namespace SampleRamManager
