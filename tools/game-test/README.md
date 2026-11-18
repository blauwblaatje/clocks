# Game tests: four CLOCKs on one scoreboard

Four simulated CLOCKs (`sim --ctl`: the real device code, no window, driven
on stdin) play a game on a throwaway Java CRG that the script starts itself:
jam timer, penalty box timer team 1, penalty box timer team 2, penalty box
manager (jammers). The script plays the other officials over its own
WebSocket (lineup tracker, penalty tracker, the operator where needed) and
checks the scoreboard's state after every device action.

Needs: `sim/build/sim` (sim/README.md), a CRG checkout compiled with
crg-format's `tools/java/build.sh CHECKOUT OUT`, Python 3 with
websocket-client, Pillow for the screenshot sheets (else PPM).

    python3 tools/game-test/game4.py sim/build/sim CHECKOUT OUT/classes RESULTS [PORT]   # about 3 min
    python3 tools/game-test/drops.py sim/build/sim CHECKOUT OUT/classes RESULTS [PORT]   # about 3 min
    python3 tools/game-test/update.py sim/build/sim firmware/.pio/build/c6/firmware.bin RESULTS \
        [firmware/.pio/build/stopwatch/firmware.bin]   # about 1 min; the other board's build adds refusal checks

`CLOCK_SIM_BOARD=stopwatch` in front of any of them runs the CLOCKs as M5Stack
StopWatches (466 x 466 screens; update.py then takes the stopwatch build
first and the c6 build as the other board). Touch positions stay in design
pixels (240 x 240); `sim --ctl` scales them.

Run one at a time: two CRGs from one checkout share its autosave folder.
RESULTS gets a screenshot sheet per step (`NN-step.png`), each device's log
and the CRG log. Exit status 1 when a check fails.

| Script | Covers |
|---|---|
| game4.py | connect, jams, 1 s crown guard, JT undo, picker, noted penalty assigned at once, jammer swap, + Penalty (`?` code), 2 o'clock release, release by tap, timeout type by touch, a device restart (same cookie), period end, "Swap sides?", device menu, period 2 start held 1 s, PBM undo |
| update.py | firmware update in the simulator (no CRG): up to date, offer and install, damaged download, cancel, low battery with and without USB, bad release addresses, setup page upload (wrong file, firmware.bin) |
| drops.py | connection cut, scoreboard unreachable with a skater in the box, presses while down (not sent, not queued), operator changes the game while the JT is away, press then drop, silent drop (noticed within 12 s), flapping, run locally (deliberate hold, nothing merged back), "CRG back" and rejoin (3 s hold, No / Yes), a real CRG restart |

`common.py` holds the shared parts: `Server` (the CRG), `SB` (the script's
own connection), `Clock` (one simulated CLOCK), `Proxy` (one per CLOCK in
drops.py: `cut()`, `down()`/`up()`, `mode = "blackhole"`), the touch
positions of the screens.

CRG clears the positions after each jam: set a lineup before every jam, or
box trips have no skater (shown as `#?`).
