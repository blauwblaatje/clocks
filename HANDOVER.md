# Handover: CLOCK (CRG-Linked Officials' Clock Kit)

Handheld jam timer and penalty box timer for the CRG scoreboard.
State of 2026-11-10 (end of day). For: Claude Code, continuing on Rogier's
laptop. Folder `clock/` (was `scoreboard-stopwatch/`), no git repository
yet. `docs/decisions.md` is the dated log behind this file; where they
differ, this file wins. `docs/handover-original.md` is the first,
design-only handover (superseded).

## 1. What this is

A stopwatch-shaped handheld for roller derby officials that controls and
mirrors the CRG scoreboard (https://github.com/rollerderby/scoreboard) over
Wi-Fi, and also works standalone. One device, one firmware, three roles:

- Jam timer: start/stop jam, timeouts, undo. The hardware version of CRG's
  `/nso/jt/` screen.
- Box timer, blockers: times one team's blockers in the penalty box
  (CRG `/nso/pbt/`).
- Box timer, jammers: a box manager timing both jammers.

## 2. Status

Nothing has run on the board yet: Rogier has ordered the parts, the board
has not arrived. Everything below runs on the PC (unit tests, desktop
simulator, live tests against a local Java CRG) or compiles for the chip.

Works on the PC
- CRG protocol for the jam timer and the box timer, read from the Java
  source and checked against a local server: `docs/protocol.md`
  (sections 1-11 jam timer, 12 box timer incl. undo, Change, tick rate).
- Client core `firmware/lib/crgclient`: session cookie, register per role,
  typed state mirror, commands with a confirmation per press, ping, dead
  connection check, reconnect, eJT settings check. Live test: 55+ checks
  (`tools/crgclient-live/`).
- WebSocket client `firmware/lib/wsposix`: BSD sockets (Linux and lwIP),
  non-blocking connect, Cookie / Set-Cookie.
- Jam timer: standalone logic `jamcore`, controller `jtctl`, screens `jtui`.
- Box timer roles (team 1 / team 2 blockers, jammers): standalone clocks
  `boxcore`, controller `boxctl`, screens `boxui`, "Swap sides?" prompt.
- Device menu `devmenu` + `devmenuui`; the device glue `lib/device` (roles,
  menu, connection, battery pill, automatic sleep) shared by the firmware
  and the simulator.
- Settings `lib/settings` (saved networks with their scoreboard, role,
  button sides per role, standalone rules, sleep, brightness) and the setup
  page via `lib/httpmini`, with a QR setup screen.
- Bitmap fonts (Montserrat, lv_font_conv) in `lib/jtui/fonts`.
- Firmware updates `lib/update` (2026-11-13): menu > Connection > Update
  checks the release address (latest.json), offers a newer version,
  downloads it with a size, image-header and SHA-256 check into the other
  app slot and restarts; the setup page takes a firmware.bin upload
  (streamed by httpmini). Refused while a clock runs or someone is in the
  box, and below 30 % battery unless on USB. Board: HTTPS with ESP-IDF's
  certificate bundle, esp_ota with sequential writes, rollback until the
  new firmware has run 30 s (or goes to sleep). End to end in the
  simulator: tools/game-test/update.py.
- Desktop simulator `sim/` (SDL; `--role`, `--crg`, `--setup`, `--shots`, `--ctl`).
- Ten unit test suites in `firmware/test/native`, all passing.

Compiles for the chip, not run (`cd firmware && pio run -e c6`)
- `src/main.cpp` + `src/board.cpp`: buttons, LVGL with partial buffers,
  GC9A01 display through Arduino_GFX 1.6.9, CST816S touch, backlight PWM,
  battery ADC, settings in NVS, saved networks tried in order, setup access
  point `CLOCK-XXXX`, deep sleep. Pins from Waveshare's repository
  (`src/pins.h`). Static RAM 74 KB of 320 KB, flash 1.6 MB of 6.25 MB.
- Display rotation (`board::kRotation`) and the touch turn are reasoned,
  not tried.

Install and update site
- `Dockerfile` + `web-installer/`: builds the firmware and serves an ESP
  Web Tools install page, `firmware.bin`, `manifest.json`, `latest.json`
  on nginx. Built and checked locally. To run at clock.quagga.rocks on
  dc-k3s; GitHub repository and dc-k3s manifests wait for Rogier.

Case
- v1 (`case/`): switch holders, motor pad, cable anchor, board pads. Not
  printed. Board depth, USB height are estimates.

Open with Rogier
- The 2 o'clock switch is about 2.8 mm from the chip antenna (Waveshare's
  drawing). Decide on the bench: keep USB at 3 o'clock, or turn the board
  to USB at 9 o'clock (`USB_ROT` in case.py, `kRotation` in board.h).
- IO6 (motor) is also the RTC interrupt line: never enable RTC interrupts.

## 3. Repo map

```
HANDOVER.md              this file
README.md                short status and test commands
Dockerfile, .dockerignore  install/update site image (firmware built inside)
.github/workflows/       image.yml: GHCR image on a vX.Y.Z tag (as RTFM)
docs/protocol.md         CRG WebSocket protocol (jam timer, box timer), from source
docs/display.md          every screen, with sizes and colours
docs/decisions.md        dated decision log (history)
docs/*.png               screen sheets, setup page, case v1 inside
docs/handover-original.md  first handover, superseded
firmware/                PlatformIO project (env c6)
  include/lv_conf.h      LVGL config for the device
  src/main.cpp           board entry: buttons, LVGL, Wi-Fi, NVS, setup AP, sleep
  src/board.cpp/.h       display (Arduino_GFX), touch, backlight, battery
  src/pins.h             all board pins (from Waveshare's repository)
  lib/jamcore/           standalone jam timer state machine
  lib/crgclient/         CRG client core (no sockets)
  lib/wsposix/           ws:// client on BSD sockets (Linux and lwIP)
  lib/jtctl/, lib/jtui/  jam timer: behaviour (View) and LVGL screens; jtui/fonts/ = bitmap fonts
  lib/boxcore/           standalone box clocks
  lib/boxctl/, lib/boxui/  box timer roles: behaviour and screens
  lib/devmenu/, lib/devmenuui/  device menu
  lib/settings/          settings: storage, setup page, form
  lib/httpmini/          small HTTP server on sockets (setup page)
  lib/update/            firmware updates: release file, SHA-256, writer, rules, flow, plain HTTP GET
  lib/device/            the device: roles, menu, connection, battery, sleep, update screen (shared with sim/)
  test/native/           host tests (ten suites)
sim/                     desktop simulator (LVGL + SDL2), see sim/README.md
web-installer/           install page, make_release.py, README
hardware/README.md       board, battery, wiring notes, header pins, antenna
case/                    case.py, check.py, render_top.py, README; out/ = STL + STEP
tools/crg-capture/       probe scripts used to write protocol.md (box.py, box2.py, ...)
tools/crgclient-live/    live test: client core and box controller in a pipe
tools/game-test/         four CLOCKs (sim --ctl) on one CRG: a game, connection trouble
```

Host tests:

    cd firmware
    g++ -std=c++17 -Ilib/jamcore test/native/test_jamcore.cpp lib/jamcore/jamcore.cpp -o /tmp/t && /tmp/t
    g++ -std=c++17 -Wall -Ilib/crgclient test/native/test_crgclient.cpp lib/crgclient/*.cpp -o /tmp/tc && /tmp/tc

    g++ -std=c++17 -Wall -Ilib/jamcore -Ilib/crgclient -Ilib/jtctl test/native/test_jtctl.cpp lib/jamcore/jamcore.cpp lib/crgclient/*.cpp lib/jtctl/jtctl.cpp -o /tmp/tt && /tmp/tt

    g++ -std=c++17 -Wall -Ilib/boxcore test/native/test_boxcore.cpp lib/boxcore/boxcore.cpp -o /tmp/tb && /tmp/tb
    g++ -std=c++17 -Wall -Ilib/boxcore -Ilib/crgclient -Ilib/boxctl test/native/test_boxctl.cpp lib/boxcore/boxcore.cpp lib/crgclient/*.cpp lib/boxctl/boxctl.cpp -o /tmp/tx && /tmp/tx
    g++ -std=c++17 -Wall -Ilib/devmenu test/native/test_devmenu.cpp lib/devmenu/devmenu.cpp -o /tmp/tm && /tmp/tm
    g++ -std=c++17 -Wall -Ilib/settings test/native/test_settings.cpp lib/settings/settings.cpp -o /tmp/ts && /tmp/ts
    g++ -std=c++17 -Wall -pthread -Ilib/httpmini test/native/test_httpmini.cpp lib/httpmini/httpmini.cpp -o /tmp/th && /tmp/th
    g++ -std=c++17 -Wall -pthread -Ilib/wsposix test/native/test_wsposix.cpp lib/wsposix/wsposix.cpp -o /tmp/tw && /tmp/tw
    g++ -std=c++17 -Wall -Ilib/update -Ilib/crgclient test/native/test_update.cpp lib/update/*.cpp lib/crgclient/crgjson.cpp -o /tmp/tu && /tmp/tu

Firmware build for the chip (PlatformIO 6.2.0 or newer; the toolchain
downloads to ~/.platformio on the first run):

    cd firmware && pio run -e c6

Live test (throwaway CRG on port 4331): `tools/crgclient-live/run.sh 4331`.
Simulator: `sim/README.md`.

## 4. Hardware (decided)

- Board: Waveshare ESP32-C6-Touch-LCD-1.28, bare. GC9A01 240x240 round
  display with touch, onboard LiPo charger, USB-C, 512 KB RAM, 16 MB flash
  soldered on the board. 2.4 GHz Wi-Fi only.
- Battery: 3.7 V LiPo up to about 8 x 30 x 40 mm, 1000 mAh, MX1.25 plug.
  Target at least 6 h; charged over USB-C by the board.
- Three 6x6x4.3 mm tactile switches, hand-wired to the 12-pin header cable.
  No PCB for the first unit.
- Vibration motor: not in the first build, but everything stays ready for
  it (IO6 reserved, case pocket, firmware cues behind a "motor fitted"
  setting, default off).

Header pins (`firmware/src/pins.h`): IO1 crown, IO2 timeout, IO3 undo,
IO6 reserved for the motor. All are deep-sleep wake pins. Board pins from
Waveshare's repository (2026-11-10, see pins.h): display GC9A01 SPI
CLK 11 / MOSI 21 / CS 10 / DC 19 / RST 20, backlight 7; touch CST816S I2C
SDA 18 / SCL 8 / INT 4 / RST 23 (address 0x15); battery ADC GPIO0 with a
3:1 divider; no charge-status pin. IO6 is also the RTC's interrupt line:
never enable RTC interrupts with a motor on IO6.

## 5. Case (v1, fit test)

- 58 mm round, 20.5 mm deep, clamshell split on the button line, four
  M2 x 12 self-tapping screws from the back. PETG.
- Seen from the front: crown at 12 in a guard collar, side buttons at 10 and
  2, USB-C at 3, lanyard lug at 6 on the back shell.
- The board is turned a quarter turn (USB at 3) so the chip antenna sits at
  about 1 to 1:30, above the palm. Firmware must rotate picture and touch
  by 90 degrees. Only quarter turns are possible.
- Estimated, to be measured on the real board: board depth (`STACK` 8.5 mm),
  USB connector height (`USB_Z` 8.3 mm), antenna position.
- v1 (2026-11-10) adds switch holders (full-height columns, plunger
  preloads the cap), a 1027 motor pad, a cable-tie anchor and two board
  hold-down pads; battery moved 1.2 mm towards 6 o'clock. Details and
  assembly: case/README.md. Not printed yet.
- The 2 o'clock switch is about 5.8 mm from the estimated antenna; it cannot
  move further (USB opening). Check Wi-Fi with the switch fitted.
- `python3 case/case.py` rebuilds; `python3 case/check.py` checks
  interference against stand-ins for board, battery, switches and motor;
  `python3 case/render_top.py` draws the inside. Needs CadQuery 2.8.

## 6. Behaviour

Rules that hold in every role
- Connected: the scoreboard is the source of truth. Show a state only when
  the scoreboard reports it. A `Set` gets no acknowledgement, so each press
  needs a "no confirming state change within N ms" timeout that is visible
  to the user.
- Never queue presses while disconnected. Never merge local history into
  the scoreboard on reconnect. Switching to local control is a deliberate
  action ("take over locally"), never silent. So is switching back: when the
  scoreboard answers again the status line says "CRG back" (no popup), and
  the 3 s menu hold offers "Rejoin scoreboard?" Yes drops the local clocks
  (decided 2026-11-13).
- Keep-alive: ping after 5 s with nothing received, connection dead after
  12 s of silence (decided 2026-11-13; was 30 s / 45 s).
- Crown and timeout/pause act instantly on the press edge. They have no
  double-click meaning (it would delay the single press).
- Long presses: undo 1 s, menu 3 s, wake from sleep 1 s. Undo happens on
  the release after 1 s; held on to 3 s it opens the menu instead and undoes
  nothing (decided 2026-11-10 while building).
- The menu cannot open while a clock is running. Role changes and side
  swaps are refused while a clock is running or someone is in the box.

### Jam timer

| State | Crown | Timeout (2 o'clock) | Undo (10 o'clock) |
|---|---|---|---|
| Idle (READY) | Start jam | Start timeout | Short press: start lineup; hold: undo |
| Lineup | Start jam | Start timeout | Undo |
| Jam | Stop jam | Start timeout | Undo |
| Timeout | End timeout | New timeout | Undo |
| Period over (standalone) | Next period | - | - |
| Connection lost | not sent | not sent | Hold: run locally |

Connected-mode guards (agreed with Rogier 2026-11-10, see protocol.md
sections 7 and 10):
- After a crown press the crown is blocked for 1 s, with a visible cue. The
  scoreboard silently drops a Start right after a Stop and a Stop right
  after a Start, so the official must see that the second press did not go.
- During an intermission and after the last period the crown must be held
  1 s. The screen says what it will do: "Extend period N" (first half of
  the intermission, and always after the last period) or "Start period
  N+1" (second half).

After the timeout button the touch screen offers the type: TTO and OR per
team, OTO. Show CRG's `Label(...)` text for what the crown does next.
The scoreboard itself ignores the same button within 1 s and treats a
different clock button within 1 s as a correction (protocol.md section 7).

Standalone rules (in `jamcore::Config`, agreed with Rogier 2026-11-10, each
a setting on the settings page): the jam ends by itself at 2:00 (on/off),
a timeout stops the period clock until the next jam starts (or until the
timeout ends, or not at all), undo is one level deep (0 to 8 levels).

### Box timer, blockers

- Role is "Team 1 blockers" or "Team 2 blockers".
- Crown: skater sat down. Standalone, the clock starts at once and runs
  until the official pauses it. Connected, it follows the scoreboard,
  which runs box clocks only during jams (protocol.md section 12).
- Then a picker shows that team's positions with roster numbers. A skater
  with a noted (unserved) penalty is highlighted. If exactly one skater has
  a noted penalty, they are assigned at once and the picker is skipped.
- Tap a clock: Release, "+ Penalty (30 s)", Change skater, Cancel trip.
  Cancel works as the scoreboard does: in connected mode the automatic "?"
  penalty CRG added stays on the skater.
- Release is always a deliberate tap, never automatic at zero.
- Connected: the 2 o'clock button releases the skater whose time ran out
  first; with nobody done it sends nothing and says so. Early releases stay
  a tap on the clock. Standalone keeps 2 o'clock = pause/resume (Rogier,
  2026-11-10).
- "+ Penalty" in connected mode must record a penalty on the skater with
  CRG's "?" (Unknown) code, not just add clock time, so the penalty tracker
  correcting it later does not add the time twice. It needs a skater
  assigned to the clock.
- Penalty length is fixed at 30 s in firmware.

### Box timer, jammers

- Screen split left/right, one half per team. Left side button = left
  team's jammer sat down, right side button = right team's. No picker: the
  jammer comes from the lineup. Crown held = undo.
- Second jammer sits: first shows Done (swap), second shows the shortened time.

### Sides, hands, standalone

- Teams usually swap sides at intermission: prompt "Swap sides?" at
  intermission and offer it in the menu. In the jammer role it flips left
  and right; in the blocker roles it switches the team being timed.
- Rogier holds the device in the right hand as jam timer and the left hand
  as box timer. Which side button does what is a per-role setting on the
  web interface only.
- Standalone box timing: clocks only, no roster numbers, no noted
  penalties, no jam state. Pause/resume all clocks by hand (2 o'clock button
  in the blocker role, short crown press in the jammer role) with a clear
  PAUSED state. "+ Penalty" adds 30 s. "Clear clocks" in the menu.

## 7. Screens

See `docs/display.md`. Jam and lineup are the most important screens. Box
colours follow CRG's stylesheet (unserved teal, serving red, stand yellow,
done green).

## 8. Menu, setup, updates, power

Device menu (touch, four quadrants at the top level)
- Game: Role (+ Team), Swap sides, Mode (connected / standalone),
  Standalone game reset / next period, Clear clocks.
- Device: Brightness (and Vibration, only when a motor is fitted).
- Connection: Wi-Fi (pick a saved network), Setup, Info, Update.
- Power: Sleep / power off.
- Info shows battery, Wi-Fi and signal strength, scoreboard address, write
  permission, firmware version, and the press-to-confirmation time.

Setup
- The device starts its own access point (from the menu, and automatically
  when no Wi-Fi is configured), shows a QR code and password, and serves a
  settings page. The access point closes only after a successful join.
- Settings page: saved Wi-Fi networks (several; each with its own
  scoreboard host and port, default 8000), role and team, per-role button
  sides, standalone durations and rules (jam auto-end, period clock after a
  timeout, undo levels), "motor fitted", firmware upload.
- After joining, show whether the scoreboard answered.
- No Bluetooth setup.

Firmware updates, one release file for all three
1. USB from a web page (ESP Web Tools). Also the first install. Rogier hosts it.
2. From the device menu: check a release address and install. Needs internet.
3. Upload on the settings page. Works without internet.
Two firmware slots with rollback, checksum check, no update while a clock
runs, refuse on low battery unless on USB.

Power
- "Power off" is deep sleep; hold the crown about a second to wake.
- Automatic sleep after a period with no clock running and no press.
- Radio power saving off while in use.

## 9. Firmware guidance

- One firmware. A common base (Wi-Fi, CRG client, display, touch, setup,
  power) with a thin layer per role: what it registers for, its screens,
  its button map.
- Keep logic free of Arduino dependencies so it runs in host tests, as
  `jamcore` does: time comes in as an argument.
- RAM is the tight resource (512 KB): draw through a partial buffer, not a
  full 240x240 frame.
- Planned stack: PlatformIO, Arduino framework (pioarduino platform for the
  C6), ArduinoJson, a WebSocket client that can read response headers and
  set a Cookie header, LVGL. None of these choices has been compiled.
- The session cookie `CRG_SCOREBOARD` must be stored and resent, or the
  device becomes a new client on every reconnect.
- Send `source` and `platform` on the WebSocket URL, including the role.

## 10. Testing approach

1. Logic on a PC: host tests, and the CRG client against a CRG server
   running locally.
2. Screens in an LVGL desktop window (mouse as touch).
3. Optional: Wokwi for the chip. Its support for this display, touch
   controller and deep sleep is unconfirmed.
4. Only on hardware: battery life, Wi-Fi in a hall, button feel, latency.

## 11. Unverified - do not assume

- Updates on the board (2026-11-13, compiles only): HTTPS with the
  certificate bundle, `esp_ota` with sequential writes, the rollback
  (`verifyRollbackLater` keeps a new firmware pending until it has run
  30 s or goes to sleep; a reset before that should boot the old slot).
  "On USB" is `usb_serial_jtag_is_connected()`: a computer, not a plain
  charger, since the board has no charge pin. A merged image flashed over
  USB at 0 may leave otadata blank: the first OTA then writes app1.

- protocol.md is from source and a capture at commit `3ba7f744` (dev head,
  2026-11-09). Pin to the version the league runs.
- Box timer protocol: read and captured 2026-11-10 (protocol.md section
  12), including undo of box clocks, changing the skater and the tick
  rate. CRG loses a "+ Penalty" when the skater is changed afterwards.
- Board GPIOs: now from Waveshare's repository (pins.h), not yet tried on a
  board. Display rotation (`board::kRotation`) and the touch turn are set
  by reasoning, check on the first run.
- `platformio.ini`: pioarduino 55.03.312-1 with the C6 DevKitC-1 board
  file and 16 MB flash compiles; whether that board file matches the
  Waveshare board (USB CDC, flash mode) is unchecked.
- RAM at runtime: estimated about 200 KB of 320 KB (52 KB static, LVGL heap
  under 50 KB measured on the PC, Wi-Fi 60 to 80 KB). Measure on the board.
- Battery runtime (8 to 10 h is an estimate), standby drain in deep sleep.
- Board depth, USB height, antenna position (from photos).
- Wi-Fi in the hand: to be measured with the bare board in each hand.

## 12. Next tasks, in order

Before the board arrives (no hardware needed)
1. Done 2026-11-13: device side of "Update" (see section 2). On the
   board still to try: HTTPS against clock.quagga.rocks, the rollback.
2. GitHub repository for `clock/` and the dc-k3s manifests for
   clock.quagga.rocks: only when Rogier says so (ask before anything on
   GitHub).
3. Done 2026-11-13, not overnight (Rogier): tools/game-test, game4.py
   (a game with JT, two PBTs and a PBM) and drops.py (cut, unreachable,
   silent drop, flapping, run locally, CRG restart).

When the board arrives
4. Bench: signal strength with the bare board in each hand, and with a
   switch held where the 2 o'clock switch sits (antenna decision).
5. First run: display, rotation, touch, buttons, backlight, battery
   reading; measure RAM at runtime; the 1 s wake hold.
6. Measure board depth, USB height, antenna; adjust and print case v1.

Ask Rogier before changing anything in sections 4 to 8; those were agreed
with him item by item.
