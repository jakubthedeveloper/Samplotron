# Calculator keypad diagnostic

Standalone ESP32-A1S firmware for discovering which two MCP23017 lines are
shorted by each calculator key. It is intentionally separate from the main
Samplotron firmware and initializes only Serial, I2C and the keypad MCP23017.

## Wiring

Disconnect power while wiring. The ESP32-A1S and MCP23017 must share ground.

| ESP32-A1S | MCP23017 |
|---|---|
| GPIO18 | SCL |
| GPIO23 | SDA |
| 3.3 V | VCC |
| GND | GND |

Connect the ten calculator lines to `PA0` through `PA7`, plus `PB1` and `PB2`.
The default keypad MCP23017 address is `0x26`; Samplotron's existing encoder
MCP normally remains at `0x27`. Ensure the MCP address straps select `0x26`.

## Build, upload and monitor

Run these commands from this directory:

```sh
pio run
pio run --target upload
pio device monitor
```

Alternatively, from the repository root:

```sh
pio run --project-dir tools/calculator_keypad_diagnostic
```

The monitor uses 115200 baud. To select another keypad MCP address, change
`KEYPAD_MCP_ADDRESS` in `platformio.ini`, for example:

```ini
-DKEYPAD_MCP_ADDRESS=0x25
```

At startup the firmware scans the I2C bus and explicitly verifies the
configured address. If it is absent, the diagnostic prints an error and retries
once per second.

## Usage

Press one calculator key at a time. A stable press and release prints, for
example:

```text
KEY DOWN: PA0 <-> PB1
KEY UP:   PA0 <-> PB1
```

Each newly observed pair is stored once. Up to all 45 possible pairs among the
ten lines can be retained until reset or power-off. Simultaneous connections
produce a warning instead of an attempted ghosting interpretation.

## Serial commands

| Command | Action |
|---|---|
| `h` | Show help |
| `s` | Scan the I2C bus again |
| **`d`** | **Dump the currently held keypad connection without debounce** |
| `r` | Reset the mapping press counter |
| `c` | Clear stored unique pairs |
| `l` | List stored unique pairs |

### Dumping the currently held key (`d`)

Hold a calculator key down and send the single character `d` in the serial
monitor. No Enter key is required. Example:

```text
> d
SNAPSHOT: PA0 <-> PB1
```

This is a single current-state snapshot and does not depend on observing a
KEY DOWN transition. A pair captured this way is also added to the unique-pair
list. Other possible results are:

```text
SNAPSHOT: no connection
SNAPSHOT: multiple connections detected; press one key at a time
```

The keypad is polled at roughly 200–300 Hz. During each step all lines are
pull-up inputs except one output driven LOW; the output is restored to a pull-up
input before the next line is selected. No interrupt pins are used.

Each connection must be observed in both scan directions before it is accepted:
`PA0` driven LOW must pull `PB1` LOW, and `PB1` driven LOW must also pull `PA0`
LOW. One-sided LOW readings are discarded as noise or an electrical fault. This
filter applies both to automatic KEY DOWN/KEY UP detection and to `d` snapshots.

## Troubleshooting false detections

If an idle keypad still reports connections after the reciprocal-reading
filter:

- check for solder bridges, contamination and calculator circuitry still tied
  to the ten keypad lines;
- verify that the MCP23017 and ESP32 use the same 3.3 V ground reference;
- keep keypad wiring short and away from I2S, audio and clock wiring;
- consider adding an external 10 kOhm pull-up from each keypad line to 3.3 V.
  The MCP23017 internal pull-ups are relatively weak and long calculator traces
  can pick up interference.
