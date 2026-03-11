#pragma once

#include <Arduino.h>

#include "ui.h"

namespace SampleLibrary {

struct Catalog {
  static constexpr int kMaxSamples = Ui::kMaxSamples;

  String paths[kMaxSamples];
  String names[kMaxSamples];
  int count = 0;
};

void clear(Catalog &catalog);
void loadFromSd(Catalog &catalog);
int findIndexByPath(const Catalog &catalog, const String &path);

}  // namespace SampleLibrary
