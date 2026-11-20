# Decisions since the original handover

Running log, in the order things were decided. Later sections supersede
earlier ones. The consolidated current state is in ../HANDOVER.md.

Agreed with Rogier on 2026-11-09/10. Where this differs from docs/handover-original.md,
this file wins.

## Hardware
- Base board: Waveshare ESP32-C6-Touch-LCD-1.28, bare (not the -B housing).
  Comes with a 100 mm SH1.0 12-pin cable ending in Dupont sockets.
- No main PCB. Switches hand-wired to the header cable for the first unit.
- Battery: 803040 LiPo, 1000 mAh, about 8 x 30 x 40 mm, protection circuit,
  MX1.25 2-pin plug. Check polarity. Target: at least 6 h, charged over USB-C
  by the board's own charger.
- Three buttons: 6x6x4.3 mm through-hole tactile switches. Vibration motor:
  10 mm coin type with transistor + diode.
- Header pins: IO1 crown, IO2 timeout, IO3 undo, IO6 motor. See firmware/src/pins.h.

## Case
- 58 mm round, 20.5 mm deep. Crown at 12, timeout at 2, undo at 10.
- Lanyard lug at 6 o'clock on the back shell, behind the USB-C opening.
- v0 is a fit test: no switch pockets, cable clamp or motor pocket yet.
  Board depth and USB height are estimates until measured.

## Buttons
| State | Crown | Timeout (2) | Undo (10) |
|---|---|---|---|
| Lineup / idle | Start jam | Start timeout | Undo |
| Jam | Stop jam | Start timeout | Undo |
| Timeout | End timeout | New timeout | Undo |
| Period over (standalone) | Next period | - | - |
| Connection lost | not sent | not sent | Hold: run locally |

- Undo needs a 1 s hold. About 3 s hold opens the menu. No menu during a jam.
- Side buttons can be swapped in settings (left-handed use).

## Display
Design canvas: "Jam timer display screens" (Claude artifact).
- Big central clock for the active clock; jam and lineup matter most.
- Coloured ring as progress: green jam, amber lineup, purple timeout.
- Bottom line shows what the crown does next (from CRG Label values).
- Top: green dot + CRG when connected, blue STANDALONE badge otherwise.
- Period clock stays a small line under the state name.
- Timeout type picker by touch: TTO and OR per team in four quadrants, OTO
  in the centre disc with the timeout clock.
- Connection lost: broken red ring, last known clock greyed, "buttons are not sent".
- Not drawn yet: battery indicator, not-authorized state, "sent, waiting"
  state, menu, setup QR screen.

## Menu (on device, touch)
Mode (connected / standalone, take over locally), standalone game reset /
next period, brightness, vibration on/off, swap side buttons, info
(battery, Wi-Fi, scoreboard address, write permission, firmware), setup,
sleep / power off.

## Configuration
- First flash: USB web installer (ESP Web Tools).
- All configuration: the device starts its own access point, shows a QR code
  and password on the display, and serves a settings page. Starts from the
  menu, and automatically when no Wi-Fi is configured.
- Settings page fields: Wi-Fi name and password, scoreboard IP or hostname.
  Standalone durations can go on the same page.
- The access point shuts down once the device has joined the Wi-Fi.
- This replaces the handover's "no captive portal". Bluetooth setup: left out.

## Testing
- Logic on a PC (state machine done), scoreboard client against a local CRG
  server, screens in an LVGL desktop window. Wokwi optional. Not started
  beyond the state machine.

## Penalty box timer role (idea stage, after the jam timer)
Same hardware and firmware, role chosen in the menu: Team 1 blockers,
Team 2 blockers, or jammers. CRG counterpart: `/nso/pbt/`.
Read from the screen's HTML only; the timing logic is not read yet.

- CRG keys seen: `StartBoxTrip`, `StartJammerBoxTrip` (game commands),
  `BoxTrip(*).Clock.Time`, `BoxTrip(*).CurrentFielding`, `BoxTrip(*).Delete`,
  `Team(*).Position(*).PenaltyBox`, `.HasUnserved`, `.RosterNumber`,
  `.StartBoxClock`, `.UnendBoxTrip`. Needs penalty box timing enabled in CRG.
- Crown: skater sat down (starts a trip). Touch: tap a clock for Release,
  Change skater, Cancel trip. Vibration at 10 s (stand) and 0 (done).
- Display: one clock full screen; two as top/bottom halves; three as bands.
- After the crown press a picker shows the positions with roster numbers.
  If exactly one skater on the team has an unserved penalty, that skater is
  selected immediately and the picker is skipped; a tap changes it.
- Release is a deliberate tap, never automatic at zero.
- Colours follow CRG's stylesheet: unserved #0ee, serving #e33 with white
  text, stand yellow, done #6fdf6f.

### Jammer role (box manager timing both jammers)
- Screen split left/right, one half per team: team chip, jammer number, clock.
- Left side button = left team's jammer sat down, right side button = right
  team's. No picker: the jammer comes from the lineup (star pass included).
  Crown, held = undo.
- Second jammer sits: first half shows DONE (swap), second shows the shortened
  time. Assumes CRG computes it (`BoxTrip.Shortened`); logic not read yet.
- Sides must be swappable: teams usually swap sides at intermission.
  Menu item "Swap sides", plus a prompt when period 2 begins. In the blocker
  roles the same swap changes which team the device times.

## Firmware structure
- One firmware for all roles (jam timer, box timer blockers, box timer
  jammers). Role set on the setup page and in the menu, stored, shown at
  startup, locked while a clock is running, reported to CRG in the client name.
- Board memory (Waveshare spec): 512 KB RAM, 16 MB flash. Flash is ample for
  all roles plus over-the-air updates. RAM is the resource to watch: draw the
  display through a partial buffer, not a full 240x240 frame (about 115 KB).

## Menu structure (supersedes the flat list above)
Top level, four touch quadrants like the timeout picker:
- Game: Role (+ Team for blockers), Swap sides, Mode (connected /
  standalone), Standalone game (reset, next period) or Clear clocks.
- Device: Brightness, Vibration, Swap side buttons.
- Connection: Setup (access point + QR), Info, Update.
- Power: Sleep / power off.
"Swap sides?" is also prompted at intermission in the box roles.
Penalty length is fixed at 30 s in firmware, not a setting.

## Firmware updates
One release file serves all three routes:
1. USB from a web page (ESP Web Tools, same technique as web.esphome.io),
   hosted by Rogier. Also the first-install route. Could later be bundled
   with the scoreboard.
2. From the device menu: check a release address for a newer version and
   install it. Needs internet on the Wi-Fi it is on.
3. From the web UI: upload the firmware file on the settings page. Works
   without internet.
Rules: two firmware slots with rollback, checksum check before switching,
no update while a clock is running, refuse on low battery unless on USB.

## Wi-Fi networks
- The device remembers several networks (venues change almost every time).
- Each saved network carries its own scoreboard address and port.
- On start it joins a known network in range; Connection > Wi-Fi lists the
  saved ones so one can be chosen by hand. Add / delete on the setup page.

## Power
- "Power off" is deep sleep. Wake by holding the crown about a second
  (buttons are on IO1-3, deep-sleep wake pins); a short bump goes back to sleep.
- Open: standby current of the whole board is unmeasured; a slide switch in
  the battery lead is the fallback for long storage, but the cell then only
  charges with the switch on.

## Box timer: standalone and extra penalties
- Standalone box timing: clocks only, no roster numbers, no noted penalties,
  no jam state. Pause/resume all clocks by hand: 2 o'clock button in the
  blocker role, short crown press in the jammer role. Clear PAUSED state.
- Tap a clock > "+ Penalty (30 s)" for a skater who gets another penalty
  while seated. Standalone: adds 30 s. Connected: must record a penalty on
  the skater (as CRG's PBT "Add" does), not just add clock time, so the PLT
  entering it later does not add the time twice. Send it with CRG's "?" (Unknown) code, which exists in
  all bundled penalty sets; the PLT then corrects the code on that entry.
  Needs a skater assigned to the clock. Not tested against a server yet.
- Idea for later: jam timer device tells box timer devices jam start/stop
  over a direct radio link when there is no scoreboard.

## Wi-Fi signal and antenna position
- Concern: 2.4 GHz only, small chip antenna inside the hand, delay spikes on
  a busy channel. To be measured: signal strength and press-to-confirmation
  time on the Info screen, logged over a scrimmage, device held in the hand.
- No antenna in the lanyard: wrong length, against the body, and the board
  has no antenna socket.
- Case change: board turned a quarter turn so USB-C is at 3 o'clock (seen
  from the front). The chip antenna then sits at about 1 to 1:30, between
  the crown and the 2 o'clock button, instead of 4:30 under the palm.
  Antenna position is estimated from Waveshare's photos (45 to 60 degrees
  from the USB tab), not measured. Only quarter turns are possible because
  the display rotates in 90 degree steps. Firmware must rotate the screen
  and touch by 90 degrees. Lanyard lug stays at 6, now clear of the port.
- Keep the 2 o'clock switch and its wiring as far from the antenna as the
  pocket allows.

## Button sides per role
- Rogier holds it in the right hand as jam timer and in the left hand as box
  timer / box manager.
- "Swap side buttons" is a per-role setting on the web interface only. It is
  removed from the device menu (Device now holds brightness and vibration).
- Bench test on arrival: bare board in each hand, read the signal strength.

## Vibration motor: later version
- Not fitted in the first build, but the design stays ready for it:
  IO6 reserved (no fourth button), a pocket for a 10 mm coin motor in the
  case, haptic cues kept in the firmware behind a "motor fitted" setting on
  the web interface (off by default). The Vibration menu item only shows
  when a motor is fitted.
- Until then the five-second cue and the box "stand" / "done" cues are
  visual only.
- First-build shopping list: board, battery, switches, screws, lanyard.

## Button gestures
- Crown and timeout/pause act instantly on press; no double-click meaning,
  because double-click detection would delay the single press.
- Long press: undo (1 s), menu (3 s), wake (1 s).
- Unassigned: double click on undo, long press on timeout, both side buttons.

## 2026-11-10: standalone rules are settings
Rogier agreed the standalone defaults (jam ends at 2:00, timeout stops the
period clock until the next jam, one undo level) and wants each one
configurable. `jamcore::Config`: `jamAutoEnd`, `periodAfterTimeout`
(NextJam / TimeoutEnd / KeepsRunning), `undoLevels` (0 to `kMaxUndo` = 8).
They go on the settings page with the standalone durations.

## 2026-11-10: capture against a local CRG (task 1)
Done on dev 3ba7f744 with a raw client and the eJT/SBO screens in headless
Chrome. protocol.md corrected: the 1 s guard ignores Start right after Stop
and Stop right after Start (only Timeout replaces), ClockReplace freezes the
server clocks until the next button, the Timeout clock runs on until the
next jam, TimeoutOwner holds `<game id>_n`, and the crown must send StopJam
for `Overtime Lineup`. Probe scripts: tools/crg-capture/

## 2026-11-10: connected-mode crown guards
Rogier agreed: crown blocked 1 s after a press with a visible cue; crown
held 1 s during an intermission and after the last period, the screen
naming the effect (extend period N / start period N+1).

## 2026-11-10: box timing read and captured (task 2)
protocol.md section 12, probe tools/crg-capture/box.py, on dev 3ba7f744.
Fits the agreed design: release is a tap (CRG never releases at zero; the
clock goes negative), "+ Penalty" is one Set of `Penalty(n+1).Code` "?",
the jammer swap is done by CRG, the first penalty of a skater without a
noted one is a "?" added by CRG itself. Jammer role sends
`Position(Jammer|Pivot).StartBoxClock` for the button's team.
Decided by Rogier the same day:
- Connected, box clocks follow the scoreboard (they run only during jams,
  the device shows the waiting clock between jams). Standalone, a clock
  starts on the crown press and runs until the official stops it with the
  pause button; no "between jams" toggle.
- Cancel trip works as the scoreboard does: it sends only `Delete` and
  leaves CRG's automatic "?" penalty on the skater.

## 2026-11-10: CRG client core (task 3)
`firmware/lib/crgclient`: no Arduino, no JSON library (a small parser for
CRG's flat state frames), no sockets (the transport feeds open / frame /
close / tick and sends `outgoing()`). Choices made while building it:
- Typed mirror (clocks, labels, teams, positions, box trips) instead of a
  key/value map, to bound RAM. Finished box trips are dropped first when
  48 are held.
- A press is Confirmed by the first state change on keys that command
  touches (e.g. `InJam`/`Label(*)` for the crown, `Clock(Timeout).Number`
  for New Timeout, the trip's `PenaltyCodes` for + Penalty), NoChange
  after 1.5 s, Refused on the authorization frame. A change made at the
  same moment by another client can confirm a press by mistake; accepted.
- Presses are NotSent while disconnected, before the first state frame, or
  while MayWrite is false. Presses in flight at a disconnect become
  NoChange and are never resent.
- Ping every 30 s; nothing received for 45 s = dead, close and reconnect.
  Reconnect after 1 s, doubling to 8 s. The mirror is cleared on every
  reconnect and rebuilt from the Register reply.
- Clock display: last value, moved on locally while running, never more
  than 999 ms past it.
Found by the live test: Stop within 1 s of the first jam's start is a
correction, not ignored (protocol.md section 7).

## 2026-11-10: press feedback and simulator
Rogier chose: while a press waits for the scoreboard, a thin pulsing ring
and "sent..."; on no change after 1.5 s the ring flashes red twice and
"NOT TAKEN - press again" shows for 2 s (display.md). Screens are built in
an interactive LVGL + SDL2 window on the laptop (Rogier installs
libsdl2-dev).

## 2026-11-10: jam timer screens (task 4)
Built: `lib/jtctl` (behaviour, host-tested), `lib/jtui` (LVGL 9.2.2),
`sim/` (SDL window, `--shots`). Choices made while building, for Rogier to
check in the simulator:
- Press feedback placement: "sent..." replaces the crown hint while
  waiting; "NOT TAKEN" replaces the state word (red) with "press again" as
  the hint; a thin inner ring pulses while waiting and flashes red with the
  outer ring. A device without write access shows "NOT ALLOWED".
- Crown block cue: the crown hint is dimmed for the 1 s the crown is blocked.
- Holds (undo 1 s, crown 1 s in an intermission) fill the thin inner ring.
- Intermission: blue (#6fb6ff, the STANDALONE pill colour), intermission
  clock, hint "Hold: Extend period N" / "Hold: Start period N+1". After the
  last period: "GAME OVER", "Hold: Extend period N".
- Before the first jam: "READY", grey, jam clock 2:00.
- Timeout state word names the type once CRG has it: TEAM 1 TO, TEAM 2 TO,
  OFFICIAL TO, REVIEW.
- Picker: opens on the timeout press, closes on a choice, a crown press, a
  timeout that did not happen, or after 15 s. Connected mode only.
- Take over locally (hold undo while disconnected) carries the last known
  jam / lineup / timeout and period clock into jamcore.
- Standalone: the timeout button during a timeout starts a new timeout
  (jamcore ignored it before; HANDOVER section 6 asks for it).
- Fonts: bitmap fonts generated with lv_font_conv 1.5.2 (Rogier allowed
  it), firmware/lib/jtui/fonts, 4 bpp. Clock fonts hold only digits, so the
  clocks are placed by their centre. The simulator uses them by default;
  `--ttf` renders the TTFs instead.

Rogier checked the screen sheet (2026-11-10): NOT TAKEN, the dimmed hint,
the hold ring, intermission, READY and the timeout type are fine.

## 2026-11-10: READY, short undo starts the lineup
Rogier: in READY (before the first jam) the crown starts the jam and a
short press of undo starts the lineup. Undo itself stays a 1 s hold, so the
lineup starts on release. Connected it sends `StopJam` (CRG's
`Label(Stop)` is "Lineup" then), standalone `JamTimer::startLineup`. Hint:
"Crown: Jam · Undo: Lineup".

## 2026-11-10: box timer roles (task 5)
Built: `lib/boxcore` (standalone clocks), `lib/boxctl` (both roles,
standalone and connected), `lib/boxui`, simulator `--role`. Choices made
while building, for Rogier to check in the simulator:
- Buttons. Blockers: crown = skater sat, 2 o'clock = pause/resume
  (standalone), 10 o'clock held 1 s = undo. Jammers: 10 o'clock = left
  jammer sat, 2 o'clock = right, crown short = pause/resume (standalone),
  crown held = undo. (The per-role side setting is not built yet.)
- Connected undo undoes this device's last action: sat -> cancel that
  trip, release -> unend, + penalty -> remove that penalty. A cancel cannot
  be undone (CRG deletes the trip).
- "Decide later" trips stay unassigned and show as "?" only on the device
  that started them (CRG keeps them at game level, not per team).
- After a star pass the picker offers the Jammer position (the old jammer,
  now a blocker) instead of the Pivot.
- Extra states: WAITING FOR JAM (dark red, connected between jams, CRG
  holds box clocks), PAUSED (grey). Press feedback ("sent...", NOT TAKEN)
  shows in the status line.
- Up to three skaters are drawn (bands); standalone holds at most four
  clocks.
- The tap-a-clock menu closes on a tap outside it or after 10 s. In
  standalone it shows the clock only (no roster number) and Change is off.
- Take over locally (connection lost, hold undo) carries the clocks on
  with their time left.
- Standalone jammer swap follows CRG exactly, including that a later
  penalty does not shorten again once the other jammer's penalty was cut.

## 2026-11-10: menu, undo/menu hold, "Swap sides?"
Built `lib/devmenu` + `lib/devmenuui`, wired into the simulator (role
switching recreates the role and its CRG client). Choices made, for Rogier
to check:
- One gesture for undo and menu (section 6 "undo 1 s, menu 3 s"): undo
  now happens on the release after 1 s (before, it fired at 1 s while
  held); held to 3 s it opens the menu and undoes nothing. The hold ring
  fills white towards undo, then blue towards the menu; the hint says
  "Release: undo, hold: menu".
- When the menu may open: jam timer only in READY / period over /
  game over, or while disconnected (connected: no Jam, Lineup, Timeout or
  Intermission clock running, so not during an intermission). Box roles:
  no box clock running (paused or empty is fine). Otherwise the hint shows
  "No menu while a clock runs".
- In the menu, a button press only closes it (never acts on the game).
  The menu closes after 30 s without a touch.
- Game page: Role, Swap sides (box roles), Mode, Reset game (jam timer,
  standalone) or Clear clocks (box roles, standalone); both ask Yes/No.
  Device: Brightness cycles 25/50/75/100 %. Connection: Wi-Fi (saved
  networks), Setup, Info, Update. Power: Sleep, asks Yes/No.
- Role change and swap sides are refused while a clock runs or someone is
  in the box, with the reason on screen.
- "Swap sides?" (box roles, connected): asked once per intermission,
  Yes/No, only when a swap is allowed (so after the box is empty). It
  closes by itself after 20 s. Standalone has no jam state: menu only.

## 2026-11-10: 2 o'clock = release (blocker role, connected)
Rogier: the 2 o'clock button, unused in the connected blocker role, now
releases a skater: the one whose time ran out first (CRG's box clock runs
below zero, so the lowest time). Nobody done: nothing is sent and a notice
says "Nobody's time is up / Tap a clock to release early". Undo of that
release works as for a release from the menu. Standalone keeps 2 o'clock =
pause/resume. Box notices (also "No menu while a clock runs") are a pill
over the middle of the screen, since the two- and three-skater layouts have
no hint line.

## 2026-11-10: firmware compiles for the ESP32-C6
`pio run -e c6` builds: pioarduino 55.03.312-1 (needs PlatformIO Core
6.2.0+), board file esp32-c6-devkitc-1 with 16 MB flash and the
default_16MB partitions (two app slots), LVGL 9.2.2, gnu++17. Static RAM
51.6 KB of 320 KB (incl. two 24-line display buffers, 23 KB), flash 852 KB
of a 6.25 MB slot.
- The simulator's device glue moved to `firmware/lib/device` behind a
  Platform interface, so the board and the simulator share it.
  `wsposix` moved to `firmware/lib` and uses select(): lwIP has no poll.h.
- The firmware builds without RTTI: no dynamic_cast (menu actions are
  virtual methods on the role).
- LVGL heap measured on the PC with LVGL's own pool: 34 to 48 KB with all
  screens alive at once (64-bit, so an upper bound for the chip).
  Estimate for the board: about 200 KB of 320 KB in use. To be measured.
- Not done on the board side: GC9A01 driver and rotation, touch,
  backlight, battery, setup page (Wi-Fi from build flags for now), sleep
  wake with a 1 s hold. WsConn::open blocks up to 1.5 s per attempt.

## 2026-11-10: settings and setup page
Built `lib/settings`, `lib/httpmini`, the setup screen (LVGL QR code) and
the wiring in `lib/device`; end to end in the simulator, compile-only on
the board. Choices made, for Rogier to check (page: docs/setup-page.png):
- Stored as "key=value" lines (one NVS string on the board, a file in the
  simulator). Unknown keys are ignored, bad values keep the default.
- Up to four saved networks, tried in order; each has its own scoreboard
  address and port (default 8000). The setup page never sends a saved
  password back: leave it empty to keep it. "Forget this network" per row.
- Per-role "swap the 10 and 2 o'clock buttons" lives on the page only,
  as agreed (not in the device menu).
- Standalone durations are entered as m:ss and checked: jam 0:10-10:00,
  lineup 0:05-5:00, timeout 0:10-10:00, period 1:00-120:00, periods 1-9,
  undo levels 0-8. Invalid input: the page lists what is wrong and saves
  nothing.
- Saved settings apply at once when no clock runs and the box is empty;
  otherwise the page says some changes apply later, and the device applies
  them as soon as it is idle.
- Setup screen: QR code (Wi-Fi join for the access point), the network
  name and password, the address, "Any button: close". Closing the screen
  stops the page; the access point stays open until the device has joined
  a network (HANDOVER section 8). Setup opens by itself when no network is
  saved.
- Access point name `CLOCK-XXXX` (last 4 hex digits of the chip id),
  password 8 random digits, new each time setup opens. Page on port 80.
- No HTTPS or login on the page: it is only on the device's own access
  point while setup is open.

## 2026-11-10: Wi-Fi picker, eJT warning, battery pill
- Menu > Connection > Wi-Fi lists the saved networks; picking one joins it
  (refused while a clock runs or someone is in the box) and the device
  follows that network's scoreboard. The board also follows by itself when
  it falls back to another saved network (checked every 2 s, when idle).
- eJT settings: the jam timer registers CRG's five global eJT settings and
  warns in the status line when they differ ("CRG · eJT settings?", amber);
  Info lists which. The device never changes them (protocol.md section 8).
- Battery: one pill for every screen on LVGL's top layer, bottom edge
  inside the ring; red at 15 % or less. Hidden until the board reads the
  battery (TODO with the board's battery-sense pin).
- "Not authorized": kept as the existing NOT ALLOWED feedback (jam timer
  state word, box status line), shown before the first press because the
  client registers its own MayWrite.

## 2026-11-10: case v1
Added to the fit-test case (case/README.md): switch holders as
full-height columns on the split plane (each shell holds half; printable
without supports), 0.1 mm plunger preload on the cap flanges, leg slots in
the back stop; a 1027 coin motor pad at 9 o'clock on the back shell (0.5 mm
recess in the back face, the motor is taller than the back shell inside);
a cable-tie anchor at 6 o'clock on the front shell; two board hold-down pads
at the board edge beside the battery (with 1 mm foam). The battery moved
1.2 mm towards 6 o'clock to clear the crown switch.
v0's comment had the side buttons' angles swapped (150 is 2 o'clock seen
from the front); the geometry was right. The 2 o'clock switch is about
5.8 mm from the estimated antenna position and cannot move further away
without hitting the USB opening: check Wi-Fi with the switches fitted.

## 2026-11-10: board pins and drivers from Waveshare's repository
Read github.com/waveshareteam/ESP32-C6-Touch-LCD-1.28 (commit d73e23c,
Apache-2.0): HARDWARE_REFERENCE.md, the ESP-IDF BSP header, the battery
code, the schematic text. All pins in firmware/src/pins.h. Board drivers
written in firmware/src/board.cpp (compile-only): display through
Arduino_GFX (as Waveshare's Arduino example; 1.6.9, because their 1.6.4
does not build with Arduino core 3.3.12), CST816S touch read directly,
backlight PWM capped at 84 % as Waveshare's BSP, battery from the ADC with
a LiPo voltage table. Findings:
- IO6 (reserved for the motor) is also the PCF85063 RTC interrupt line.
- The board has no charge-status GPIO (only the CHG LED), so the battery
  pill never shows "charging" on this board.
- The chip antenna is the "C3" ceramic part on the board edge: with USB at
  3 o'clock it is at about 1:36, about 2.8 mm from the 2 o'clock switch
  in case v1 (was estimated 5.8 mm). Question for Rogier, see below.
- Display rotation and touch turn (`board::kRotation` = 1) are reasoned,
  not tried: if the picture is upside down, set 3.

## 2026-11-10: connect without freezing, automatic sleep
- WsConn connects step by step (startOpen / pollOpen from the main loop,
  3 s deadline); the screen keeps running while it connects. Only a host
  name lookup still blocks (an IP address does not); lwIP has no
  non-blocking lookup. Tested with a fake server (slow, refused, silent).
- Automatic sleep: setting "Sleep after" in minutes (default 10, 0 =
  never, on the setup page). Counts only while the role is idle (no clock
  running, nobody in the box, so never during a connected game: the lineup
  clock runs between jams), never with the menu or setup open. A notice
  with a countdown shows for the last 10 s; any press or touch cancels it
  and does nothing else. On the board: display off, deep sleep, crown
  wakes (the 1 s wake hold is still a TODO).

## 2026-11-10: install/update site as a container (Rogier: run it in dc-k3s)
`Dockerfile` + `web-installer/`: firmware built with PlatformIO in the
build stage (version from the image tag, shown in Info), served by
nginx-unprivileged 1.27.4 on 8080 with an ESP Web Tools page,
manifest.json and latest.json (version, size, SHA-256). Built and checked
locally (79 MB). Workflow copied from RTFM (GHCR, tag vX.Y.Z). Waiting on:
a GitHub repository for this project, and the dc-k3s manifests (no local
clone of dc-k3s here). The device side of "Update" (download, check the
SHA-256, two app slots with rollback) is not built yet.
Rogier on "Change" after "+ Penalty": keep CRG's behaviour, nothing extra.

## 2026-11-10: name
Rogier chose the name CLOCK: CRG-Linked Officials' Clock Kit (over PIVOT,
START, AHOJ, BRAUSE). Setup Wi-Fi `CLOCK-XXXX`, page titles, install site.
Folder renamed from scoreboard-stopwatch to clock (Rogier, 2026-11-10).
The install/update site will be at clock.quagga.rocks (dc-k3s); the GitHub
repository and the manifests wait for now.

## 2026-11-13: game tests with four CLOCKs
`sim --ctl`: the simulator without a window, scripted on stdin (buttons,
taps, visible label text, screenshots). tools/game-test runs four of them
(JT, PBT team 1, PBT team 2, PBM) on a throwaway CRG (dev 95286c14):
game4.py plays a game (63 checks), drops.py breaks connections through a
proxy per device (42 checks). Rogier: no overnight run needed. No firmware
bug found. Seen: a silent drop (no FIN/RST) shows NO CONN only after
`deadMs` (45 s), the screen looks live until then; after "run locally" a
device stays standalone when the scoreboard comes back (by design, the way
back is the menu). CRG restart: devices back within about 1 s as the same
clients, the game survives (autosave).

## 2026-11-13: quicker keep-alive (Rogier)
Was: ping every 30 s, dead after 45 s without data, so a silent drop (out
of Wi-Fi range) looked live for 45 s. Now `pingMs` 5 s (only after 5 s with
nothing received; while clocks run CRG sends updates anyway) and `deadMs`
12 s (two Pongs missed). Changes the 2026-11-10 line "Ping every 30 s;
nothing received for 45 s = dead".

## 2026-11-13: back to the scoreboard after "run locally" (Rogier)
Rogier: offer it, but never in the way of active play ("you don't want to
check the CLOCK to see if you can release someone and then have a popup").
Chosen from three options (3 s hold / tap the top line / menu only): while
running locally and the scoreboard answers again, the status reads "CRG
back" (JT pill "CRG BACK"; "scoreboard back" was clipped by the round
screen), nothing else changes. The 3 s menu hold, which is refused while a
clock runs, then opens "Rejoin scoreboard?" Yes / No instead of the menu.
Yes drops the local clocks; nothing local is sent (skaters seated locally
are seated again on the scoreboard by hand if needed). Found while testing:
the menu's Mode switch was not reachable during local play, since the
lineup clock always runs between jams.
Host tests in test_jtctl and test_boxctl; drops.py step 7b.

PlatformIO in ~/.platformio/penv had become 6.2.0.post1, which refuses
pioarduino 55.03.312-1 (needs >= 6.2.0) and uninstalls the platform. Pinned
back to 6.2.0 (as the Dockerfile).

## 2026-11-13: firmware updates on the device
`lib/update` (no Arduino): release file (latest.json as make_release.py
writes it), version compare (x.y.z; a dev build takes any release),
SHA-256, a Writer that checks size, image header (0xE9, chip id 13) and
SHA-256 before the slot is made the boot slot, the rules, and the flow
check -> offer -> download -> installed -> restart (a budget of 32 KB per
loop, so the screen keeps running; 20 s without data = failed).
Choices made while building, not yet confirmed with Rogier:
- Battery limit 30 %; "on USB" on the board = a computer on the USB port
  (usb_serial_jtag_is_connected), as the board cannot see a charger.
- Release address is a setting (default
  https://clock.quagga.rocks/latest.json, empty = no menu update).
- The setup page uploads the raw file with XMLHttpRequest (no multipart);
  httpmini streams that one path, everything else keeps the 16 KB limit.
- Rollback: the Arduino core's `verifyRollbackLater()` returns true, the
  firmware marks itself valid after 30 s of its main loop, or before deep
  sleep (a reset while pending would roll back).
- Only an offered newer version installs from the menu; the setup page
  installs whatever firmware.bin it gets (a way back to an older one).
Simulator: the slot is ~/.cache/crg-sim-update.bin, restart exits;
CLOCK_SIM_VERSION, CLOCK_SIM_BATTERY, CLOCK_SIM_USB stand in for the
board. tools/game-test/update.py: 27 checks.

## 2026-11-13: update choices confirmed, repository
Rogier agreed to the four update choices above (30 % battery limit; USB =
a computer; release address as a setting; menu installs only newer,
setup page installs any firmware.bin). Repository
github.com/blauwblaatje/clocks (public, GPL-3.0 as the CRG scoreboard),
case/out models included, crg-jam-timer.zip left out. v0.1.0 tagged:
ghcr.io/blauwblaatje/clocks:0.1.0.

## 2026-11-18: second board, M5Stack StopWatch
Rogier: also make it work on the M5Stack StopWatch (ESP32-S3, 466 x 466
AMOLED, two buttons and a power button, battery and motor built in). The
firmware knows its board and checks for that board's firmware online.
Decided by Rogier:
- Screens are scaled, not the picture: every size through `ui::px`
  (466/240), fonts generated at the scaled sizes (lib/jtfonts466).
- Which button is the crown: decided with the device in hand. For now the
  power button (yellow = 10 o'clock, blue = 2 o'clock), one constant in
  board_m5_stopwatch.cpp.
Built (choices made while building, for Rogier to check):
- One firmware per board, one shared code base: `src/board.h` with
  board_waveshare_c6.cpp and board_m5_stopwatch.cpp, PlatformIO envs `c6`
  and `stopwatch`. The simulator: `--board stopwatch`.
- Board identity `upd::Board` (id, name, chip). Every image carries
  "CLOCK-BOARD=<id>;"; an update or upload must have the device's own chip
  in the header and its own marker, so firmware for the other board is
  refused even on the same chip. The Waveshare also takes images without a
  marker (0.1.0); the StopWatch does not.
- Release address is now the install site (default
  https://clock.quagga.rocks/); the device adds `<board id>/latest.json`.
  An address ending in .json is used as it stands. A saved 0.1.0 default
  becomes the site. The site keeps the Waveshare's latest.json and
  firmware.bin at the top for 0.1.0 devices; manifest.json has a build per
  chip, so one install button serves both boards.
- StopWatch power button as a game button: its chip's click-reset and
  double-click power-off are turned off (as M5Stack's demo does for the
  first). Sleep = power off through the chip; the power button starts it
  again. "On USB" = 5 V on the port (the chip sees it, chargers included);
  charging is shown.
- Not built: the vibration motor (as on the Waveshare, the cues are still
  missing), the CST9217 touch variant some units have.

## 2026-11-20: clock faces, face and buttons per role

Designs on the canvas "CLOCK faces"; Rogier picked all of them to build
(2026-11-20) and asked for both boards at once.
- Faces: Classic, LCD (day, night, night in colour), Big, Big outline,
  Rings, Chronograph, High contrast, Flip, Nixie. One firmware, chosen in
  menu > Device > Face or on the setup page.
- Per role (Rogier: "I only need to switch role, not face"): each role keeps
  its own face and its own buttons. Settings `face.<role>` and
  `buttons.<role>`; 0.2's `face` (all roles) and `swap.<role>` are still
  read.
- Buttons per role: any of the role's three actions on any button (six
  mappings, setup page and menu > Device > Buttons). Undo and the menu
  hold stay one action, so every mapping keeps the menu reachable. Screen
  texts name the button that does it.
- Team colours: an option (setup page). The box timer faces draw CRG's
  `Team(n).Color(scoreboard.bg)` (else `overlay.bg`) as an edge round the
  disc and on the team names. High contrast stays black and white.
- Digits of the new faces are strokes (lines and round arcs), not fonts:
  any size, narrow enough for the round screen, no flash.
- Big, Rings and Chronograph show the button legends only during a hold
  and 3 s after a press. On the Waveshare (buttons at 12, 10 and 2
  o'clock) every face does that, on plates, because the legends would sit
  on the faces' top rows.
