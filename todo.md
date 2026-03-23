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
