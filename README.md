# Samplotron

## Overview

Samplotron is an ESP32-based sample player focused on fast hardware triggering and a clean architecture for future expansion.

The project currently supports:
- WAV sample playback from SD card
- ES8388 codec initialization and audio output
- Triggering samples from onboard buttons
- SSD1309 OLED status screen

The codebase is structured into separate modules (`audio`, `codec`, `storage`, `input`, `display`) so new features like MIDI and menu-driven UI can be added without turning `main.cpp` into a monolith.

## OLED Wiring (2.42" SSD1309, SPI)

Current firmware uses the display in 4-wire SPI mode with this mapping:

- `OLED GND` -> `ESP32 GND`
- `OLED VCC` -> `ESP32 3V3`
- `OLED SCK` -> `ESP32 IO18`
- `OLED SDA (MOSI)` -> `ESP32 IO23`
- `OLED CS` -> `ESP32 IO5`
- `OLED DC` -> `ESP32 IO22`
- `OLED RES` -> `ESP32 IO21`

Notes:
- Display logic should run at 3.3V.
- This mapping matches the current code in `include/pins.h` and `src/display_ssd1309.cpp`.

## TODO

- MIDI: note on/off, velocity
- Polyphony
- External buttons for menu navigation
- Screen menu
