# TODO (sampling_engine_spec_v1)

Status as of: 2026-03-10  
Source: `spec/sampling_engine_spec_v1.json`

## 1. Task and queue architecture (still open)

- [ ] Split out a dedicated `sample_loader` task (core 0, priority between audio and UI).
- [ ] Add `loader_command_queue` (UI/boot -> loader) as defined in the spec.
- [ ] Add `ui_status_queue` (loader/audio -> UI) as defined in the spec.
- [ ] Move loader operations (classification, preload, active registry refresh) from the current orchestration flow into the `sample_loader` domain.

## 2. Spec-compliant streaming (main gap)

- [ ] Implement `StreamManager` with read-ahead buffers for `STREAM` voices.
- [ ] Remove blocking SD reads from the critical audio path (`No blocking SD read in the critical audio mixing loop`).
- [ ] Define safe stream-data handoff between loader and audio (immutable/prepared descriptors + queues/ownership signaling).
- [ ] Rework preview playback so it uses the loader path (not direct audio-domain SD open/start).

## 3. Missing diagnostics vs spec

- [ ] Add `stream_underrun_count`.
- [ ] Add `audio_buffer_underrun_count` (output/mixer-level underrun counter).
- [ ] Add `boot_load_time_ms`.
- [ ] Expose consistent diagnostic status to UI (through `ui_status_queue` once implemented).

## 4. Policy parameterization and alignment

- [ ] Decide whether to restore the preload threshold from the spec (`preload_threshold_seconds = 2.0`) or keep the current deviation as an explicit product decision.
- [ ] (Optional) Make `preload_threshold_seconds` configurable in JSON (currently an accepted deviation in the spec).

## 5. Code cleanup and modularity (technical task)

- [ ] Slim down `src/main.cpp` by splitting it into modules, e.g.:
  - `app_bootstrap` (initialization and boot sequence),
  - `app_tasks` (task entrypoints and queues),
  - `app_orchestrator` (UI/MIDI/loader/audio integration),
  - `app_logging` (diagnostics/telemetry).
- [ ] Reduce global state in `main.cpp` (move into dedicated module contexts/structures).
- [ ] Normalize callbacks and event handlers into smaller, testable units.

## 6. Follow-up cleanup after recent fixes

- [ ] Decide whether to keep fade-in as an optional mechanism (`kVoiceFadeInUs`) or remove dead logic when set to `0`.
- [ ] Once audio behavior is stable, keep runtime diagnostics disabled by default and provide a short debug-profile instruction in docs.
