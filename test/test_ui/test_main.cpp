#include <unity.h>

#include "Arduino.h"
#include "input_ui_bridge.h"
#include "ui.h"

// Build selected production units directly for native tests.
#include "../support/arduino_stubs.cpp"
#include "../../src/input_ui_bridge.cpp"
#include "../../src/ui.cpp"

namespace {

constexpr int kSampleCount = 3;
const String kNames[kSampleCount] = {"Kick", "Snare", "Hat"};
const String kPaths[kSampleCount] = {"/samples/kick.wav", "/samples/snare.wav", "/samples/hat.wav"};

int gPreviewCalls = 0;
int gLastPreviewIndex = -1;
bool gSaveResult = true;
int gSaveCalls = 0;

void onPreview(int sampleIndex, void *) {
  gPreviewCalls++;
  gLastPreviewIndex = sampleIndex;
}

bool onSave(void *) {
  gSaveCalls++;
  return gSaveResult;
}

Ui createUi() {
  Ui ui;
  ui.begin(kNames, kPaths, kSampleCount);
  ui.setPreviewCallback(onPreview, nullptr);
  ui.setSaveCallback(onSave, nullptr);
  return ui;
}

void test_begin_sets_initial_model() {
  Ui ui = createUi();
  const Ui::RenderModel &model = ui.model();

  TEST_ASSERT_EQUAL(Ui::State::Main, model.state);
  TEST_ASSERT_TRUE(ui.hasSamples());
  TEST_ASSERT_EQUAL_INT(0, model.currentSampleIndex);
  TEST_ASSERT_EQUAL_INT(kSampleCount, model.sampleCount);
  TEST_ASSERT_EQUAL_INT(-1, ui.assignedSampleForMidiNote(60));
  TEST_ASSERT_EQUAL_INT(100, ui.sampleVolumeForSample(0));
}

void test_midi_assignment_keeps_sample_unique() {
  Ui ui = createUi();

  TEST_ASSERT_TRUE(ui.setMidiAssignment(60, 1));
  TEST_ASSERT_EQUAL_INT(1, ui.assignedSampleForMidiNote(60));

  TEST_ASSERT_TRUE(ui.setMidiAssignment(61, 1));
  TEST_ASSERT_EQUAL_INT(-1, ui.assignedSampleForMidiNote(60));
  TEST_ASSERT_EQUAL_INT(1, ui.assignedSampleForMidiNote(61));
}

void test_input_ui_bridge_maps_right_click_to_library_transition() {
  Ui ui = createUi();

  Input::Event event;
  event.type = Input::EventType::RightClick;
  event.value = 0;
  InputUiBridge::routeToUi(event, ui);

  TEST_ASSERT_EQUAL(Ui::State::Library, ui.model().state);
}

void test_library_preview_triggers_callback_and_marks_last_sample() {
  Ui ui = createUi();

  ui.handleEvent({Ui::EventType::RightClick, 0});
  ui.handleEvent({Ui::EventType::RightRotate, 1});
  ui.handleEvent({Ui::EventType::RightClick, 0});

  TEST_ASSERT_EQUAL_INT(1, gPreviewCalls);
  TEST_ASSERT_EQUAL_INT(1, gLastPreviewIndex);
  TEST_ASSERT_EQUAL_INT(1, ui.model().lastTriggeredSampleIndex);
}

void test_library_left_rotate_toggles_panic_assign_mode() {
  Ui ui = createUi();

  ui.handleEvent({Ui::EventType::RightClick, 0});
  TEST_ASSERT_FALSE(ui.model().libraryAssignsPanic);

  ui.handleEvent({Ui::EventType::LeftRotate, 1});
  TEST_ASSERT_TRUE(ui.model().libraryAssignsPanic);

  ui.handleEvent({Ui::EventType::LeftRotate, 1});
  TEST_ASSERT_FALSE(ui.model().libraryAssignsPanic);
}

void test_assign_panic_note_from_library_mode() {
  Ui ui = createUi();

  ui.handleEvent({Ui::EventType::RightClick, 0});
  ui.handleEvent({Ui::EventType::LeftRotate, 1});
  ui.handleEvent({Ui::EventType::RightLongPress, 0});
  TEST_ASSERT_EQUAL(Ui::State::AssignNote, ui.model().state);
  TEST_ASSERT_TRUE(ui.model().assigningPanic);

  ui.handleEvent({Ui::EventType::MidiNoteOn, 42});

  TEST_ASSERT_EQUAL(Ui::State::Library, ui.model().state);
  TEST_ASSERT_EQUAL_INT(42, ui.panicMidiNote());
  TEST_ASSERT_FALSE(ui.model().assigningPanic);
  TEST_ASSERT_TRUE(ui.model().hasUnsavedChanges);
}

void test_library_right_rotate_is_ignored_in_panic_mode() {
  Ui ui = createUi();

  ui.handleEvent({Ui::EventType::RightClick, 0});
  ui.handleEvent({Ui::EventType::RightRotate, 1});
  TEST_ASSERT_EQUAL_INT(1, ui.model().currentSampleIndex);

  ui.handleEvent({Ui::EventType::LeftRotate, 1});
  TEST_ASSERT_TRUE(ui.model().libraryAssignsPanic);

  ui.handleEvent({Ui::EventType::RightRotate, 1});
  TEST_ASSERT_EQUAL_INT(1, ui.model().currentSampleIndex);
}

void test_main_menu_hides_vol_and_shot_without_last_sample() {
  Ui ui = createUi();

  ui.handleEvent({Ui::EventType::LeftRotate, 1});
  TEST_ASSERT_EQUAL_INT(0, ui.model().mainSelection);

  ui.handleEvent({Ui::EventType::RightClick, 0});
  TEST_ASSERT_EQUAL(Ui::State::Library, ui.model().state);
}

void test_main_menu_shot_loop_toggle_marks_unsaved_changes() {
  Ui ui = createUi();
  ui.reportTriggeredSample(0);

  TEST_ASSERT_EQUAL(Ui::PlaybackMode::OneShot, ui.playbackModeForSample(0));
  TEST_ASSERT_FALSE(ui.model().hasUnsavedChanges);

  ui.handleEvent({Ui::EventType::LeftRotate, 2});  // LIB -> VOL -> SHOT/LOOP
  TEST_ASSERT_EQUAL_INT(2, ui.model().mainSelection);

  ui.handleEvent({Ui::EventType::RightClick, 0});
  TEST_ASSERT_EQUAL(Ui::PlaybackMode::Loop, ui.playbackModeForSample(0));
  TEST_ASSERT_TRUE(ui.model().hasUnsavedChanges);

  ui.handleEvent({Ui::EventType::RightRotate, 1});
  TEST_ASSERT_EQUAL(Ui::PlaybackMode::OneShot, ui.playbackModeForSample(0));
}

void test_main_menu_hides_vol_and_shot_after_last_sample_clear() {
  Ui ui = createUi();
  ui.reportTriggeredSample(0);

  ui.handleEvent({Ui::EventType::LeftRotate, 2});  // SHOT/LOOP
  TEST_ASSERT_EQUAL_INT(2, ui.model().mainSelection);

  ui.clearTriggeredSample();
  TEST_ASSERT_EQUAL_INT(0, ui.model().mainSelection);
}

void test_save_flow_executes_callback_and_clears_unsaved_changes_after_delay() {
  Ui ui = createUi();

  TEST_ASSERT_TRUE(ui.setSampleVolume(0, 70));
  ui.reportTriggeredSample(0);

  ui.handleEvent({Ui::EventType::LeftRotate, 3});
  TEST_ASSERT_EQUAL_INT(3, ui.model().mainSelection);

  ui.handleEvent({Ui::EventType::RightClick, 0});
  TEST_ASSERT_EQUAL(Ui::State::Saving, ui.model().state);

  ui.update();
  TEST_ASSERT_EQUAL_INT(0, gSaveCalls);

  ui.update();
  TEST_ASSERT_EQUAL_INT(1, gSaveCalls);
  TEST_ASSERT_EQUAL(Ui::State::Saving, ui.model().state);

  testAdvanceMillis(1000);
  ui.update();
  TEST_ASSERT_EQUAL(Ui::State::Main, ui.model().state);
  TEST_ASSERT_FALSE(ui.model().hasUnsavedChanges);
  TEST_ASSERT_TRUE(ui.model().showSavedFeedback);

  testAdvanceMillis(1000);
  ui.update();
  TEST_ASSERT_FALSE(ui.model().showSavedFeedback);
}

}  // namespace

void setUp() {
  testSetMillis(0);
  gPreviewCalls = 0;
  gLastPreviewIndex = -1;
  gSaveResult = true;
  gSaveCalls = 0;
}

void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_begin_sets_initial_model);
  RUN_TEST(test_midi_assignment_keeps_sample_unique);
  RUN_TEST(test_input_ui_bridge_maps_right_click_to_library_transition);
  RUN_TEST(test_library_preview_triggers_callback_and_marks_last_sample);
  RUN_TEST(test_library_left_rotate_toggles_panic_assign_mode);
  RUN_TEST(test_assign_panic_note_from_library_mode);
  RUN_TEST(test_library_right_rotate_is_ignored_in_panic_mode);
  RUN_TEST(test_main_menu_hides_vol_and_shot_without_last_sample);
  RUN_TEST(test_main_menu_shot_loop_toggle_marks_unsaved_changes);
  RUN_TEST(test_main_menu_hides_vol_and_shot_after_last_sample_clear);
  RUN_TEST(test_save_flow_executes_callback_and_clears_unsaved_changes_after_delay);
  return UNITY_END();
}
