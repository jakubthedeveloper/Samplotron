#pragma once

#include <Arduino.h>
#include <stdint.h>

class CalculatorKeypad {
 public:
  using OnNoteOnCallback = void (*)(int midiNote, void *context);

  void begin();
  void update(OnNoteOnCallback callback, void *context);

 private:
  enum class ObservationKind : uint8_t {
    None,
    Single,
    Multiple,
  };

  struct Pair {
    uint8_t first;
    uint8_t second;
  };

  struct Observation {
    ObservationKind kind;
    Pair pair;
  };

  bool initializeMcp();
  bool restoreAllInputs();
  bool scan(Observation &observation);
  static bool sameObservation(const Observation &left, const Observation &right);
  static int midiNoteForPair(const Pair &pair);
  void resetDebounce(uint32_t now);
  void updateDebounce(const Observation &observation,
                      uint32_t now,
                      OnNoteOnCallback callback,
                      void *context);

  bool ready_ = false;
  Observation candidate_ = {ObservationKind::None, {0, 0}};
  Observation stable_ = {ObservationKind::None, {0, 0}};
  uint32_t candidateSinceMs_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t lastRetryMs_ = 0;
};
