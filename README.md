# Samplotron

## Overview

Samplotron is an ESP32-based sample player focused on fast hardware triggering and a clean architecture for future expansion.

The project currently supports:
- WAV sample playback from SD card
- ES8388 codec initialization and audio output
- MCP23017 input expander for rotary encoder input
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
- OLED shares I2C bus with MCP23017 on `IO18/IO23`.
- ES8388 control I2C remains on `IO32/IO33` during codec initialization.

## MCP23017 Wiring (encoder input)

The firmware expects two rotary encoders connected through MCP23017:
- `GPA0` = encoder 1 `A`
- `GPA1` = encoder 1 `B`
- `GPA2` = encoder 1 switch (push button)
- `GPA3` = encoder 2 `A`
- `GPA4` = encoder 2 `B`
- `GPA5` = encoder 2 switch (push button)

ESP32 to MCP23017 mapping:
- `MCP VCC` -> `ESP32 3V3`
- `MCP GND` -> `ESP32 GND`
- `MCP SDA` -> `ESP32 IO23`
- `MCP SCL` -> `ESP32 IO18`
- `MCP INTA` -> `ESP32 IO5`
- `MCP INTB` -> optional (not required by current firmware)

Notes:
- MCP23017 I2C address is `0x27` (A0/A1/A2 jumpers open on Waveshare board).
- INTA/INTB are mirrored in firmware, so a single interrupt line (`INTA`) is enough.
- Existing direct key inputs (`KEY1`, `KEY3`) were removed.

Controls:
- Encoder 1/2 press: play selected sample
- Encoder 1/2 clockwise: next sample
- Encoder 1/2 counterclockwise: previous sample

## Input Debug Firmware

There is a separate debug firmware that does not use `src/main.cpp`:
- source: `src/debug_input_main.cpp`
- env: `esp-wrover-kit-debug-input`

What it shows:
- Serial: encoder rotations (`ENC1/ENC2 CW/CCW`) and button press/release
- OLED: encoder positions, button states, last event

Build and upload:
- `pio run -e esp-wrover-kit-debug-input`
- `pio run -e esp-wrover-kit-debug-input -t upload`
- `pio device monitor -b 115200`

