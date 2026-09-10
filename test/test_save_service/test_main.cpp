#include <unity.h>
#include "sampler_save_service.h"
#include "../support/arduino_stubs.cpp"

namespace {
int polls, collectCalls, saveCalls, idleCalls;
bool sendOk, rebuildOk, sdOk, idleOk, panicOk;
constexpr int kDelayedPolls = 350;  // 7 seconds at the service's 20 ms poll interval.
}

BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t) {
  return sendOk ? pdTRUE : pdFALSE;
}
BaseType_t xQueueReceive(QueueHandle_t, void *value, TickType_t wait) {
  testSetMillis(millis() + wait);
  ++polls;
  auto &event = *static_cast<UiStatusEvent *>(value);
  if (polls == 1) {
    event.source = UiStatusSource::AudioEngine;
    event.type = UiStatusType::AudioTaskStarted;
    event.success = true;
    return pdTRUE;
  }
  if (polls < kDelayedPolls) return pdFALSE;
  event = UiStatusEvent{};
  event.success = rebuildOk;
  return pdTRUE;
}

bool TriggerEngine::waitForIdle(uint32_t) const { ++idleCalls; return idleOk; }
bool TriggerEngine::panicAll() { return panicOk; }
void SamplerRuntime::collectAssignmentsFromUi(const Ui &, const SampleLibrary::Catalog &) {
  ++collectCalls;
}
bool SamplerRuntime::saveSettingsToSd() const {
  TEST_ASSERT_EQUAL_INT(kDelayedPolls, polls);
  ++saveCalls;
  return sdOk;
}
#include "../../src/sampler_save_service.cpp"

void setUp() {
  polls = collectCalls = saveCalls = idleCalls = 0;
  sendOk = rebuildOk = sdOk = idleOk = panicOk = true;
  testSetMillis(0);
}
void tearDown() {}

bool save() {
  Ui ui;
  SampleLibrary::Catalog catalog;
  SamplerRuntime runtime;
  TriggerEngine trigger;
  SamplerSaveService service;
  int queue;
  service.begin(&ui, &catalog, &runtime, &trigger, &queue, &queue);
  return service.saveConfiguration();
}
void test_slow_rebuild_finishes_before_sd_save() {
  TEST_ASSERT_TRUE(save());
  TEST_ASSERT_GREATER_THAN_UINT32(5000, millis());
  TEST_ASSERT_EQUAL_INT(kDelayedPolls, polls);
  TEST_ASSERT_EQUAL_INT(1, collectCalls);
  TEST_ASSERT_EQUAL_INT(1, saveCalls);
  polls = 0;
  TEST_ASSERT_TRUE(save());
  TEST_ASSERT_EQUAL_INT(kDelayedPolls, polls);
  TEST_ASSERT_EQUAL_INT(2, saveCalls);
}
void test_loader_failure_does_not_write_sd() {
  rebuildOk = false;
  TEST_ASSERT_FALSE(save());
  TEST_ASSERT_EQUAL_INT(0, saveCalls);
}
void test_full_queue_does_not_wait_or_write() {
  sendOk = false;
  TEST_ASSERT_FALSE(save());
  TEST_ASSERT_EQUAL_INT(0, polls);
  TEST_ASSERT_EQUAL_INT(0, saveCalls);
}
void test_sd_failure_is_reported() {
  sdOk = false;
  TEST_ASSERT_FALSE(save());
  TEST_ASSERT_EQUAL_INT(1, saveCalls);
}
void test_failed_stop_does_not_rebuild() {
  idleOk = panicOk = false;
  TEST_ASSERT_FALSE(save());
  TEST_ASSERT_EQUAL_INT(0, collectCalls);
  TEST_ASSERT_EQUAL_INT(0, polls);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_slow_rebuild_finishes_before_sd_save);
  RUN_TEST(test_loader_failure_does_not_write_sd);
  RUN_TEST(test_full_queue_does_not_wait_or_write);
  RUN_TEST(test_sd_failure_is_reported);
  RUN_TEST(test_failed_stop_does_not_rebuild);
  return UNITY_END();
}
