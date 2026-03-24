#include "settings_store.h"
#include "debug_flags.h"

#include <Arduino.h>
#include <SD.h>

#include <ArduinoJson.h>
#include <string.h>

namespace {

constexpr const char *kSettingsPath = "/sampler_config.json";
constexpr const char *kSettingsBackupPath = "/sampler_config.bak.json";
constexpr const char *kSettingsTempPath = "/sampler_config.tmp.json";
constexpr const char *kCurrentVersion = "1.0";
constexpr size_t kSettingsJsonCapacity = 12288;
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
  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }

  if (serializeJsonPretty(doc, file) == 0) {
    file.close();
    return false;
  }

  file.println();
  file.flush();
  file.close();
  return true;
}

bool verifyJsonAtPath(const char *path) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }

  gSettingsJsonDoc.clear();
  const DeserializationError error = deserializeJson(gSettingsJsonDoc, file);
  file.close();
  return !error;
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

  if (!writeJsonToPath(kSettingsTempPath, gSettingsJsonDoc)) {
    
    return false;
  }

  if (!verifyJsonAtPath(kSettingsTempPath)) {
    
    SD.remove(kSettingsTempPath);
    return false;
  }

  if (SD.exists(kSettingsPath)) {
    SD.remove(kSettingsBackupPath);
    if (!SD.rename(kSettingsPath, kSettingsBackupPath)) {
      
      SD.remove(kSettingsTempPath);
      return false;
    }
  }

  if (!SD.rename(kSettingsTempPath, kSettingsPath)) {
    
    if (SD.exists(kSettingsBackupPath)) {
      SD.rename(kSettingsBackupPath, kSettingsPath);
    }
    SD.remove(kSettingsTempPath);
    return false;
  }

  return true;
}

bool logRawJsonFromSd() {
  if (!DebugFlags::kEnableDebugLogs) {
    return true;
  }

  
  if (!SD.exists(kSettingsPath)) {
    
    
    return false;
  }

  File file = SD.open(kSettingsPath, FILE_READ);
  if (!file) {
    
    
    return false;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    
  }
  file.close();
  
  return true;
}

}  // namespace SettingsStore
