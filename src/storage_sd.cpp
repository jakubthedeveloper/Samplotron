#include "storage_sd.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "pins.h"

namespace StorageSD {

bool init() {
  pinMode(Pins::SD_CS, OUTPUT);
  digitalWrite(Pins::SD_CS, HIGH);
  delay(10);

  SPI.begin(Pins::SD_SCK, Pins::SD_MISO, Pins::SD_MOSI, Pins::SD_CS);
  delay(800);

  if (!SD.begin(Pins::SD_CS)) {
    Serial.println("SD FAIL (check CS pin / wiring / card).");
    return false;
  }

  Serial.println("SD OK");
  return true;
}

}  // namespace StorageSD
