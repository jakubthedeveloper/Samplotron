# Samplotron Documentation

This document collects the information needed to build, run, and maintain the device.  

## 1. Firmware and Build Environments

`platformio.ini` defines three environments:

- `esp-wrover-kit`: main firmware (`src/main.cpp`)
- `esp-wrover-kit-debug-input`: input test firmware (`src/debug_input_main.cpp`)
- `esp-wrover-kit-debug-midi`: MIDI test firmware (`src/debug_midi_main.cpp`)

PSRAM is enabled in all environments (`board_build.psram = enabled`, `-DBOARD_HAS_PSRAM`).

## 2. Pinout and Buses

Source of truth: `include/pins.h`.

### SD (SPI)

- `CS`: GPIO13
- `MISO`: GPIO2
- `MOSI`: GPIO15
- `SCK`: GPIO14

### Audio (I2S to ES8388)

- `BCLK`: GPIO27
- `LRC/WS`: GPIO25
- `DOUT`: GPIO26
- `PA_EN`: GPIO21

### Codec control I2C (ES8388)

- `SDA`: GPIO33
- `SCL`: GPIO32
- I2C address: `0x10`

### OLED + MCP23017 (shared I2C bus)

- `SDA`: GPIO23
- `SCL`: GPIO18
- OLED: auto-detected address `0x3C` / `0x3D`
- MCP23017: default address `0x27`
- `MCP_INTA`: GPIO5
- `MCP_INTB`: GPIO0 (currently not used by main firmware)

### MIDI

- `MIDI_IN`: GPIO22
- UART: `Serial2`, `31250 bps`

## 3. Encoder Mapping (MCP23017 GPA)

- `GPA0`: Encoder 1 A
- `GPA1`: Encoder 1 B
- `GPA2`: Encoder 1 switch
- `GPA3`: Encoder 2 A
- `GPA4`: Encoder 2 B
- `GPA5`: Encoder 2 switch

## 4. SD Card and Files

### Layout

- samples: `/samples/*.wav`
- configuration: `/sampler_config.json`

### Sample Loading Rules

- `/samples` is scanned (non-recursive),
- `.wav` and `.WAV` are recognized,
- file list is sorted alphabetically,
- UI sample limit: `32`.

## 5. `sampler_config.json` Configuration

Location and parser: `src/settings_store.cpp`.

Minimal format:

```json
{
  "version": "1.0",
  "global_settings": {
    "sample_ram_budget_bytes": 1048576
  },
  "midi_assignments": [
    {
      "note": 60,
      "sample_path": "/samples/kick.wav",
      "volume": 100
    }
  ]
}
```

Notes:

- `note`: `0..127`
- `volume`: clamped to `0..127`
- `sample_path`: full SD path, for example `/samples/snare.wav`
- maximum assignments: `128`

## 6. RAM Preload and Playback Modes

Current pipeline (recently updated):

- MIDI-assigned samples are classified as `RAM` or `STREAM`,
- `RAM` is used only for WAV files that meet all conditions:
  - PCM format (`audioFormat = 1`),
  - `16-bit`,
  - duration `<= 5.0 s`,
  - fit into the RAM budget,
- if preload fails, the entry falls back to `STREAM`.

Important behavior:

- RAM pool budget is "locked" after the first `prepare()` (`sample_ram_manager.cpp`),
- changing `sample_ram_budget_bytes` in the same runtime session is reported as `fixedBudgetMismatch`,
- a real budget change requires a device reboot.

## 7. UI and Device Interaction

UI states:

- `Main`
- `Library`
- `AssignNote`
- `Saving`

Flow:

- in `Library`, you select a sample and trigger preview,
- long-pressing the right button enters `AssignNote`,
- the first received MIDI note assigns the current sample to that note,
- `SAVE` writes configuration (`/sampler_config.json`) and refreshes classification + RAM preload.

Assignment rules:

- one sample can be assigned to only one note at a time (new assignment clears older one),
- volume is stored per sample (`0..127`), not per note.

## 8. Hardcoded Values (Where to Change)

- UI sample limit: `Ui::kMaxSamples = 32` (`include/ui.h`)
- Default RAM budget: `kDefaultSampleRamBudgetBytes = 1 MB` (`include/settings_store.h`)
- RAM preload threshold: `kFixedPreloadThresholdSeconds = 5.0f` (`include/sample_classifier.h`)
- Config path: `"/sampler_config.json"` (`src/settings_store.cpp`)
- JSON document capacity: `12288` (`src/settings_store.cpp`)
- Button debounce: `35 ms` (`include/input.h`)
- Encoder detent: `4` ticks (`include/input.h`)
- Long press (right encoder): `700 ms` (`include/input.h`)
- Boot screen duration: `5000 ms` (`src/main.cpp`)
- Minimum saving screen: `1000 ms` (`include/ui.h`)
- "Saved" feedback duration: `1000 ms` (`include/ui.h`)
- MIDI pulse indicator duration: `100 ms` (`include/ui.h`)
- Global debug logs switch: `DebugFlags::kEnableDebugLogs` (`include/debug_flags.h`)

## 9. Useful Commands

### Via Makefile

- `make help`
- `make build-main`
- `make upload-main`
- `make build-debug`
- `make upload-debug`
- `make build-debug-midi`
- `make upload-debug-midi`
- `make monitor`

### Direct PlatformIO

- `pio run -e esp-wrover-kit`
- `pio run -e esp-wrover-kit -t upload`
- `pio run -e esp-wrover-kit-debug-input -t upload`
- `pio run -e esp-wrover-kit-debug-midi -t upload`
- `pio device monitor -b 115200`

## 10. Module Map (Code Orientation)

- `src/main.cpp`: startup orchestration, SD, UI, MIDI, settings save, RAM preload
- `src/ui.cpp`: UI state, navigation, assignment logic, save flow
- `src/display_ssd1309.cpp`: OLED screen rendering
- `src/input.cpp`: MCP23017 reading, encoder decode, click/long-press handling
- `src/midi.cpp`: MIDI parser (running status), NOTE_ON -> UI + callback
- `src/settings_store.cpp`: JSON load/save on SD
- `src/sample_classifier.cpp`: assigned sample classification (RAM/STREAM/error modes)
- `src/sample_ram_manager.cpp`: pool allocation and WAV data preload into RAM
- `src/active_sample_registry.cpp`: final active-sample registry after fallback handling
- `src/audio.cpp`: playback from SD and RAM via `ESP8266Audio`
