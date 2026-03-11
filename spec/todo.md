# TODO (sampling_engine_spec_v1)

Status as of: 2026-03-11  
Source: `spec/sampling_engine_spec_v1.json`

## 1. Spec-compliant streaming (main gap)

- [ ] Remove blocking SD reads from the critical audio path (`No blocking SD read in the critical audio mixing loop`).
- [ ] Define safe stream-data handoff between loader and audio (immutable/prepared descriptors + queues/ownership signaling).
- [ ] Rework preview playback so it uses the loader path (not direct audio-domain SD open/start).

## 2. UI / assignment

- [ ] Panic button MIDI note assignment.
