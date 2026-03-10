# Samplotron
[![Build Main Firmware](https://github.com/jakubthedeveloper/Samplotron/actions/workflows/build-main.yml/badge.svg)](https://github.com/jakubthedeveloper/Samplotron/actions/workflows/build-main.yml)

[Latest firmware.bin](https://github.com/jakubthedeveloper/Samplotron/releases/download/main-latest/firmware.bin)

Samplotron is an `ESP32 (ESP-WROVER-KIT)` sampler: it plays WAV files from SD, maps samples to MIDI notes, and stores device configuration on the card.

## Current Features

- WAV sample playback from `/samples` on SD.
- OLED UI with 2 encoders (via MCP23017).
- On-device MIDI note to sample assignment.
- Persistent assignments and sample volumes in `sampler_config.json`.
- Automatic RAM preload for short samples (faster trigger response).

## Quick Start

1. Requirements:
- `PlatformIO CLI` (`pio`) or PlatformIO IDE extension.
- `esp-wrover-kit` board (PSRAM is used).
- Connected hardware: ES8388, SSD1309 OLED (I2C), MCP23017 + 2 encoders, MIDI IN, SD card.
2. Prepare SD card (FAT32):
- create `/samples`,
- copy `.wav` files into `/samples`,
- optionally add `sampler_config.json` (if missing, firmware starts with defaults).
3. Flash firmware:
- `make upload-main`
4. Open serial log:
- `make monitor`

## Flashing Prebuilt Firmware (from GitHub Releases)

1. Download these files from release tag [`main-latest`](https://github.com/jakubthedeveloper/Samplotron/releases/tag/main-latest):
- `firmware.bin`
- `bootloader.bin`
- `partitions.bin`
- `boot_app0.bin`
2. Install esptool:
- `python -m pip install esptool`
3. Flash device (replace `/dev/ttyUSB0` with your port, e.g. `COM3` on Windows):

```bash
python -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash -z \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin
```

## Controls (Short Version)

- Main screen: `LIB`, `VOL`, `SAVE`.
- Library: browse samples and preview playback.
- Hold right encoder button in Library: MIDI note assignment mode.
- `SAVE`: writes assignments and volumes to SD.

## Debug Firmware

- Input debug: `make upload-debug`
- MIDI debug: `make upload-debug-midi`

## Technical Documentation

Hardware details, pinout, configuration, and hardcoded values: [`docs/documentation.md`](docs/documentation.md).
