# TODO

- Investigate and fix residual audible artifact on `panic` (retrigger is acceptable; panic still clicks/distorts intermittently).

- [ ] **Investigate the remaining artifact at the end of a sample.**
  Hardware feedback: the DAC Control 3 change appears to help, but an audible artifact remains at the sample's end. Its character and cause are not yet established; do not assume it is the same pop as before. Keep the current codec configuration as the baseline (`0x26` muted, `0x22` unmuted).

  **Checks:** Record the artifact and its timing relative to the last audible signal and EOF. Compare an original WAV with a smoothly faded version containing 200–500 ms of trailing exact-zero PCM. Compare identical PCM played from RAM and SD, and test a single voice and overlapping voices separately. Inspect sample endpoints for nonzero amplitude or DC offset; measure file-close latency and I2S underruns around EOF. Use these results to distinguish endpoint discontinuities, EOF handling, gain changes, and the codec's response to silence before choosing a fix.

  **Evidence already collected:** A host probe using the local WAV decoder and mixer preserved the queued tail, then emitted zeros, without calling the final output's `stop()`. Setting the released input's gain to zero did not erase samples already accumulated in the mixer. Firmware continues servicing the mixer while idle. Do not assume that natural EOF shuts down I2S or discards its queued tail; actual analog behavior and timing still need hardware verification.

## Audio path audit

Findings from source review and targeted host-side probes of the locally installed ESP8266Audio library. Hardware noise, analog clipping, and actual SD underrun frequency still require device measurements.

- [ ] **Make loop restarts continuous and handle voice stealing without abrupt cuts.**
  File edges now fade, but `restartVoiceLoop()` still closes/reopens sources and the decoder emits an initial zero on each restart. Boundary ramps prevent an instantaneous step but can cause a short amplitude dip; they do not implement a seamless crossfade or hide SD open latency. At full 32-voice occupancy, replacing the oldest voice can still cut a nonzero signal. Test seamless loops and repeated triggers at saturation before changing these policies.

- [ ] **Move SD reads out of the time-critical playback path and measure underruns.**
  `StreamManager::BufferedSdSource` performs synchronous SD reads and has no read-ahead buffer; the local WAV decoder reads 128-byte chunks. File opening also runs in the audio task; WAV headers now come from the boot validation cache. Add a dedicated reader task and per-stream ring buffers, distinguish temporary starvation from EOF, and expose buffer occupancy and underrun counters. Stress-test multiple streams and repeated file starts on hardware; compare with RAM playback.

- [ ] **Review ES8388 startup sequencing, register definitions, and analog gain.**
  Startup now establishes I2S and digital silence before unmuting (startup hum fix confirmed on hardware), and DAC Control 3 preserves its default control bits. Remaining review: analog outputs now use `0x1E` (0 dB) with full-level digital mixing; verify available analog headroom with the actual headphone/potentiometer output and mixer load. Choose analog levels based on measured headroom. Measure startup pops, idle noise, and distortion at several output levels, including the headphone output path. Reference: [ES8388 datasheet](https://www.boardcon.com/download/ES8388_datasheet.pdf).
