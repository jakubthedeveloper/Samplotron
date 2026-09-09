# Samplotron Documentation

This document collects the information needed to build, run, and maintain the device. For an overview, see the [README](../README.md); for screen-by-screen operation, see the [musician's manual](manual.md).

## 1. Firmware and Build Environments

`platformio.ini` defines four environments:

- `esp-wrover-kit`: main firmware (`src/main.cpp`)
- `esp-wrover-kit-debug-input`: input test firmware (`src/debug_input_main.cpp`)
- `esp-wrover-kit-debug-midi`: MIDI test firmware (`src/debug_midi_main.cpp`)
- `native`: host-side Unity tests with Arduino/FreeRTOS stubs (`test/`)

PSRAM is enabled in all three ESP32 environments (`board_build.psram = enabled`, `-DBOARD_HAS_PSRAM`).

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
- `PA_EN`: GPIO21, held LOW to disable the unused L/R speaker amplifiers

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
- Nonzero-velocity Note On messages are accepted on all channels. Velocity is not used for volume; Note Off (including zero-velocity Note On), CC, pitch bend, and MIDI clock do not control playback.
- The learned panic note takes precedence over sample playback for the same note.

### Audio/Power Isolation (Noise Mitigation)

Current hardware revision includes additional isolation elements to reduce audible noise caused by ground loops between Samplotron and an external mixer:

- audio output path: **one channel of headphones out (mono)** → `600:600` audio isolation transformer → volume potentiometer → isolated mono output jack,
- output jack: isolated from chassis,
- power path: Hi-Link `B0505S-3WR3` DC/DC isolator.

Use a TRS plug at headphones out: tip is left, ring is right, sleeve is headphone ground. Connect the transformer's primary between **either tip or ring** and sleeve. Leave the other channel unconnected; never short left and right together. The secondary feeds the volume potentiometer and mono jack, and stays electrically isolated from board ground and chassis.

Do not use the separate L/R **speaker terminals** for this path. They are outputs of bridge-tied Class-D amplifiers: both terminals are active, neither is ground, and the signal includes switching components at speaker level. A 600:600 audio transformer is not a Class-D output filter or a speaker-to-line converter; this connection can cause excessive level, buzz and distortion. Headphones out takes the signal before the speaker amplifiers. Moving the existing transformer to this connector noticeably improved sound quality on the current device.

The [Ai-Thinker AudioKit V2.2 schematic (mirror)](https://github.com/johnradford49/ESP32-Audio-Kit/blob/main/esp32-audio-kit_v2.2_sch.pdf) shows the separate headphone and speaker paths. ES8388 register definitions are in the [Everest Semiconductor datasheet](https://www.armdesigner.com/download/ES8388_datasheet.pdf), sections 6.1.3–6.1.5 and 6.3.17–6.3.21.

Playback-only configuration in `src/codec_es8388.cpp`:

- `PA_EN = LOW` throughout initialization and unmute: external L/R speaker amplifiers stay shut down.
- `CHIPPOWER (0x02) = 0x00`: retain the original board clock/reference state. Analog capture remains powered down independently by `ADCPOWER`; this does not enable line-in or microphone capture.
- `ADCPOWER (0x03) = 0xFC`: both analog inputs, both ADCs, microphone bias and ADC bias generator powered down. This disables capture from line-in and microphones.
- `ADCCONTROL7 (0x0F) = 0x24`: ADC digital output muted as well.
- Left/right output mixers (`0x27`, `0x2A`) retain DAC playback with analog-input bypass disabled (bit 6 clear).
- `DACPOWER (0x04) = 0x3C`: both DAC channels and output pairs remain available, so either headphone channel can be used. The board schematic does not expose the internal ES8388-to-module output mapping; do not assume output pair 1 means speakers and pair 2 means headphones. Speaker shutdown uses the independent `PA_EN` pin.
- DAC soft-ramp/mute profile stays `0x26` during startup and `0x22` after I2S starts.

Hardware verification (2026-09-09): `CHIPPOWER = 0xAA` introduced output noise on the current device. Restoring this register alone to `0x00` removed the noise, as confirmed by the user after flashing. Keep `0x00` for this board profile; line-in, microphones and ADC analog blocks remain disabled through `ADCPOWER = 0xFC`, and speaker amplifiers remain off through `PA_EN = LOW`. This comparison identifies the problematic register setting, but does not establish the internal electrical mechanism. Host tests verify register programming; analog noise performance requires an on-device check.

## 3. Encoder Mapping (MCP23017 GPA)

- `GPA0`: Encoder 1 A
- `GPA1`: Encoder 1 B
- `GPA2`: Encoder 1 switch
- `GPA3`: Encoder 2 A
- `GPA4`: Encoder 2 B
- `GPA5`: Encoder 2 switch

### Matrix keypad (MCP23017 port B, main firmware)

- Assumed wiring: 4x4 matrix, rows 1..4 on `GPB0..GPB3`, columns 1..4 on `GPB4..GPB7`.
- Columns use internal pull-ups. Scanning drives one row LOW and leaves the other rows as inputs; all rows are released after each scan.
- A full scan runs at most once every `5 ms`, subject to UI loop timing, with `35 ms` debounce per key.
- Serial monitor: `115200 baud`. A press prints e.g. `[KEYPAD] PRESS key=1 row=1 col=1` once; holding does not repeat. Release rearms the key after debounce.
- Debug key numbers are electrical row-major, `1..16`; printed row/column numbers are `1..4`.
- Physical key order (measured debug numbers `1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16`) maps to MIDI notes `36..51`. Edit `include/keypad_mapping.h` to change the mapping.
- Presses enter the same `Midi::handleNoteOn()` path as external MIDI, including sample/panic note learning and assigned sample playback. Assign samples using the existing Library workflow and save as usual. Serial also prints `note=36` etc.
- Releases do not stop playback, matching the current external MIDI implementation, which ignores Note Off. Holding a key does not retrigger it.
- Without matrix diodes, simultaneous presses can produce ghost keys; the keypad scanner does not suppress ghosting.
- Register configuration follows the [Microchip MCP23017 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/20001952C.pdf), with `IOCON.BANK=0`.
- The separate `esp-wrover-kit-debug-input` firmware still tests encoders only.

## 4. SD Card and Files

### Layout

- samples: `/samples/*.wav`
- configuration: `/sampler_config.json`
- previous saved configuration: `/sampler_config.bak.json`
- temporary file used during saving: `/sampler_config.tmp.json`

### Sample Loading Rules

- `/samples` is scanned (non-recursive),
- `.wav` and `.WAV` are recognized,
- file list is sorted alphabetically,
- UI sample limit: `32` (the first 32 matching files encountered are collected, then sorted).
- All playback paths require uncompressed PCM (`audioFormat = 1`), 16-bit, 44100 Hz, mono, including previews and stream fallback.
- Use a FAT32 card. The conversion command in section 9 modifies files in place, including leading-silence trimming and gain adjustment.

### Boot-time WAV validation

Before playback tasks start, `SampleLibrary::loadFromSd()` validates every collected entry, including unassigned files. The boot screen displays `Checking WAV: n/N` and `Rejected`. Invalid entries stay in the library with `!` and a status (`BAD FORMAT`, `BAD WAV`, or `READ ERROR`); the playback router blocks them before enqueueing preview or MIDI triggers. Unchecked entries are also blocked. Valid entries continue to work.

`wav_validation.cpp` checks RIFF/WAVE identification, exact RIFF/file size agreement, chunk boundaries and odd-byte padding, one `fmt ` chunk before one nonempty `data` chunk, supported PCM parameters, byte rate, block alignment, and whole PCM16 frames. Extended `fmt ` chunks must have a consistent extension length. Unknown chunks are skipped, including metadata after PCM; duplicate or truncated chunks are rejected. Files must fit the signed 32-bit playback seek range. Chunk padding follows the [RIFF specification](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-).

Validation reads headers and seeks past PCM and metadata payloads; it does not scan audio content, detect clicks/clipping, or guarantee that every PCM sector is readable. Each catalog entry caches status, PCM offset, and length in RAM. Assignment classification and saving reuse this cache. RAM preload reads the cached PCM range directly. SD streaming uses `ValidatedWavSource` to supply a canonical 44-byte header from memory and expose only the cached PCM range, avoiding on-disk header parsing at each trigger or loop restart. Opening the file, checking that the cached range still fits, and reading PCM still involve SD access during playback.

Restart after changing files on the SD card. There is no runtime revalidation or hot-swap support; cache validity assumes files remain unchanged for the session. Validation covers the loaded library's existing 32-entry limit, not additional files outside that list.

## 5. `sampler_config.json` Configuration

Location and parser: `src/settings_store.cpp`.

Minimal format:

```json
{
  "version": "1.0",
  "global_settings": {
    "sample_ram_budget_bytes": 1048576,
    "panic_note": 24
  },
  "midi_assignments": [
    {
      "note": 60,
      "sample_path": "/samples/kick.wav",
      "volume": 100,
      "playback_mode": "shot"
    }
  ]
}
```

Notes:

- `note`: `0..127`
- `panic_note`: optional `0..127`; when received as MIDI NOTE ON, all active voices are quickly faded out
- `playback_mode`: optional `"shot"` or `"loop"` stored per assignment/sample
- `volume`: clamped to `0..100`
- `volume = 100`: unity per-voice gain; a single sample keeps its original level (no automatic normalization of quiet WAV files)
- `sample_path`: full SD path, for example `/samples/snare.wav`
- maximum assignments in the settings structure: `128`; the UI catalog holds at most `32` samples and assigns each sample to one note
- without a readable configuration, loading begins from defaults: no assignments, no panic note, a 1 MiB RAM budget, and one-shot playback

The writer also saves `sample_playback_modes`, an array of `sample_path` / `playback_mode` objects. The UI save flow includes library samples set to `loop`, including those without note assignments; omitted unassigned samples use the default `shot` mode. Assigned sample volumes are saved in `midi_assignments`; unassigned preview volumes are not persisted.

Saving writes and parses the temporary JSON file before rotating the previous configuration to `.bak.json` and renaming the temporary file into place. If the final rename fails, the writer attempts to restore the backup. Startup loads only `/sampler_config.json`; it does not automatically recover from the backup.

## 6. RAM Preload and Playback Modes

Sample preparation pipeline:

- MIDI-assigned samples are classified from cached boot validation as `RAM` or `STREAM`,
- `RAM` is used only for WAV files that meet all conditions:
  - PCM format (`audioFormat = 1`),
  - `16-bit`,
  - `44100 Hz`,
  - `mono`,
  - duration `<= 5.0 s`,
  - fit into the RAM budget,
- if preload fails, the entry falls back to `STREAM`.
- if assigned sample format is unsupported/missing, playback for that note is blocked (`UNAVAILABLE`) instead of trying to decode anyway.

Playback engine behavior:

- fixed `32`-voice playback pool (`Audio::kVoiceCount`),
- each trigger allocates a free voice slot when available,
- retriggering the same sample starts a new voice instance and requests short fade-out on already active voices in the same retrigger group,
- if all voices are active, the incoming trigger steals the oldest active voice (deterministic `oldest-voice` policy),
- if incoming MIDI NOTE ON matches configured panic note, all currently active voices are quickly faded out and pending trigger backlog is cleared,
- works for both SD-streamed and RAM-backed sample playback,
- voice update loop applies bounded per-voice decode budget (`kVoiceLoopSampleBudget`) to keep scheduling predictable,
- per-voice gain follows `VOL / 100` in floating point. The 32-input `SamplerMixer` sums before limiting; it never narrows an overloaded sum to PCM16 first. A stereo-linked peak limiter uses 64 frames of look-ahead (1.45 ms at 44.1 kHz), linear predictive gain bounds reached within 32 frames and held for the remainder of the look-ahead, and a 50 ms peak-envelope decay constant. It reduces gain only around overloads, including the look-ahead and release intervals.
- trigger events are sent through a queue from UI/MIDI domain to dedicated audio task (no direct playback calls from UI code path).

Important behavior:

- RAM pool budget is "locked" after the first `prepare()` (`sample_ram_manager.cpp`),
- changing `sample_ram_budget_bytes` in the same runtime session is recorded in the preparation result as `fixedBudgetMismatch`,
- a real budget change requires a device reboot.

### Mixing level policy

| Method | Single voice | Overlapping voices | Decision |
| --- | --- | --- | --- |
| Divide the sum by active voice count `N` | Unity at `N=1` | Bounds normalized inputs, but even silent active voices turn down the mix | Rejected |
| Divide by `sqrt(N)` | Unity at `N=1` | Compensates average power for uncorrelated sources; aligned peaks can still overflow | Insufficient as peak protection |
| Hard/soft clipping | Depends on curve | Changes the waveform to fit the range | Rejected as the main overload control |
| Wide sum + look-ahead limiter | Unity below ceiling after release recovers | Attenuates the whole mix when actual peaks require it | Implemented |

Summation and the distinction between correlated amplitude and uncorrelated power are described in [Miller Puckette, Superposing Signals](https://msp.ucsd.edu/techniques/latest/book-html/node13.html). For nonlinear clipping see [Julius O. Smith, Soft Clipping](https://www.dsprelated.com/freebooks/pasp/Soft_Clipping.html). The selected approach follows the look-ahead limiting principle described by [FFmpeg alimiter](https://ffmpeg.org/ffmpeg-filters.html#alimiter), implemented locally for the ESP32 sample pipeline rather than running FFmpeg on the device.

The limiter stores a linked gain bound for each delayed stereo frame. An over-ceiling incoming frame schedules a linear bound from the current gain to its required gain over the next 32 frames, then holds that bound until the peak reaches the output at frame 64. This anticipates attenuation and also caps recovery before a subsequent peak. Overlapping bounds take their minimum; release can never exceed the bound for the current frame. Recovery multiplies gain by `exp(1 / (44100 * 0.050))` per accepted frame, up to unity. Unlike interpolating gain directly toward unity, this avoids excessive recovery after deep limiting. A 32-fold reduction takes about 173 ms to fully recover once no further bounds apply. The bound comes from the **unclipped float sum**, so both aligned peaks and cancellation are handled before conversion. Rejected sink writes leave the mix, delay line, gain envelope and waveform capture unchanged. Playback tails drain during idle; I2S remains running with zeros. A reused input appends after its queued tail, or starts at the current mix position if that tail has drained.

At `VOL=100` a single voice now has unity digital gain instead of about 0.22 (the old effective gain including 1/64 quantization). Removing the old analog +4.5 dB boost gives roughly **8.7 dB higher single-voice output** below limiting, for the same WAV and external settings. This is a calculation, not an analog measurement. Quiet source files remain quiet; their dynamics are preserved. Peaks can briefly reduce other sounds in the same mix, and release means a lone voice immediately after an overload may still be attenuated.

The guarantee is a bounded **digital sample peak**, not an oversampled true-peak limit or a guarantee against transformer/mixer analog overload. [EBU peak guidance](https://qc.ebu.io/items/0084B/) distinguishes reconstructed true peaks from PCM sample peaks. Keep `CHIPPOWER=0x00`, which was confirmed quiet on this board. Verify the new level on hardware with single and overlapping samples, including bass-heavy material through the 600:600 transformer. Start the external volume low. CPU/SD throughput at high polyphony still requires an on-device check. The 32-aligned-sine regression covers the entire waveform, including the first attack and final delayed frames: maximum deviation from a unity-level reference is bounded to 128 PCM units and RMS error to 64 units. It also rejects consecutive rail samples and includes a deliberately hard-clipped negative control. These thresholds validate that specific test waveform, not arbitrary-signal transparency.

## 7. UI and Device Interaction

UI states:

- `Main`
- `Library`
- `AssignNote`
- `Saving`
- `Visualizer`

Flow:

- in `Library`, you select a sample and trigger preview,
- preview requests are sent as `PreviewSample` command to `sample_loader` queue before playback routing,
- in `Main`, `VOL` and `SHOT/LOOP` are shown only when an active sample exists,
- in `Main`, `SHOT/LOOP` toggles one-shot vs loop for the currently active sample,
- `L rotate` in `Library` toggles assignment mode between `Sample` and `Panic`,
- long-pressing the right button enters `AssignNote` for the current mode,
- the first received MIDI note assigns either the current sample or the global panic note (depending on selected mode),
- left-clicking on `Main` opens `Visualizer`, which displays the captured output waveform; any encoder rotation or button action returns to `Main`, while MIDI/keypad notes continue to trigger samples,
- selecting `SHOT` stops running loops for that sample with a short fade; retriggering a loop restarts it, and releasing a MIDI/keypad key does not stop it,
- volume changes are applied to subsequent triggers,
- `SAVE` waits up to 3 seconds for idle playback, then requests panic if needed and waits another 1.5 seconds; it rebuilds prepared samples before writing `/sampler_config.json`. Saving can interrupt a performance.

Assignment rules:

- one sample can be assigned to only one note at a time (new assignment clears older one),
- volume is stored per sample (`0..100`), not per note.

## 8. Hardcoded Values (Where to Change)

### `include/audio.h`

- Audio voice count: `Audio::kVoiceCount = 32`

### `include/boot_screen_flow.h`

- Boot screen dismiss timeout: `BootScreenFlow::kDefaultDismissTimeoutMs = 5000`

### `include/input.h`

- Button debounce: `35 ms`
- Encoder detent: `4` ticks
- Long press (right encoder): `700 ms`

### `include/sample_classifier.h`

- RAM preload threshold: `kFixedPreloadThresholdSeconds = 5.0f`

### `include/settings_store.h`

- Default RAM budget: `kDefaultSampleRamBudgetBytes = 1 MB`

### `include/ui.h`

- UI sample limit: `Ui::kMaxSamples = 32`
- Minimum saving screen: `1000 ms`
- "Saved" feedback duration: `1000 ms`
- MIDI pulse indicator duration: `100 ms`

### `include/audio_internal.h`

- Audio mixer buffer size: `kMixerBufferSamples = 512`
- Retrigger fade-in (new voice): `kRetriggerFadeInUs = 800`
- Retrigger fade-out (older voices in same group): `kRetriggerFadeOutUs = 6000`
- Default control stop fade-out: `kDefaultStopFadeOutUs = 9000`
- Decode budget per voice update: `kVoiceLoopSampleBudget = 96`
### `include/sampler_mixer.h`

- Mixer inputs: `kMaxInputs = 32`
- Per-voice gain: `VOL / 100`, no fixed attenuation or voice-count divisor
- PCM output ceiling: `±32767` (negative full scale is limited by one LSB)
- Look-ahead: `64` samples at fixed `44100 Hz`; other rates are rejected
- Attack ramp: `32` samples, followed by holding the bound through the remaining look-ahead
- Peak-envelope decay constant: `0.050 s` (gain recovers multiplicatively; not a fixed 50 ms return to unity)
- Analog ES8388 outputs: `0x1E` (0 dB), replacing the previous +4.5 dB boost now that the digital mixer uses the full range

### `src/trigger_engine.cpp`

- Panic fade-out: `kPanicFadeOutUs = 12000`

### `src/input.cpp` and `include/keypad_mapping.h`

- Keypad scan interval: `5 ms`
- Keypad note mapping: `KeypadMapping::kMidiNotes`

### `src/sampler_app.cpp`

- Audio task core/priority: core `1`, priority `6`
- Sample loader task core/priority: core `0`, priority `4`
- UI task core/priority: core `0`, priority `2`
- Trigger queue length: `32`
- Loader command queue length: `12`
- UI status queue length: `16`

### `src/settings_store.cpp`

- Config path: `"/sampler_config.json"`
- JSON document capacity: `12288`

### `src/ui.cpp`

- Volume change step in UI: `5` per right-encoder tick on `VOL`

## 9. Useful Commands

### Via Makefile

- `make help`
- `make test` (native UI and playback-router tests)
- `make build-main`
- `make upload-main`
- `make build-debug`
- `make upload-debug`
- `make build-debug-midi`
- `make upload-debug-midi`
- `make monitor` (115200 baud; main firmware prints keypad initialization and presses)
- `make convert-samples SAMPLES_DIR=/path/to/samples` (requires `ffmpeg`; replaces WAV files in place, converting to PCM16/44.1kHz/mono, trimming leading silence at −45 dB / 10 ms, and applying gain calculated for a −1 dBFS source peak target; use copies of original recordings)
- `make convert-samples /path/to/samples` (equivalent positional form)
  - conversion runs `ffmpeg` with `-nostdin` to avoid stdin conflicts in batch processing.

### Direct PlatformIO

- `pio test -e native`
- `pio run -e esp-wrover-kit`
- `pio run -e esp-wrover-kit -t upload`
- `pio run -e esp-wrover-kit-debug-input -t upload`
- `pio run -e esp-wrover-kit-debug-midi -t upload`
- `pio device monitor -b 115200` (optional raw UART monitor)

### Flashing prebuilt firmware

Download these four files from the same [main-latest release](https://github.com/jakubthedeveloper/Samplotron/releases/tag/main-latest): `firmware.bin`, `bootloader.bin`, `partitions.bin`, and `boot_app0.bin`.

Install esptool:

```bash
python -m pip install esptool
```

From the directory containing the downloaded files, flash the board (replace `/dev/ttyUSB0` with your serial port, such as `COM3` on Windows):

```bash
python -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash -z \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin
```

The repository workflow runs native tests and builds the main firmware. Pushes to `main` also update the `main-latest` prerelease and its four binaries.

### Test scope and diagnostics

`pio test -e native` covers UI navigation, sample/panic learning, keypad mapping, saving state, and playback routing, including RAM-to-stream fallback and loop control. These tests use stubs and do not exercise the real audio engine, SD hardware, or I2C wiring.

Main firmware serial output is limited to keypad initialization and key presses from `src/input.cpp`. For encoder or MIDI diagnostics, upload the corresponding debug environment and open the monitor at 115200 baud. These are separate applications; upload the main environment again to resume sampling.

## 10. Module Map (Code Orientation)

- `src/main.cpp`: thin Arduino entrypoint delegating to `SamplerApp`
- `src/sampler_app.cpp`: high-level orchestration (boot sequence, module wiring, task startup)
- `include/sampler_loader_ipc.h`: cross-task command/status types for `sample_loader` and UI/audio domains
- `src/boot_screen_flow.cpp`: boot screen render model and dismiss/timeout flow
- `src/sampler_callback_binder.cpp`: callback wiring for UI/MIDI/input routing
- `src/input_ui_bridge.cpp`: `Input::Event` -> `Ui::Event` mapping
- `src/sampler_playback_router.cpp`: preview and MIDI-triggered playback routing
- `src/sampler_save_service.cpp`: save flow orchestration (wait idle -> collect -> rebuild -> persist)
- `src/trigger_engine.cpp`: trigger queue and dedicated audio task loop
- `src/sample_library.cpp`: SD sample discovery, sorting, and path lookup
- `src/sampler_runtime.cpp`: settings <-> UI mapping, classification, RAM preload, active registry
- `src/ui.cpp`: UI state, navigation, assignment logic, save flow
- `src/display_ssd1309.cpp`: OLED screen rendering
- `src/input.cpp`: MCP23017 reading, encoder decode, click/long-press handling, and matrix keypad scanning
- `src/midi.cpp`: MIDI parser (running status), NOTE_ON -> UI + trigger callback
- `src/settings_store.cpp`: JSON load/save on SD
- `src/sample_classifier.cpp`: assigned sample classification (RAM/STREAM/error modes)
- `src/sample_ram_manager.cpp`: pool allocation and WAV data preload into RAM
- `src/active_sample_registry.cpp`: final active-sample registry after fallback handling
- `src/audio.cpp`: audio engine initialization, update loop, and public playback/control API
- `src/audio_voice_engine.cpp`: voice allocation, oldest-voice stealing, retrigger fades, SD/RAM voice setup, and loop restart
- `src/audio_output_chain.cpp`: RAM WAV source, I2S rate handling, and waveform capture
- `src/budgeted_audio_output.cpp`: per-voice decode budget and fades, committed only after sample acceptance
- `src/sampler_mixer.cpp`: float summation, 32 mixer inputs, look-ahead peak limiting before PCM16 conversion
- `include/audio_internal.h`: shared engine state and audio tuning constants
- `src/stream_manager.cpp`: per-voice SD stream source wrappers and stream diagnostics
