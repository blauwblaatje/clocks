"""Live test of the C++ client core (firmware/lib/crgclient) against a running CRG.

The C++ core runs in ./pipe (built by run.sh); this script owns the WebSocket
and passes frames, times and presses through, like the firmware's transport
will. Use a throwaway server: it creates a game and changes settings.

Usage: python3 live.py PIPE [PORT]   (default port 4331)
"""
import json, re, subprocess, sys, threading, time, websocket

PIPE = sys.argv[1]
PORT = sys.argv[2] if len(sys.argv) > 2 else "4331"
T0 = time.monotonic()
fails = []


def now():
    return int((time.monotonic() - T0) * 1000)


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        fails.append(what)


class Device:
    """The C++ client plus a socket, as the firmware will run it."""

    def __init__(self, role, cookie=None):
        self.p = subprocess.Popen([PIPE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.lock = threading.Lock()
        self.log = []
        self.results, self.cookie, self.ws, self.state_lines, self.drops = {}, None, None, [], 0
        self.url = self.cmd(f"R {role}")[0].split(" ", 1)[1]
        if cookie:
            self.cmd(f"K {cookie}")
        self.alive = True
        threading.Thread(target=self._ticker, daemon=True).start()

    def cmd(self, line):
        with self.lock:
            self.p.stdin.write(line.replace("\n", " ") + "\n")
            self.p.stdin.flush()
            out = []
            while True:
                l = self.p.stdout.readline().rstrip("\n")
                if l == "OK":
                    break
                if l.startswith("SEND "):
                    if self.ws:
                        self.ws.send(l[5:])
                elif l.startswith("RES "):
                    _, i, o, lat = l.split()
                    self.results[int(i)] = (o, int(lat))
                elif l.startswith("COOKIE "):
                    self.cookie = l[7:]
                elif l == "DROP":
                    self.drops += 1
                else:
                    out.append(l)
            return out

    def _ticker(self):
        while self.alive:
            try:
                self.cmd(f"T {now()}")
            except (BrokenPipeError, ValueError):
                return
            time.sleep(0.05)

    def connect(self):
        self.cmd(f"N {now()}")
        hdr = [f"Cookie: CRG_SCOREBOARD={self.cookie}"] if self.cookie else []
        ws = websocket.create_connection(f"ws://localhost:{PORT}{self.url}", header=hdr, suppress_origin=True)
        sc = ", ".join(v for k, v in ws.getheaders().items() if k.lower() == "set-cookie")
        self.ws = ws
        self.cmd(f"O {now()} {sc}")
        threading.Thread(target=self._reader, args=(ws,), daemon=True).start()

    def _reader(self, ws):
        while True:
            try:
                m = ws.recv()
            except Exception:
                if self.ws is ws and self.alive:
                    self.ws = None
                    self.cmd(f"C {now()}")
                return
            if not self.alive:
                return
            self.log.append((now(), m))
            self.cmd(f"F {now()} {m}")

    def close(self):
        ws, self.ws = self.ws, None
        ws.close()
        self.cmd(f"C {now()}")

    def q(self):
        return self.cmd("Q")

    def s(self):
        line = self.q()[0]
        return {k: v for k, v in re.findall(r"([\w#]+)=('[^']*'|\S*)", line)} | {"raw": line}

    def wait(self, i, secs=3):
        end = time.time() + secs
        while time.time() < end:
            if i in self.results:
                return self.results[i]
            time.sleep(0.02)
        return ("Pending", 0)

    def press(self, c):
        return int(self.cmd(f"P {now()} {c}")[0].split()[1])

    def box(self, args):
        return int(self.cmd(f"B {now()} {args}")[0].split()[1])

    def trips(self):
        out = []
        for l in self.q():
            if l.startswith("B "):
                d = {"id": l.split()[1]}
                d |= {k: v.strip("'") for k, v in re.findall(r"(\w+)=('[^']*'|\S*)", l)}
                m = re.search(r" #(\S*)", l)
                d["number"] = m.group(1) if m else ""
                out.append(d)
        return out

    def stop(self):
        self.alive = False
        with self.lock:
            if self.ws:
                self.ws.close()
            self.p.stdin.close()


def outcome(d, i, want, what):
    o, lat = d.wait(i)
    check(o == want, f"{what}: {o}" + (f" in {lat} ms" if o == "Confirmed" else ""))
    return lat


# ---------- operator: a plain socket that sets up the game ----------
op = websocket.create_connection(f"ws://localhost:{PORT}/WS/?source=livetest-operator", suppress_origin=True)
op.recv()


def opset(k, v=True, flag="", wait=0.1):
    op.send(json.dumps({"action": "Set", "key": k, "value": v, "flag": flag}))
    time.sleep(wait)


S = "ScoreBoard.Settings.Setting(ScoreBoard.Penalties."
opset(S + "UseLT)", "true"); opset(S + "UsePBT)", "true")
skaters = {}
for i, name in enumerate(["Red", "Blue"]):
    P = f"ScoreBoard.PreparedTeam(lv{i})"
    opset(P + ".Id", f"lv{i}"); opset(P + ".TeamName", name)
    for s in range(6):
        opset(P + f".Skater(lv{i}s{s}).Name", f"{name} {s}", wait=0.03)
        opset(P + f".Skater(lv{i}s{s}).RosterNumber", str(20 + s), wait=0.03)
GID = "live" + time.strftime("%H%M%S")
opset(f"ScoreBoard.Game({GID}).Id", GID, wait=0.3)
opset(f"ScoreBoard.Game({GID}).Team(1).PreparedTeam", "lv0", wait=0.3)
opset(f"ScoreBoard.Game({GID}).Team(2).PreparedTeam", "lv1", wait=0.3)
opset("ScoreBoard.CurrentGame.Game", GID, wait=2)
# skater ids in the game: read through the operator socket
op.send(json.dumps({"action": "Register", "paths": [f"ScoreBoard.Game({GID}).Team(*).Skater(*).RosterNumber"]}))
time.sleep(1)
op.settimeout(0.5)
while True:
    try:
        m = json.loads(op.recv())
    except Exception:
        break
    for k, v in m.get("state", {}).items():
        if k.endswith(".RosterNumber") and ".Skater(" in k:
            skaters[(int(k.split("Team(")[1][0]), v)] = k.split("Skater(")[1].split(")")[0]
G = "ScoreBoard.CurrentGame."
print("game", GID, "skaters", len(skaters))

# ---------- jam timer ----------
print("\n== jam timer: session")
jt = Device("jt")
jt.connect()
time.sleep(1)
s = jt.s()
check(jt.cookie is not None and jt.cookie.startswith("node0"), f"cookie stored: {jt.cookie}")
check(s["synced"] == "1" and s["mayWrite"] == "1", f"synced, may write: {s['raw']}")
dev1 = s["device"]
check(s["crown"] == "StartJam", "crown = StartJam before the first jam")

print("\n== crown")
outcome(jt, jt.press("Crown"), "Confirmed", "crown starts the first jam")
check(jt.s()["crown"] == "StopJam", "crown = StopJam in the jam")
i = jt.press("Crown")
outcome(jt, i, "Confirmed", "crown within 1 s of the FIRST jam's start: CRG replaces the start with a lineup")
s = jt.s()
check(s["inJam"] == "0" and s["jam#"] == "0", f"jam undone, lineup running: {s['raw']}")
time.sleep(1.1)
outcome(jt, jt.press("Crown"), "Confirmed", "crown starts jam 1")
time.sleep(1.1)
outcome(jt, jt.press("Crown"), "Confirmed", "crown stops jam 1")
time.sleep(1.1)
outcome(jt, jt.press("Crown"), "Confirmed", "crown starts jam 2")
outcome(jt, jt.press("Crown"), "NoChange", "crown within 1 s of a later start (server guard) gives NoChange")
check(jt.s()["inJam"] == "1", "jam 2 still running")
time.sleep(1.1)
outcome(jt, jt.press("Crown"), "Confirmed", "crown after 1 s stops jam 2")

print("\n== timeouts")
time.sleep(1.1)
outcome(jt, jt.press("Timeout"), "Confirmed", "Timeout")
s = jt.s()
check(s["stop"] == "'End Timeout'" and s["crown"] == "StopJam", f"labels in timeout: {s['raw']}")
to = int(s["to#"])
time.sleep(1.1)
outcome(jt, jt.press("Timeout"), "Confirmed", "Timeout during a timeout (New Timeout)")
check(int(jt.s()["to#"]) == to + 1, "timeout clock number went up")
outcome(jt, jt.press("Team1Timeout"), "Confirmed", "typed as team 1 timeout")
s = jt.s()
check(s["owner"].strip("'").endswith("_1") and s["t1TO"] == "2", f"owner team 1, team 1 has 2 left: {s['raw']}")
time.sleep(1.1)
outcome(jt, jt.press("Crown"), "Confirmed", "crown ends the timeout")
time.sleep(1.1)
outcome(jt, jt.press("ClockReplace"), "Confirmed", "undo (ClockReplace)")
check(jt.s()["undo"] == "'No Action'", "undo label No Action while the replace is pending")
outcome(jt, jt.press("ClockReplace"), "Confirmed", "undo again = No Action")

print("\n== reconnect with the stored cookie")
jt.close()
i = jt.press("StartJam")
check(jt.results.get(i, ("?",))[0] == "NotSent", "press while disconnected is NotSent, never queued")
jt.connect()
time.sleep(1)
check(jt.s()["device"] == dev1, "same device id after reconnect")
check(jt.s()["synced"] == "1", "mirror rebuilt")

print("\n== refused write")
opset("ScoreBoard.Settings.Setting(ScoreBoard.Clients.AllLocalDevicesWrite)", "false", wait=0.3)
opset(f"ScoreBoard.Clients.Device({dev1}).MayWrite", False, wait=0.5)
s = jt.s()
check(s["mayWrite"] == "0", "MayWrite false reaches the device before any press")
i = jt.press("StartJam")
check(jt.wait(i)[0] == "NotSent", "press while not allowed is NotSent")
opset(f"ScoreBoard.Clients.Device({dev1}).MayWrite", True, wait=0.5)
check(jt.s()["mayWrite"] == "1", "MayWrite true again")
opset("ScoreBoard.Settings.Setting(ScoreBoard.Clients.AllLocalDevicesWrite)", "true", wait=0.3)

# ---------- box timer ----------
print("\n== box timer (blockers)")
bx = Device("blockers")
bx.connect()
time.sleep(1.5)
check(bx.s()["usePBT"] == "1", "UsePBT seen")
s20, s21, s22 = skaters[(1, "20")], skaters[(1, "21")], skaters[(1, "22")]
opset(G + "Team(1).Position(Jammer).Skater", s20); opset(G + "Team(1).Position(Blocker1).Skater", s21)
opset(G + "Team(1).Position(Blocker2).Skater", s22)
opset(G + "Team(2).Position(Jammer).Skater", skaters[(2, "20")], wait=0.5)
for _ in range(4):  # end a leftover timeout, then start a jam
    if jt.s()["inJam"] == "1":
        break
    time.sleep(1.1)
    jt.press("Crown")
    time.sleep(0.3)
time.sleep(1.1)
check(jt.s()["inJam"] == "1", "jam running for the box tests")

outcome(bx, bx.box("sbc 1 Blocker1"), "Confirmed", "#21 sat (StartBoxClock)")
t21 = [t for t in bx.trips() if t.get("number") == "21" and t["cur"] == "1"]
check(len(t21) == 1 and t21[0]["codes"] == "?" and t21[0]["pos"] == "Blocker1", f"trip #21 with CRG's '?' penalty: {t21}")
t21 = t21[0]["id"]
lat = outcome(bx, bx.box(f"pen {t21}"), "Confirmed", "+ Penalty on #21")
time.sleep(0.3)
t = [x for x in bx.trips() if x["id"] == t21][0]
check(t["codes"] == "? ?" and t["max"] == "60000", f"two penalties, 60 s: {t}")

opset(G + f"Team(1).Skater({s22}).Penalty(1).Code", "B", wait=0.5)
check(any(l.startswith("P 1 Blocker2") and "unserved=1" in l for l in bx.q()), "#22 noted penalty shows on the position")
before = {t["id"] for t in bx.trips()}
outcome(bx, bx.box("sbt"), "Confirmed", "blocker sat, who unknown (StartBoxTrip)")
new = [t for t in bx.trips() if t["id"] not in before]
check(len(new) == 1 and new[0]["team"] == "0", f"unassigned trip at game level: {new}")
tn = new[0]["id"]
outcome(bx, bx.box(f"assign {tn} 1 Blocker2"), "Confirmed", "assign it to Blocker2 (#22)")
t = [x for x in bx.trips() if x["id"] == tn]
check(len(t) == 1 and t[0]["team"] == "1" and t[0]["codes"] == "B", f"trip moved to team 1, noted B linked: {t}")

outcome(bx, bx.box(f"stop {tn} 1"), "Confirmed", "pause #22's clock")
time.sleep(1.2)
t = [x for x in bx.trips() if x["id"] == tn][0]
check(t["stopped"] == "1" and t["run"] == "0", f"paused: {t}")
outcome(bx, bx.box(f"stop {tn} 0"), "Confirmed", "resume #22's clock")

outcome(bx, bx.box(f"rel {tn}"), "Confirmed", "release #22 early")
t = [x for x in bx.trips() if x["id"] == tn][0]
check(t["cur"] == "0" and t["clock"] == "0", f"trip finished, clock gone: {t}")
outcome(bx, bx.box("unend 1 Blocker2"), "Confirmed", "undo the release")
t = [x for x in bx.trips() if x["id"] == tn][0]
check(t["cur"] == "1" and t["clock"] == "1", f"current again with its clock: {t}")
outcome(bx, bx.box(f"cancel {tn}"), "Confirmed", "cancel #22's trip")
check(not [x for x in bx.trips() if x["id"] == tn], "trip gone from the mirror")

print("\n== jammer swap")
jm = Device("jammers")
jm.connect()
time.sleep(1.5)
outcome(jm, jm.box("sbc 1 Jammer"), "Confirmed", "team 1 jammer sat")
time.sleep(4)
outcome(jm, jm.box("sbc 2 Jammer"), "Confirmed", "team 2 jammer sat 4 s later")
time.sleep(0.5)
js = [t for t in jm.trips() if t["jammer"] == "1" and t["cur"] == "1"]
check(len(js) == 2 and all(t["short"] == "1" for t in js), f"both shortened: {js}")

print("\n== box controller (team 1 blockers), driven like the device")
bc = Device("bctl")
bc.connect()
time.sleep(1.5)


def bview():
    lines = bc.cmd(f"V {now()}")
    head = lines[0]
    slots = [dict(re.findall(r"(\w+)=(\S*)", l)) for l in lines if l.startswith("L ")]
    picks = [dict(re.findall(r"(\w+)=('[^']*'|\S*)", l)) for l in lines if l.startswith("K ")]
    return head, slots, picks


def bpress(btn, ms=60):
    bc.cmd(f"X {now()} {btn} 1")
    time.sleep(ms / 1000)
    bc.cmd(f"X {now()} {btn} 0")


if jt.s()["inJam"] != "1":
    time.sleep(1.1)
    jt.press("Crown")
    time.sleep(0.5)
head, slots, _ = bview()
check("Jam on" in head, f"status: {head}")
s23 = skaters[(1, "23")]
opset(G + "Team(1).Position(Blocker3).Skater", s23, wait=0.5)
# #22 still has the noted B from the cancelled trip: the only noted penalty -> no picker
bpress("crown")
time.sleep(0.6)
head, slots, _ = bview()
check("screen=0" in head, f"no picker when exactly one skater has a noted penalty: {head}")
check(any(s.get("number") == "22" for s in slots), f"#22 seated at once: {slots}")
# nobody else noted: StartBoxTrip and the picker
bpress("crown")
time.sleep(0.5)
head, slots, picks = bview()
check("screen=1" in head and len(picks) == 4, f"picker open: {head} {picks}")
check(any(s.get("number") == "?" for s in slots), f"new unassigned trip shown as ?: {slots}")
bc.cmd(f"Y {now()} 160 145")  # Blocker 3
time.sleep(0.6)
head, slots, _ = bview()
check(any(s.get("number") == "23" for s in slots) and not any(s.get("number") == "?" for s in slots),
      f"picked #23, trip assigned: {slots}")
n_before = len(slots)
bpress("left", 1100)  # undo = cancel the trip that sat last
time.sleep(0.6)
head, slots, _ = bview()
check(len(slots) == n_before - 1 and not any(s.get("number") == "23" for s in slots), f"undo cancelled #23: {slots}")
# tap #22's clock, then Release
idx = [i for i, s in enumerate(slots) if s.get("number") == "22"][0]
n = len(slots)
y = 120 if n == 1 else ((60 if idx == 0 else 180) if n == 2 else [40, 120, 200][idx])
bc.cmd(f"Y {now()} 120 {y}")
head, _, _ = bview()
check("screen=2" in head, f"menu open: {head}")
check("feedback=0" in head, f"no 'sent...' left over once CRG confirmed: {head}")
bc.cmd(f"Y {now()} 120 110")  # Release
time.sleep(0.6)
head, slots, _ = bview()
check(not any(s.get("number") == "22" for s in slots), f"#22 released: {slots}")
bc.stop()

print("\n== latency (last confirmed press):", jm.s()["latency"], "ms")
for d in (jt, bx, jm):
    check(d.drops == 0, "no dead-connection drops")
    d.stop()
op.close()
print("\n" + ("ALL LIVE CHECKS PASSED" if not fails else f"{len(fails)} FAILED: {fails}"))
sys.exit(1 if fails else 0)
