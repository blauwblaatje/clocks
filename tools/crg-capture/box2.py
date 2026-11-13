"""Box timer probe 2: the points protocol.md section 12 left open.
Undo (ClockReplace / ClockUndo) with box clocks running, changing the skater
of a running trip, and the box clock update rate.

Usage: python3 box2.py [PORT]   (default 4331, a throwaway CRG)
"""
import json, re, sys, threading, time, websocket

PORT = sys.argv[1] if len(sys.argv) > 1 else "4331"
G = "ScoreBoard.CurrentGame."


class SB:
    def __init__(self):
        self.state, self.lock, self.log = {}, threading.Lock(), []
        self.ws = websocket.create_connection(f"ws://localhost:{PORT}/WS/?source=boxprobe2", suppress_origin=True)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while True:
            try:
                m = json.loads(self.ws.recv())
            except Exception:
                return
            t = time.monotonic()
            for k, v in m.get("state", {}).items():
                with self.lock:
                    self.log.append((t, k, v))
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
sb.reg(["ScoreBoard.Settings", "ScoreBoard.CurrentGame"])
time.sleep(1.5)
S = "ScoreBoard.Settings.Setting(ScoreBoard.Penalties."
sb.set(S + "UseLT)", "true"); sb.set(S + "UsePBT)", "true")
for i, name in enumerate(["Red", "Blue"]):
    P = f"ScoreBoard.PreparedTeam(b2{i})"
    sb.set(P + ".Id", f"b2{i}", wait=0.1); sb.set(P + ".TeamName", name, wait=0.1)
    for s in range(6):
        sb.set(P + f".Skater(b2{i}s{s}).Name", f"{name} {s}", wait=0.05)
        sb.set(P + f".Skater(b2{i}s{s}).RosterNumber", str(30 + s), wait=0.05)
GID = "box2" + time.strftime("%H%M%S")
sb.set(f"ScoreBoard.Game({GID}).Id", GID, wait=0.3)
sb.set(f"ScoreBoard.Game({GID}).Team(1).PreparedTeam", "b20", wait=0.3)
sb.set(f"ScoreBoard.Game({GID}).Team(2).PreparedTeam", "b21", wait=0.5)
sb.set("ScoreBoard.CurrentGame.Game", GID, wait=2.5)


def skater(t, n):
    for k, v in sb.keys(G + f"Team({t}).Skater(").items():
        if k.endswith(".RosterNumber") and v == n:
            return k.split("Skater(")[1].split(")")[0]


def trips():
    out = {}
    for k, v in sb.keys(G).items():
        m = re.match(r"((?:Team\(\d\)\.)?BoxTrip\([^)]+\))\.(.*)$", k[len(G):])
        if m and "Fielding(" not in m.group(2) and "Penalty(" not in m.group(2):
            out.setdefault(m.group(1), {})[m.group(2)] = v
    return out


def trip_of(number):
    for p, t in trips().items():
        if t.get("IsCurrent") and t.get("RosterNumber") == number:
            return p


def show(label):
    print(f"--- {label}  InJam={sb.g(G + 'InJam')} Jam#={sb.g(G + 'Clock(Jam).Number')} jam={sb.g(G + 'Clock(Jam).Time')} "
          f"Undo={sb.g(G + 'Label(Undo)')!r}")
    for p, t in trips().items():
        if t.get("IsCurrent"):
            print(f"   {p[:24]:24} #{t.get('RosterNumber')!r} time={t.get('Clock.Time')} max={t.get('Clock.MaximumTime')} "
                  f"run={t.get('Clock.Running')} codes={t.get('PenaltyCodes')!r} pens={t.get('TotalPenalties')}")


def pens(t, sid):
    pre = G + f"Team({t}).Skater({sid}).Penalty("
    return {k[len(pre):]: v for k, v in sb.keys(pre).items() if k.endswith(".Code")}


pos = lambda t, p: G + f"Team({t}).Position({p})."
s30, s31, s32 = skater(1, "30"), skater(1, "31"), skater(1, "32")
sb.set(pos(1, "Blocker1") + "Skater", s30); sb.set(pos(1, "Blocker2") + "Skater", s31)
sb.set(pos(1, "Blocker3") + "Skater", s32)
sb.set(G + "StartJam", wait=1.5)

print("\n== A. box clock update rate (#30 sat, 4 s of updates)")
sb.set(pos(1, "Blocker1") + "StartBoxClock", wait=0.3)
t30 = trip_of("30")
t0 = time.monotonic()
time.sleep(4)
ticks = [(t - t0, v) for t, k, v in list(sb.log) if t >= t0 and k == G + t30 + ".Clock.Time"]
print("   updates:", len(ticks), [f"{dt:.2f}s={v}" for dt, v in ticks])

print("\n== B. change the skater of a running trip: #30's trip to Blocker2 (#31)")
show("before")
print("   #30 penalties", pens(1, s30), "| #31 penalties", pens(1, s31))
f2 = sb.g(pos(1, "Blocker2") + "CurrentFielding")
sb.set(G + t30 + ".CurrentFielding", f2, wait=1.0)
show("after changing to Blocker2")
print("   #30 penalties", pens(1, s30), "| #31 penalties", pens(1, s31))
print("   Blocker1 PenaltyBox", sb.g(pos(1, "Blocker1") + "PenaltyBox"), "Blocker2 PenaltyBox", sb.g(pos(1, "Blocker2") + "PenaltyBox"))

print("\n== C. + penalty, then change the skater again (#31 -> #32)")
t31 = trip_of("31")
n = sb.g(G + t31 + ".TotalPenalties") or 0
sb.set(G + f"Team(1).Skater({s31}).Penalty({n + 1}).Code", "?", wait=0.8)
show("after + penalty on #31")
f3 = sb.g(pos(1, "Blocker3") + "CurrentFielding")
sb.set(G + t31 + ".CurrentFielding", f3, wait=1.0)
show("after changing to Blocker3")
print("   #31 penalties", pens(1, s31), "| #32 penalties", pens(1, s32))

print("\n== D. undo with a box clock running")
time.sleep(1.2)
sb.set(G + "StopJam", wait=1.5)
show("jam stopped")
sb.set(G + "ClockReplace", wait=1.5)
show("after ClockReplace (undo of Stop Jam)")
time.sleep(1.5)
show("+1.5 s, replace pending")
sb.set(G + "ClockReplace", wait=1.5)
show("after ClockReplace again (No Action)")
time.sleep(1.2)
sb.set(G + "StopJam", wait=1.5)
sb.set(G + "ClockUndo", wait=1.5)
show("after ClockUndo (undo of Stop Jam)")
time.sleep(1.5)
show("+1.5 s")
