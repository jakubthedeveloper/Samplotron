#include "storage_sd.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>

#include "pins.h"

namespace StorageSD {

namespace {

// Long samples stream from SD in the audio task. At the library default of
// 4 MHz, three simultaneous streams need most of the audio core's time and
// synchronized 4 KiB refills outlast the I2S DMA buffer. Fall back to slower
// clocks if the wiring cannot mount the card at full speed.
constexpr uint32_t kSpiFrequenciesHz[] = {20000000, 10000000, 4000000};
// Each streamed voice keeps a file open; retriggers briefly need two. The
// library default of 5 leaves room for only a few simultaneous SD voices.
constexpr uint8_t kMaxOpenFiles = 16;
// Mounting reads only a few sectors, so wiring that is marginal at a given
// clock can still mount and then fail directory and WAV reads. Read a larger
// range twice before trusting a clock.
constexpr uint32_t kVerifySectorCount = 128;
constexpr size_t kSectorBytes = 512;

uint32_t mountedFrequencyHz = 0;

bool readsAreStable() {
  static uint8_t first[kSectorBytes];
  static uint8_t second[kSectorBytes];
  const size_t sectorCount = SD.numSectors();
  const uint32_t count =
      sectorCount < kVerifySectorCount ? static_cast<uint32_t>(sectorCount) : kVerifySectorCount;
  for (uint32_t sector = 0; sector < count; ++sector) {
    if (!SD.readRAW(first, sector) || !SD.readRAW(second, sector) ||
        memcmp(first, second, kSectorBytes) != 0) {
      Serial.printf("SD: unstable read of sector %lu\n", static_cast<unsigned long>(sector));
      return false;
    }
  }
  return count > 0;
}

}  // namespace

bool init() {
  pinMode(Pins::SD_CS, OUTPUT);
  digitalWrite(Pins::SD_CS, HIGH);
  delay(10);

  SPI.begin(Pins::SD_SCK, Pins::SD_MISO, Pins::SD_MOSI, Pins::SD_CS);
  delay(800);

  constexpr size_t kFrequencyCount = sizeof(kSpiFrequenciesHz) / sizeof(kSpiFrequenciesHz[0]);
  for (size_t i = 0; i < kFrequencyCount; ++i) {
    const uint32_t frequencyHz = kSpiFrequenciesHz[i];
    const bool slowest = (i + 1 == kFrequencyCount);
    if (SD.begin(Pins::SD_CS, SPI, frequencyHz, "/sd", kMaxOpenFiles)) {
      // The slowest clock is the previous known-good baseline; keep it even
      // if verification fails, as before this check existed.
      if (slowest || readsAreStable()) {
        mountedFrequencyHz = frequencyHz;
        Serial.printf("SD: mounted at %lu Hz\n", static_cast<unsigned long>(frequencyHz));
        return true;
      }
    }
    Serial.printf("SD: %lu Hz rejected\n", static_cast<unsigned long>(frequencyHz));
    SD.end();
  }

  Serial.println("SD: mount failed");
  mountedFrequencyHz = 0;
  return false;
}

uint32_t spiFrequencyHz() { return mountedFrequencyHz; }

}  // namespace StorageSD
