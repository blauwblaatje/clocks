# Handover: ESP32-C6 handheld jam timer for CRG

Status: design only. No code, hardware or case files exist yet.
Date: 2026-11-09

## 1. Summary

A stopwatch-shaped handheld that controls the jam clock of the CRG roller
derby scoreboard (https://github.com/rollerderby/scoreboard). Jam timers
often prefer a physical stopwatch to a phone. The device is an ESP32-C6 mini
with a round display and a few buttons, in a 3D-printed case.

- Connected mode: Wi-Fi WebSocket client of the CRG server. It sends jam
  commands and mirrors the clock. The server is the source of truth.
- Standalone mode: its own jam/lineup/timeout/period state machine for
  practices, scrimmages and as a fallback if the scoreboard or network fails.
- Provisioning over USB from a browser: flash firmware, set Wi-Fi credentials
  and the scoreboard address, with no captive portal.

Wi-Fi reliability is not a concern for the author: the venue already runs a
dedicated network for the ePLT.

## 2. What is verified and what is not

Verified or well supported:
- CRG's clients are web clients using one WebSocket per client. A Stream Deck
  plugin (https://github.com/rcrderby/crg-streamdeck-plugin) already
  controls CRG from hardware and is the best reference implementation.
- That plugin fetches an HTTP session first, uses a single WebSocket,
  registers only for the paths it needs, pings every 30 s, reconnects with a
  growing delay, requires a one-second hold for Undo, and needs the device
  authorized in CRG Settings > Clients to write.
- CRG state paths look like `ScoreBoard.CurrentGame.Clock(Jam).Running`.

NOT verified, check first:
- The exact WebSocket JSON framing (Register / Set / Ping) and the exact
  command keys for start jam, stop jam, timeout, undo. Capture them from the
  operator panel with browser dev tools (Network > WS) or read the JS in the
  scoreboard repo's html folder. Do not trust any key name written from memory.
- What the "eJT" option in CRG actually does. It was not found in the docs.
  The assumption is "electronic jam timer role". The device should work as a
  normal CRG client regardless.
- The pin map against the actual ESP32-C6 board used.
- Whether ESPHome could be used. It probably has no built-in WebSocket client,
  so plain Arduino/ESP-IDF is the working assumption.

## 3. Hardware

Board: ESP32-C6 mini (native USB Serial/JTAG, Wi-Fi 6, BLE). Confirm the
board exposes the native USB port and which GPIOs it breaks out.

Display: GC9A01 1.28 inch round, SPI. Alternative: 1.69 inch ST7789
rectangular if more text room is wanted.

Buttons: three tactile switches.
- Crown (top, largest, guarded): start/stop jam
- Side A (about 2 o'clock): timeout
- Side B (about 10 o'clock): undo / menu, hold to confirm

Extras: small vibration motor (haptic confirm and a buzz 5 s before a jam
should start), LiPo cell (500-1000 mAh), charger with protection, USB-C.

### Proposed pin map (verify!)

| Function | GPIO | Notes |
|---|---|---|
| Crown button | 0 | to GND, internal pull-up |
| Side A (timeout) | 1 | to GND, internal pull-up |
| Side B (undo/menu) | 2 | to GND, internal pull-up |
| Battery sense | 3 | equal-resistor divider to ADC |
| Display SCK | 6 | SPI clock |
| Display MOSI | 7 | SPI data |
| Display CS | 18 | |
| Display DC | 19 | |
| Display RST | 20 | |
| Display backlight | 21 | PWM via small transistor |
| Vibration motor | 10 | via transistor + flyback diode |

Rules: avoid GPIO 8, 9, 15 (strapping), 12/13 (USB), 24-30 (flash). Keep
GPIO 16/17 free for UART debug. Buttons are on GPIO 0-2 because those are
believed to be low-power pins usable for deep-sleep wake (confirm in the
datasheet).

### Power

- Easiest: a board with a built-in LiPo connector and charger.
- Otherwise: charger module with protection -> low-dropout 3.3 V regulator
  -> 3V3 pin. Never connect a raw LiPo (up to 4.2 V) to 3V3.
- Make sure USB power and the battery cannot back-feed each other.

## 4. Case

Stopwatch form factor, printed in PETG or ABS (not PLA, it is handled and
can warm up). Plastic only near the antenna end, no metal or battery next
to it.

- Round case, lanyard ring at the top, large guarded crown button
- Two smaller pushers, shaped differently from the crown so they can be told
  apart by touch
- USB-C port at the bottom (6 o'clock), in the rear half
- Clamshell (front/back), four screws
- Internal stack: display, ESP32 board, battery at the back
- Vibration motor mounted against the case wall so the buzz is felt

## 5. Firmware design

Stack: PlatformIO, Arduino framework, ArduinoJson, a WebSocket client
library, LovyanGFX or LVGL for the display.

Components:
1. Input: GPIO interrupt on press edge, 10-20 ms lockout, hold detection for
   undo. Act on press, not release.
2. State machine: standalone jam/lineup/timeout/period logic with
   configurable durations (defaults commonly 2:00 jam, 0:30 lineup,
   1:00 team timeout, 30:00 period, but rulesets differ).
3. CRG client: HTTP session fetch, persist the cookie in NVS, one WebSocket,
   register only for needed clock paths, ping every 30 s, reconnect with
   growing delay.
4. Clock interpolation: store last Time + Running + a local timestamp, count
   locally between server updates, resync on each update.
5. Display task: draws the clock and state. Always shows connection state
   clearly (Connected / Standalone / NO CONN / NOT AUTHORIZED).
6. Haptics and power: buzz 5 s before a jam should start. Wi-Fi power save
   off while the screen is awake, on when idle. Deep sleep with button wake.

Behavior rules:
- Never show "jam started" on a button press. Show it only when the server
  reports the jam clock running. A slow or missing response must be visible
  to the user.
- Do not queue button presses while disconnected.
- Set TCP_NODELAY on the socket.
- Server is the source of truth when connected. Local state only mirrors it.
- Do not auto-merge offline history into CRG on reconnect. Show the CRG state
  and let the operator decide. Offer a manual "take over locally" action
  instead of switching silently.
- Only one device should believe it owns the clock at a time.

## 6. Provisioning and install

- Static web page (GitHub Pages) using ESP Web Tools: flash from Chrome or
  Edge over Web Serial, with a manifest and firmware binaries. Not supported
  in Firefox or Safari, and phone support is patchy, so assume a laptop.
- Wi-Fi credentials via Improv Wi-Fi Serial (firmware must implement it).
- Scoreboard address: Improv only covers Wi-Fi, so use one of:
  a) mDNS / fixed hostname on the dedicated network
  b) a small Web Serial config page that sends the host and stores it in NVS
  c) a settings page served by the device once on Wi-Fi
- Authorize the device in CRG Settings > Clients before expecting write
  access.

## 7. Suggested milestones

1. Verify the CRG WebSocket protocol and command keys (section 2).
2. Standalone state machine with buttons and display on a dev board. No
   network needed.
3. CRG client: connect, read clocks, show them, local interpolation.
4. CRG control: send jam/timeout/undo and show server-confirmed state.
5. Provisioning page (ESP Web Tools + Improv + host setting).
6. Battery, power management, haptics.
7. Case design, print, fit check, a full 2-3 hour bout battery test.
8. Latency check: measure press-to-server-update time on the real network.

## 8. Suggested repo layout

```
firmware/       PlatformIO project
hardware/       wiring, BOM, pin map
case/           3D print files (STEP/STL) and notes
web-installer/  ESP Web Tools page and manifest
docs/           protocol notes, test log
HANDOVER.md     this file
```

## 9. Risks

- Protocol details are unverified (section 2). Biggest unknown.
- CRG may change its WebSocket interface between versions. Pin to the
  version the league runs and note it in docs/.
- Battery life with Wi-Fi awake needs a real-world test.
- Accidental button presses. Mitigate with guards, hold-to-confirm for undo,
  and clear state feedback.
- Pin map may not match the exact mini board. Check the pinout.

## 10. First task for Claude Code

Clone https://github.com/rollerderby/scoreboard locally, find the WebSocket
handler and the client-side JS, and write docs/protocol.md with the exact
message format, session handling, authorization behavior, and the keys for
start jam, stop jam, timeout, undo and the clock values. Then scaffold the
repo layout above and begin milestone 2.