#include "storage_sd.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

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

uint32_t mountedFrequencyHz = 0;

}  // namespace

bool init() {
  pinMode(Pins::SD_CS, OUTPUT);
  digitalWrite(Pins::SD_CS, HIGH);
  delay(10);

  SPI.begin(Pins::SD_SCK, Pins::SD_MISO, Pins::SD_MOSI, Pins::SD_CS);
  delay(800);

  for (const uint32_t frequencyHz : kSpiFrequenciesHz) {
    if (SD.begin(Pins::SD_CS, SPI, frequencyHz, "/sd", kMaxOpenFiles)) {
      mountedFrequencyHz = frequencyHz;
      Serial.printf("SD: mounted at %lu Hz\n", static_cast<unsigned long>(frequencyHz));
      return true;
    }
    SD.end();
  }

  mountedFrequencyHz = 0;
  return false;
}

uint32_t spiFrequencyHz() { return mountedFrequencyHz; }

}  // namespace StorageSD
