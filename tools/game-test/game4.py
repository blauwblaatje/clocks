"""Game test with four CLOCKs on one scoreboard: jam timer, two penalty box
timers (team 1 and team 2 blockers) and a penalty box manager (jammers).

    python3 game4.py SIM CRG_CHECKOUT CRG_CLASSES OUTDIR [PORT]

SIM is sim/build/sim (scripted with --ctl). The script starts its own Java
CRG on PORT (default 4340; CRG_CHECKOUT holds lib/deps, CRG_CLASSES the
compiled classes, see crg-format/tools/java/build.sh). It plays the other
officials itself over its own WebSocket: lineup tracker (positions) and
penalty tracker (noted penalties). It checks the scoreboard's state after
every device action and writes screenshots of all four devices at each step
to OUTDIR (PNG with Pillow, else PPM). Exit status 1 when a check fails.
"""
import os, sys, time
import common as C
from common import (check, wait_for, gg, G, S, skater, pos, trips, trip, penalties, in_box, lineup,
                    PICK, MENU, ROW, TO)

SIM, CHECKOUT, CLASSES, OUT = sys.argv[1:5]
PORT = int(sys.argv[5]) if len(sys.argv) > 5 else 4340
os.makedirs(OUT, exist_ok=True)
C.OUT = OUT
server = C.Server(CHECKOUT, CLASSES, PORT, os.path.join(OUT, "crg.log"))
server.start()
GID = C.setup_game(PORT, "g4")
sb = C.sb

jt = C.Clock(SIM, "jt", "JT", PORT, OUT)
pbt1 = C.Clock(SIM, "blockers1", "PBT team 1", PORT, OUT)
pbt2 = C.Clock(SIM, "blockers2", "PBT team 2", PORT, OUT)
pbm = C.Clock(SIM, "jammers", "PBM", PORT, OUT)
clocks = [jt, pbt1, pbt2, pbm]
online = C.online


def shots(label):
    C.shots(clocks, label)


def texts():
    C.texts(clocks)


try:
    # ---- 1. connect ----
    print("\n== 1. four CLOCKs connect")
    for c in clocks:
        check(f"{c.name} connected", wait_for(lambda: online(c), 8), c.text())
    devs = {}
    for k, v in sb.keys("ScoreBoard.Clients.Device(").items():
        if ".Client(" in k and k.endswith(".Source") and str(v).startswith(("jamtimer", "boxtimer")):
            devs[k] = v
    check("scoreboard lists the four as clients, source names the role",
          sorted(devs.values()) == ["boxtimer-blockers1", "boxtimer-blockers2", "boxtimer-jammers", "jamtimer"],
          str(sorted(devs.values())))
    texts()
    shots("connected")

    # ---- 2. jam 1 ----
    print("\n== 2. lineup, JT starts jam 1")
    lineup(1, "10", "11", "12", "13", "14")
    lineup(2, "10", "11", "12", "13", "14")
    jt.press("crown")
    check("jam 1 running on the scoreboard", wait_for(lambda: gg("InJam") is True))
    check("JT shows the jam", jt.has("Stop", timeout=2) or jt.has("JAM"), jt.text())
    t0 = time.time()
    jt.press("crown")  # within 1 s of the start: blocked by the guard
    time.sleep(0.4)
    check("second crown press within 1 s is not sent (jam still running)", gg("InJam") is True)
    print("   JT after the guarded press:", jt.text())
    texts()
    shots("jam1")

    # ---- 3. PBT team 1: #12 sits, picker ----
    print("\n== 3. PBT team 1: blocker #12 sits (picker)")
    time.sleep(2)
    pbt1.press("crown")
    check("PBT1 shows the picker", pbt1.has("12", timeout=2), pbt1.text())
    shots("pbt1-picker")
    pbt1.touch(*PICK["Blocker2"])
    t12 = wait_for(lambda: trip(1, "12"), 3)
    check("team 1 #12 has a box trip", t12)
    check("#12's box clock runs (in a jam)", wait_for(lambda: (trip(1, "12") or {}).get("Clock.Running"), 3))
    check("Blocker2 PenaltyBox on the scoreboard", wait_for(lambda: in_box(1, "Blocker2"), 2))
    check("PBT1 shows a running clock for #12", pbt1.has("12", timeout=2), pbt1.text())

    # ---- 4. PT notes a penalty on team 2 #11; PBT2 crown assigns it at once ----
    print("\n== 4. noted penalty on team 2 #11, PBT team 2: crown, no picker")
    sb.set(G + f"Team(2).Skater({skater(2, '11')}).Penalty(1).Code", "X", wait=0.8)
    check("team 2 Blocker1 HasUnserved", sb.g(pos(2, "Blocker1") + "HasUnserved"))
    pbt2.press("crown")
    check("team 2 #11 trip created and assigned without picker", wait_for(lambda: trip(2, "11"), 3), pbt2.text())
    check("#11's penalty X is serving", wait_for(lambda: penalties(2, "11").get(1, {}).get("Serving"), 2),
          str(penalties(2, "11")))
    texts()
    shots("both-pbt")

    # ---- 5. PBM: team 1 jammer sits, then team 2 jammer ----
    print("\n== 5. PBM: team 1 jammer sits, 5 s later team 2 jammer (jammer swap)")
    pbm.press("left")
    check("team 1 jammer trip", wait_for(lambda: trip(1, jammer=True), 3), pbm.text())
    check("team 1 Jammer PenaltyBox", wait_for(lambda: in_box(1, "Jammer"), 2))
    time.sleep(5)
    shots("pbm-one-jammer")
    pbm.press("right")
    check("team 2 jammer trip", wait_for(lambda: trip(2, jammer=True), 3), pbm.text())
    time.sleep(0.5)
    j1 = trip(1, jammer=True) or {}
    j2 = trip(2, jammer=True) or {}
    check("jammer swap: team 1 jammer's time is up (served time = their whole time)",
          j1.get("Clock.Time", 1) <= 0 and j1.get("Clock.MaximumTime", 30000) < 30000,
          str({k: j1.get(k) for k in ("Clock.Time", "Clock.MaximumTime", "Shortened")}))
    check("jammer swap: team 2 jammer's time shortened", j2.get("Shortened") or (j2.get("Clock.MaximumTime", 30000) < 30000),
          str({k: j2.get(k) for k in ("Shortened", "Clock.MaximumTime", "Clock.Time")}))
    check("PBM shows Done for team 1", pbm.has("DONE", timeout=2) or pbm.has("Done"), pbm.text())
    texts()
    shots("pbm-swap")
    pbm.touch(60, 120)  # team 1 (left): Done -> Release
    check("PBM clock menu open for team 1", pbm.has("Release", timeout=2), pbm.text())
    pbm.touch(*MENU["release"])
    check("PBM released team 1 jammer", wait_for(lambda: not in_box(1, "Jammer"), 3), pbm.text())

    # ---- 6. JT stops jam 1; box clocks stop ----
    print("\n== 6. JT stops jam 1")
    time.sleep(max(0, 1.2 - (time.time() - t0)))
    jt.press("crown")
    check("jam 1 stopped", wait_for(lambda: gg("InJam") is False, 3))
    check("box clocks stop between jams", wait_for(lambda: not (trip(1, "12") or {}).get("Clock.Running"), 2),
          str(trip(1, "12")))
    check("JT shows lineup", jt.has("LINEUP", timeout=2) or jt.has("Lineup"), jt.text())
    texts()
    shots("lineup")

    # ---- 7. undo on the JT and redo ----
    print("\n== 7. JT: undo (hold 1 s) brings jam 1 back, crown stops it again")
    time.sleep(1.2)
    jt.press("left", hold=1.3)
    check("undo: jam 1 running again", wait_for(lambda: gg("InJam") is True, 3), jt.text())
    time.sleep(1.2)
    jt.press("crown")
    check("jam 1 stopped again", wait_for(lambda: gg("InJam") is False, 3))
    jn = gg("Clock(Jam).Number")

    # ---- 8. jam 2: +penalty on #12, release by 2 o'clock, release by tap ----
    print("\n== 8. jam 2: PBT1 '+ Penalty' on #12; PBT2 releases #11 with 2 o'clock when done")
    lineup(1, "15", "16", None, "17", "18")  # #12 stays in the box (lineup keeps Blocker2 for them)
    sb.set(pos(1, "Blocker2") + "Skater", skater(1, "12"), wait=0.1)
    lineup(2, "10", "11", "16", "17", "18")  # team 2 jammer #10 and #11 still in the box
    time.sleep(1.2)
    jt.press("crown")
    check("jam 2 running", wait_for(lambda: gg("InJam") is True and gg("Clock(Jam).Number") == jn + 1, 3))
    before = len(penalties(1, "12"))
    pbt1.touch(*ROW[1][0])
    check("PBT1 clock menu open", pbt1.has("Release", timeout=2), pbt1.text())
    shots("pbt1-menu")
    pbt1.touch(*MENU["penalty"])
    p12 = wait_for(lambda: penalties(1, "12") if len(penalties(1, "12")) > before else None, 3)
    check("'+ Penalty' recorded a '?' penalty on #12",
          p12 and any(p.get("Code") == "?" for p in p12.values()), str(penalties(1, "12")))
    check("#12's box time grew by 30 s", wait_for(lambda: (trip(1, "12") or {}).get("Clock.MaximumTime", 0) >= 60000, 2),
          str((trip(1, "12") or {}).get("Clock.MaximumTime")))

    # PBT2: nobody done yet -> 2 o'clock sends nothing
    pbt2.press("right")
    time.sleep(0.5)
    check("PBT2 2 o'clock with nobody done: #11 stays in", in_box(2, "Blocker1"), pbt2.text())
    print("   PBT2 says:", pbt2.text())
    # fast-forward #11's clock to 1 s before zero (as the box timer would wait)
    t11 = [p for p, t in trips().items() if p.startswith("Team(2)") and t.get("RosterNumber") == "11" and t.get("IsCurrent")][0]
    ct = gg(t11 + ".Clock.Time")
    sb.set(G + t11 + ".Clock.Time", -(ct - 1000), "change", wait=2.5)
    check("#11's time is up", (trip(2, "11") or {}).get("Clock.Time", 1) <= 0, str((trip(2, "11") or {}).get("Clock.Time")))
    shots("pbt2-done")
    pbt2.press("right")
    check("PBT2 2 o'clock released #11", wait_for(lambda: not in_box(2, "Blocker1"), 3), pbt2.text())
    check("#11's penalty served", wait_for(lambda: penalties(2, "11").get(1, {}).get("Served"), 2), str(penalties(2, "11")))

    # PBM: release team 2's jammer by tap (Release)
    print("   PBM: release team 2 jammer by tap")
    jt2 = [p for p, t in trips().items() if p.startswith("Team(2)") and t.get("Jammer") and t.get("IsCurrent")]
    if jt2:
        ct = gg(jt2[0] + ".Clock.Time")
        sb.set(G + jt2[0] + ".Clock.Time", -(ct - 500), "change", wait=1.5)
    pbm.touch(180, 120)
    check("PBM clock menu open", pbm.has("Release", timeout=2), pbm.text())
    pbm.touch(*MENU["release"])
    check("team 2 jammer released", wait_for(lambda: not in_box(2, "Jammer"), 3), pbm.text())
    texts()
    shots("jam2")

    # ---- 9. timeout from the JT, type by touch ----
    print("\n== 9. JT: timeout, team 1 timeout by touch, crown ends it")
    time.sleep(1.2)
    jt.press("right")
    check("timeout running", wait_for(lambda: gg("Label(Stop)") == "End Timeout", 3))
    check("jam ended by the timeout", wait_for(lambda: gg("InJam") is False, 2))
    check("box clocks stop in the timeout", wait_for(lambda: not (trip(1, "12") or {}).get("Clock.Running"), 2))
    check("JT offers the timeout type", jt.has("OTO", timeout=2) or jt.has("TTO"), jt.text())
    shots("timeout-picker")
    jt.touch(*TO["T1 TO"])
    check("team 1 timeout recorded", wait_for(lambda: gg("TimeoutOwner") == f"{GID}_1", 3) or
          wait_for(lambda: gg("Team(1).Timeouts") == 2, 1), str((gg("TimeoutOwner"), gg("Team(1).Timeouts"))))
    texts()
    shots("team-timeout")
    time.sleep(1.2)
    jt.press("crown")
    check("timeout ended", wait_for(lambda: gg("Label(Stop)") != "End Timeout", 3), jt.text())

    # ---- 10. PBT2 restarts (power cycle): same client, sees the game ----
    print("\n== 10. PBT team 2 restarts: reconnects as the same device")
    cookie = open(os.path.join(pbt2.home, ".cache", "crg-sim-blockers2.cookie")).read().strip()
    pbt2.stop()
    time.sleep(1)
    pbt2.start()
    check("PBT2 back online", wait_for(lambda: online(pbt2), 8), pbt2.text())
    check("PBT2 kept its session cookie",
          open(os.path.join(pbt2.home, ".cache", "crg-sim-blockers2.cookie")).read().strip() == cookie)

    # ---- 11. jam 3 to the end of the period, intermission ----
    print("\n== 11. jam 3, period runs out, intermission: 'Swap sides?' on the box timers")
    lineup(2, "10", "11", "12", "13", "14")
    jt.press("crown")
    check("jam 3 running", wait_for(lambda: gg("InJam") is True, 3))
    # PBT1 releases #12 by tap after their (now 60 s) time: fast-forward
    t12p = [p for p, t in trips().items() if p.startswith("Team(1)") and t.get("RosterNumber") == "12" and t.get("IsCurrent")]
    if t12p:
        ct = gg(t12p[0] + ".Clock.Time")
        sb.set(G + t12p[0] + ".Clock.Time", -(ct - 500), "change", wait=1.5)
    pbt1.touch(*ROW[1][0])
    pbt1.has("Release", timeout=2)
    pbt1.touch(*MENU["release"])
    check("PBT1 released #12", wait_for(lambda: not in_box(1, "Blocker2"), 3), pbt1.text())
    # team 1 jammer is still Done on the scoreboard? release from PBM if needed
    if in_box(1, "Jammer"):
        pbm.touch(60, 120)
        pbm.has("Release", timeout=2)
        pbm.touch(*MENU["release"])
        check("PBM released team 1 jammer", wait_for(lambda: not in_box(1, "Jammer"), 3), pbm.text())
    # run the period clock out
    pc = gg("Clock(Period).Time")
    sb.set(G + "Clock(Period).Time", -(pc - 2000), "change", wait=0.5)
    time.sleep(2.5)
    jt.press("crown")
    check("period 1 over", wait_for(lambda: gg("InPeriod") is False and gg("InJam") is False, 4),
          str((gg("InPeriod"), gg("InJam"), gg("Clock(Period).Time"))))
    check("intermission clock running", wait_for(lambda: gg("Clock(Intermission).Running") is True, 3))
    for c in (pbt1, pbt2, pbm):
        check(f"{c.name} asks 'Swap sides?'", c.has("Swap", timeout=3), c.text())
    texts()
    shots("intermission")
    check("JT: crown needs a hold now", "hold" in jt.text().lower() or "Extend" in jt.text() or "Period" in jt.text(), jt.text())
    t1name, t2name = gg("Team(1).Name"), gg("Team(2).Name")
    pbm.touch(78, 154)  # yes
    check("PBM swapped sides (team 2 on the left)",
          wait_for(lambda: 0 <= pbm.text().find(t2name) < pbm.text().find(t1name), 2), pbm.text())
    pbt1.touch(162, 154)  # no
    pbt2.touch(162, 154)
    texts()
    shots("after-swap")

    # ---- 12. guards: menus refused while clocks run, open in intermission ----
    print("\n== 12. device menu opens in the intermission (3 s hold)")
    pbt1.press("left", hold=3.3)
    check("PBT1 menu opens", pbt1.has("Game", timeout=2), pbt1.text())
    pbt1.press("crown")  # any button closes
    check("PBT1 menu closed", wait_for(lambda: "Game" not in pbt1.text() or "blockers" in pbt1.text(), 2), pbt1.text())

    # ---- 13. period 2 ----
    print("\n== 13. JT starts period 2 (crown held 1 s)")
    ic = gg("Clock(Intermission).Time")
    sb.set(G + "Clock(Intermission).Time", -(ic - 20000), "change", wait=0.8)
    lineup(1, "10", "11", "12", "13", "14")
    lineup(2, "10", "11", "12", "13", "14")
    jt.press("crown", hold=0.2)
    time.sleep(0.5)
    check("short crown press in the intermission does nothing", gg("InPeriod") is False, jt.text())
    jt.press("crown", hold=1.3)
    check("period 2, jam 1 running", wait_for(lambda: gg("InJam") is True and gg("Clock(Period).Number") == 2, 3),
          str((gg("InJam"), gg("Clock(Period).Number"))))
    pbm.press("left")  # after the swap, left is team 2
    check("PBM left button (after the swap) sits team 2's jammer", wait_for(lambda: in_box(2, "Jammer"), 3), pbm.text())
    texts()
    shots("period2")
    pbm.press("crown", hold=1.3)  # undo
    check("PBM crown held = undo: team 2 jammer out again", wait_for(lambda: not in_box(2, "Jammer"), 3), pbm.text())
    shots("end")
finally:
    for c in clocks:
        c.stop()
    server.stop()

sys.exit(C.summary())
