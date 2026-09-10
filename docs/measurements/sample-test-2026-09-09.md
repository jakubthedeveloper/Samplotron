# Recording investigation: sample-test.wav, 2026-09-09

User setup: one headphone channel through the existing 600:600 transformer, Allen & Heath ZEDi-10, JACK and Reaper at 0 dB. User reports maximum Samplotron volume and +40 channel gain. Host audio settings were not inspected or changed.

Inputs supplied on the desktop:

- `sample-test.wav`: 27 s, 44.1 kHz, mono PCM24; SHA256 `cbf1e5763b5c22b7dd55c6b300e2f8d5d604af59cad6ebc512ffcb996e8c4c47`.
- `amen.wav`: 11.162789 s, 44.1 kHz, mono PCM16; SHA256 `49fa405e2aaf21a25d34c5dd8483c9c4dfb0eea9a0a239b6d5dabb50274fe3f3`.

Levels were calculated from signed PCM normalized to its full-scale integer range, without normalization or gain processing. RMS includes all frames in each selected interval.

| Signal / interval | Peak dBFS | RMS dBFS |
| --- | ---: | ---: |
| Original amen.wav | -0.080 | -17.690 |
| Recorded drum solo, 1.15–12.31 s | -28.421 | -46.057 |
| Recorded overlap, 21–27 s | -21.682 | -36.392 |
| Recorded idle, 18–21 s | -74.429 | -85.744 |

The near-full-scale source rules out a quietly prepared drum sample. The solo peak difference is about 28.3 dB between source PCM and recorded PCM; it is not a calibrated voltage measurement or a measurement of transformer loss alone. Mixer input gain, routing and converter headroom belong to the complete transfer path. The ZEDi-10 specifies 18 dB USB headroom above nominal, so a nominal analog meter indication is not 0 dBFS. Its line gain range reaches +40 dB. [Manufacturer specifications](https://www.allen-heath.com/content/uploads/2023/06/ZEDi-10-Technical-Datasheet-1.pdf).

Large one-frame steps concentrate in the overlaps. Examples: 23.145850, 23.198095, 23.294966, 23.444785, 23.494127, 23.540476, 25.388503, 25.438549, 25.486508 and 26.079932 s. These were selected with absolute sample difference >0.023 FS and a 300-frame minimum separation. That threshold is a locator for this recording, not a general-purpose click detector. Many steps are spaced roughly 50 ms apart and occur during sustained overlap, rather than only at note or file boundaries.

Local normalized cross-correlation of a 6 kHz high-pass version against the source was used to track drum timing while suppressing the lower-frequency added sounds. Example matches (20 ms windows, local search around the expected source position):

| Recording time | Source position | Correlation | Recording minus source time |
| ---: | ---: | ---: | ---: |
| 22.000 s | 0.900159 s | 0.8602 | 21.099841 s |
| 23.000 s | 1.900272 s | 0.9802 | 21.099728 s |
| 25.500 s | 4.331293 s | 0.9806 | 21.168707 s |
| 26.000 s | 4.800113 s | 0.8600 | 21.199887 s |

The increase is consistent with roughly 0.1 s of lost timing continuity by 26 s. Repeated drum patterns, analog filtering and independent recording/playback clocks limit exact alignment; this does not identify a specific DMA or SD failure by itself. Short local matches around 23.1–23.35 s also show stepwise lag increases on the order of one 128-frame DMA descriptor. Repeated or interrupted transport is a stronger hypothesis than file-edge clicks for this recording. Neither CPU deadlines nor physical underrun counters were measured on the ESP32.

## Changes prepared for another device test

The ESP32 output previously called `i2s_channel_write()` for each four-byte stereo frame. `StableAudioOutputI2S` now stages 128 frames, matching the existing DMA descriptor size, and submits 512 bytes per call when possible. Partial writes retain their exact unsent byte suffix; a full queue returns backpressure without advancing the rejected input. Continuous idle silence completes partial tail blocks. This uses about 0.5 KiB of staging memory and adds up to 2.9 ms of buffering. It reduces API overhead, but does not move SD reads to another task or certify real-time performance on hardware. The driver itself performs queue/semaphore work per write. [ESP-IDF 5.5.4 implementation](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_i2s/i2s_common.c).

New transport regressions check exact signed stereo packing at unity, one driver call per full DMA block, partial-byte writes, timeouts, and a clock that keeps consuming samples during processing and driver calls. The timing model explicitly assumes 19 us processing per frame and 6 us API overhead per call: the single-frame negative control underruns while the block writer does not. Those costs are synthetic, not claimed ESP32 measurements. The actual ESP32 adapter branch is compiled against a fake I2S driver in this suite.

Codec startup now reads back both DAC volume registers and all four analog output level registers, rejecting mismatches before unmute. A successful boot reports `Codec: DAC and analog outputs verified at 0 dB`. This checks register state, not output voltage. The existing 0 dB settings remain; the datasheet documents only up to +4.5 dB analog output gain, which would not explain a loss on the scale observed here. [ES8388 datasheet, sections 6.3.24–27](https://www.boardcon.com/download/ES8388_datasheet.pdf).

## Follow-up measurement

A four-second mono PCM16/44.1 kHz 1 kHz probe with -12 dBFS peak and 10 ms boundary fades was generated at `/tmp/samplotron-audio-check/calibration-1k-minus12dBFS.wav`. Its sustained sine RMS is approximately -15 dBFS. Play it at sample volume 100 to obtain a known digital reference; start the external mixer gain low, then record the settings, PFL indication and recorded level. Compare the one-channel headphone signal before and after the transformer if practical. Input socket/cable topology and actual analog levels remain to be checked.

Firmware build and all 64 native test cases passed.

## Device confirmation — 2026-09-10

The user flashed the firmware with 128-frame I2S writes and confirmed that the sound interruption disappeared. The reported overlapping-playback issue is therefore resolved in the user’s device test. This is listening feedback, not a measurement of underrun counts or maximum sustainable polyphony. Low output level remains unresolved; no level confirmation was provided.
