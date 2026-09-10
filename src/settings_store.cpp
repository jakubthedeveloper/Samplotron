#include "settings_store.h"

#include <Arduino.h>
#include <SD.h>

#include <ArduinoJson.h>
#include <string.h>
#include <memory>
#include <new>

namespace {

constexpr const char *kSettingsPath = "/sampler_config.json";
constexpr const char *kSettingsBackupPath = "/sampler_config.bak.json";
constexpr const char *kSettingsTempPath = "/sampler_config.tmp.json";
constexpr const char *kCurrentVersion = "1.0";
constexpr size_t kSettingsJsonCapacity = 12288;
constexpr size_t kSettingsIoBlockBytes = 512;
StaticJsonDocument<kSettingsJsonCapacity> gSettingsJsonDoc;

bool isValidNote(long note) {
  return note >= 0 && note <= 127;
}

bool isNonEmptyPath(const String &path) {
  return path.length() > 0;
}

uint8_t clampVolume(long volume) {
  if (volume < 0) return 0;
  if (volume > 100) return 100;
  return static_cast<uint8_t>(volume);
}

bool parseLoopPlaybackEnabled(const JsonVariantConst &variant, bool defaultValue) {
  if (variant.is<const char *>()) {
    const char *value = variant.as<const char *>();
    if (!value) return defaultValue;
    return strcmp(value, "loop") == 0;
  }
  if (variant.is<bool>()) {
    return variant.as<bool>();
  }
  return defaultValue;
}

void ensureParentDirectoryExists(const char *path) {
  String filePath(path);
  const int slash = filePath.lastIndexOf('/');
  if (slash <= 0) return;

  const String dirPath = filePath.substring(0, slash);
  if (dirPath.length() == 0 || SD.exists(dirPath)) return;
  SD.mkdir(dirPath);
}

bool writeJsonToPath(const char *path, const JsonDocument &doc) {
  // Serialize before touching SD. Keep the bytes until the closed file has
  // been reopened and checked, so verification also detects valid but wrong JSON.
  const size_t expectedBytes = measureJsonPretty(doc);
  std::unique_ptr<char[]> payload(new (std::nothrow) char[expectedBytes + 1]);
  if (!payload) {
    Serial.printf("Save: cannot allocate %u bytes for JSON\n",
                  static_cast<unsigned>(expectedBytes + 1));
    return false;
  }
  if (serializeJsonPretty(doc, payload.get(), expectedBytes + 1) != expectedBytes) {
    Serial.println("Save: JSON serialization length mismatch");
    return false;
  }

  File file = SD.open(path, FILE_WRITE);  // Truncate any previous temporary file.
  if (!file) {
    Serial.printf("Save: cannot open %s for writing\n", path);
    return false;
  }

  // Limit both stdio buffering and each write to one SD sector. Flushing each
  // block prevents stdio from combining them into a multi-sector transfer.
  if (!file.setBufferSize(kSettingsIoBlockBytes)) {
    Serial.println("Save: cannot configure SD write buffer");
    file.close();
    return false;
  }
  size_t written = 0;
  while (written < expectedBytes) {
    const size_t remaining = expectedBytes - written;
    const size_t requested = remaining < kSettingsIoBlockBytes ? remaining : kSettingsIoBlockBytes;
    const size_t sent = file.write(
        reinterpret_cast<const uint8_t *>(payload.get() + written), requested);
    file.flush();
    if (sent != requested) {
      Serial.printf("Save: incomplete write at %u: %u/%u bytes\n",
                    static_cast<unsigned>(written), static_cast<unsigned>(sent),
                    static_cast<unsigned>(requested));
      file.close();
      return false;
    }
    written += sent;
  }
  file.close();

  file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.printf("Save: cannot reopen %s for verification\n", path);
    return false;
  }
  if (!file.setBufferSize(kSettingsIoBlockBytes)) {
    Serial.println("Save: cannot configure SD read buffer");
    file.close();
    return false;
  }
  const size_t storedBytes = file.size();
  if (storedBytes != expectedBytes) {
    Serial.printf("Save: stored size mismatch: %u/%u bytes\n",
                  static_cast<unsigned>(storedBytes), static_cast<unsigned>(expectedBytes));
    file.close();
    return false;
  }

  uint8_t buffer[kSettingsIoBlockBytes];
  size_t offset = 0;
  while (offset < expectedBytes) {
    const size_t remaining = expectedBytes - offset;
    const size_t requested = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const size_t received = file.read(buffer, requested);
    if (received == 0 || received > requested) {
      Serial.printf("Save: verification read failed at %u/%u bytes\n",
                    static_cast<unsigned>(offset), static_cast<unsigned>(expectedBytes));
      file.close();
      return false;
    }
    if (memcmp(buffer, payload.get() + offset, received) != 0) {
      size_t mismatch = 0;
      while (buffer[mismatch] == static_cast<uint8_t>(payload[offset + mismatch])) {
        ++mismatch;
      }
      Serial.printf("Save: data mismatch at %u/%u: expected %02X, read %02X\n",
                    static_cast<unsigned>(offset + mismatch),
                    static_cast<unsigned>(expectedBytes),
                    static_cast<unsigned>(static_cast<uint8_t>(payload[offset + mismatch])),
                    static_cast<unsigned>(buffer[mismatch]));
      file.close();
      return false;
    }
    offset += received;
  }
  file.close();
  Serial.printf("Save: verified %u bytes\n", static_cast<unsigned>(expectedBytes));
  return true;
}

}  // namespace

namespace SettingsStore {

void applyDefaults(SamplerSettings &settings) {
  settings.version = kCurrentVersion;
  settings.sampleRamBudgetBytes = SamplerSettings::kDefaultSampleRamBudgetBytes;
  settings.panicNote = -1;
  settings.assignmentCount = 0;
  settings.playbackModeCount = 0;
}

bool loadFromSd(SamplerSettings &settings) {
  applyDefaults(settings);

  if (!SD.exists(kSettingsPath)) {
    
    return false;
  }

  File file = SD.open(kSettingsPath, FILE_READ);
  if (!file) {
    
    return false;
  }

  gSettingsJsonDoc.clear();
  DeserializationError error = deserializeJson(gSettingsJsonDoc, file);
  file.close();

  if (error) {
    
    return false;
  }

  if (gSettingsJsonDoc["version"].is<const char *>()) {
    settings.version = String(gSettingsJsonDoc["version"].as<const char *>());
  }

  JsonObject globalSettings = gSettingsJsonDoc["global_settings"].as<JsonObject>();
  bool defaultLoopPlaybackEnabled = false;
  if (!globalSettings.isNull()) {
    if (globalSettings["sample_ram_budget_bytes"].is<uint32_t>()) {
      settings.sampleRamBudgetBytes = globalSettings["sample_ram_budget_bytes"].as<uint32_t>();
    }
    const long panicNote = globalSettings["panic_note"] | -1;
    settings.panicNote = isValidNote(panicNote) ? static_cast<int16_t>(panicNote) : -1;
    defaultLoopPlaybackEnabled =
        parseLoopPlaybackEnabled(globalSettings["playback_mode"], defaultLoopPlaybackEnabled);
  }

  JsonArray playbackModes = gSettingsJsonDoc["sample_playback_modes"].as<JsonArray>();
  if (!playbackModes.isNull()) {
    for (JsonObject modeEntry : playbackModes) {
      if (settings.playbackModeCount >= SamplerSettings::kMaxPlaybackModes) {
        
        break;
      }

      const char *pathCStr = modeEntry["sample_path"] | "";
      const String samplePath(pathCStr);
      const bool loopPlaybackEnabled =
          parseLoopPlaybackEnabled(modeEntry["playback_mode"], defaultLoopPlaybackEnabled);

      if (!isNonEmptyPath(samplePath)) {
        continue;
      }

      settings.playbackModes[settings.playbackModeCount].samplePath = samplePath;
      settings.playbackModes[settings.playbackModeCount].loopPlaybackEnabled = loopPlaybackEnabled;
      settings.playbackModeCount++;
    }
  }

  JsonArray assignments = gSettingsJsonDoc["midi_assignments"].as<JsonArray>();
  if (!assignments.isNull()) {
    for (JsonObject assignment : assignments) {
      if (settings.assignmentCount >= SamplerSettings::kMaxAssignments) {
        
        break;
      }

      const long note = assignment["note"] | -1;
      const char *pathCStr = assignment["sample_path"] | "";
      const long volume = assignment["volume"] | 100;
      const String samplePath(pathCStr);
      const bool loopPlaybackEnabled =
          parseLoopPlaybackEnabled(assignment["playback_mode"], defaultLoopPlaybackEnabled);

      if (!isValidNote(note) || !isNonEmptyPath(samplePath)) {
        continue;
      }

      settings.assignments[settings.assignmentCount].note = static_cast<uint8_t>(note);
      settings.assignments[settings.assignmentCount].samplePath = samplePath;
      settings.assignments[settings.assignmentCount].volume = clampVolume(volume);
      settings.assignments[settings.assignmentCount].loopPlaybackEnabled = loopPlaybackEnabled;
      settings.assignmentCount++;
    }
  }

  return true;
}

bool saveToSd(const SamplerSettings &settings) {
  ensureParentDirectoryExists(kSettingsPath);
  ensureParentDirectoryExists(kSettingsBackupPath);
  ensureParentDirectoryExists(kSettingsTempPath);

  gSettingsJsonDoc.clear();
  gSettingsJsonDoc["version"] =
      (settings.version.length() > 0) ? settings.version : String(kCurrentVersion);

  JsonObject globalSettings = gSettingsJsonDoc.createNestedObject("global_settings");
  globalSettings["sample_ram_budget_bytes"] = settings.sampleRamBudgetBytes;
  if (settings.panicNote >= 0 && settings.panicNote <= 127) {
    globalSettings["panic_note"] = settings.panicNote;
  }

  JsonArray playbackModes = gSettingsJsonDoc.createNestedArray("sample_playback_modes");
  for (int i = 0; i < settings.playbackModeCount; i++) {
    const SamplePlaybackMode &mode = settings.playbackModes[i];
    if (!isNonEmptyPath(mode.samplePath)) {
      continue;
    }

    JsonObject modeEntry = playbackModes.createNestedObject();
    modeEntry["sample_path"] = mode.samplePath;
    modeEntry["playback_mode"] = mode.loopPlaybackEnabled ? "loop" : "shot";
  }

  JsonArray assignments = gSettingsJsonDoc.createNestedArray("midi_assignments");
  for (int i = 0; i < settings.assignmentCount; i++) {
    const MidiAssignment &assignment = settings.assignments[i];
    if (!isValidNote(assignment.note) || !isNonEmptyPath(assignment.samplePath)) {
      continue;
    }

    JsonObject entry = assignments.createNestedObject();
    entry["note"] = assignment.note;
    entry["sample_path"] = assignment.samplePath;
    entry["volume"] = assignment.volume;
    entry["playback_mode"] = assignment.loopPlaybackEnabled ? "loop" : "shot";
  }

  if (gSettingsJsonDoc.overflowed()) {
    Serial.println("Save: configuration exceeds JSON capacity");
    return false;
  }

  if (!writeJsonToPath(kSettingsTempPath, gSettingsJsonDoc)) {
    // Keep the failed temporary file for inspection; the next save truncates it.
    return false;
  }

  if (SD.exists(kSettingsPath)) {
    SD.remove(kSettingsBackupPath);
    if (!SD.rename(kSettingsPath, kSettingsBackupPath)) {
      Serial.println("Save: cannot create configuration backup");
      SD.remove(kSettingsTempPath);
      return false;
    }
  }

  if (!SD.rename(kSettingsTempPath, kSettingsPath)) {
    Serial.println("Save: cannot replace configuration file");
    if (SD.exists(kSettingsBackupPath)) {
      SD.rename(kSettingsBackupPath, kSettingsPath);
    }
    SD.remove(kSettingsTempPath);
    return false;
  }

  return true;
}

}  // namespace SettingsStore
