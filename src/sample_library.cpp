#include "sample_library.h"

#include <SD.h>

namespace {

bool isWavFile(const String &name) {
  return name.endsWith(".wav") || name.endsWith(".WAV");
}

void sortByName(SampleLibrary::Catalog &catalog) {
  for (int i = 0; i < catalog.count - 1; i++) {
    for (int j = i + 1; j < catalog.count; j++) {
      if (catalog.names[j] < catalog.names[i]) {
        String n = catalog.names[i];
        String p = catalog.paths[i];
        catalog.names[i] = catalog.names[j];
        catalog.paths[i] = catalog.paths[j];
        catalog.names[j] = n;
        catalog.paths[j] = p;
      }
    }
  }
}

}  // namespace

namespace SampleLibrary {

void clear(Catalog &catalog) {
  catalog.count = 0;
  for (int i = 0; i < Catalog::kMaxSamples; i++) {
    catalog.paths[i] = "";
    catalog.names[i] = "";
  }
}

void loadFromSd(Catalog &catalog) {
  clear(catalog);

  File dir = SD.open("/samples");
  if (!dir || !dir.isDirectory()) {
    
    return;
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      String entryName = entry.name();
      int slash = entryName.lastIndexOf('/');
      String fileName = (slash >= 0) ? entryName.substring(slash + 1) : entryName;
      if (isWavFile(fileName) && catalog.count < Catalog::kMaxSamples) {
        catalog.paths[catalog.count] = "/samples/" + fileName;
        catalog.names[catalog.count] = fileName;
        catalog.count++;
      }
    }
    entry.close();
  }
  dir.close();

  sortByName(catalog);
  
}

int findIndexByPath(const Catalog &catalog, const String &path) {
  for (int i = 0; i < catalog.count; i++) {
    if (catalog.paths[i] == path) {
      return i;
    }
  }
  return -1;
}

}  // namespace SampleLibrary
