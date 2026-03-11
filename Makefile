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
	@echo "  make convert-samples SAMPLES_DIR=/path/to/samples - convert *.wav to PCM16 44.1kHz mono"
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
	count="$$(find "$(SAMPLES_DIR)" -maxdepth 1 -type f \( -iname '*.wav' \) | wc -l)"; \
	if [ "$$count" -eq 0 ]; then \
		echo "No .wav files found in $(SAMPLES_DIR)"; \
	else \
		find "$(SAMPLES_DIR)" -maxdepth 1 -type f \( -iname '*.wav' \) | while IFS= read -r f; do \
			tmp="$${f}.tmp.wav"; \
			echo "Converting: $$f"; \
			ffmpeg -nostdin -hide_banner -loglevel error -y -i "$$f" -ac 1 -ar 44100 -c:a pcm_s16le "$$tmp"; \
			mv "$$tmp" "$$f"; \
		done; \
	fi
