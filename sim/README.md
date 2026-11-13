# Desktop simulator

The device screens in an LVGL window, driven by the same controllers,
standalone logic and CRG client core as the firmware: jam timer
(`jtctl`, `jtui`, `jamcore`) and box timer roles (`boxctl`, `boxui`,
`boxcore`).

Needs `libsdl2-dev`, a C/C++ compiler and make.

    ./fetch-deps.sh          # LVGL v9.2.2 and Montserrat into deps/
    make                     # build/sim (BUILD=dir to build elsewhere)
    build/sim                            # standalone jam timer
    build/sim --role blockers1           # team 1 blockers box timer (blockers2, jammers)
    build/sim --crg localhost:8000       # connected to a CRG scoreboard (with any --role)
    build/sim --setup                    # open the setup page at once (also: menu > Connection > Setup)
    build/sim --shots DIR                # no window: every screen state as DIR/*.ppm
    build/sim --ctl [--crg ..] [--role ..]  # no window, scripted on stdin (tools/game-test)

Keys: Space or C = crown (12 o'clock), U or Left arrow = the 10 o'clock
button, T or Right arrow = the 2 o'clock button. Hold a key as you would
hold the button (undo is a 1 s hold). Mouse = touch. The window is zoomed
x2 and not rotated.

| Role | Crown | 10 o'clock | 2 o'clock |
|---|---|---|---|
| jt | start/stop jam, end timeout | short in READY: lineup; held: undo | timeout |
| blockers1/2 | skater sat down | held: undo | connected: release the first skater whose time is up; standalone: pause/resume |
| jammers | standalone: pause; held: undo | left jammer sat | right jammer sat |

"Held: undo" means: release after 1 s = undo; keep holding to 3 s = the
device menu (only while no clock runs). In the menu, any button closes it.
Role, mode, swap sides, reset game / clear clocks, brightness, info and
sleep work in the simulator; Wi-Fi, setup and update answer "not in the
simulator".

Settings (role, saved networks, button sides, standalone rules ...) are in
`~/.cache/crg-sim-settings.txt`, edited on the setup page: Setup prints
and shows `http://localhost:<port>/` (the first free port from 8090).
The simulator uses the first saved network's scoreboard; `--crg` and
`--role` override the saved settings.

Connected, the simulator stores its CRG session cookie in
`~/.cache/crg-sim-<role>.cookie`, so it stays the same device in CRG's
Settings > Clients. Use a throwaway scoreboard: presses change its game.

Fonts: by default the bitmap fonts the device uses
(`firmware/lib/jtui/fonts`, see `jtfonts.h`). `--ttf` renders the Montserrat
TTFs with LVGL's tiny_ttf instead (no kerning), handy to try a new size
before generating it. Regenerate a bitmap font from sim/, e.g.:

    npx -y lv_font_conv@1.5.2 --no-compress --no-prefilter --bpp 4 --format lvgl \
      --lv-include lvgl.h --font deps/montserrat/Montserrat-Bold.ttf --size 17 \
      -r 0x20-0x7E,0xB7,0x2026 -o ../firmware/lib/jtui/fonts/jt_b17.c

Clock fonts (jt_sb*) use `--symbols 0123456789:.-` instead of `-r`.

The device glue (roles, menu, CRG connection) is `firmware/lib/device`,
shared with the firmware; the simulator only adds SDL, keys and files.
`firmware/lib/wsposix` is the ws:// client both use (upgrade with Cookie,
Set-Cookie read back, masked text frames, ping reply; select(), which lwIP
has).

LVGL heap per screen: build with LVGL's own pool and run the shots,

    make BUILD=build-mem "DEFS=-DLV_CONF_INCLUDE_SIMPLE -DSIM_MEMCHECK -DSIM_FONT_DIR='\"$PWD/deps/montserrat\"'"
    build-mem/sim --shots /tmp/shots | grep MEM

("max" includes the screenshot buffers, which the device never allocates).
