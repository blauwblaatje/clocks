# Display screens

Target: 240 x 240 round GC9A01, picture and touch rotated 90 degrees
(the board is mounted with USB-C at 3 o'clock). All sizes below are in real
display pixels. Font used in the mockups: Montserrat 500/600/700 (LVGL ships
Montserrat). Clock digits are tabular.

The mockups live in a Claude design canvas ("Jam timer display screens",
private to Rogier). This file is the written version; if they differ, ask.

Common rules
- Black background. Smallest text 12 px.
- Count-down clocks show ceil(ms/1000), count-up clocks floor(ms/1000).
- A screen never shows a state the scoreboard has not confirmed (connected mode).
- Usable width shrinks toward the top and bottom of the circle; keep long
  text near the middle.

## Jam timer role

Layout, top to bottom, centred:
1. Status line, 12 px: 8 px green dot (#3ddc84) + "CRG" when connected, or a
   blue pill (#6fb6ff, dark text) "STANDALONE".
2. Context, 14 px, #d6d6d6: "P1 · Jam 7" or "P1 · Next: Jam 8".
3. Clock, 72 px, weight 600, white: the active clock.
4. State word, 17 px bold, letter-spaced, in the state colour.
5. Period line, 14 px, #d6d6d6: "Period 24:10" (add "stopped" in a timeout).
6. Crown hint, 12 px, #b4b4b4: "Crown: Stop Jam" - from CRG `Label(...)`.

Ring: radius 113 px, 8 px stroke, track #2b2b2b, progress in the state
colour, starting at 12 o'clock, round caps.

| State | Clock shown | Ring | Colour |
|---|---|---|---|
| Jam | jam clock, counting down | fraction remaining | green #3ddc84 |
| Lineup | lineup clock, counting up | fraction of lineup time elapsed | amber #ffb020 |
| Timeout | timeout clock, counting up | fraction of 1:00 | purple #d9a0ff |

Jam and lineup are the screens that matter most. Last 5 s of lineup: flash
the whole ring (no vibration motor in the first version).

Timeout type picker (touch, shown after the timeout button):
- Four quadrants, 3 px gaps. Left column = team 1 (blue #17406e top,
  #0f2c4d bottom), right column = team 2 (brown #7a4310 top, #552e0a bottom).
- Top row: "TEAM n" (12 px) + "TTO" (17 px bold). Bottom row: "TEAM n" + "OR".
- Centre disc, 90 px, light (#e9e9e9, dark text): timeout clock (27 px) and
  "OTO" (17 px bold).
- Touch is only active here and in the menu.

Connection lost:
- Dashed red ring (#ff6a5e). "NO CONN" 27 px red. "Buttons are not sent"
  12 px. Last known clock, 43 px, grey #8f8f8f, with "last known: Jam 7".
  "Hold Undo: run locally" 12 px white.

Back to the scoreboard (all roles, Rogier 2026-11-13): running locally after
"run locally" while the scoreboard answers again, the status line says so
and nothing else changes: the JT pill reads "CRG BACK", the box status line
"T1 blockers · CRG back". No popup during play. Holding the menu button 3 s
(10 o'clock; PBM: crown) opens "Rejoin scoreboard?" 17 px bold, "Local
clocks stop, the scoreboard's show" 12 px, Yes (green) / No buttons 80 x 48
at y 130 (as "Swap sides?"). Any button closes it and does its own job; it
closes by itself after 20 s or when the scoreboard is lost again. Yes drops
the local clocks and follows the scoreboard; nothing local is sent.

Press feedback (connected), decided by Rogier 2026-11-10:
- Sent, waiting for the scoreboard (up to 1.5 s): thin pulsing ring, "sent..."
  under the clock.
- No change within 1.5 s (ignored by the scoreboard's 1 s guard, or slow):
  ring flashes red twice, label "NOT TAKEN - press again" for 2 s, then back
  to normal.

Battery (all screens, decided while building 2026-11-10): small pill at
the bottom edge inside the ring, "82%", grey; red at 15 % or less; "↯"
while charging; hidden while the board has no reading.

eJT settings (jam timer, connected): when CRG's AutoStart, AutoStart5,
Auto5, AutoEndJam or AutoEndTTO differ from the eJT's values, the status
line reads "CRG · eJT settings?" in amber (amber dot); Info names them.

Update screen (all roles, 2026-11-13; menu > Connection > Update, or a
setup page upload): black round screen over everything. Title 17 px bold
at y 46 ("Update", "Up to date", "Update to 0.2.0?", "Updating",
"Receiving update", "Update installed", "Update failed"); one line 14 px
#d6d6d6 at y 76 (host being checked, "Now 0.1.0 · 1.8 MB", "45 %",
"Restarting…"); a 160 x 8 progress bar at y 104 (green fill) while
downloading or receiving; a note 12 px at y 118 (red: refusal or error;
light grey: "Do not power off"); Install (green) / Cancel buttons 80 x 44
at y 158 when an update is offered; hint 12 px grey at y 208 ("Any button:
cancel" / "close"). Any button closes it and stops a check or download;
once installed it restarts after 1.5 s.

Not drawn yet: not-authorized state (shown as NOT ALLOWED feedback for now), menu, setup QR screen, role splash at
startup, "Swap sides?" prompt.

## Box timer, blocker role

State colours follow CRG's stylesheet:

| State | Fill | Text |
|---|---|---|
| Penalty noted, not seated (picker only) | #00eeee | dark |
| Serving | #ee3333 | white |
| Stand (10 s or less) | #ffff00 | black |
| Done | #6fdf6f | black |

- One skater: jam-timer layout. Status "T1 blockers", roster number 29 px,
  clock 72 px, state word, hint "Crown: skater sat down". Ring in the
  state colour.
- Two skaters: top half and bottom half (107 px each), each filled with its
  state colour: roster number 29 px + clock 51 px + state word 12 px.
  27 px strip between them: dot + "T1 blockers · Jam on".
- Three skaters: three bands 75 / 85 / 75 px with 3 px gaps. Middle band as
  in the two-skater layout; top and bottom bands roster number 23 px +
  clock 36 px, state by fill colour only.
- "Who sat down?" picker: title 13 px, 2 x 2 grid of position buttons
  (75 x 49 px, #2f2f33): position name 12 px + roster number 20 px. A skater
  with an unserved penalty is #00eeee with "Penalty" as the label.
  "Decide later" pill underneath.
- Tap a clock: header roster number + clock; buttons Release (green
  #6fdf6f), "+ Penalty (30 s)" (teal #00eeee), then Change and Cancel side
  by side.

## Box timer, jammer role

- Screen split left / right, one half per team, 3 px gap.
- Each half: team chip (team colours, 12 px bold), jammer number 23 px,
  clock 43 px, state word 12 px. Half filled with the state colour; empty
  half is #1c1c1f with "Left button: sat down" / "Right button: sat down".
- Second jammer sits: first half Done ("swap · tap to release"), second half
  Serving with the shortened time.
- No connection indicator drawn on these screens yet.
