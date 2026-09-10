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
  catalog.checkedCount = catalog.rejectedCount = 0;
  for (int i = 0; i < Catalog::kMaxSamples; i++) {
    catalog.paths[i] = "";
    catalog.names[i] = "";
    catalog.validation[i] = {};
  }
}

void loadFromSd(Catalog &catalog, ValidationProgress progress, void *context) {
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
  if (progress) progress(context);
  class SdReader : public WavValidation::Reader {
   public:
    explicit SdReader(File &file) : file_(file) {}
    uint32_t size() const override { return file_.size(); }
    bool readAt(uint32_t offset, uint8_t *dst, size_t bytes) override {
      return file_.seek(offset) && file_.read(dst, bytes) == static_cast<int>(bytes);
    }
   private:
    File &file_;
  };
  for (int i = 0; i < catalog.count; ++i) {
    File file = SD.open(catalog.paths[i], FILE_READ);
    if (!file || file.isDirectory()) {
      catalog.validation[i].status = WavValidation::Status::ReadError;
    } else {
      SdReader reader(file);
      catalog.validation[i] = WavValidation::validate(reader);
    }
    file.close();
    ++catalog.checkedCount;
    if (!catalog.playable(i)) {
      ++catalog.rejectedCount;
      Serial.printf("WAV rejected: %s (%s)\n", catalog.paths[i].c_str(),
                    WavValidation::label(catalog.validation[i].status));
    }
    if (progress) progress(context);
  }
  
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
