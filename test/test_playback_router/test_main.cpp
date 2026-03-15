#include <unity.h>

#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "Arduino.h"
#include "active_sample_registry.h"
#include "sample_ram_manager.h"
#include "sampler_playback_router.h"
#include "sampler_runtime.h"
#include "trigger_engine.h"
#include "ui.h"

#include "../support/arduino_stubs.cpp"

namespace {

struct TriggerStubState {
  std::vector<TriggerEvent> events;
  std::deque<bool> enqueueResults;
  int panicCalls = 0;
  bool panicResult = true;
};

struct RuntimeStubState {
  std::unordered_map<int, ActiveSampleRegistry::Entry> entryByNote;
  std::unordered_map<std::string, SampleClassifier::AssignedSampleClassification> classByPath;
};

TriggerStubState gTriggerStub;
std::unordered_map<const SamplerRuntime *, RuntimeStubState> gRuntimeState;
std::unordered_map<std::string, SampleRamManager::LoadedSampleData> gRamByPath;

constexpr int kSampleCount = 2;
const String kNames[kSampleCount] = {"Kick", "Snare"};
const String kPaths[kSampleCount] = {"/samples/kick.wav", "/samples/snare.wav"};

void resetTriggerStub() {
  gTriggerStub.events.clear();
  gTriggerStub.enqueueResults.clear();
  gTriggerStub.panicCalls = 0;
  gTriggerStub.panicResult = true;
}

void setTriggerEnqueueResults(std::initializer_list<bool> results) {
  gTriggerStub.enqueueResults = std::deque<bool>(results);
}

void setRegistryEntry(const SamplerRuntime &runtime, int note, const ActiveSampleRegistry::Entry &entry) {
  gRuntimeState[&runtime].entryByNote[note] = entry;
}

void setClassification(const SamplerRuntime &runtime,
                       const String &path,
                       const SampleClassifier::AssignedSampleClassification &classification) {
  gRuntimeState[&runtime].classByPath[path.c_str()] = classification;
}

void clearRuntimeState(const SamplerRuntime &runtime) {
  gRuntimeState.erase(&runtime);
}

void setLoadedRamSample(const String &path, const uint8_t *data, uint32_t bytes) {
  SampleRamManager::LoadedSampleData sample;
  sample.data = data;
  sample.dataBytes = bytes;
  gRamByPath[path.c_str()] = sample;
}

void clearLoadedRamSamples() {
  gRamByPath.clear();
}

Ui createUi() {
  Ui ui;
  ui.begin(kNames, kPaths, kSampleCount);
  return ui;
}

SampleLibrary::Catalog createCatalog() {
  SampleLibrary::Catalog catalog;
  catalog.count = kSampleCount;
  catalog.names[0] = kNames[0];
  catalog.names[1] = kNames[1];
  catalog.paths[0] = kPaths[0];
  catalog.paths[1] = kPaths[1];
  return catalog;
}

void test_preview_enqueues_stream_event_with_ui_volume_and_path() {
  Ui ui = createUi();
  const SampleLibrary::Catalog catalog = createCatalog();
  SamplerRuntime runtime;
  TriggerEngine trigger;
  SamplerPlaybackRouter router;
  router.begin(&ui, &catalog, &runtime, &trigger);

  TEST_ASSERT_TRUE(ui.setSampleVolume(1, 65));

  router.onPreviewSample(1);

  TEST_ASSERT_EQUAL_UINT32(1, gTriggerStub.events.size());
  const TriggerEvent &event = gTriggerStub.events[0];
  TEST_ASSERT_EQUAL(TriggerSourceType::StreamPath, event.source);
  TEST_ASSERT_EQUAL_UINT8(65, event.volume);
  TEST_ASSERT_EQUAL_INT16(1, event.retriggerGroupId);
  TEST_ASSERT_EQUAL_STRING("/samples/snare.wav", event.path);
}

void test_assigned_note_without_assignment_clears_last_triggered_sample() {
  Ui ui = createUi();
  const SampleLibrary::Catalog catalog = createCatalog();
  SamplerRuntime runtime;
  TriggerEngine trigger;
  SamplerPlaybackRouter router;
  router.begin(&ui, &catalog, &runtime, &trigger);

  ui.reportTriggeredSample(1);
  TEST_ASSERT_EQUAL_INT(1, ui.model().lastTriggeredSampleIndex);

  router.onAssignedMidiNoteOn(60);

  TEST_ASSERT_EQUAL_INT(-1, ui.model().lastTriggeredSampleIndex);
  TEST_ASSERT_EQUAL_UINT32(0, gTriggerStub.events.size());
}

void test_panic_note_calls_trigger_engine_panic_and_skips_playback() {
  Ui ui = createUi();
  const SampleLibrary::Catalog catalog = createCatalog();
  SamplerRuntime runtime;
  TriggerEngine trigger;
  SamplerPlaybackRouter router;
  router.begin(&ui, &catalog, &runtime, &trigger);

  TEST_ASSERT_TRUE(ui.setMidiAssignment(60, 1));
  TEST_ASSERT_TRUE(ui.setPanicMidiNote(60));

  router.onAssignedMidiNoteOn(60);

  TEST_ASSERT_EQUAL_INT(1, gTriggerStub.panicCalls);
  TEST_ASSERT_EQUAL_UINT32(0, gTriggerStub.events.size());
}

void test_assigned_note_unavailable_does_not_enqueue() {
  Ui ui = createUi();
  const SampleLibrary::Catalog catalog = createCatalog();
  SamplerRuntime runtime;
  TriggerEngine trigger;
  SamplerPlaybackRouter router;
  router.begin(&ui, &catalog, &runtime, &trigger);

  TEST_ASSERT_TRUE(ui.setMidiAssignment(60, 1));

  ActiveSampleRegistry::Entry entry;
  entry.note = 60;
  entry.path = "/samples/snare.wav";
  entry.effectiveMode = ActiveSampleRegistry::EffectiveStorageMode::Unavailable;
  setRegistryEntry(runtime, 60, entry);

  router.onAssignedMidiNoteOn(60);

  TEST_ASSERT_EQUAL_UINT32(0, gTriggerStub.events.size());
  clearRuntimeState(runtime);
}

void test_assigned_note_ram_mode_enqueues_ram_event() {
  Ui ui = createUi();
  const SampleLibrary::Catalog catalog = createCatalog();
  SamplerRuntime runtime;
  TriggerEngine trigger;
  SamplerPlaybackRouter router;
  router.begin(&ui, &catalog, &runtime, &trigger);

  static const uint8_t kRamData[8] = {1, 2, 3, 4, 5, 6, 7, 8};

  TEST_ASSERT_TRUE(ui.setMidiAssignment(60, 0));
  TEST_ASSERT_TRUE(ui.setSampleVolume(0, 88));

  ActiveSampleRegistry::Entry entry;
  entry.note = 60;
  entry.path = "/samples/kick.wav";
  entry.effectiveMode = ActiveSampleRegistry::EffectiveStorageMode::Ram;
  setRegistryEntry(runtime, 60, entry);

  SampleClassifier::AssignedSampleClassification cls;
  cls.path = "/samples/kick.wav";
  cls.channelCount = 1;
  cls.sampleRate = 44100;
  cls.bitsPerSample = 16;
  setClassification(runtime, "/samples/kick.wav", cls);

  setLoadedRamSample("/samples/kick.wav", kRamData, sizeof(kRamData));

  router.onAssignedMidiNoteOn(60);

  TEST_ASSERT_EQUAL_UINT32(1, gTriggerStub.events.size());
  const TriggerEvent &event = gTriggerStub.events[0];
  TEST_ASSERT_EQUAL(TriggerSourceType::RamData, event.source);
  TEST_ASSERT_EQUAL_UINT8(88, event.volume);
  TEST_ASSERT_EQUAL_INT16(0, event.retriggerGroupId);
  TEST_ASSERT_EQUAL_PTR(kRamData, event.ramData);
  TEST_ASSERT_EQUAL_UINT32(sizeof(kRamData), event.ramDataBytes);
  TEST_ASSERT_EQUAL_UINT16(1, event.channelCount);
  TEST_ASSERT_EQUAL_UINT32(44100, event.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(16, event.bitsPerSample);

  clearRuntimeState(runtime);
}

void test_assigned_note_ram_enqueue_failure_falls_back_to_stream() {
  Ui ui = createUi();
  const SampleLibrary::Catalog catalog = createCatalog();
  SamplerRuntime runtime;
  TriggerEngine trigger;
  SamplerPlaybackRouter router;
  router.begin(&ui, &catalog, &runtime, &trigger);

  static const uint8_t kRamData[4] = {9, 8, 7, 6};

  TEST_ASSERT_TRUE(ui.setMidiAssignment(60, 0));

  ActiveSampleRegistry::Entry entry;
  entry.note = 60;
  entry.path = "/samples/kick.wav";
  entry.effectiveMode = ActiveSampleRegistry::EffectiveStorageMode::Ram;
  setRegistryEntry(runtime, 60, entry);

  SampleClassifier::AssignedSampleClassification cls;
  cls.path = "/samples/kick.wav";
  cls.channelCount = 1;
  cls.sampleRate = 44100;
  cls.bitsPerSample = 16;
  setClassification(runtime, "/samples/kick.wav", cls);
  setLoadedRamSample("/samples/kick.wav", kRamData, sizeof(kRamData));

  setTriggerEnqueueResults({false, true});

  router.onAssignedMidiNoteOn(60);

  TEST_ASSERT_EQUAL_UINT32(2, gTriggerStub.events.size());
  TEST_ASSERT_EQUAL(TriggerSourceType::RamData, gTriggerStub.events[0].source);
  TEST_ASSERT_EQUAL_INT16(0, gTriggerStub.events[0].retriggerGroupId);
  TEST_ASSERT_EQUAL(TriggerSourceType::StreamPath, gTriggerStub.events[1].source);
  TEST_ASSERT_EQUAL_INT16(0, gTriggerStub.events[1].retriggerGroupId);
  TEST_ASSERT_EQUAL_STRING("/samples/kick.wav", gTriggerStub.events[1].path);

  clearRuntimeState(runtime);
}

}  // namespace

void setUp() {
  testSetMillis(0);
  resetTriggerStub();
  clearLoadedRamSamples();
  gRuntimeState.clear();
}

void tearDown() {}

bool TriggerEngine::begin(Audio *,
                          UBaseType_t,
                          BaseType_t,
                          uint16_t,
                          uint16_t,
                          QueueHandle_t) {
  return true;
}

bool TriggerEngine::enqueue(const TriggerEvent &event) {
  gTriggerStub.events.push_back(event);
  if (gTriggerStub.enqueueResults.empty()) {
    return true;
  }
  const bool result = gTriggerStub.enqueueResults.front();
  gTriggerStub.enqueueResults.pop_front();
  return result;
}

bool TriggerEngine::panicAll() {
  gTriggerStub.panicCalls++;
  return gTriggerStub.panicResult;
}

bool TriggerEngine::waitForIdle(uint32_t) const {
  return true;
}

const ActiveSampleRegistry::Entry *SamplerRuntime::findRegistryEntryForNote(int note) const {
  const auto runtimeIt = gRuntimeState.find(this);
  if (runtimeIt == gRuntimeState.end()) {
    return nullptr;
  }
  const auto it = runtimeIt->second.entryByNote.find(note);
  if (it == runtimeIt->second.entryByNote.end()) {
    return nullptr;
  }
  return &it->second;
}

const SampleClassifier::AssignedSampleClassification *SamplerRuntime::findClassificationByPath(
    const String &path) const {
  const auto runtimeIt = gRuntimeState.find(this);
  if (runtimeIt == gRuntimeState.end()) {
    return nullptr;
  }
  const auto it = runtimeIt->second.classByPath.find(path.c_str());
  if (it == runtimeIt->second.classByPath.end()) {
    return nullptr;
  }
  return &it->second;
}

namespace ActiveSampleRegistry {

const char *effectiveStorageModeLabel(EffectiveStorageMode mode) {
  switch (mode) {
    case EffectiveStorageMode::Ram:
      return "RAM";
    case EffectiveStorageMode::Stream:
      return "STREAM";
    case EffectiveStorageMode::Unavailable:
      return "UNAVAILABLE";
  }
  return "UNKNOWN";
}

}  // namespace ActiveSampleRegistry

namespace SampleRamManager {

bool getLoadedSampleDataByPath(const String &path, LoadedSampleData &data) {
  const auto it = gRamByPath.find(path.c_str());
  if (it == gRamByPath.end()) {
    data = LoadedSampleData{};
    return false;
  }
  data = it->second;
  return true;
}

}  // namespace SampleRamManager

#include "../../src/ui.cpp"
#include "../../src/sampler_playback_router.cpp"

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_preview_enqueues_stream_event_with_ui_volume_and_path);
  RUN_TEST(test_assigned_note_without_assignment_clears_last_triggered_sample);
  RUN_TEST(test_panic_note_calls_trigger_engine_panic_and_skips_playback);
  RUN_TEST(test_assigned_note_unavailable_does_not_enqueue);
  RUN_TEST(test_assigned_note_ram_mode_enqueues_ram_event);
  RUN_TEST(test_assigned_note_ram_enqueue_failure_falls_back_to_stream);
  return UNITY_END();
}
