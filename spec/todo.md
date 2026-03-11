# TODO (sampling_engine_spec_v1)

Status as of: 2026-03-11  
Source: `spec/sampling_engine_spec_v1.json`

## 1. Task and queue architecture (still open)

- [ ] Split out a dedicated `sample_loader` task (core 0, priority between audio and UI).
- [ ] Add `loader_command_queue` (UI/boot -> loader) as defined in the spec.
- [ ] Add `ui_status_queue` (loader/audio -> UI) as defined in the spec.
- [ ] Move loader operations (classification, preload, active registry refresh) from the current orchestration flow into the `sample_loader` domain.

## 2. Spec-compliant streaming (main gap)

- [ ] Remove blocking SD reads from the critical audio path (`No blocking SD read in the critical audio mixing loop`).
- [ ] Define safe stream-data handoff between loader and audio (immutable/prepared descriptors + queues/ownership signaling).
- [ ] Rework preview playback so it uses the loader path (not direct audio-domain SD open/start).

## 3. Missing diagnostics vs spec

- [ ] Add `stream_underrun_count`.
- [ ] Add `audio_buffer_underrun_count` (output/mixer-level underrun counter).
- [ ] Add `boot_load_time_ms`.
- [ ] Expose consistent diagnostic status to UI (through `ui_status_queue` once implemented).

## 4. Follow-up cleanup after recent fixes

- [ ] Once audio behavior is stable, keep runtime diagnostics disabled by default and provide a short debug-profile instruction in docs.

## 5. UI / assignment

- [ ] Panic button MIDI note assignment.
