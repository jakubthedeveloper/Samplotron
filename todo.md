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
