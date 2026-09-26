#include "sample_classifier.h"

#include "sample_library.h"
#include <string.h>

namespace {

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
    report.items[i].dataOffset = 0;
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
                             const SampleLibrary::Catalog &catalog,
                             ClassificationReport &report) {
  resetReport(report);
  report.sampleRamBudgetBytes = settings.sampleRamBudgetBytes;

  // First occurrence of each path; later duplicates share its classification.
  int firstIndex[ClassificationReport::kMaxItems];
  // Playable first occurrences, packed into RAM shortest first.
  int candidates[ClassificationReport::kMaxItems];
  int candidateCount = 0;

  for (int i = 0;
       i < settings.assignmentCount && report.itemCount < ClassificationReport::kMaxItems;
       i++) {
    const SettingsStore::MidiAssignment &assignment = settings.assignments[i];
    const int itemIndex = report.itemCount++;
    AssignedSampleClassification &item = report.items[itemIndex];
    item.note = assignment.note;
    item.path = assignment.samplePath;

    const int existingIndex = findExistingPathIndex(report, item.path);
    firstIndex[itemIndex] = existingIndex;
    if (existingIndex < itemIndex) continue;

    const int index = SampleLibrary::findIndexByPath(catalog, item.path);
    if (index < 0) {
      item.mode = StorageMode::MissingFile;
    } else if (!catalog.playable(index)) {
      item.mode = catalog.validation[index].status == WavValidation::Status::ReadError ||
                          catalog.validation[index].status == WavValidation::Status::Unchecked
                      ? StorageMode::ReadError : StorageMode::InvalidFormat;
    } else {
      const auto &info = catalog.validation[index];
      item.channelCount = kRequiredChannelCount;
      item.bitsPerSample = kRequiredBitsPerSample;
      item.sampleRate = kRequiredSampleRate;
      item.dataBytes = info.dataBytes;
      item.dataOffset = info.dataOffset;
      item.durationSeconds = static_cast<float>(info.dataBytes) / 88200.0f;
      item.mode = StorageMode::Stream;
      // Stable insertion keeps assignment order among equal sizes.
      int pos = candidateCount++;
      while (pos > 0 && report.items[candidates[pos - 1]].dataBytes > item.dataBytes) {
        candidates[pos] = candidates[pos - 1];
        pos--;
      }
      candidates[pos] = itemIndex;
    }
  }

  // Streaming blocks the audio task on SD reads, so preload as many samples
  // as the budget allows. Shortest first leaves the fewest samples on SD.
  for (int i = 0; i < candidateCount; i++) {
    AssignedSampleClassification &item = report.items[candidates[i]];
    if (item.dataBytes <= report.sampleRamBudgetBytes - report.sampleRamUsedBytes) {
      item.mode = StorageMode::Ram;
      report.sampleRamUsedBytes += item.dataBytes;
    }
  }

  for (int i = 0; i < report.itemCount; i++) {
    AssignedSampleClassification &item = report.items[i];
    if (firstIndex[i] < i) {
      const AssignedSampleClassification &existing = report.items[firstIndex[i]];
      item.channelCount = existing.channelCount;
      item.bitsPerSample = existing.bitsPerSample;
      item.sampleRate = existing.sampleRate;
      item.dataBytes = existing.dataBytes;
      item.dataOffset = existing.dataOffset;
      item.durationSeconds = existing.durationSeconds;
      item.mode = existing.mode;
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
