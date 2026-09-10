#pragma once

#include <Arduino.h>

#include "ui.h"
#include "wav_validation.h"

namespace SampleLibrary {

struct Catalog {
  static constexpr int kMaxSamples = Ui::kMaxSamples;

  String paths[kMaxSamples];
  String names[kMaxSamples];
  WavValidation::Result validation[kMaxSamples];
  int count = 0;
  int checkedCount = 0;
  int rejectedCount = 0;
  bool playable(int index) const {
    return index >= 0 && index < count && validation[index].playable();
  }
};

void clear(Catalog &catalog);
using ValidationProgress = void (*)(void *context);
void loadFromSd(Catalog &catalog, ValidationProgress progress = nullptr, void *context = nullptr);
int findIndexByPath(const Catalog &catalog, const String &path);

}  // namespace SampleLibrary
