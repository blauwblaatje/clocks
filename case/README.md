# Case v1 (fit test)

Parametric model in `case.py` (CadQuery 2.8). `python3 case.py` writes STL
and STEP files to `out/`; `check.py` checks interference against stand-ins
for the board, battery, switches and motor; `render_top.py` draws the
inside of both shells (`out/inside_top.png`).

Parts: front shell, back shell, 1 crown cap, 2 side caps (print `cap_side` twice).
Size: 58 mm diameter, 20.5 mm deep. USB-C at 3 o'clock, lanyard lug at 6.
Four M2 x 12 self-tapping screws from the back.

Print: PETG. Front shell screen side down, back shell outer face down (already
oriented that way), caps on their flange. The button bores and USB opening sit
on the split line, so no supports should be needed except under the crown
collar halves.

## Inside (v1)

- Switch holders: a sleeve around each 6x6x4.3 mm tactile switch on the
  split plane, half in each shell, as full-height columns (no overhangs).
  The plunger presses the cap's flange onto the wall with 0.1 mm preload,
  so the caps do not rattle. Back stop behind the switch with slots for
  the legs; trim the legs to about 1 mm and solder the wires there.
- Vibration motor (later version): flat pad with a locating rim on the back
  shell at 9 o'clock for a 1027 coin motor with its own adhesive. The back
  face has a 0.5 mm recess there.
- Cable-tie anchor on the front shell at 6 o'clock for the 12-pin cable
  (`CLAMP["ang"]`, move it once the header position is known).
- Board hold-down: two pads on the back shell press on the board edge beside
  the battery (9 o'clock side). Put a 1 mm foam dot on each; `PAD["gap"]`
  sets how far above the assumed board back they stop.
- Battery moved 1.2 mm towards 6 o'clock (`BATT_Y`) to clear the crown switch.

## Assembly

1. Board into the front shell, screen down, USB at 3 o'clock.
2. Caps into their bores from the inside, flange against the wall.
3. Switches into the front halves of the holders, plunger out, legs
   inward; wire them (shared ground) to the header cable; tie the cable
   to the anchor.
4. Battery into the back shell frame, motor (later) on its pad.
5. Close, four M2 x 12 screws from the back.

## Estimated, not measured (parameters at the top of case.py)

- STACK 8.5 mm: board depth, glass to tallest rear part
- USB_Z 8.3 mm: height of the USB-C connector centre
- ANTENNA: the chip antenna ("C3" on Waveshare's drawing) at about 1:36
  on the board edge. The 2 o'clock switch is about 2.8 mm from it
  (`case.py` prints it). Moving the button towards 2:30 is not possible:
  its holder would run into the USB opening.
- FIT 0.3 mm clearance; SW fit 0.2 mm around the switch body
