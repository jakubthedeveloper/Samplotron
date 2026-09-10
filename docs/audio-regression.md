# Audio playback regression

Run `pio test -e native -f test_audio_playback` for playback continuity, or `pio test -e native` for the complete regression suite. No ESP32 is needed. These tests run on the development computer, not during firmware boot or performance.

The playback suite compiles the production `Audio` entry points and update loop, voice allocation/retrigger/stop code, ESP8266Audio 2.4.1 WAV decoder, RAM source, validated SD source view, stream manager, budgeted output, mixer and waveform capture. Only the SD filesystem, I2S device, clock and RTOS primitives are simulated. It therefore tests the actual decoding and scheduling interaction, not just arithmetic on an ideal array of simultaneous samples.

| Scenario | Assertion |
| --- | --- |
| Solo and 2/8 simultaneous distinct samples, RAM and SD | Output matches the independently summed source PCM within 1 PCM unit, including unequal file endings and trailing silence. |
| Mixed RAM/SD, offsets 1, 63, 96, 511, 977 and 3000 frames | Sample order, phase, duration and start offsets are preserved across decoder-budget and mixer-buffer boundaries. |
| Same sample triggered again before EOF | New occurrence plus the independently rendered 6 ms fade of the previous one matches the actual retrigger output. Both RAM and SD are covered. |
| Repeated source-slot reuse after EOF | No stale PCM, missing tail, extra I2S start/stop, or unintended voice stealing. |
| Nonzero start, EOF and retrigger | Boundary steps are bounded; an ongoing background voice continues unchanged. Sustain retains unity level. |
| Staggered 2/8/32 loud voices | Actual overload occurs; PCM stays bounded, follows the wide sum's polarity, and has continuous inferred limiter gain. |
| Output rejection and retry | A blocked sink accepts no frames and advances no simulated audio time. Once it accepts data again, the output matches the uninterrupted run. No claim about real hardware deadlines is made. |
| Very short files and positive/negative full-scale edges | 1–1501-frame fixtures stay bounded and end cleanly, identically under retries. |
| 6 ms explicit fade | Fade lasts 265 frames (rounding tolerance 1 frame) after queued PCM; every step of a 12000-level fixture is below 48 PCM units. |
| Deliberately damaged output | The same PCM comparison used by the continuity tests rejects a dropped frame, repeated frame, inserted zero and a 500-unit spike, even though those signals remain below full scale. |

Source tones have silent margins and smooth envelopes so an exact-reference failure identifies a playback defect rather than a discontinuity in the test recording. Separate constant-level fixtures deliberately have discontinuous endpoints to test boundary handling. The reference includes 64 frames of mixer look-ahead and the WAV decoder's initial pending zero. Changes to that decoder behavior must update the interface deliberately; the test should not silently realign broken output.

Before the boundary fix, the start and retrigger tests measured a 12000-unit jump in one frame; the EOF test measured 12045 including the continuing background tone. Each exceeded its 900/1000-unit fixture-specific limit. Playback now applies 35-frame (about 0.8 ms) smoothstep ramps at file boundaries. The 6 ms fade test also found a 108-unit final jump caused by discarded Q15 division remainder; the remainder is now distributed across the ramp. These are reproducible digital defects, not proof that every audible crackle has the same cause.

The existing `test_audio_mixer` suite separately checks unity gain, cancellation, linked limiting, 32 full-scale voices, aligned-sine shape against an independent unity sine, attack/release and sample acceptance. The playback suite's inferred-gain check bounds adjacent gain changes to 0.033 away from zero crossings (32-frame attack plus PCM rounding tolerance). It is a discontinuity check, not a perceptual transparency or distortion measurement.

Host tests cannot establish ESP32 CPU headroom, SD read latency, I2S underruns or analog distortion. Confirm on the device with the same pair of samples played solo, simultaneously, with offsets, and retriggered, comparing RAM-loaded short samples against SD-streamed long ones. Capture the headphone-derived output if crackles remain and keep note of trigger timing and volume. Continuous loop crossfades and click-free stealing when all 32 slots are occupied remain separate work; the current suite does not certify them.


## I2S transport timing regression

`pio test -e native -f test_i2s_transport` tests the 128-frame staging buffer and the actual ESP32 branch of `StableAudioOutputI2S` against a driver fake. The ordinary playback suite still uses its immediate simulated I2S sink; it does not include this additional staging latency in its PCM timeline reference. Transport tests cover exact stereo packing and unity level, one driver call per complete DMA block, byte-granular partial writes and failed writes. A continuously advancing clock model also demonstrates underruns with a deliberately insufficient single-frame write budget; this corrects the blind spot of tests where simulated time stops whenever output is blocked. The timing assumptions are synthetic and do not benchmark the ESP32. See the [recording investigation](measurements/sample-test-2026-09-09.md).
