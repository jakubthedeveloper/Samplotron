PIO ?= pio

MAIN_ENV := esp-wrover-kit
DEBUG_ENV := esp-wrover-kit-debug-input
DEBUG_MIDI_ENV := esp-wrover-kit-debug-midi

.PHONY: help upload-main upload-debug upload-debug-midi build-main build-debug build-debug-midi monitor

help:
	@echo "Available targets:"
	@echo "  make upload-main    - build and upload main firmware"
	@echo "  make upload-debug   - build and upload debug firmware"
	@echo "  make upload-debug-midi - build and upload MIDI debug firmware"
	@echo "  make build-main     - build main firmware"
	@echo "  make build-debug    - build debug firmware"
	@echo "  make build-debug-midi  - build MIDI debug firmware"
	@echo "  make monitor        - open serial monitor (115200)"

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

monitor:
	$(PIO) device monitor -b 115200
