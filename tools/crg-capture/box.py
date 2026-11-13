"""Box timer probe: drives penalty box trips on a throwaway CRG and prints what changes.

Usage: python3 box.py [PORT]   (default 4331). Creates game "boxprobe" with two
prepared teams, makes it the current game and turns on UseLT + UsePBT.
"""
import json, re, sys, threading, time, websocket

PORT = sys.argv[1] if len(sys.argv) > 1 else "4331"
G = "ScoreBoard.CurrentGame."


class SB:
    def __init__(self):
        self.state, self.lock = {}, threading.Lock()
        self.ws = websocket.create_connection(f"ws://localhost:{PORT}/WS/?source=boxprobe&platform=probe",
                                              suppress_origin=True, timeout=10)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while True:
            try:
                m = json.loads(self.ws.recv())
            except Exception:
                return
            for k, v in m.get("state", {}).items():
                with self.lock:
                    if v is None:
                        self.state.pop(k, None)
                    else:
                        self.state[k] = v

    def reg(self, paths):
        self.ws.send(json.dumps({"action": "Register", "paths": paths}))

    def set(self, key, value=True, flag="", wait=0.6):
        self.ws.send(json.dumps({"action": "Set", "key": key, "value": value, "flag": flag}))
        time.sleep(wait)

    def g(self, k, d=None):
        with self.lock:
            return self.state.get(k, d)

    def keys(self, prefix):
        with self.lock:
            return {k: v for k, v in self.state.items() if k.startswith(prefix)}


sb = SB()
sb.reg(["ScoreBoard.Settings", "ScoreBoard.Game", "ScoreBoard.CurrentGame"])
time.sleep(1.5)
S = "ScoreBoard.Settings.Setting(ScoreBoard.Penalties."
sb.set(S + "UseLT)", "true"); sb.set(S + "UsePBT)", "true")
for i, name in enumerate(["Red", "Blue"]):
    P = f"ScoreBoard.PreparedTeam(bp{i})"
    sb.set(P + ".Id", f"bp{i}", wait=0.1); sb.set(P + ".TeamName", name, wait=0.1)
    for s in range(8):
        sb.set(P + f".Skater(bp{i}s{s}).Name", f"{name} {s}", wait=0.05)
        sb.set(P + f".Skater(bp{i}s{s}).RosterNumber", str(10 + s), wait=0.05)
GID = "box" + time.strftime("%H%M%S")
Gm = f"ScoreBoard.Game({GID})"
sb.set(Gm + ".Id", GID, wait=0.3)
sb.set(Gm + ".Team(1).PreparedTeam", "bp0", wait=0.3); sb.set(Gm + ".Team(2).PreparedTeam", "bp1", wait=0.5)
sb.set("ScoreBoard.CurrentGame.Game", GID, wait=2.5)
print("UsePBT", sb.g(S + "UsePBT)"), "UseLT", sb.g(S + "UseLT)"), "game", sb.g(G + "Game"))


def skater(t, number):
    for k, v in sb.keys(G + f"Team({t}).Skater(").items():
        if k.endswith(".RosterNumber") and v == number:
            return k.split("Skater(")[1].split(")")[0]


def trips():
    """{path prefix 'Team(1).BoxTrip(id)' or 'BoxTrip(id)': {key: value}}"""
    out = {}
    for k, v in sb.keys(G).items():
        m = re.match(r"((?:Team\(\d\)\.)?BoxTrip\([^)]+\))\.(.*)$", k[len(G):])
        if m and "Fielding(" not in m.group(2) and "Penalty(" not in m.group(2):
            out.setdefault(m.group(1), {})[m.group(2)] = v
    return out


def trip_of(number=None, jammer=None):
    for p, t in trips().items():
        if t.get("IsCurrent") and (number is None or t.get("RosterNumber") == number) and (jammer is None or t.get("Jammer") == jammer):
            return p


def show(label, all_=False):
    print(f"--- {label}  InJam={sb.g(G + 'InJam')} Jam#={sb.g(G + 'Clock(Jam).Number')} jam={sb.g(G + 'Clock(Jam).Time')}")
    for tid, t in trips().items():
        if not all_ and not t.get("IsCurrent"):
            continue
        print(f"   {tid[:16]:16} cur={t.get('IsCurrent')} #{t.get('RosterNumber')!r} jammer={t.get('Jammer')} "
              f"time={t.get('Clock.Time')} max={t.get('Clock.MaximumTime')} run={t.get('Clock.Running')} "
              f"codes={t.get('PenaltyCodes')!r} short={t.get('Shortened')} stopped={t.get('TimingStopped')} "
              f"endJam={t.get('EndJamNumber')} startJam={t.get('StartJamNumber')} betw={t.get('StartBetweenJams')}")


def pos(t, p):
    return G + f"Team({t}).Position({p})."


def penalties(t, sid):
    pre = G + f"Team({t}).Skater({sid}).Penalty("
    return {k[len(pre):]: v for k, v in sb.keys(pre).items() if k.endswith((".Code", ".Served", ".Serving"))}


section = sys.argv[2] if len(sys.argv) > 2 else "all"
s10, s11, s12 = skater(1, "10"), skater(1, "11"), skater(1, "12")
j20 = skater(2, "10")
print("skaters", s10, s11, s12, j20)

# lineup for jam 1
sb.set(pos(1, "Jammer") + "Skater", s10); sb.set(pos(1, "Blocker1") + "Skater", s11); sb.set(pos(1, "Blocker2") + "Skater", s12)
sb.set(pos(2, "Jammer") + "Skater", j20)
sb.set(G + "StartJam", wait=1.2)
show("jam 1 running")

print("\n== A. Position.StartBoxClock on Blocker1 (#11, no noted penalty), in a jam")
sb.set(pos(1, "Blocker1") + "StartBoxClock", wait=1.5)
show("after StartBoxClock"); print("   #11 penalties", penalties(1, s11))
print("   Position PenaltyBox", sb.g(pos(1, "Blocker1") + "PenaltyBox"), "PenaltyTime", sb.g(pos(1, "Blocker1") + "PenaltyTime"))

print("\n== B. '+ Penalty': Skater(#11).Penalty(n+1).Code = '?'")
n = sb.g(G + f"Team(1).Skater({s11}).PenaltyCount")
sb.set(G + f"Team(1).Skater({s11}).Penalty({n + 1}).Code", "?", wait=1)
show("after + penalty"); print("   #11 penalties", penalties(1, s11))

print("\n== C. noted penalty on #12 first (Code B), then StartBoxTrip (unassigned) + CurrentFielding")
sb.set(G + f"Team(1).Skater({s12}).Penalty(1).Code", "B", wait=0.8)
print("   #12 HasUnserved", sb.g(G + f"Team(1).Skater({s12}).HasUnserved"), "Position HasUnserved",
      sb.g(pos(1, "Blocker2") + "HasUnserved"))
before = set(trips())
sb.set(G + "StartBoxTrip", wait=1.2)
new = [t for t in trips() if t not in before]
print("   new trip path:", new)
f = sb.g(pos(1, "Blocker2") + "CurrentFielding")
print("   Blocker2 CurrentFielding =", f)
sb.set(G + new[0] + ".CurrentFielding", f, wait=1.2)
t12 = trip_of("12")
print("   trip path after assigning:", t12, "| old path still present:", new[0] in trips())
show("after CurrentFielding set"); print("   #12 penalties", penalties(1, s12))

print("\n== D. TimingStopped on #12's trip (2 s)")
sb.set(G + t12 + ".TimingStopped", True, wait=2)
show("timing stopped 2 s")
sb.set(G + t12 + ".TimingStopped", False, wait=1)
show("timing resumed 1 s")

print("\n== E. jam ends: box clocks stop; #13 sits between jams")
sb.set(G + "StopJam", wait=1.5)
s13 = skater(1, "13")
sb.set(pos(1, "Blocker3") + "Skater", s13)
sb.set(pos(1, "Blocker3") + "StartBoxClock", wait=2)
show("lineup, #13 sat +2 s", all_=True)
sb.set(G + "StartJam", wait=1.5)
show("jam 2 +1.5 s")

print("\n== F. #12: release early (PenaltyBox false), then UnendBoxTrip")
sb.set(pos(1, "Blocker2") + "PenaltyBox", False, wait=1)
show("#12 released", all_=True); print("   #12 penalties", penalties(1, s12), "ExtraPenaltyTime", sb.g(G + f"Team(1).Skater({s12}).ExtraPenaltyTime"))
sb.set(pos(1, "Blocker2") + "UnendBoxTrip", wait=1)
show("#12 unend")

print("\n== G. #11: clock past zero in the jam, then release")
t11 = trip_of("11")
ct = sb.g(G + t11 + ".Clock.Time")
sb.set(G + t11 + ".Clock.Time", -(ct - 1000), "change", wait=2.5)
show("#11 clock past zero", all_=True)
print("   Blocker1 PenaltyBox", sb.g(pos(1, "Blocker1") + "PenaltyBox"), "PenaltyTime", sb.g(pos(1, "Blocker1") + "PenaltyTime"))
sb.set(pos(1, "Blocker1") + "PenaltyBox", False, wait=1)
show("#11 released", all_=True)

print("\n== H. Cancel (Delete) #13's trip")
t13 = trip_of("13")
sb.set(G + t13 + ".Delete", wait=1)
print("   #13 trip still there:", t13 in trips(), "| #13 penalties", penalties(1, s13), "PenaltyBox", sb.g(pos(1, "Blocker3") + "PenaltyBox"))

print("\n== I. jammers by position: team 1 jammer sits, 5 s later team 2 jammer")
sb.set(pos(1, "Jammer") + "StartBoxClock", wait=5)
show("T1 jammer +5 s")
sb.set(pos(2, "Jammer") + "StartBoxClock", wait=1.5)
show("T2 jammer sat", all_=True)
print("   T1 jammer PenaltyBox", sb.g(pos(1, "Jammer") + "PenaltyBox"), "T2", sb.g(pos(2, "Jammer") + "PenaltyBox"))

print("\n== J. jammer swap with skaters: jam 3, T1 jammer (#10) sits, 5 s later T2 jammer (#10)")
for t in (1, 2):
    if sb.g(pos(t, "Jammer") + "PenaltyBox"):
        sb.set(pos(t, "Jammer") + "PenaltyBox", False, wait=0.5)
sb.set(G + "StopJam", wait=1.2)
sb.set(pos(1, "Jammer") + "Skater", skater(1, "15")); sb.set(pos(2, "Jammer") + "Skater", skater(2, "15"))
sb.set(G + "StartJam", wait=1.2)
sb.set(pos(1, "Jammer") + "StartBoxClock", wait=5)
show("T1 jammer +5 s")
sb.set(pos(2, "Jammer") + "StartBoxClock", wait=1.5)
show("T2 jammer sat")
print("   T1 jammer PenaltyBox", sb.g(pos(1, "Jammer") + "PenaltyBox"), "T2", sb.g(pos(2, "Jammer") + "PenaltyBox"))
print("   T2 jammer: + penalty while both sit")
s2 = skater(2, "15"); n = sb.g(G + f"Team(2).Skater({s2}).PenaltyCount")
sb.set(G + f"Team(2).Skater({s2}).Penalty({n + 1}).Code", "?", wait=1)
show("T2 jammer + penalty")
