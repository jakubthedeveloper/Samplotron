#include "settings_store.h"

#include <Arduino.h>
#include <SD.h>

#include <ArduinoJson.h>

namespace {

constexpr const char *kSettingsPath = "/sampler_config.json";
constexpr const char *kCurrentVersion = "1.0";
constexpr size_t kSettingsJsonCapacity = 12288;
StaticJsonDocument<kSettingsJsonCapacity> gSettingsJsonDoc;

bool isValidNote(long note) {
  return note >= 0 && note <= 127;
}

bool isNonEmptyPath(const String &path) {
  return path.length() > 0;
}

void ensureParentDirectoryExists(const char *path) {
  String filePath(path);
  const int slash = filePath.lastIndexOf('/');
  if (slash <= 0) return;

  const String dirPath = filePath.substring(0, slash);
  if (dirPath.length() == 0 || SD.exists(dirPath)) return;
  SD.mkdir(dirPath);
}

}  // namespace

namespace SettingsStore {

void applyDefaults(SamplerSettings &settings) {
  settings.version = kCurrentVersion;
  settings.sampleRamBudgetBytes = SamplerSettings::kDefaultSampleRamBudgetBytes;
  settings.preloadThresholdSeconds = SamplerSettings::kDefaultPreloadThresholdSeconds;
  settings.assignmentCount = 0;
}

bool loadFromSd(SamplerSettings &settings) {
  applyDefaults(settings);

  if (!SD.exists(kSettingsPath)) {
    Serial.printf("Settings file not found (%s), using defaults\n", kSettingsPath);
    return false;
  }

  File file = SD.open(kSettingsPath, FILE_READ);
  if (!file) {
    Serial.printf("Failed to open settings for read (%s)\n", kSettingsPath);
    return false;
  }

  gSettingsJsonDoc.clear();
  DeserializationError error = deserializeJson(gSettingsJsonDoc, file);
  file.close();

  if (error) {
    Serial.printf("Settings JSON parse error: %s\n", error.c_str());
    return false;
  }

  if (gSettingsJsonDoc["version"].is<const char *>()) {
    settings.version = String(gSettingsJsonDoc["version"].as<const char *>());
  }

  JsonVariant globalSettings = gSettingsJsonDoc["global_settings"];
  if (!globalSettings.isNull()) {
    if (globalSettings["sample_ram_budget_bytes"].is<uint32_t>()) {
      settings.sampleRamBudgetBytes = globalSettings["sample_ram_budget_bytes"].as<uint32_t>();
    }
    if (globalSettings["preload_threshold_seconds"].is<float>() ||
        globalSettings["preload_threshold_seconds"].is<double>() ||
        globalSettings["preload_threshold_seconds"].is<int>()) {
      settings.preloadThresholdSeconds = globalSettings["preload_threshold_seconds"].as<float>();
    }
  }

  JsonArray assignments = gSettingsJsonDoc["midi_assignments"].as<JsonArray>();
  if (assignments.isNull()) {
    return true;
  }

  for (JsonObject assignment : assignments) {
    if (settings.assignmentCount >= SamplerSettings::kMaxAssignments) {
      Serial.println("Too many assignments in config, truncating");
      break;
    }

    const long note = assignment["note"] | -1;
    const char *pathCStr = assignment["sample_path"] | "";
    const String samplePath(pathCStr);

    if (!isValidNote(note) || !isNonEmptyPath(samplePath)) {
      continue;
    }

    settings.assignments[settings.assignmentCount].note = static_cast<uint8_t>(note);
    settings.assignments[settings.assignmentCount].samplePath = samplePath;
    settings.assignmentCount++;
  }

  return true;
}

bool saveToSd(const SamplerSettings &settings) {
  ensureParentDirectoryExists(kSettingsPath);

  gSettingsJsonDoc.clear();
  gSettingsJsonDoc["version"] =
      (settings.version.length() > 0) ? settings.version : String(kCurrentVersion);

  JsonObject globalSettings = gSettingsJsonDoc.createNestedObject("global_settings");
  globalSettings["sample_ram_budget_bytes"] = settings.sampleRamBudgetBytes;
  globalSettings["preload_threshold_seconds"] = settings.preloadThresholdSeconds;

  JsonArray assignments = gSettingsJsonDoc.createNestedArray("midi_assignments");
  for (int i = 0; i < settings.assignmentCount; i++) {
    const MidiAssignment &assignment = settings.assignments[i];
    if (!isValidNote(assignment.note) || !isNonEmptyPath(assignment.samplePath)) {
      continue;
    }

    JsonObject entry = assignments.createNestedObject();
    entry["note"] = assignment.note;
    entry["sample_path"] = assignment.samplePath;
  }

  SD.remove(kSettingsPath);
  File file = SD.open(kSettingsPath, FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open settings for write (%s)\n", kSettingsPath);
    return false;
  }

  if (serializeJsonPretty(gSettingsJsonDoc, file) == 0) {
    Serial.println("Failed to serialize settings JSON");
    file.close();
    return false;
  }

  file.println();
  file.close();
  return true;
}

}  // namespace SettingsStore
