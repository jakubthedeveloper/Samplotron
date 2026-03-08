#include "sample_classifier.h"

#include <SD.h>
#include <string.h>

namespace {

struct WavInfo {
  uint16_t audioFormat = 0;
  uint16_t channelCount = 0;
  uint16_t bitsPerSample = 0;
  uint32_t sampleRate = 0;
  uint32_t dataBytes = 0;
  bool hasFmt = false;
  bool hasData = false;
};

uint32_t readLe32(const uint8_t *buf) {
  return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
         (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
}

uint16_t readLe16(const uint8_t *buf) {
  return static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
}

bool readWavInfo(const String &path, WavInfo &info) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }

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
      break;
    }

    const uint32_t chunkSize = readLe32(&chunkHeader[4]);
    const size_t chunkDataPos = file.position();

    if (memcmp(chunkHeader, "fmt ", 4) == 0) {
      if (chunkSize < 16) {
        file.close();
        return false;
      }
      uint8_t fmtCore[16] = {0};
      if (file.read(fmtCore, sizeof(fmtCore)) != static_cast<int>(sizeof(fmtCore))) {
        file.close();
        return false;
      }
      info.audioFormat = readLe16(&fmtCore[0]);
      info.channelCount = readLe16(&fmtCore[2]);
      info.sampleRate = readLe32(&fmtCore[4]);
      info.bitsPerSample = readLe16(&fmtCore[14]);
      info.hasFmt = true;
    } else if (memcmp(chunkHeader, "data", 4) == 0) {
      info.dataBytes = chunkSize;
      info.hasData = true;
    }

    if (info.hasFmt && info.hasData) {
      file.close();
      return true;
    }

    size_t skipTo = chunkDataPos + chunkSize;
    if ((chunkSize & 1U) != 0) {
      skipTo += 1U;
    }
    if (!file.seek(skipTo)) {
      file.close();
      return false;
    }
  }

  file.close();
  return info.hasFmt && info.hasData;
}

bool isSupportedV1Format(const WavInfo &info) {
  return info.audioFormat == 1 && info.bitsPerSample == 16 && info.sampleRate > 0 &&
         info.channelCount > 0;
}

float durationSeconds(const WavInfo &info) {
  const uint32_t bytesPerFrame =
      static_cast<uint32_t>(info.channelCount) * (static_cast<uint32_t>(info.bitsPerSample) / 8U);
  if (bytesPerFrame == 0 || info.sampleRate == 0) return 0.0f;
  return static_cast<float>(info.dataBytes) /
         static_cast<float>(bytesPerFrame * static_cast<uint32_t>(info.sampleRate));
}

int findExistingPathIndex(const SampleClassifier::ClassificationReport &report, const String &path) {
  for (int i = 0; i < report.itemCount; i++) {
    if (report.items[i].path == path) return i;
  }
  return -1;
}

void resetReport(SampleClassifier::ClassificationReport &report) {
  for (int i = 0; i < SampleClassifier::ClassificationReport::kMaxItems; i++) {
    report.items[i].note = 0;
    report.items[i].path = "";
    report.items[i].channelCount = 0;
    report.items[i].bitsPerSample = 0;
    report.items[i].sampleRate = 0;
    report.items[i].dataBytes = 0;
    report.items[i].durationSeconds = 0.0f;
    report.items[i].mode = SampleClassifier::StorageMode::ReadError;
  }

  report.itemCount = 0;
  report.sampleRamBudgetBytes = 0;
  report.sampleRamUsedBytes = 0;
  report.ramSampleCount = 0;
  report.streamSampleCount = 0;
  report.missingFileCount = 0;
  report.invalidFormatCount = 0;
  report.readErrorCount = 0;
}

}  // namespace

namespace SampleClassifier {

void classifyAssignedSamples(const SettingsStore::SamplerSettings &settings,
                             ClassificationReport &report) {
  resetReport(report);
  report.sampleRamBudgetBytes = settings.sampleRamBudgetBytes;

  for (int i = 0;
       i < settings.assignmentCount && report.itemCount < ClassificationReport::kMaxItems;
       i++) {
    const SettingsStore::MidiAssignment &assignment = settings.assignments[i];
    AssignedSampleClassification &item = report.items[report.itemCount++];
    item.note = assignment.note;
    item.path = assignment.samplePath;

    const int existingIndex = findExistingPathIndex(report, item.path);
    if (existingIndex >= 0 && existingIndex < (report.itemCount - 1)) {
      const AssignedSampleClassification &existing = report.items[existingIndex];
      item.channelCount = existing.channelCount;
      item.bitsPerSample = existing.bitsPerSample;
      item.sampleRate = existing.sampleRate;
      item.dataBytes = existing.dataBytes;
      item.durationSeconds = existing.durationSeconds;
      item.mode = existing.mode;
    } else if (!SD.exists(item.path)) {
      item.mode = StorageMode::MissingFile;
    } else {
      WavInfo info;
      if (!readWavInfo(item.path, info)) {
        item.mode = StorageMode::ReadError;
      } else if (!isSupportedV1Format(info)) {
        item.channelCount = info.channelCount;
        item.bitsPerSample = info.bitsPerSample;
        item.sampleRate = info.sampleRate;
        item.dataBytes = info.dataBytes;
        item.durationSeconds = durationSeconds(info);
        item.mode = StorageMode::InvalidFormat;
      } else {
        item.channelCount = info.channelCount;
        item.bitsPerSample = info.bitsPerSample;
        item.sampleRate = info.sampleRate;
        item.dataBytes = info.dataBytes;
        item.durationSeconds = durationSeconds(info);

        const bool fitsThreshold = item.durationSeconds <= settings.preloadThresholdSeconds;
        const uint32_t ramRemaining =
            (report.sampleRamBudgetBytes > report.sampleRamUsedBytes)
                ? (report.sampleRamBudgetBytes - report.sampleRamUsedBytes)
                : 0;
        const bool fitsRamBudget = item.dataBytes <= ramRemaining;
        item.mode = (fitsThreshold && fitsRamBudget) ? StorageMode::Ram : StorageMode::Stream;
        if (item.mode == StorageMode::Ram) {
          report.sampleRamUsedBytes += item.dataBytes;
        }
      }
    }

    switch (item.mode) {
      case StorageMode::Ram:
        report.ramSampleCount++;
        break;
      case StorageMode::Stream:
        report.streamSampleCount++;
        break;
      case StorageMode::MissingFile:
        report.missingFileCount++;
        break;
      case StorageMode::InvalidFormat:
        report.invalidFormatCount++;
        break;
      case StorageMode::ReadError:
        report.readErrorCount++;
        break;
    }
  }
}

const char *storageModeLabel(StorageMode mode) {
  switch (mode) {
    case StorageMode::Ram:
      return "RAM";
    case StorageMode::Stream:
      return "STREAM";
    case StorageMode::MissingFile:
      return "MISSING";
    case StorageMode::InvalidFormat:
      return "INVALID_FMT";
    case StorageMode::ReadError:
      return "READ_ERR";
  }
  return "UNKNOWN";
}

}  // namespace SampleClassifier
