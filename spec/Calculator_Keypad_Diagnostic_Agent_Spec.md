# Calculator Keypad Diagnostic Tool -- Agent Specification

## Goal

Create a standalone diagnostic tool inside the **Samplotron** repository
for reverse-engineering the keypad from a hacked desktop calculator.

Repository: https://github.com/jakubthedeveloper/Samplotron

The tool must **not** be integrated with the main Samplotron firmware.
It should build and run independently on an **ESP32-A1S** board.

## Hardware

The calculator keypad exposes **10 electrical lines**.

Each key shorts **exactly two lines**.

The lines are connected to a dedicated MCP23017: - PA0--PA7 - PB0--PB1

The MCP23017 address shall be configurable (default `0x26`). The
existing Samplotron MCP uses `0x27`.

## ESP32 I²C wiring

Use the same I²C bus as Samplotron.

    ESP32-A1S              MCP23017

    GPIO18 (SCL)  -------> SCL
    GPIO23 (SDA)  -------> SDA
    3.3V           -------> VCC
    GND            -------> GND

Before starting diagnostics: - scan the I²C bus, - verify that the
MCP23017 responds, - print a clear error if it is not detected.

## Project layout

Create an independent project, e.g.

`tools/calculator_keypad_diagnostic/`

Do not initialize: - ES8388 - I2S - SD card - OLED - MIDI - audio
engine - encoders

Only: - ESP32 - Wire - MCP23017 - Serial

Reuse the same PlatformIO configuration and MCP23017 library as
Samplotron.

## Scanning algorithm

1.  Configure every keypad line as INPUT with pull-up.
2.  For each line:
    -   change only this line to OUTPUT LOW,
    -   keep all other lines as INPUT_PULLUP,
    -   wait briefly,
    -   read remaining lines,
    -   LOW means the two lines are connected.
3.  Restore the line to INPUT_PULLUP.
4.  Continue.

Never drive two outputs simultaneously.

Normalize detected pairs so A-B == B-A.

## Debounce

Implement: - KEY DOWN - KEY UP

Print only once per press.

Example:

    KEY DOWN: PA0 <-> PB1
    KEY UP:   PA0 <-> PB1

## Mapping mode

Implement serial commands: - h --- help - s --- I²C scan - r --- reset
mapping counter - c --- clear stored pairs - l --- list stored pairs

## Multiple key presses

If more than one connection is detected simultaneously:

`WARNING: multiple connections detected; press one key at a time`

Do not attempt to resolve ghosting.

## Interrupts

Do **not** implement interrupt-based key detection yet.

Reason: When all keypad lines are configured as INPUT_PULLUP, pressing a
key shorts two HIGH inputs together, which does not generate an
interrupt.

Use polling (approximately 200--500 Hz).

## Acceptance criteria

The tool is complete when it: - builds independently, - detects the
MCP23017, - detects every key as a pair of lines, - reports KEY DOWN /
KEY UP, - avoids repeated reports while held, - stores unique pairs, -
warns about multiple simultaneous connections, - contains a README with
wiring and usage instructions.
