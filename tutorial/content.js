// Static slide content. No runtime authoring or persistence.
window.SAMPLOTRON_GUIDE = {
  "title": "Samplotron — interactive build guide",
  "assets": [
    {
      "id": "esp",
      "name": "ESP32-A1S / Audio Kit",
      "image": "assets/esp.svg"
    },
    {
      "id": "oled",
      "name": "OLED SSD1309 · 128 × 64",
      "image": "assets/oled.svg"
    },
    {
      "id": "mcp",
      "name": "MCP23017",
      "image": "assets/mcp.svg"
    },
    {
      "id": "enc",
      "name": "Rotary encoders · left / right",
      "image": "assets/enc.svg"
    },
    {
      "id": "keypad",
      "name": "Matrix keypad · 4 × 4",
      "image": "assets/keypad.svg"
    },
    {
      "id": "sd",
      "name": "microSD card",
      "image": "assets/sd.svg"
    },
    {
      "id": "audio",
      "name": "Audio output / monitoring",
      "image": "assets/audio.svg"
    },
    {
      "id": "midi",
      "name": "MIDI IN · isolated receiver",
      "image": "assets/midi.svg"
    }
  ],
  "scenes": [
    {
      "id": "intro",
      "title": "Build Samplotron step by step",
      "caption": "This interactive guide shows you how to build Samplotron step by step.",
      "left": "esp",
      "right": "oled",
      "wires": [],
      "note": "Use Previous and Next, or choose any step below. Play animates the current step; playback pauses at the end unless Auto-advance is enabled. The illustrations show functional connections, so verify physical connector positions on your board revision.",
      "command": "",
      "duration": 50,
      "screenCues": [],
      "direction": {
        "mode": "device",
        "readSeconds": 24,
        "actions": [
          {
            "at": 24,
            "screen": "demo-ready",
            "control": "none",
            "gesture": "Three samples. Ready to play."
          },
          {
            "at": 27,
            "screen": "demo-kick-press",
            "settledScreen": "demo-kick",
            "control": "pad1",
            "gesture": "Press key 1 → kick · C2 (36)"
          },
          {
            "at": 31,
            "screen": "demo-snare-press",
            "settledScreen": "demo-snare",
            "control": "pad2",
            "gesture": "Press key 2 → snare · C#2 (37)"
          },
          {
            "at": 35,
            "screen": "demo-hat-press",
            "settledScreen": "demo-hat",
            "control": "pad3",
            "gesture": "Press key 3 → hi-hat · D2 (38)"
          },
          {
            "at": 39,
            "screen": "demo-volume",
            "control": "left-turn",
            "gesture": "Turn the left encoder → select VOL"
          },
          {
            "at": 44,
            "screen": "demo-volume-80",
            "control": "right-turn",
            "gesture": "Turn the right encoder → adjust volume",
            "turn": -4
          }
        ],
        "layout": "vertical-photo-s",
        "introduction": {
          "description": "Samplotron is a standalone hardware WAV sampler. Load sounds from a microSD card and play them with a 16-key pad or an external MIDI controller.",
          "link": "https://github.com/jakubthedeveloper/Samplotron"
        }
      }
    },
    {
      "id": "parts",
      "title": "What you will need",
      "caption": "An ESP32-A1S Audio Kit with an ES8388 codec, an SSD1309 OLED, an MCP23017, two push-button rotary encoders and a 4 × 4 matrix keypad.",
      "left": "esp",
      "right": "oled",
      "wires": [],
      "note": "Also needed: a microSD card, wiring, suitable power, a programming USB cable and monitoring equipment. The module illustrations show their functions, not a verified physical pin layout.",
      "command": "",
      "duration": 35,
      "screenCues": [],
      "direction": {
        "mode": "spotlight",
        "readSeconds": 0,
        "focusCues": [
          {
            "at": 0,
            "asset": "esp",
            "heading": "ESP32-A1S / Audio Kit",
            "text": "The heart of the sampler. Runs the firmware and sends audio through its onboard ES8388 codec."
          },
          {
            "at": 7,
            "asset": "oled",
            "heading": "SSD1309 OLED",
            "text": "See the sample library, assignments and settings on a compact 128 × 64 display."
          },
          {
            "at": 14,
            "asset": "mcp",
            "heading": "MCP23017",
            "text": "Adds the inputs for two encoders and a 4 × 4 keypad using the shared I²C bus."
          },
          {
            "at": 21,
            "asset": "enc",
            "heading": "Two rotary encoders",
            "text": "Turn to browse and adjust. Click to enter or preview. Hold the right button to assign a sample."
          },
          {
            "at": 28,
            "asset": "keypad",
            "heading": "4 × 4 matrix keypad",
            "text": "Sixteen keys trigger your assigned samples. Each key uses the same note-mapping system as MIDI."
          }
        ]
      }
    },
    {
      "id": "sd",
      "title": "Prepare your sample library",
      "caption": "Format the card as FAT32. Create a /samples folder and copy in WAV files: uncompressed PCM, 16-bit, 44.1 kHz, mono.",
      "left": "esp",
      "right": "sd",
      "wires": [],
      "note": "Up to 32 files, no subfolders. Insert the card with power off. The screen demo uses 01-kick.wav, 02-snare.wav and 03-hat.wav in this order.",
      "command": "",
      "duration": 26,
      "screenCues": [],
      "direction": {
        "mode": "cards",
        "readSeconds": 0,
        "cards": [
          {
            "at": 0,
            "heading": "Start with FAT32",
            "text": "Format the microSD card as FAT32.",
            "detail": "Use the card slot on the Audio Kit.",
            "icon": "sd"
          },
          {
            "at": 7,
            "heading": "Create /samples",
            "text": "Copy your WAV files into this folder at the root of the card.",
            "detail": "/samples/01-kick.wav   /samples/02-snare.wav   /samples/03-hat.wav",
            "icon": "folder"
          },
          {
            "at": 16,
            "heading": "Use the right audio format",
            "text": "Uncompressed PCM · 16-bit · 44.1 kHz · mono",
            "detail": "Up to 32 files. No subfolders. Insert the card with power off.",
            "icon": "audio"
          }
        ]
      }
    },
    {
      "id": "oled-power",
      "title": "01 / Connect OLED GND and VCC",
      "caption": "Power off first. Connect ESP32 GND to OLED GND. Connect ESP32 3V3 to OLED VCC only if your display module supports a 3.3 V supply.",
      "left": "esp",
      "right": "oled",
      "wires": [
        {
          "source": "GND",
          "target": "GND",
          "color": "#a4afbd"
        },
        {
          "source": "3V3*",
          "target": "VCC*",
          "color": "#ff737c"
        }
      ],
      "note": "*3.3 V is conditional on the exact OLED module. SDA/SCL pull-ups must go to 3.3 V. Functional labels do not identify physical pin positions.",
      "command": "",
      "duration": 25,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 9
      },
      "connectionSummary": [
        "POWER CONNECTIONS",
        "GND → GND",
        "3V3 → VCC*"
      ]
    },
    {
      "id": "oled-bus",
      "title": "01 / Connect the display bus",
      "caption": "With OLED GND and VCC connected, add the I²C signals: GPIO23 to SDA and GPIO18 to SCL.",
      "left": "esp",
      "right": "oled",
      "wires": [
        {
          "source": "GPIO23",
          "target": "SDA",
          "color": "#69d6f5"
        },
        {
          "source": "GPIO18",
          "target": "SCL",
          "color": "#ffd275"
        }
      ],
      "note": "Connect ESP32 GND to OLED GND. Connect ESP32 3V3 to OLED VCC only if the display module supports a 3.3 V supply. Keep SDA/SCL pull-ups at 3.3 V. OLED I²C address: 0x3C or 0x3D. OLED_MOSI and OLED_SCK in pins.h are historical names; this display uses I²C, not SPI.",
      "command": "",
      "duration": 25,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 8
      },
      "connectionSummary": [
        "POWER ALREADY CONNECTED",
        "GND → GND",
        "3V3 → VCC*"
      ]
    },
    {
      "id": "flash",
      "title": "01 / Download and flash the firmware",
      "caption": "Download all four firmware files from the same Samplotron release. Install esptool, connect the programming USB port, then flash the files at their documented addresses.",
      "left": "esp",
      "right": "oled",
      "wires": [],
      "note": "Download all four binary assets from the same release. The displayed multiline command uses Bash continuations. On Windows, use the same command on one line and replace /dev/ttyUSB0 with your COM port. This guide does not download or flash firmware automatically.",
      "command": "python -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 921600 write-flash 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin",
      "duration": 84,
      "screenCues": [],
      "direction": {
        "mode": "cards",
        "readSeconds": 0,
        "cards": [
          {
            "at": 0,
            "heading": "Download the firmware",
            "text": "Open the Samplotron release page on GitHub.",
            "detail": "Select the main-latest release and expand Assets.",
            "icon": "folder",
            "link": "https://github.com/jakubthedeveloper/Samplotron/releases/tag/main-latest"
          },
          {
            "at": 12,
            "heading": "Keep all four files together",
            "text": "Download these files from the same release.",
            "detail": "Save them in one folder. Use the binary assets, not Source code.zip.",
            "icon": "folder",
            "files": [
              "bootloader.bin",
              "partitions.bin",
              "boot_app0.bin",
              "firmware.bin"
            ]
          },
          {
            "at": 28,
            "heading": "Install the flashing tool",
            "text": "",
            "detail": "Install Python first if needed. Then open a terminal and run this command.",
            "icon": "terminal",
            "command": "python -m pip install esptool"
          },
          {
            "at": 40,
            "heading": "Connect the programming USB port",
            "text": "Select the serial port assigned to the Audio Kit.",
            "detail": "Example: /dev/ttyUSB0 on Linux or COM3 on Windows. BOOT procedure depends on the board.",
            "icon": "usb"
          },
          {
            "at": 52,
            "heading": "Flash the downloaded files",
            "text": "",
            "detail": "Run in the download folder. Replace /dev/ttyUSB0 with your board’s port.",
            "icon": "terminal",
            "commandLines": [
              "python -m esptool --chip esp32 --port /dev/ttyUSB0 \\",
              "  --baud 921600 write-flash \\",
              "  0x1000 bootloader.bin \\",
              "  0x8000 partitions.bin \\",
              "  0xe000 boot_app0.bin \\",
              "  0x10000 firmware.bin"
            ]
          },
          {
            "at": 76,
            "heading": "Restart and check the OLED",
            "text": "Look for the startup screen, then Ready.",
            "detail": "The next scene shows the actual firmware screens.",
            "icon": "oled"
          }
        ]
      }
    },
    {
      "id": "oled-test",
      "title": "01 / Check the first screen",
      "caption": "After restarting, the OLED shows the startup screen, sample information and Ready. It then opens the main screen. The encoders are not connected yet.",
      "left": "esp",
      "right": "oled",
      "wires": [],
      "note": "The OLED screens use the firmware renderer and its U8g2 fonts. Example data: three files, no assignments and 0% sample RAM. These are software-rendered screens, not a hardware test.",
      "command": "",
      "duration": 34,
      "screenCues": [
        {
          "at": 10,
          "screen": "startup",
          "gesture": "Power on",
          "control": "none"
        },
        {
          "at": 14,
          "screen": "boot-loading",
          "gesture": "Read the sample library",
          "control": "none"
        },
        {
          "at": 18,
          "screen": "boot-ready",
          "gesture": "Startup complete",
          "control": "none"
        },
        {
          "at": 24,
          "screen": "main-empty",
          "gesture": "Main screen · no sample selected",
          "control": "none"
        }
      ],
      "direction": {
        "mode": "guided",
        "readSeconds": 10
      }
    },
    {
      "id": "mcp-power",
      "title": "02 / Prepare the MCP23017",
      "caption": "Disconnect power. Connect the MCP to ground and 3.3 V. Set A0, A1 and A2 HIGH to select address 0x27.",
      "left": "esp",
      "right": "mcp",
      "wires": [
        {
          "source": "GND",
          "target": "VSS / GND",
          "color": "#a4afbd"
        },
        {
          "source": "3V3",
          "target": "VDD / VCC",
          "color": "#ff737c"
        },
        {
          "source": "3V3",
          "target": "A0, A1, A2",
          "color": "#ffb67c"
        },
        {
          "source": "3V3",
          "target": "RESET ↑",
          "color": "#f5a5d8"
        }
      ],
      "note": "Keep RESET HIGH. Check breakout jumpers and resistors. A bare IC requires decoupling and a complete hardware schematic.",
      "command": "",
      "duration": 32,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 9
      }
    },
    {
      "id": "mcp-bus",
      "title": "02 / Share the I²C bus",
      "caption": "Add the MCP to the existing OLED bus: GPIO23 to SDA, GPIO18 to SCL. Connect INTA to GPIO05.",
      "left": "esp",
      "right": "mcp",
      "wires": [
        {
          "source": "GPIO23 · SDA OLED",
          "target": "SDA",
          "color": "#69d6f5"
        },
        {
          "source": "GPIO18 · SCL OLED",
          "target": "SCL",
          "color": "#ffd275"
        },
        {
          "source": "GPIO05",
          "target": "INTA",
          "color": "#bd9aff"
        }
      ],
      "note": "Check I²C pull-ups to 3.3 V. Main firmware does not use INTB; do not connect it to GPIO00.",
      "command": "",
      "duration": 28,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 8
      }
    },
    {
      "id": "enc-left",
      "title": "02 / Connect the left encoder",
      "caption": "Connect the left encoder’s A, B and switch to GPA0, GPA1 and GPA2. Connect its common contact and the other switch contact to GND.",
      "left": "mcp",
      "right": "enc",
      "wires": [
        {
          "source": "GPA0",
          "target": "LEFT · A",
          "color": "#69d6f5"
        },
        {
          "source": "GPA1",
          "target": "LEFT · B",
          "color": "#ffd275"
        },
        {
          "source": "GPA2",
          "target": "LEFT · SW",
          "color": "#bd9aff"
        },
        {
          "source": "GND",
          "target": "C + other SW contact",
          "color": "#a4afbd"
        }
      ],
      "note": "Assumes a passive encoder with contacts. Verify A/B/common and switch pin positions on the exact part.",
      "command": "",
      "duration": 30,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 10
      }
    },
    {
      "id": "enc-right",
      "title": "02 / Connect the right encoder",
      "caption": "Connect the right encoder’s A, B and switch to GPA3, GPA4 and GPA5. Connect its common contact and the other switch contact to GND.",
      "left": "mcp",
      "right": "enc",
      "wires": [
        {
          "source": "GPA3",
          "target": "RIGHT · A",
          "color": "#69d6f5"
        },
        {
          "source": "GPA4",
          "target": "RIGHT · B",
          "color": "#ffd275"
        },
        {
          "source": "GPA5",
          "target": "RIGHT · SW",
          "color": "#bd9aff"
        },
        {
          "source": "GND",
          "target": "C + other SW contact",
          "color": "#a4afbd"
        }
      ],
      "note": "Assumes a passive encoder with contacts. Verify breakout labels and any onboard electronics.",
      "command": "",
      "duration": 29,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 10
      }
    },
    {
      "id": "enc-test",
      "title": "02 / Take control of the screen",
      "caption": "Power on. Select LIB with the left encoder and click the right button. Turn the right encoder to browse samples. Click the left button to go back.",
      "left": "enc",
      "right": "oled",
      "wires": [],
      "note": "The main firmware remains installed. For optional encoder diagnostics, use make upload-debug; restore make upload-main afterwards.",
      "command": "",
      "duration": 41,
      "screenCues": [
        {
          "at": 11,
          "screen": "main-empty",
          "gesture": "Left encoder · select LIB",
          "control": "none"
        },
        {
          "at": 16,
          "screen": "library-kick",
          "gesture": "Right button · click",
          "control": "right-press"
        },
        {
          "at": 22,
          "screen": "library-snare",
          "gesture": "Right encoder · turn one step",
          "control": "right-turn"
        },
        {
          "at": 27,
          "screen": "library-hat",
          "gesture": "Right encoder · turn one step",
          "control": "right-turn"
        },
        {
          "at": 33,
          "screen": "main-empty",
          "gesture": "Left button · back",
          "control": "left-press"
        }
      ],
      "direction": {
        "mode": "guided",
        "readSeconds": 11
      }
    },
    {
      "id": "audio-connect",
      "title": "03 / Connect the audio output",
      "caption": "With power disconnected, connect your monitoring equipment to the appropriate analog output on the Audio Kit. Start with a low monitoring volume.",
      "left": "esp",
      "right": "audio",
      "wires": [
        {
          "source": "ANALOG OUTPUT*",
          "target": "MONITOR INPUT*",
          "color": "#74e3b4"
        }
      ],
      "note": "*Exact jack and cable depend on the board revision. ES8388 is onboard. The transformer-isolated output variant needs a separate confirmed schematic.",
      "command": "",
      "duration": 25,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 10
      }
    },
    {
      "id": "preview",
      "title": "03 / Preview your first sample",
      "caption": "Open LIB, select a sample with the right encoder and briefly click its button. The sample plays while the display stays in the library.",
      "left": "enc",
      "right": "audio",
      "wires": [],
      "note": "Preview leaves LIBRARY on the screen. It does not open the waveform visualizer. This guide is silent; check sound through the connected monitoring equipment.",
      "command": "",
      "duration": 36,
      "screenCues": [
        {
          "at": 10,
          "screen": "library-snare",
          "gesture": "LIBRARY · select a sample",
          "control": "none"
        },
        {
          "at": 16,
          "screen": "library-kick",
          "gesture": "Right encoder · select the kick",
          "control": "right-turn"
        },
        {
          "at": 22,
          "screen": "library-preview",
          "gesture": "Right button · click to preview",
          "control": "right-press"
        },
        {
          "at": 30,
          "screen": "main-preview",
          "gesture": "Left button · back",
          "control": "left-press"
        }
      ],
      "direction": {
        "mode": "guided",
        "readSeconds": 10
      }
    },
    {
      "id": "key-rows",
      "title": "04 / Connect the keypad rows",
      "caption": "Disconnect power. Identify the row and column wires with a meter. Connect rows R1–R4 to GPB0–GPB3 in order.",
      "left": "mcp",
      "right": "keypad",
      "wires": [
        {
          "source": "GPB0",
          "target": "R1",
          "color": "#69d6f5"
        },
        {
          "source": "GPB1",
          "target": "R2",
          "color": "#ffd275"
        },
        {
          "source": "GPB2",
          "target": "R3",
          "color": "#bd9aff"
        },
        {
          "source": "GPB3",
          "target": "R4",
          "color": "#74e3b4"
        }
      ],
      "note": "Ribbon pin order is not universal. Do not assume the first four wires are the rows.",
      "command": "",
      "duration": 30,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 8
      }
    },
    {
      "id": "key-cols",
      "title": "04 / Connect the keypad columns",
      "caption": "Connect columns C1–C4 to GPB4–GPB7 in order. A passive matrix uses eight signal lines and does not need a separate VCC connection.",
      "left": "mcp",
      "right": "keypad",
      "wires": [
        {
          "source": "GPB4",
          "target": "C1",
          "color": "#ffb67c"
        },
        {
          "source": "GPB5",
          "target": "C2",
          "color": "#f5a5d8"
        },
        {
          "source": "GPB6",
          "target": "C3",
          "color": "#a8df7e"
        },
        {
          "source": "GPB7",
          "target": "C4",
          "color": "#8cafff"
        }
      ],
      "note": "Firmware scans the rows and enables column pull-ups. A matrix without diodes can produce ghost keys with simultaneous presses.",
      "command": "",
      "duration": 29,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 10
      }
    },
    {
      "id": "assign",
      "title": "04 / Assign a sample and a panic key",
      "caption": "In LIB, select a sample and hold the right button for at least 700 ms. When ASSIGN NOTE appears, press the keypad key you want to use.",
      "left": "enc",
      "right": "keypad",
      "wires": [],
      "note": "Keys map to notes 36–51 in the firmware. The demo assigns the kick to C2 (36) and panic to the unused D key, D#3 (51). Check this mapping on your matrix. Panic takes priority over a sample on the same note, so choose an unused key. SAVE in the next step also stores the panic assignment. The firmware displays Waiting for MIDI during both MIDI and keypad assignment.",
      "command": "",
      "duration": 88,
      "screenCues": [
        {
          "at": 11,
          "screen": "library-preview",
          "gesture": "LIBRARY · select the kick",
          "control": "none"
        },
        {
          "at": 17,
          "screen": "assign-note",
          "gesture": "Right button · hold ≥ 700 ms",
          "control": "right-hold"
        },
        {
          "at": 28,
          "screen": "library-assigned",
          "gesture": "Keypad · press a key → C2 (36)",
          "control": "pad1"
        },
        {
          "at": 41,
          "screen": "library-assigned",
          "control": "none",
          "gesture": "Add a panic button",
          "readText": "Assign a panic key to stop every sound, including loops. In LIB, turn the left encoder to PANIC MODE. Hold the right button, then press an unused key."
        },
        {
          "at": 56,
          "screen": "panic-mode",
          "control": "left-turn",
          "gesture": "Left encoder · select PANIC MODE"
        },
        {
          "at": 62,
          "screen": "assign-panic",
          "control": "right-hold",
          "gesture": "Right button · hold ≥ 700 ms"
        },
        {
          "at": 70,
          "screen": "panic-assigned",
          "control": "pad16",
          "gesture": "Press unused key D → panic · D#3 (51)"
        },
        {
          "at": 76,
          "screen": "panic-trigger",
          "settledScreen": "panic-settled",
          "control": "pad16",
          "gesture": "Press D again → stop every sound",
          "footer": "Panic fades out all voices, including loops."
        },
        {
          "at": 82,
          "screen": "panic-back-to-library",
          "control": "left-turn",
          "turn": -1,
          "gesture": "Left encoder · return to sample mode",
          "footer": "Save the setup in the next step to keep your panic key."
        }
      ],
      "direction": {
        "mode": "guided",
        "readSeconds": 11
      }
    },
    {
      "id": "trigger",
      "title": "04 / Play and save",
      "caption": "Press the assigned key again to play the sample. Return to the main screen with the left button, select SAVE and click the right button.",
      "left": "keypad",
      "right": "audio",
      "wires": [],
      "note": "Holding a key does not repeat its sample, and releasing it does not stop playback. SAVE stores the sample and panic assignments. Check that they return after a power cycle.",
      "command": "",
      "duration": 41,
      "screenCues": [
        {
          "at": 11,
          "screen": "library-assigned",
          "gesture": "Sample assigned to C2 (36)",
          "control": "none"
        },
        {
          "at": 16,
          "screen": "library-trigger",
          "gesture": "Keypad · press the same key again",
          "control": "pad1"
        },
        {
          "at": 21,
          "screen": "main-trigger",
          "gesture": "Left button · back",
          "control": "left-press"
        },
        {
          "at": 27,
          "screen": "main-save",
          "gesture": "Left encoder · select SAVE",
          "control": "left-turn"
        },
        {
          "at": 33,
          "screen": "saving",
          "gesture": "Right button · click",
          "control": "right-press"
        },
        {
          "at": 35,
          "screen": "saved",
          "gesture": "Save completed",
          "control": "none"
        },
        {
          "at": 36,
          "screen": "main-saved",
          "gesture": "Assignment saved on SD",
          "control": "none"
        }
      ],
      "direction": {
        "mode": "guided",
        "readSeconds": 11
      }
    },
    {
      "id": "midi-wire",
      "title": "05 / Optional MIDI input",
      "caption": "Connect the 3.3 V logic output of an opto-isolated MIDI receiver to GPIO22. On the ESP32 side, connect the receiver ground to board GND.",
      "left": "midi",
      "right": "esp",
      "wires": [
        {
          "source": "OUT · 3.3 V logic",
          "target": "GPIO22 · RX",
          "color": "#bd9aff"
        },
        {
          "source": "GND · ESP32 side",
          "target": "GND",
          "color": "#a4afbd"
        }
      ],
      "note": "Block diagram only: choose a specific receiver and confirm its supply wiring. Never connect DIN pins directly to GPIO. UART: 31,250 bit/s.",
      "command": "",
      "duration": 30,
      "screenCues": [],
      "direction": {
        "mode": "guided",
        "readSeconds": 10
      }
    },
    {
      "id": "midi-test",
      "title": "05 / Play from a MIDI controller",
      "caption": "Connect the controller’s MIDI OUT to Samplotron’s MIDI IN. In LIB, hold the right button and play a note. Play it again to trigger the sample, then save.",
      "left": "midi",
      "right": "audio",
      "wires": [],
      "note": "The demo reassigns the same sample from C2 (36) to C4 (60), removing its keypad assignment. Note On with nonzero velocity works on any channel. Save after assigning.",
      "command": "",
      "duration": 42,
      "screenCues": [
        {
          "at": 12,
          "screen": "library-keypad-saved",
          "gesture": "LIBRARY · choose the sample",
          "control": "none"
        },
        {
          "at": 17,
          "screen": "assign-midi",
          "gesture": "Right button · hold ≥ 700 ms",
          "control": "right-hold"
        },
        {
          "at": 25,
          "screen": "library-midi",
          "gesture": "Controller · play C4 (60)",
          "control": "midi-key"
        },
        {
          "at": 32,
          "screen": "main-midi",
          "gesture": "Play C4 again · left button to return",
          "control": "left-press"
        },
        {
          "at": 36,
          "screen": "main-midi-save",
          "gesture": "Left encoder · select SAVE",
          "control": "left-turn"
        },
        {
          "at": 38,
          "screen": "saving-midi",
          "gesture": "Right button · click",
          "control": "right-press"
        },
        {
          "at": 40,
          "screen": "saved-midi",
          "gesture": "MIDI assignment saved",
          "control": "none"
        }
      ],
      "direction": {
        "mode": "guided",
        "readSeconds": 12
      }
    },
    {
      "id": "done",
      "title": "Ready to play",
      "caption": "The OLED shows the interface. Encoders browse and preview samples. The keypad triggers your saved sounds. Optional MIDI lets you play from an external controller.",
      "left": "esp",
      "right": "oled",
      "wires": [],
      "note": "Check the completed device after restarting: browse the library, preview a sample, trigger an assigned key and verify your panic key. Use your own sample library and save any changes.",
      "command": "",
      "duration": 24,
      "screenCues": [],
      "direction": {
        "mode": "cards",
        "readSeconds": 0,
        "cards": [
          {
            "at": 0,
            "heading": "Browse. Preview. Play.",
            "text": "The OLED and encoders put your sample library at your fingertips.",
            "detail": "The keypad triggers your assigned sounds.",
            "icon": "oled"
          },
          {
            "at": 8,
            "heading": "Save your setup",
            "text": "Store assignments before powering off.",
            "detail": "Then check that they return after a restart.",
            "icon": "sd"
          },
          {
            "at": 16,
            "heading": "Make it your instrument",
            "text": "Load your own samples. Add an external MIDI controller if you want one.",
            "detail": "Samplotron · built by you.",
            "icon": "audio"
          }
        ]
      }
    }
  ],
  "language": "en",
  "repository": "https://github.com/jakubthedeveloper/Samplotron"
};
