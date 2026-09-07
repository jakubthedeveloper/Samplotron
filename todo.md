# TODO

- Investigate and fix residual audible artifact on `panic` (retrigger is acceptable; panic still clicks/distorts intermittently).

- [ ] **Investigate the remaining artifact at the end of a sample.**
  Hardware feedback: the DAC Control 3 change appears to help, but an audible artifact remains at the sample's end. Its character and cause are not yet established; do not assume it is the same pop as before. Keep the current codec configuration as the baseline (`0x26` muted, `0x22` unmuted).

  **Checks:** Record the artifact and its timing relative to the last audible signal and EOF. Compare an original WAV with a smoothly faded version containing 200–500 ms of trailing exact-zero PCM. Compare identical PCM played from RAM and SD, and test a single voice and overlapping voices separately. Inspect sample endpoints for nonzero amplitude or DC offset; measure file-close latency and I2S underruns around EOF. Use these results to distinguish endpoint discontinuities, EOF handling, gain changes, and the codec's response to silence before choosing a fix.

  **Evidence already collected:** A host probe using the local WAV decoder and mixer preserved the queued tail, then emitted zeros, without calling the final output's `stop()`. Setting the released input's gain to zero did not erase samples already accumulated in the mixer. Firmware continues servicing the mixer while idle. Do not assume that natural EOF shuts down I2S or discards its queued tail; actual analog behavior and timing still need hardware verification.

## Audio path audit

Findings from source review and targeted host-side probes of the locally installed ESP8266Audio library. Hardware noise, analog clipping, and actual SD underrun frequency still require device measurements.

- [ ] **Fix fade-out state updates when the mixer rejects a sample.**
  In `src/audio_output_chain.cpp`, `BudgetedAudioOutput::ConsumeSample()` advances the envelope and modifies the decoder's pending sample before checking whether the sink accepts it. Retrying a rejected sample attenuates it repeatedly, and the fade can complete without delivering any samples. Process a copy and commit envelope state only after successful delivery. Verify that rejected writes preserve both the pending sample and envelope, and that fade duration follows accepted samples. Audit the limiter's state and waveform capture for the same acceptance-order issue.

- [ ] **Apply limiting before the mixed signal is clipped to PCM16.**
  ESP8266Audio's `AudioOutputMixer::loop()` clamps its accumulator before passing it to `SimpleLimiterAudioOutput`, so the limiter receives an already distorted waveform. Dynamic mix gain takes time to respond to newly started voices. Keep the sum in 32-bit or floating-point form through gain control and limiting, then convert to PCM16. Test simultaneous, phase-aligned full-scale voices and sudden increases in polyphony.

- [ ] **Synchronize mixer input positions when starting or restarting voices.**
  The local mixer's `begin(int id)` only marks an input as running; its write position is not synchronized with the current output position. Inputs started after an idle interval can therefore introduce misplaced samples or gaps. Define how new voices align with the output timeline and how queued tails are handled. Test delayed first use, stop/start reuse, retriggering, and loop restarts while other voices continue playing.

- [ ] **Replace coarse per-voice gain quantization with precise, sample-based smoothing.**
  ESP8266Audio's `AudioOutput::SetGain()` quantizes gain in steps of 1/64. Combined with the application's attenuation, this leaves very few useful volume levels: the host probe produced silence at VOL=5 for a single voice. It also turns dynamic gain adjustments into amplitude steps. Use a higher-precision gain stage and smooth gain per accepted sample. Verify low-volume playback, monotonic volume response, and transitions as the active voice count changes.

- [ ] **Make sample starts, natural endings, and loop boundaries click-safe.**
  Both playback entry points in `src/audio.cpp` set `fadeInUs` to zero, natural EOF has no fade-out, and `restartVoiceLoop()` closes and reopens the source without a crossfade. Resetting `lastSample` also inserts a zero sample at each decoder restart. Add suitable start/end envelopes and continuous loop handling with an optional short crossfade. Preserve intentional percussion attacks. Test nonzero endpoints, DC-offset samples, seamless loops, and repeated triggers.

- [ ] **Move SD reads out of the time-critical playback path and measure underruns.**
  `StreamManager::BufferedSdSource` performs synchronous SD reads and has no read-ahead buffer; the local WAV decoder reads 128-byte chunks. File opening and header parsing also run in the audio task. Add a dedicated reader task and per-stream ring buffers, distinguish temporary starvation from EOF, and expose buffer occupancy and underrun counters. Stress-test multiple streams and repeated file starts on hardware; compare with RAM playback.

- [ ] **Enforce the supported WAV format on preview and fallback playback paths.**
  `SamplerPlaybackRouter::onPreviewSample()` routes files directly to streaming without the assigned-sample format validation. A different sample rate can reconfigure the shared I2S output while other voices are playing, and the mixer does not resample. The locally installed decoder also has a PCM8 conversion bug involving stale `lastSample` values. Validate PCM16, 44.1 kHz, mono before every playback path, or implement explicit conversion. Verify that unsupported previews cannot disrupt active playback.

- [ ] **Resolve the mismatch between 32 application voices and 8 mixer inputs.**
  The local `AudioOutputMixer` has `maxStubs = 8`, while `Audio::kVoiceCount` is 32. Further `NewInput()` calls return null; allocation can select an unusable ninth slot instead of stealing a valid voice. Provide the intended input capacity, check initialization failures, and allocate only usable slots. Test the advertised polyphony and deterministic voice stealing beyond capacity. Keep any dependency fix reproducible rather than editing only the `.pio` cache.

- [ ] **Review ES8388 startup sequencing, register definitions, and analog gain.**
  Startup now establishes I2S and digital silence before unmuting (startup hum fix confirmed on hardware), and DAC Control 3 preserves its default control bits. Remaining review: analog outputs are set to code `0x21` (+4.5 dB, not 0 dB), several register comments are inaccurate, and volume mapping needs correction. Choose analog levels based on measured headroom. Measure startup pops, idle noise, and distortion at several output levels, including the transformer path. Reference: [ES8388 datasheet](https://www.boardcon.com/download/ES8388_datasheet.pdf).
