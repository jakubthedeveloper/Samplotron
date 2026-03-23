PIO ?= pio
SAMPLES_DIR ?= samples

ifneq ($(filter convert-samples,$(MAKECMDGOALS)),)
CONVERT_SAMPLES_ARGS := $(filter-out convert-samples,$(MAKECMDGOALS))
ifneq ($(CONVERT_SAMPLES_ARGS),)
SAMPLES_DIR := $(firstword $(CONVERT_SAMPLES_ARGS))
.PHONY: $(CONVERT_SAMPLES_ARGS)
$(CONVERT_SAMPLES_ARGS):
	@:
endif
endif

MAIN_ENV := esp-wrover-kit
DEBUG_ENV := esp-wrover-kit-debug-input
DEBUG_MIDI_ENV := esp-wrover-kit-debug-midi
TEST_ENV := native

.PHONY: help upload-main upload-debug upload-debug-midi build-main build-debug build-debug-midi test monitor convert-samples

help:
	@echo "Available targets:"
	@echo "  make upload-main    - build and upload main firmware"
	@echo "  make upload-debug   - build and upload debug firmware"
	@echo "  make upload-debug-midi - build and upload MIDI debug firmware"
	@echo "  make build-main     - build main firmware"
	@echo "  make build-debug    - build debug firmware"
	@echo "  make build-debug-midi  - build MIDI debug firmware"
	@echo "  make test           - run unit tests (native)"
	@echo "  make monitor        - open serial monitor (115200)"
	@echo "  make convert-samples SAMPLES_DIR=/path/to/samples - convert *.wav to PCM16 44.1kHz mono + peak normalize to -1 dBFS"
	@echo "  make convert-samples /path/to/samples            - same as above (path positional)"

upload-main:
	$(PIO) run -e $(MAIN_ENV) -t upload

upload-debug:
	$(PIO) run -e $(DEBUG_ENV) -t upload

upload-debug-midi:
	$(PIO) run -e $(DEBUG_MIDI_ENV) -t upload

build-main:
	$(PIO) run -e $(MAIN_ENV)

build-debug:
	$(PIO) run -e $(DEBUG_ENV)

build-debug-midi:
	$(PIO) run -e $(DEBUG_MIDI_ENV)

test:
	$(PIO) test -e $(TEST_ENV)

monitor:
	$(PIO) device monitor -b 115200

convert-samples:
	@command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg not found in PATH"; exit 1; }
	@[ -d "$(SAMPLES_DIR)" ] || { echo "Missing directory: $(SAMPLES_DIR)"; exit 1; }
	@set -e; \
	target_peak_db="-1.0"; \
	count="$$(find "$(SAMPLES_DIR)" -maxdepth 1 -type f \( -iname '*.wav' \) | wc -l)"; \
	if [ "$$count" -eq 0 ]; then \
		echo "No .wav files found in $(SAMPLES_DIR)"; \
	else \
		find "$(SAMPLES_DIR)" -maxdepth 1 -type f \( -iname '*.wav' \) | while IFS= read -r f; do \
			tmp="$${f}.tmp.wav"; \
			max_vol="$$(ffmpeg -nostdin -hide_banner -i "$$f" -af volumedetect -f null - 2>&1 | awk -F'max_volume: ' '/max_volume:/ {split($$2, a, " dB"); print a[1]}' | tail -n 1)"; \
			if [ -z "$$max_vol" ] || [ "$$max_vol" = "-inf" ]; then \
				gain_db="0.000"; \
				echo "Converting + normalizing: $$f (max=unknown, gain=$$gain_db dB, target=$$target_peak_db dBFS)"; \
			else \
				gain_db="$$(awk -v target="$$target_peak_db" -v max="$$max_vol" 'BEGIN { printf "%.3f", (target - max) }')"; \
				echo "Converting + normalizing: $$f (max=$$max_vol dB, gain=$$gain_db dB, target=$$target_peak_db dBFS)"; \
			fi; \
			ffmpeg -nostdin -hide_banner -loglevel error -y -i "$$f" -af "volume=$${gain_db}dB" -ac 1 -ar 44100 -c:a pcm_s16le "$$tmp"; \
			mv "$$tmp" "$$f"; \
		done; \
	fi
