# Samplotron

Hardware WAV Sampler

[![Build Main Firmware](https://github.com/jakubthedeveloper/Samplotron/actions/workflows/build-main.yml/badge.svg)](https://github.com/jakubthedeveloper/Samplotron/actions/workflows/build-main.yml)

Samplotron is a standalone hardware sampler played with an external MIDI controller or its built-in 16-key pad. Load your own WAV files from an SD card, assign sounds to notes, and play one-shots or loops with per-sample volume control. Two encoders and an OLED display let you browse, preview, assign, and save sounds directly on the device.

![Samplotron hardware sampler](Samplotron.jpg)

The current photos show an earlier version played only through external MIDI. Photos of the version with the built-in keypad are coming soon.

[Why I built my own sampler — full article on Medium](https://medium.com/@KubaPisze/i-built-my-own-sampler-to-fit-my-needs-217493f4067c)

For a walkthrough with screen photos, see the [musician's manual](docs/manual.md). Firmware binaries are available in the [latest main release](https://github.com/jakubthedeveloper/Samplotron/releases/tag/main-latest).

The [interactive build guide](tutorial/README.md) walks through assembly with animated wiring diagrams and firmware screen demonstrations. It runs locally or as a static website, with step selection, play/pause and optional automatic progression.

## Technical Details

Samplotron uses an ESP32 with PSRAM, built with the `esp-wrover-kit` PlatformIO board configuration, and an ES8388 audio codec. It plays mono PCM16 WAV files at 44.1 kHz through a shared 32-voice engine. Short assigned samples can be preloaded into RAM; longer samples stream from SD.

### Controls

The front panel uses two rotary encoders with push buttons and a 4×4 matrix keypad:

| Screen / control | Action | Effect |
| --- | --- | --- |
| Main / left encoder | Rotate | Select `LIB`, `VOL`, `SHOT/LOOP`, or `SAVE`. |
| Main / right button on `LIB` | Click | Open the sample library. |
| Main / right encoder on `VOL` | Rotate | Set the last triggered or previewed sample's volume from 0 to 100, in steps of 5, for subsequent triggers. |
| Main / right encoder on `SHOT/LOOP` | Rotate or click | Toggle the sample's playback mode. Switching to `SHOT` also fades out its running loops. |
| Main / right button on `SAVE` | Click | Save the setup to SD. |
| Main / left button | Click | Show the output waveform; use either encoder or button to return. |
| Library / right encoder | Rotate | Browse samples. |
| Library / right button | Click | Preview the selected sample. |
| Library / right button | Hold for 700 ms | Learn a note for the selected sample or panic function. |
| Library / left encoder | Rotate | Switch between sample assignment and `PANIC MODE`. |
| Library or note assignment / left button | Click | Return to the previous screen. |
| Keypad | Press a key | Trigger its assigned sample, or supply a note while learning an assignment. |

`VOL` and `SHOT/LOOP` appear after a sample has been triggered or previewed. `SAVE` appears when there are unsaved changes. One sample can be assigned to only one note at a time; learning a new note removes its previous assignment.

### MIDI and keypad behavior

External MIDI Note On messages with nonzero velocity trigger samples on any MIDI channel. Playback level comes from the sample's `VOL` setting; MIDI velocity does not change loudness. Note Off, sustain, pitch bend, and MIDI clock do not control playback.

The keypad sends notes `36..51` in the measured physical key order and uses the same assignments as MIDI IN. Hold the right button in `LIB`, then press a keypad key or play a MIDI note to assign the selected sample. Holding a keypad key does not repeat it, and releasing a key does not stop playback.

`SHOT` plays the sample once. `LOOP` repeats it until stopped; another press retriggers it from the beginning. Change the sample to `SHOT` to stop its loops, or use the learned panic note to fade out all voices and clear pending triggers. To assign panic, select `PANIC MODE` with the left encoder in `LIB`, hold the right button, and send the desired note.

### How playback works

At startup, the firmware scans `/samples`, loads saved assignments, and prepares eligible samples in RAM. Preloading is limited to supported files no longer than 5 seconds that fit within the configured RAM budget (1 MiB by default). Other supported files stream from SD; failed preloads fall back to streaming. Missing or unsupported files are marked unavailable when assignments are prepared.

Each trigger starts a voice. Retriggering the same sample fades out its older voices, and if all 32 slots are occupied, the oldest voice is replaced. RAM and SD playback use the same decoder and mixer, with dynamic headroom and an output limiter. The audio task runs on core 1; the UI and sample loader run on core 0 and communicate with it through queues.

Saving stores assignments, assigned sample volumes, playback modes, and the panic note in `/sampler_config.json`, and refreshes RAM preparation. Save between performances: the save process waits for playback to finish and can stop running loops before rebuilding the sample pool.

### Hardware and connections

| Component | Connection |
| --- | --- |
| SD card over SPI | CS GPIO13, MISO GPIO2, MOSI GPIO15, SCK GPIO14 |
| ES8388 audio over I2S | BCLK GPIO27, WS GPIO25, DOUT GPIO26; amplifier enable GPIO21 |
| ES8388 control bus | SDA GPIO33, SCL GPIO32; address `0x10` |
| SSD1309 OLED and MCP23017 shared I2C bus | SDA GPIO23, SCL GPIO18; OLED `0x3C` or `0x3D`, MCP23017 `0x27` |
| Left encoder | MCP23017 GPA0 / GPA1 / GPA2: A / B / switch |
| Right encoder | MCP23017 GPA3 / GPA4 / GPA5: A / B / switch |
| 4×4 keypad | MCP23017 GPB0–GPB3: rows; GPB4–GPB7: columns |
| MIDI input | GPIO22, `Serial2`, 31250 baud |

Pin assignments are defined in [include/pins.h](include/pins.h); keypad note mapping is in [include/keypad_mapping.h](include/keypad_mapping.h). The [technical documentation](docs/documentation.md#2-pinout-and-buses) includes interrupt pins, keypad scanning details, and hardware notes.

The current hardware revision uses a 600:600 audio isolation transformer between one output channel and a jack isolated from the chassis, plus a Hi-Link `B0505S-3WR3` DC/DC isolator in the power path, to reduce ground-loop noise with an external mixer.

### Preparing samples and first use

1. Prepare a FAT32 SD card and create a `/samples` directory at its root.
2. Copy up to 32 WAV files into that directory. Use uncompressed PCM, 16-bit, 44.1 kHz, mono, with a `.wav` or `.WAV` extension. Subdirectories are not scanned; the loaded list is sorted by filename.
3. Insert the card, power on, and wait for `Ready`.
4. Open `LIB`, preview a sample, then hold the right button and send a MIDI note or press a keypad key to assign it.
5. Trigger the sound, set `VOL` and `SHOT/LOOP`, then select `SAVE` before powering off.

No configuration file is required for first boot; without one, the device starts with no assignments, one-shot playback, and the default RAM budget.

The repository includes a batch conversion command, requiring `ffmpeg` and Make:

```bash
make convert-samples SAMPLES_DIR=/path/to/sample-copies
```

Run it on copies of your source recordings: it replaces WAV files in place, converts them to PCM16/44.1 kHz/mono, trims leading silence using a −45 dB threshold and 10 ms duration, and applies gain calculated for a −1 dBFS source peak target.

### Configuration and diagnostics

Display orientation is configured by `DisplayConfig::ROTATE_180` in [include/display_config.h](include/display_config.h). It defaults to `true` (180° rotation); set it to `false` for the original orientation. Rebuild and upload the firmware after changing it. This applies to all screens in the main firmware and the input diagnostic firmware.

The on-device `SAVE` action writes `/sampler_config.json`. For manual configuration, including the RAM budget and panic note, see the [configuration format](docs/documentation.md#5-sampler_configjson-configuration). Changing the RAM budget requires a reboot.

The main firmware prints keypad initialization and key-press diagnostics at 115200 baud. Dedicated firmware environments are available for testing encoders and MIDI input:

```bash
make upload-debug
make upload-debug-midi
make monitor
```

Each upload replaces the firmware on the board. Restore normal operation with `make upload-main`.

### Build and upload

Install PlatformIO CLI (`pio`) or the PlatformIO IDE extension, then run these commands from the project root:

```bash
pio run -e esp-wrover-kit
pio run -e esp-wrover-kit -t upload
```

With Make installed, the equivalents are `make build-main` and `make upload-main`. Dependencies and PSRAM settings are declared in [platformio.ini](platformio.ini).

To flash without building, download `firmware.bin`, `bootloader.bin`, `partitions.bin`, and `boot_app0.bin` from the same [main-latest release](https://github.com/jakubthedeveloper/Samplotron/releases/tag/main-latest), then follow the [prebuilt firmware instructions](docs/documentation.md#flashing-prebuilt-firmware).

### Tests

Run the native tests without an ESP32 connected:

```bash
pio test -e native
```

`make test` runs the same command. The tests cover UI navigation, sample and panic assignment, keypad mapping, saving state, and routing playback requests to RAM or SD, including fallback and loop controls. They use hardware stubs; audio timing, SD throughput, and physical wiring require checks on the device.

### Code structure

The source is organized around application flow, playback, and hardware access:

- [sampler_app.cpp](src/sampler_app.cpp) handles boot, module setup, and task startup; [main.cpp](src/main.cpp) is the Arduino entrypoint.
- [ui.cpp](src/ui.cpp), [input.cpp](src/input.cpp), and [display_ssd1309.cpp](src/display_ssd1309.cpp) implement screen navigation, encoders, keypad input, and OLED rendering.
- [midi.cpp](src/midi.cpp) parses incoming MIDI; [sampler_playback_router.cpp](src/sampler_playback_router.cpp) turns note and preview requests into playback events.
- [trigger_engine.cpp](src/trigger_engine.cpp) owns the trigger queue and audio task.
- [audio.cpp](src/audio.cpp), [audio_voice_engine.cpp](src/audio_voice_engine.cpp), and [audio_output_chain.cpp](src/audio_output_chain.cpp) implement voice playback, retriggering, looping, mixing, limiting, and waveform capture.
- [sampler_runtime.cpp](src/sampler_runtime.cpp), [sample_classifier.cpp](src/sample_classifier.cpp), and [sample_ram_manager.cpp](src/sample_ram_manager.cpp) prepare assigned samples and RAM playback.
- [settings_store.cpp](src/settings_store.cpp) and [sampler_save_service.cpp](src/sampler_save_service.cpp) persist the setup and coordinate saving.

See the [technical documentation](docs/documentation.md) for the full module map, configuration schema, and tuning constants.
