# CLOCK: CRG-Linked Officials' Clock Kit

Stopwatch-style controller for the CRG roller derby scoreboard: jam timer
and penalty box timer in one device and one firmware. Runs on two boards,
each with its own build of that firmware:

- Waveshare ESP32-C6-Touch-LCD-1.28 (240 x 240) in the CLOCK case (case/);
- M5Stack StopWatch (ESP32-S3, 466 x 466 AMOLED), in its own housing.

The screens are designed at 240 x 240 and scaled on the bigger display;
updates and uploads only take firmware for the board they run on.

Start with HANDOVER.md. Then docs/protocol.md (CRG WebSocket protocol, read
from the scoreboard source) and docs/display.md (the screens).

## Status

| Part | State |
|---|---|
| CRG protocol, jam timer | Read from source, captured from a local server 2026-11-10. |
| CRG protocol, box timer | Read from source, captured 2026-11-10 (protocol.md section 12). |
| CRG client core | `firmware/lib/crgclient`, host tests and a live test against a local CRG pass. No socket code yet. |
| Standalone jam timer logic | Done, host tests pass. |
| Firmware for the boards | Compiles for both (`pio run -e c6` Waveshare, `pio run -e stopwatch` M5Stack StopWatch); never run on either. |
| Jam timer, box timer and menu screens | Built in LVGL, run in the desktop simulator (sim/, `--role`). |
| Setup page, firmware updates | Setup page and Wi-Fi settings built; updates (menu and setup page upload) run end to end in the simulator, compile for the board (tools/game-test/update.py). |
| Case | v0 fit-test model, not printed, no switch pockets. |
| Hardware | Parts ordered, no board yet. |

## Host tests

    cd firmware
    g++ -std=c++17 -Ilib/jamcore test/native/test_jamcore.cpp lib/jamcore/jamcore.cpp -o /tmp/t && /tmp/t
    g++ -std=c++17 -Wall -Ilib/crgclient test/native/test_crgclient.cpp lib/crgclient/*.cpp -o /tmp/tc && /tmp/tc

    g++ -std=c++17 -Wall -Ilib/jamcore -Ilib/crgclient -Ilib/jtctl -Ilib/buttons test/native/test_jtctl.cpp lib/jamcore/jamcore.cpp lib/crgclient/*.cpp lib/jtctl/jtctl.cpp -o /tmp/tt && /tmp/tt

    g++ -std=c++17 -Wall -Ilib/boxcore test/native/test_boxcore.cpp lib/boxcore/boxcore.cpp -o /tmp/tb && /tmp/tb
    g++ -std=c++17 -Wall -Ilib/boxcore -Ilib/crgclient -Ilib/boxctl -Ilib/buttons test/native/test_boxctl.cpp lib/boxcore/boxcore.cpp lib/crgclient/*.cpp lib/boxctl/boxctl.cpp -o /tmp/tx && /tmp/tx
    g++ -std=c++17 -Wall -Ilib/devmenu test/native/test_devmenu.cpp lib/devmenu/devmenu.cpp -o /tmp/tm && /tmp/tm
    g++ -std=c++17 -Wall -Ilib/settings test/native/test_settings.cpp lib/settings/settings.cpp -o /tmp/ts && /tmp/ts
    g++ -std=c++17 -Wall -pthread -Ilib/httpmini test/native/test_httpmini.cpp lib/httpmini/httpmini.cpp -o /tmp/th && /tmp/th
    g++ -std=c++17 -Wall -pthread -Ilib/wsposix test/native/test_wsposix.cpp lib/wsposix/wsposix.cpp -o /tmp/tw && /tmp/tw
    g++ -std=c++17 -Wall -Ilib/update -Ilib/crgclient test/native/test_update.cpp lib/update/*.cpp lib/crgclient/crgjson.cpp -o /tmp/tu && /tmp/tu

Desktop simulator: sim/README.md (`--board stopwatch` for the StopWatch's screens).

Live test of the client core against a throwaway CRG on port 4331
(tools/crg-capture/README.md says how to start one):

    tools/crgclient-live/run.sh 4331

## License

GPL-3.0, as the CRG scoreboard. See LICENSE.
