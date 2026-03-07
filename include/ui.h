#pragma once

#include <Arduino.h>

class Ui {
 public:
  static constexpr int kMaxSamples = 32;

  enum class State : uint8_t {
    Main,
    Library,
    AssignNote,
    Saving,
  };

  enum class EventType : uint8_t {
    LeftRotate,
    LeftClick,
    RightRotate,
    RightClick,
    RightLongPress,
    MidiNoteOn,
  };

  struct Event {
    EventType type;
    int value;  // Rotation delta (+1/-1) or MIDI note number (0..127).
  };

  struct RenderModel {
    State state = State::Main;
    bool dirty = true;

    int mainSelection = 0;  // 0:LIB 1:VOL 2:PITCH 3:SAVE
    int currentSampleIndex = -1;
    int sampleCount = 0;

    int lastTriggeredSampleIndex = -1;
    String lastTriggeredSampleName = "-";
    int lastMidiNote = -1;

    int currentVolume = 0;
    int currentPitch = 0;

    int libraryWindowStart = 0;
    int assignedNoteForSelectedSample = -1;
    bool showSavedFeedback = false;
    bool lastSaveSucceeded = true;
  };

  using OnPreviewSampleCallback = void (*)(int sampleIndex, void *context);
  using OnSaveCallback = bool (*)(void *context);

  void begin(const String *sampleNames, const String *samplePaths, int sampleCount);
  void setPreviewCallback(OnPreviewSampleCallback callback, void *context);
  void setSaveCallback(OnSaveCallback callback, void *context);
  void handleEvent(const Event &event);
  void update();
  bool consumeDirty();

  const RenderModel &model() const;
  const String &samplePathAt(int sampleIndex) const;
  const String &sampleNameAt(int sampleIndex) const;
  bool hasSamples() const;
  bool setMidiAssignment(int note, int sampleIndex);
  int assignedSampleForMidiNote(int note) const;

 private:
  static constexpr int kMenuItems = 4;
  static constexpr unsigned long kSaveFeedbackMs = 1000;

  void markDirty();
  void updateDerivedModel();
  void handleMainEvent(const Event &event);
  void handleLibraryEvent(const Event &event);
  void handleAssignEvent(const Event &event);
  void transitionTo(State state);
  void startSave();
  void completeSave();
  void logNotImplemented(const char *functionName) const;
  void triggerPreview(int sampleIndex);
  static int wrapIndex(int value, int size);
  int findAssignedNoteForSample(int sampleIndex) const;

  const String *sampleNames_ = nullptr;
  const String *samplePaths_ = nullptr;
  int sampleCount_ = 0;

  int currentSampleIndex_ = -1;
  int lastTriggeredSampleIndex_ = -1;
  int lastMidiNote_ = -1;
  int mainSelection_ = 0;
  int libraryWindowStart_ = 0;
  int sampleVolumes_[kMaxSamples] = {0};
  int samplePitches_[kMaxSamples] = {0};
  int sampleForMidiNote_[128] = {0};  // -1 means unassigned.

  bool saveCompletedPending_ = false;
  bool lastSaveSucceeded_ = true;
  unsigned long saveFeedbackUntilMs_ = 0;

  State state_ = State::Main;
  RenderModel model_;
  OnPreviewSampleCallback onPreview_ = nullptr;
  void *previewContext_ = nullptr;
  OnSaveCallback onSave_ = nullptr;
  void *saveContext_ = nullptr;
};
