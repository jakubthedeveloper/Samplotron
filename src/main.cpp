#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "driver/i2s.h"

// ESP8266Audio library uses separate source/generator/output classes now
#include "AudioFileSourceSD.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"  // change to NoDAC if you don't have a DAC

// objects used for playback
AudioGeneratorWAV *wav = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputI2S *out = nullptr;
bool i2sWasStarted = false;

// ===== przyciski =====

#define NUM_KEYS 2

// Use KEY1 and KEY3 for sample triggering.
const int KEY1_PIN = 36;
const int KEY3_PIN = 19;
const int keys[NUM_KEYS] = {KEY1_PIN, KEY3_PIN};
int lastReadKeyState[NUM_KEYS] = {HIGH, HIGH};
int stableKeyState[NUM_KEYS] = {HIGH, HIGH};
unsigned long lastDebounceMs[NUM_KEYS] = {0, 0};
const unsigned long DEBOUNCE_MS = 35;


#define SD_CS   13
#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCK  14

// ===== I2S =====
#define I2S_BCLK 27
#define I2S_LRC  25
#define I2S_DOUT 26
#define I2S_MCLK 0

// ===== ES8388 codec on ESP-WROVER-KIT =====
#define ES8388_ADDR 0x10
#define I2C_SCL 32
#define I2C_SDA 33
#define GPIO_PA_EN 21

static bool codecWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8388_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool codecRead(uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(ES8388_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)ES8388_ADDR, (uint8_t)1) != 1) return false;
  val = Wire.read();
  return true;
}

static void setCodecHeadphoneVolume(uint8_t percent) {
  // ES8388 OUT2 volume registers: 0x30/0x31 (0x00..0x21), where 0x1E is 0 dB.
  uint8_t p = constrain(percent, 0, 100);
  uint8_t v = (uint8_t)((0x21UL * p) / 100UL);
  codecWrite(0x30, v);
  codecWrite(0x31, v);
}

static void setCodecSpeakerVolume(uint8_t percent) {
  // ES8388 OUT1 volume registers: 0x2E/0x2F (0x00..0x21).
  uint8_t p = constrain(percent, 0, 100);
  uint8_t v = (uint8_t)((0x21UL * p) / 100UL);
  codecWrite(0x2E, v);
  codecWrite(0x2F, v);
}

static void setCodecMainDacVolume(uint8_t percent) {
  // ES8388 main DAC volume registers: 0x1A/0x1B, reversed scale (0 = loudest).
  uint8_t p = constrain(percent, 0, 100);
  uint8_t v = (uint8_t)(96UL - ((96UL * p) / 100UL));
  codecWrite(0x1A, v);
  codecWrite(0x1B, v);
}

static void unmuteCodecOutputs() {
  uint8_t v = 0;
  if (codecRead(0x19, v)) codecWrite(0x19, (uint8_t)(v & ~(1 << 2))); // unmute main DAC
  if (codecRead(0x04, v)) codecWrite(0x04, (uint8_t)(v | (3 << 4) | (3 << 2))); // enable OUT1+OUT2
}

static bool initCodecES8388() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000U);
  Wire.beginTransmission(ES8388_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("ES8388 not found on I2C");
    return false;
  }

  const uint8_t initSeq[][2] = {
    {0x19, 0x04}, {0x01, 0x50}, {0x02, 0x00}, {0x08, 0x00},
    {0x04, 0x3e}, {0x00, 0x12}, {0x17, 0x18}, {0x18, 0x02},
    {0x26, 0x1B}, {0x27, 0x90}, {0x2A, 0x90}, {0x2B, 0x80},
    {0x2D, 0x00}, {0x1B, 0x00}, {0x1A, 0x00}, {0x03, 0xff},
    {0x09, 0x88}, {0x0a, 0xf0}, {0x0b, 0x80}, {0x0c, 0x0e},
    {0x0d, 0x02}, {0x10, 0x20}, {0x11, 0x20}, {0x2e, 0x1e},
    {0x2f, 0x1e}, {0x30, 0x1e}, {0x31, 0x1e}, {0x04, 0x3c},
    {0x19, 0x00}, {0x03, 0x00}
  };

  for (size_t i = 0; i < sizeof(initSeq) / sizeof(initSeq[0]); i++) {
    if (!codecWrite(initSeq[i][0], initSeq[i][1])) {
      Serial.printf("ES8388 init failed at reg 0x%02X\n", initSeq[i][0]);
      return false;
    }
  }

#ifdef FUNC_GPIO0_CLK_OUT1
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
#endif
  WRITE_PERI_REG(PIN_CTRL, 0xFFF0);

  pinMode(GPIO_PA_EN, OUTPUT);
  digitalWrite(GPIO_PA_EN, HIGH);

  // Explicit output activation and volume, to avoid silent analog path.
  unmuteCodecOutputs();
  setCodecMainDacVolume(100);
  setCodecHeadphoneVolume(100);
  setCodecSpeakerVolume(100);

  uint8_t hpL = 0, hpR = 0, mainL = 0;
  if (codecRead(0x30, hpL) && codecRead(0x31, hpR) && codecRead(0x1A, mainL)) {
    Serial.printf("Codec volume HP(L/R)=0x%02X/0x%02X MAIN=0x%02X\n", hpL, hpR, mainL);
  }
  return true;
}

static void resetI2SIfNeeded() {
  if (!i2sWasStarted) return;
  // ESP8266Audio (older versions) does not uninstall the ESP32 I2S driver on stop().
  // Explicitly uninstalling here avoids "register I2S object to platform failed".
  esp_err_t err = i2s_driver_uninstall((i2s_port_t)0);
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    i2sWasStarted = false;
  } else {
    Serial.printf("I2S uninstall error: %d\n", (int)err);
  }
}

void playSample(int n) {
  String path = "/samples/test" + String(n) + ".wav";
  Serial.println(path);

  // Stop previous playback cleanly before opening a new file.
  if (wav && wav->isRunning()) {
    wav->stop();
  }

  // clean up old file object
  if (file) {
    file->close();
    delete file;
    file = nullptr;
  }

  file = new AudioFileSourceSD(path.c_str());
  if (wav && file && file->isOpen()) {
    resetI2SIfNeeded();
    if (!wav->begin(file, out)) {
      Serial.println("WAV start failed");
    } else {
      Serial.println("WAV started");
      i2sWasStarted = true;
    }
  } else {
    Serial.println("Sample open failed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  for (int i = 0; i < NUM_KEYS; i++) {
    pinMode(keys[i], INPUT_PULLUP);
    int s = digitalRead(keys[i]);
    lastReadKeyState[i] = s;
    stableKeyState[i] = s;
    lastDebounceMs[i] = millis();
  }

  // Trzymaj CS wysoko zanim odpalisz SPI
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(10);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  delay(800); // daj czas karcie / zasilaniu

  if (!SD.begin(SD_CS)) {
    Serial.println("SD FAIL (check CS pin / wiring / card).");
    while (true) delay(1000);
  }
  Serial.println("SD OK");

  if (!initCodecES8388()) {
    Serial.println("Codec init failed");
  } else {
    Serial.println("Codec OK");
  }

  // create audio output and generator
  out = new AudioOutputI2S(
    0,
    AudioOutputI2S::EXTERNAL_I2S,
    8,
    AudioOutputI2S::APLL_ENABLE
  );
  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  out->SetGain(1.0);

  wav = new AudioGeneratorWAV();

  Serial.println("Sampler ready");
}

void loop() {
// run the audio generator if it's playing
    if (wav && wav->isRunning()) {
      if (!wav->loop()) {
        wav->stop();
        Serial.println("WAV finished");
      }
    }

  for (int i = 0; i < NUM_KEYS; i++) {
    int raw = digitalRead(keys[i]);

    if (raw != lastReadKeyState[i]) {
      lastReadKeyState[i] = raw;
      lastDebounceMs[i] = millis();
    }

    if ((millis() - lastDebounceMs[i]) >= DEBOUNCE_MS && raw != stableKeyState[i]) {
      stableKeyState[i] = raw;
      // Trigger only on press (active LOW), never on release.
      if (stableKeyState[i] == LOW) {
        playSample(i + 1);
      }
    }
  }
}
