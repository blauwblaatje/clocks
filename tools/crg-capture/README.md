# CRG capture scripts

Probes used on 2026-11-10 to check docs/protocol.md against a running CRG
(Java scoreboard, dev 3ba7f744). They expect a server on localhost:4330 and
change its game state: use a throwaway server, never one in use.
Python 3 with websocket-client; the Chrome ones need google-chrome.

| Script | What it checks |
|---|---|
| raw.py COOKIEFILE | upgrade Set-Cookie, first frame, Ping, Register reply, reconnect with/without cookie; writes the cookie for the others |
| guard.py | 1 s button guard and corrections (reads cookie.txt) |
| auth.py | refused writes (turns AllLocalDevicesWrite off and back on) |
| idle.py | server idle timeout on a silent connection |
| ends.py, ends2.py | labels at period end, intermission, tied last period, overtime |
| cdp.py | headless Chrome DevTools helper (WebSocket frame log) |
| jt_probe.py, jt_seq.py SCRIPTDIR OUTDIR | eJT `/nso/jt/` registrations and button presses |
| sbo_seq.py SCRIPTDIR OUTDIR | operator screen; edit the game id in the URL |
| box2.py [PORT] | box timing points left open: undo with a box clock running, changing the skater, update rate (protocol.md section 12, "Checked later") |
| box.py [PORT] | penalty box timing: creates a game with rosters, turns on UsePBT, runs blocker and jammer trips (protocol.md section 12). Default port 4331 |

Server from a CRG checkout, with crg-format's build script:
`tools/java/build.sh <checkout> OUT`, then in a copy of the checkout in its
own parent folder: `java -cp OUT/classes:lib/deps/* com.carolinarollergirls.scoreboard.Main --nogui --port=4330`.
