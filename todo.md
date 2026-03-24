# TODO

## Open issues
1. Click/pop on retrigger of the same sample.
2. Click/pop when a sample is interrupted by the `panic` trigger.

## Constraints (do not break these)
1. Do not redesign polyphony behavior.
2. The same sample must continue to use the same voice slot on retrigger.

## Pop/click: attempts that did not solve the problem
1. Soft fade-in/fade-out on retrigger and panic (both short and longer times).
2. Deferred retrigger in the same voice slot (pending start after fade-out).
3. Changes to voice stop order (`wav->stop`, `stub->SetGain`, `stub->stop`) and suppressing `sink->stop` during pending start.
4. Additional short ramp-down of the last sample during voice output `stop()`.
5. Removal of blocking wait loops (`while(update())`) for retrigger/panic and switching to asynchronous fade-out.

Result: pop/click still occurs in both retrigger and panic cases.

## Additional attempts (2026-03-23)
1. Added detailed `VOICE_EVT` lifecycle logging (trigger, queued start, deferred start, loop end, stop commit, panic/group stop).
2. Verified retrigger path transitions are ordered as: queue pending start -> fade-out stop request -> zero-gain hold -> hard stop -> deferred restart.
3. Added time-order guards in fade ratio code (`nowUs <= startUs` and `nowUs <= stopStartUs`) to avoid unsigned time underflow around retrigger boundaries.
4. Moved `nowUs` sampling in `Audio::update()` to occur after deferred-start processing so newly started voices use a valid timestamp.
5. Temporarily reduced per-voice gain by 50% for A/B listening (diagnostic only).
6. Observed that heavy UART `Serial.printf` diagnostics in the real-time path can introduce audible digital stutter, so voice-state logs should remain disabled by default.
7. Dual-lane per-slot retrigger experiment (primary + shadow voice in the same logical slot, delayed fade-out on old lane, immediate start on new lane) with doubled physical stream/voice resources.
   Result: click/pop still present on retrigger.

## Additional attempts (2026-03-24)
1. RAM-side click tracing (`CLICK_TRACE` + `CLICK_EVT`) with ring-buffer snapshots and no realtime UART in audio callback.
   Result: useful data; recurrent correlation with `begin_ram` and occasional `stop_voice`.
2. Staged diagnostics with `STOP_ONLY` and `BEGIN_ONLY` retrigger modes.
   Result: jumps still present; strongest correlation around `begin_ram` transitions.
3. Retrigger start-offset matching (phase-like alignment in RAM source start).
   Result: rejected due to behavior regression (sample did not start from beginning); reverted.
4. One-sample muted start priming after `wav->begin`.
   Result: no reliable improvement; reverted.
5. Applying control operations at deterministic `Audio::update()` boundary via pending-op queue.
   Result: no improvement in user tests; reverted.

## Current observed signature
1. Typical jump: `jump_op=begin_ram`, `jump_prev=(~3k..5k)`, `jump_curr=(0,0)`.
2. This indicates a hard step to zero near source start or immediately after begin.

## Recommended next steps (not yet implemented)
1. Keep retrigger/panic logic, replace WAV/RAM playback with a trivial synthetic source feeding the same mixer/output path.
   Goal: determine whether discontinuity appears before or inside WAV/RAM decode path.
2. Force silence without stopping/resetting lower playback objects.
   Goal: compare against normal stop/begin and isolate lifecycle-related discontinuity.
3. Capture first 32-64 output frames per voice after each `begin_ram` into RAM trace.
   Goal: verify whether initial decoder/source frames are zeros or malformed.
4. Add DC/bias checks per stage (per-voice pre-mix and post-mix around retrigger/panic).
   Goal: confirm whether any stage injects offset discontinuity.
