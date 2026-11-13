"""Connection trouble with four CLOCKs on one scoreboard (about 4 minutes).

    python3 drops.py SIM CRG_CHECKOUT CRG_CLASSES OUTDIR [PORT]

Starts its own Java CRG (CRG_CHECKOUT holds lib/deps, CRG_CLASSES the
compiled classes, see crg-format/tools/java/build.sh) on PORT (default
4341), so it can restart it. Each CLOCK connects through its own proxy,
which the script uses to cut the connection, refuse reconnects or silently
drop every byte. Rules checked (HANDOVER section 6): the scoreboard stays
the source of truth, no press is sent or queued while the connection is
down, nothing local is merged back on reconnect, running locally is a
deliberate hold, and a device comes back as the same scoreboard client.
"""
import os, sys, time
import common as C
from common import check, note, wait_for, timed, gg, G, pos, in_box, lineup, trip, trip_path, current_trips

SIM, CHECKOUT, CLASSES, OUT = sys.argv[1:5]
PORT = int(sys.argv[5]) if len(sys.argv) > 5 else 4341
os.makedirs(OUT, exist_ok=True)
C.OUT = OUT

server = C.Server(CHECKOUT, CLASSES, PORT, os.path.join(OUT, "crg.log"))
server.start()
GID = C.setup_game(PORT, "dr")
sb = C.sb

roles = [("jt", "JT"), ("blockers1", "PBT team 1"), ("blockers2", "PBT team 2"), ("jammers", "PBM")]
proxies = {r: C.Proxy(PORT) for r, _ in roles}
clocks = [C.Clock(SIM, r, n, proxies[r].port, OUT) for r, n in roles]
jt, pbt1, pbt2, pbm = clocks
px = {c: proxies[c.role] for c in clocks}


def shots(label):
    C.shots(clocks, label)


def one_client_each():
    by = C.clients_by_source()
    return all(len(by.get(s, [])) == 1 for s in ("jamtimer", "boxtimer-blockers1", "boxtimer-blockers2", "boxtimer-jammers"))


def device_of(c):
    src = "jamtimer" if c.role == "jt" else "boxtimer-" + c.role
    ids = C.clients_by_source().get(src, [])
    return ids[0][0] if len(ids) == 1 else None


def screen_time(c):
    """First m:ss on the screen, in seconds."""
    import re
    m = re.search(r"\b(\d+):(\d\d)\b", c.text())
    return int(m.group(1)) * 60 + int(m.group(2)) if m else None


try:
    print("\n== 0. four CLOCKs through their proxies")
    for c in clocks:
        check(f"{c.name} connected", wait_for(lambda: C.online(c), 8), c.text())
    check("one scoreboard client per CLOCK", wait_for(one_client_each, 3), str(C.clients_by_source()))
    devices = {c: device_of(c) for c in clocks}
    lineup(1, "10", "11", "12", "13", "14")
    lineup(2, "10", "11", "12", "13", "14")
    jt.press("crown")
    check("jam 1 running", wait_for(lambda: gg("InJam") is True, 3))

    # ---- 1. cut while idle: back at once, same client ----
    print("\n== 1. PBT team 1: connection cut (box empty, jam running)")
    px[pbt1].cut()
    check("PBT1 shows NO CONN", wait_for(lambda: C.no_conn(pbt1), 2), pbt1.text())
    shots("pbt1-cut")
    t = timed(lambda: C.online(pbt1), 10)
    check("PBT1 reconnects by itself", t is not None)
    note(f"back online after {t} s")
    check("PBT1 is the same scoreboard device (cookie kept)", wait_for(lambda: device_of(pbt1) == devices[pbt1], 3),
          f"{device_of(pbt1)} vs {devices[pbt1]}")
    check("still one client per CLOCK (no stale client)", wait_for(one_client_each, 3), str(C.clients_by_source()))

    # ---- 2. scoreboard unreachable for PBT1 with a skater in the box; presses not sent or queued ----
    print("\n== 2. PBT team 1: #12 in the box, then the scoreboard is unreachable for 12 s")
    pbt1.press("crown")
    pbt1.has("12", timeout=2)
    pbt1.touch(*C.PICK["Blocker2"])
    check("#12 sits (trip on the scoreboard)", wait_for(lambda: trip(1, "12"), 3))
    px[pbt1].down()
    px[pbt1].cut()
    check("PBT1 shows NO CONN", wait_for(lambda: C.no_conn(pbt1), 2), pbt1.text())
    n_before = len(current_trips(1))
    pbt1.press("crown")  # a second skater sits: must not be sent
    time.sleep(0.3)
    pbt1.press("right")  # release: must not be sent
    shots("pbt1-down-presses")
    note("PBT1 while down: " + pbt1.text())
    time.sleep(1.5)
    check("presses while down: no new trip", len(current_trips(1)) == n_before, str(len(current_trips(1))))
    check("presses while down: #12 still in the box", in_box(1, "Blocker2"))
    time.sleep(10)
    px[pbt1].up()
    t = timed(lambda: C.online(pbt1), 12)
    check("PBT1 reconnects once the scoreboard answers", t is not None, pbt1.text())
    note(f"back online {t} s after the scoreboard came back (backoff 1-8 s)")
    time.sleep(2)
    check("after reconnect: no queued press arrived (no new trip, #12 still in)",
          len(current_trips(1)) == n_before and in_box(1, "Blocker2"), str(len(current_trips(1))))
    crg_left = round((trip(1, "12") or {}).get("Clock.Time", 0) / 1000)
    dev_left = screen_time(pbt1)
    check("PBT1 shows #12 with the scoreboard's time (within 1 s)",
          dev_left is not None and abs(dev_left - crg_left) <= 1, f"device {dev_left} s, CRG {crg_left} s: {pbt1.text()}")
    shots("pbt1-back")

    # ---- 3. JT unreachable mid-jam, the operator stops the jam meanwhile ----
    print("\n== 3. JT unreachable mid-jam; the operator stops the jam on the scoreboard")
    jams = gg("Clock(Jam).Number")
    px[jt].down()
    px[jt].cut()
    check("JT shows NO CONN", wait_for(lambda: C.no_conn(jt), 2), jt.text())
    sb.set(G + "StopJam", wait=0.5)  # scoreboard operator
    jt.press("crown")  # would be "Stop jam" on the JT's last picture: not sent
    time.sleep(1.2)
    jt.press("crown")
    shots("jt-down")
    note("JT while down: " + jt.text())
    px[jt].up()
    check("JT reconnects", wait_for(lambda: C.online(jt), 12), jt.text())
    time.sleep(1.5)
    check("JT follows the scoreboard: lineup, jam not restarted by queued presses",
          gg("InJam") is False and gg("Clock(Jam).Number") == jams and ("LINEUP" in jt.text() or "Lineup" in jt.text()),
          f"InJam={gg('InJam')} jams={gg('Clock(Jam).Number')} {jt.text()}")

    # ---- 4. press, then the connection drops before the answer ----
    print("\n== 4. JT presses Start Jam, the connection drops at once")
    lineup(1, "15", "11", "16", "17", "18")  # CRG clears positions after a jam
    sb.set(pos(1, "Blocker2") + "Skater", C.skater(1, "12"), wait=0.1)  # #12 is still in the box
    lineup(2, "10", "11", "12", "13", "14")
    time.sleep(1.2)
    jt.cmd("down crown")
    px[jt].cut()
    jt.cmd("up crown")
    check("JT reconnects", wait_for(lambda: C.online(jt), 12), jt.text())
    time.sleep(1.5)
    crg_jam = gg("InJam")
    note(f"the scoreboard {'got' if crg_jam else 'did not get'} the press")
    check("JT shows what the scoreboard has",
          ("JAM" in jt.text() and "LINEUP" not in jt.text()) if crg_jam else ("LINEUP" in jt.text()), jt.text())
    if not crg_jam:
        jt.press("crown")
        wait_for(lambda: gg("InJam") is True, 3)

    # ---- 5. silent drop (Wi-Fi gone): the dead connection check ----
    print("\n== 5. PBM: every byte dropped, no FIN or RST (as walking out of Wi-Fi range)")
    pbm.press("left")
    check("team 1 jammer sits", wait_for(lambda: in_box(1, "Jammer"), 3), pbm.text())
    px[pbm].mode = "blackhole"
    t = timed(lambda: C.no_conn(pbm), 30, 0.25)
    check("PBM notices the dead connection within 15 s", t is not None and t <= 15, f"{t} s: {pbm.text()}")
    note(f"noticed after {t} s (ping after 5 s quiet, dead after 12 s without data)")
    shots("pbm-dead")
    pbm.press("right")  # not sent
    px[pbm].mode = "pass"
    t = timed(lambda: C.online(pbm), 15)
    check("PBM reconnects", t is not None, pbm.text())
    time.sleep(1.5)
    check("PBM's press while dead was not sent (team 2 jammer not in the box)", not in_box(2, "Jammer"))
    check("PBM shows team 1's jammer (#15) again", "#15" in pbm.text(), pbm.text())

    # ---- 6. flapping ----
    print("\n== 6. PBT team 2: connection cut 6 times in 10 s")
    for i in range(6):
        px[pbt2].cut()
        time.sleep(0.6 + 0.4 * (i % 3))
    t = timed(lambda: C.online(pbt2), 15)
    check("PBT2 settles connected", t is not None, pbt2.text())
    check("PBT2 still the same scoreboard device", wait_for(lambda: device_of(pbt2) == devices[pbt2], 3),
          f"{device_of(pbt2)} vs {devices[pbt2]}")
    check("one client per CLOCK after flapping", wait_for(one_client_each, 5), str(C.clients_by_source()))
    note(f"PBT2 log: {pbt2.logged('connected as')} connects, {pbt2.logged('closed')} closes, "
         f"{pbt2.logged('connect failed')} failed")
    pbt2.press("crown")
    pbt2.has("12", timeout=2)
    pbt2.touch(*C.PICK["Blocker1"])
    check("PBT2 works after flapping (#11 sits)", wait_for(lambda: trip(2, "11"), 3), pbt2.text())

    # ---- 7. run locally ----
    print("\n== 7. scoreboard unreachable for JT and PBT1; both run locally (deliberate hold)")
    px[jt].down(); px[jt].cut()
    px[pbt1].down(); px[pbt1].cut()
    wait_for(lambda: C.no_conn(jt) and C.no_conn(pbt1), 3)
    jt.press("left", hold=0.5)  # short: nothing
    check("JT: a short Undo press does not take over", C.no_conn(jt), jt.text())
    jt.press("left", hold=1.3)
    check("JT runs locally after the 1 s hold", wait_for(lambda: not C.no_conn(jt), 2), jt.text())
    note("JT local: " + jt.text())
    pbt1.press("left", hold=1.3)
    check("PBT1 runs locally after the 1 s hold", wait_for(lambda: not C.no_conn(pbt1), 2), pbt1.text())
    note("PBT1 local: " + pbt1.text())
    crg_left = max(0, round((trip(1, "12") or {}).get("Clock.Time", 0) / 1000))  # negative once the time is up
    check("PBT1 local clock carries on from the scoreboard's time for #12",
          screen_time(pbt1) is not None and abs(screen_time(pbt1) - crg_left) <= 2, f"{pbt1.text()} CRG {crg_left}")
    shots("local")
    jt.press("crown")  # local stop jam
    time.sleep(1)
    check("local presses stay local (scoreboard jam still running)", gg("InJam") is True)
    px[jt].up(); px[pbt1].up()
    for c in (jt, pbt1):
        t = timed(lambda: C.back(c), 12)
        check(f"{c.name}: a quiet 'CRG back' once it answers", t is not None, c.text())
    time.sleep(5)
    note("scoreboard back: JT   " + jt.text())
    note("                 PBT1 " + pbt1.text())
    check("no prompt by itself, the local clocks stay on screen",
          "Rejoin" not in jt.text() and "Rejoin" not in pbt1.text() and C.back(jt) and C.back(pbt1))
    check("nothing local merged into the scoreboard (jam still running, #12 still in)",
          gg("InJam") is True and in_box(1, "Blocker2"))
    shots("local-scoreboard-back")

    print("\n== 7b. rejoin: hold 3 s, then Yes (No keeps running locally)")
    jt.press("left", hold=3.3)
    check("JT: 3 s hold opens 'Rejoin scoreboard?'", jt.has("Rejoin scoreboard?", timeout=2), jt.text())
    shots("rejoin-prompt")
    jt.touch(*C.NO)
    check("JT: No keeps running locally", wait_for(lambda: C.back(jt), 2), jt.text())
    jt.press("left", hold=3.3)
    jt.has("Rejoin scoreboard?", timeout=2)
    jt.touch(*C.YES)
    check("JT: Yes follows the scoreboard again (jam running there)",
          wait_for(lambda: C.online(jt) and "JAM" in jt.text() and "LINEUP" not in jt.text(), 3), jt.text())
    pbt1.press("left", hold=3.3)
    check("PBT1: 3 s hold opens 'Rejoin scoreboard?'", pbt1.has("Rejoin scoreboard?", timeout=2), pbt1.text())
    pbt1.touch(*C.YES)
    check("PBT1: Yes shows the scoreboard's #12 again", wait_for(lambda: C.online(pbt1) and "#12" in pbt1.text(), 3),
          pbt1.text())
    check("rejoining sent nothing (jam running, #12 still in)", gg("InJam") is True and in_box(1, "Blocker2"))
    jt.press("crown")
    check("JT controls the scoreboard again (Stop Jam)", wait_for(lambda: gg("InJam") is False, 3), jt.text())
    shots("rejoined")

    # ---- 8. real scoreboard restart ----
    print("\n== 8. the scoreboard restarts (java stopped, started again)")
    sb_state = {"jams": gg("Clock(Jam).Number"), "period": gg("Clock(Period).Number"), "game": gg("Game")}
    server.stop()
    for c in clocks:
        check(f"{c.name} shows NO CONN while the scoreboard is down", wait_for(lambda: C.no_conn(c), 5), c.text())
    shots("crg-down")
    t0 = time.time()
    server.start()
    note(f"CRG answered again after {round(time.time() - t0, 1)} s")
    sb.connect()
    for c in clocks:
        t = timed(lambda: C.online(c), 15)
        check(f"{c.name} reconnects after the restart", t is not None, c.text())
        note(f"{c.name}: {t} s after CRG answered")
    note(f"after the restart CRG has: game={gg('Game')} jam={gg('Clock(Jam).Number')} InJam={gg('InJam')} "
         f"(before: {sb_state})")
    note("restarted devices known again: " + ", ".join(f"{c.name}={'same' if device_of(c) == devices[c] else 'new'}"
                                                       for c in clocks))
    texts = {c: c.text() for c in clocks}
    for c in clocks:
        note(f"{c.name:11} {texts[c]}")
    shots("crg-restarted")
finally:
    for c in clocks:
        c.stop()
    server.stop()

sys.exit(C.summary())
