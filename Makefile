PIO ?= pio

MAIN_ENV := esp-wrover-kit
DEBUG_ENV := esp-wrover-kit-debug-input

.PHONY: help upload-main upload-debug build-main build-debug monitor

help:
	@echo "Available targets:"
	@echo "  make upload-main    - build and upload main firmware"
	@echo "  make upload-debug   - build and upload debug firmware"
	@echo "  make build-main     - build main firmware"
	@echo "  make build-debug    - build debug firmware"
	@echo "  make monitor        - open serial monitor (115200)"

upload-main:
	$(PIO) run -e $(MAIN_ENV) -t upload

upload-debug:
	$(PIO) run -e $(DEBUG_ENV) -t upload

build-main:
	$(PIO) run -e $(MAIN_ENV)

build-debug:
	$(PIO) run -e $(DEBUG_ENV)

monitor:
	$(PIO) device monitor -b 115200
