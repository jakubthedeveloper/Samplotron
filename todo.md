# TODO

- Investigate and fix residual audible artifact on `panic` (retrigger is acceptable; panic still clicks/distorts intermittently).

- [ ] **Investigate the remaining artifact at the end of a sample.**
  Hardware feedback: the DAC Control 3 change appears to help, but an audible artifact remains at the sample's end. Its character and cause are not yet established; do not assume it is the same pop as before. Keep the current codec configuration as the baseline (`0x26` muted, `0x22` unmuted).

  **Checks:** Record the artifact and its timing relative to the last audible signal and EOF. Compare an original WAV with a smoothly faded version containing 200–500 ms of trailing exact-zero PCM. Compare identical PCM played from RAM and SD, and test a single voice and overlapping voices separately. Inspect sample endpoints for nonzero amplitude or DC offset; measure file-close latency and I2S underruns around EOF. Use these results to distinguish endpoint discontinuities, EOF handling, gain changes, and the codec's response to silence before choosing a fix.

  **Evidence already collected:** A host probe using the local WAV decoder and mixer preserved the queued tail, then emitted zeros, without calling the final output's `stop()`. Setting the released input's gain to zero did not erase samples already accumulated in the mixer. Firmware continues servicing the mixer while idle. Do not assume that natural EOF shuts down I2S or discards its queued tail; actual analog behavior and timing still need hardware verification.

## Audio path audit

Findings from source review and targeted host-side probes of the locally installed ESP8266Audio library. Hardware noise, analog clipping, and actual SD underrun frequency still require device measurements.

- [x] **Limit the wide sum before PCM16 conversion and remove excessive fixed attenuation.**
  `SamplerMixer` now uses float summation, 32 inputs and a linked look-ahead limiter. Regression tests cover unity solo level, quiet overlaps, wide peaks, phase cancellation, 32 full-scale voices, attack/release and rejected sink writes. Voice input gains no longer use 1/64 quantization or active-count gain steps. Mixer reuse aligns to the current frame or its remaining queued tail. Fade budgets/envelopes and waveform capture advance only on accepted writes.

  **Remaining hardware verification:** analog headroom (including inter-sample peaks and transformer saturation), perceived limiter pumping, SD/CPU throughput with many voices, and end/retrigger artifacts. The digital ceiling is not an oversampled true-peak guarantee. File boundary ramps, explicit fade-out and master limiting advance per accepted sample.

- [x] **Add playback continuity regression coverage and ramp file boundaries.**
  `test_audio_playback` runs the production decoder, RAM/SD source adapters, voice engine, fades and mixer. It covers single/simultaneous/staggered voices, same-sample retriggers, source-slot reuse, output backpressure, short samples and full-scale boundaries. Three tests reproduced abrupt start/EOF/retrigger steps (about 12000 PCM units for a 12000-level fixture); 35-frame smoothstep boundary ramps now pass them. A separate 6 ms fade test caught the final Q15 remainder drop; distributing that remainder removes the extra step. The retrigger policy remains a 6 ms fade of older voices. See [test details](docs/audio-regression.md).

  **Hardware feedback (2026-09-10):** the user confirmed that the reported overlapping-sample interruption disappeared after flashing the subsequent 128-frame I2S block-write firmware. The boundary tests remain separate regression coverage; broader SD/I2S timing and CPU headroom measurements remain open.

- [ ] **Make loop restarts continuous and handle voice stealing without abrupt cuts.**
  File edges now fade, but `restartVoiceLoop()` still closes/reopens sources and the decoder emits an initial zero on each restart. Boundary ramps prevent an instantaneous step but can cause a short amplitude dip; they do not implement a seamless crossfade or hide SD open latency. At full 32-voice occupancy, replacing the oldest voice can still cut a nonzero signal. Test seamless loops and repeated triggers at saturation before changing these policies.

- [ ] **Move SD reads out of the time-critical playback path and measure underruns.**
  `StreamManager::BufferedSdSource` performs synchronous SD reads and has no read-ahead buffer; the local WAV decoder reads 128-byte chunks. File opening also runs in the audio task; WAV headers now come from the boot validation cache. Add a dedicated reader task and per-stream ring buffers, distinguish temporary starvation from EOF, and expose buffer occupancy and underrun counters. Stress-test multiple streams and repeated file starts on hardware; compare with RAM playback.

- [x] **Enforce the supported WAV format on preview and fallback playback paths.**
  All loaded library entries are validated once during boot (PCM16, 44.1 kHz, mono and RIFF/chunk bounds), including unassigned files. Cached results gate preview, MIDI and stream fallback. Boot shows progress/rejections; library entries show `!` and a reason. Classification, RAM preload and streamed playback reuse cached PCM offsets/lengths; streams supply a canonical header from memory. Restart after SD file changes. Native tests cover malformed/unsupported files, metadata padding, read failures, cache-only classification and blocked playback paths.

- [ ] **Review ES8388 startup sequencing, register definitions, and analog gain.**
  Startup now establishes I2S and digital silence before unmuting (startup hum fix confirmed on hardware), and DAC Control 3 preserves its default control bits. Remaining review: analog outputs now use `0x1E` (0 dB) with full-level digital mixing; verify available analog headroom with the actual transformer and mixer load. Choose analog levels based on measured headroom. Measure startup pops, idle noise, and distortion at several output levels, including the transformer path. Reference: [ES8388 datasheet](https://www.boardcon.com/download/ES8388_datasheet.pdf).

- [x] **Resolve the reported interruption during overlapping playback.**
  The user's `sample-test.wav` showed periodic discontinuities and increasing drum timeline lag during overlap. Switching to 128-frame I2S writes reduced per-frame driver overhead. On 2026-09-10, the user flashed the firmware and confirmed that the interruption disappeared. Transport regressions include a continuously advancing simulated clock; all 64 native tests and the ESP32 build passed. This confirms the reported case, not a measured maximum polyphony or SD throughput guarantee. Details: [recording investigation](docs/measurements/sample-test-2026-09-09.md).

- [ ] **Investigate low output level on hardware.**
  `amen.wav` peaks at -0.08 dBFS; its recorded solo peaks at -28.42 dBFS despite the reported +40 mixer gain (ZEDi-10, JACK/Reaper at 0 dB). Firmware reads back DAC/analog volume registers at boot. Analog level localization remains open; the user's confirmation about uninterrupted playback does not establish that the level issue is resolved.
