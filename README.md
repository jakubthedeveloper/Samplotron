# Samplotron

## Overview

Samplotron is an ESP32-based sample player focused on fast hardware triggering and a clean architecture for future expansion.

The project currently supports:
- WAV sample playback from SD card
- ES8388 codec initialization and audio output
- Triggering samples from onboard buttons
- SSD1309 OLED status screen

The codebase is structured into separate modules (`audio`, `codec`, `storage`, `input`, `display`) so new features like MIDI and menu-driven UI can be added without turning `main.cpp` into a monolith.

## OLED Wiring (2.42" SSD1309, I2C)

Firmware now uses the display in I2C mode with this mapping:

- `OLED GND` -> `ESP32 GND`
- `OLED VCC` -> `ESP32 3V3`
- `OLED SCK (SCL)` -> `ESP32 IO18`
- `OLED SDA` -> `ESP32 IO23`

On this module revision (`2.42" OLED Ver: 4.1`), switch solder jumpers to IIC mode (silkscreen: `IIC: R9,R10,R11,R12`, `SPI: R8`).

Notes:
- Display logic should run at 3.3V.
- Firmware auto-detects OLED I2C address `0x3C` / `0x3D`.
- OLED uses a dedicated I2C bus on `IO18/IO23`; ES8388 remains on `IO32/IO33`.

## TODO

- MIDI: note on/off, velocity
- Polyphony
- External buttons for menu navigation
- Screen menu
