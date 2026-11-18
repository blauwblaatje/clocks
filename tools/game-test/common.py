"""Shared by the game tests: a throwaway Java CRG, the script's own scoreboard
connection (it plays lineup tracker, penalty tracker and, when needed,
the scoreboard operator), simulated CLOCKs (sim --ctl) and a TCP proxy per
CLOCK to break its connection on purpose.
"""
import json, os, re, select, shutil, socket, subprocess, sys, threading, time
import websocket

G = "ScoreBoard.CurrentGame."
S = "ScoreBoard.Settings.Setting(ScoreBoard."
results = []
OUT = "."


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print(("PASS " if ok else "FAIL ") + name + (f"  [{detail}]" if detail and not ok else ""), flush=True)
    return ok


def note(text):
    print("   " + text, flush=True)


def wait_for(fn, timeout=3.0, step=0.05):
    end = time.time() + timeout
    while True:
        try:
            v = fn()
        except Exception:
            v = None
        if v or time.time() > end:
            return v
        time.sleep(step)


def timed(fn, timeout, step=0.1):
    """Seconds until fn() is true, None if it never was."""
    t0 = time.time()
    return round(time.time() - t0, 1) if wait_for(fn, timeout, step) else None


def summary():
    fails = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(fails)}/{len(results)} checks passed")
    for n, _, d in fails:
        print("FAIL", n, d)
    return 1 if fails else 0


# ---------------- a throwaway CRG ----------------

class Server:
    """java CRG from a checkout (lib/deps) and its compiled classes."""

    def __init__(self, checkout, classes, port, log):
        self.checkout, self.classes, self.port, self.logf = checkout, classes, port, log
        self.p = None

    def start(self):
        self.p = subprocess.Popen(["java", "-cp", f"{self.classes}:lib/deps/*", "com.carolinarollergirls.scoreboard.Main",
                                   "--nogui", f"--port={self.port}"], cwd=self.checkout,
                                  stdout=open(self.logf, "a"), stderr=subprocess.STDOUT)
        ok = wait_for(self.answers, 60, 0.3)
        assert ok, "CRG did not start"

    def answers(self):
        try:
            ws = websocket.create_connection(f"ws://localhost:{self.port}/WS/?source=probe&platform=script",
                                             suppress_origin=True, timeout=2)
            ws.close()
            return True
        except Exception:
            return False

    def stop(self):
        if self.p:
            self.p.terminate()
            try:
                self.p.wait(15)
            except subprocess.TimeoutExpired:
                self.p.kill()
            self.p = None


# ---------------- the script's own scoreboard connection ----------------

class SB:
    def __init__(self, port):
        self.port = port
        self.state, self.lock = {}, threading.Lock()
        self.connect()

    def connect(self):
        self.ws = websocket.create_connection(f"ws://localhost:{self.port}/WS/?source=gametest&platform=script",
                                              suppress_origin=True, timeout=None)
        with self.lock:
            self.state = {}
        threading.Thread(target=self._read, args=(self.ws,), daemon=True).start()
        self.ws.send(json.dumps({"action": "Register", "paths": ["ScoreBoard.Settings", "ScoreBoard.Game",
                                                                  "ScoreBoard.CurrentGame", "ScoreBoard.Clients"]}))
        time.sleep(1.5)

    def _read(self, ws):
        while True:
            try:
                m = json.loads(ws.recv())
            except Exception:
                return
            with self.lock:
                for k, v in m.get("state", {}).items():
                    if v is None:
                        self.state.pop(k, None)
                    else:
                        self.state[k] = v

    def set(self, key, value=True, flag="", wait=0.3):
        self.ws.send(json.dumps({"action": "Set", "key": key, "value": value, "flag": flag}))
        time.sleep(wait)

    def g(self, k, d=None):
        with self.lock:
            return self.state.get(k, d)

    def keys(self, prefix):
        with self.lock:
            return {k: v for k, v in self.state.items() if k.startswith(prefix)}


sb = None


def gg(k, d=None):
    return sb.g(G + k, d)


def skater(t, number):
    for k, v in sb.keys(G + f"Team({t}).Skater(").items():
        if k.endswith(".RosterNumber") and v == number:
            return k.split("Skater(")[1].split(")")[0]


def pos(t, p):
    return G + f"Team({t}).Position({p})."


def trips():
    out = {}
    for k, v in sb.keys(G).items():
        m = re.match(r"((?:Team\(\d\)\.)?BoxTrip\([^)]+\))\.(.*)$", k[len(G):])
        if m and "Fielding(" not in m.group(2) and "Penalty(" not in m.group(2):
            out.setdefault(m.group(1), {})[m.group(2)] = v
    return out


def trip_path(team, number=None, jammer=None, current=True):
    for p, t in trips().items():
        if not p.startswith(f"Team({team})"):
            continue
        if current and not t.get("IsCurrent"):
            continue
        if number is not None and t.get("RosterNumber") != number:
            continue
        if jammer is not None and bool(t.get("Jammer")) != jammer:
            continue
        return p
    return None


def trip(team, number=None, jammer=None, current=True):
    p = trip_path(team, number, jammer, current)
    return trips()[p] if p else None


def current_trips(team):
    return [t for p, t in trips().items() if p.startswith(f"Team({team})") and t.get("IsCurrent")]


def penalties(t, number):
    sid = skater(t, number)
    pre = G + f"Team({t}).Skater({sid}).Penalty("
    out = {}
    for k, v in sb.keys(pre).items():
        n, f = k[len(pre):].split(").", 1)
        out.setdefault(int(n), {})[f] = v
    return out


def in_box(t, p):
    return sb.g(pos(t, p) + "PenaltyBox")


def lineup(t, jammer, b1, b2, b3, pivot=None):
    for p, n in (("Jammer", jammer), ("Pivot", pivot), ("Blocker1", b1), ("Blocker2", b2), ("Blocker3", b3)):
        sb.set(pos(t, p) + "Skater", skater(t, n) if n else "", wait=0.1)


def fast_forward(path, left_ms):
    """Set a box trip's clock so that left_ms remain."""
    ct = gg(path + ".Clock.Time")
    sb.set(G + path + ".Clock.Time", -(ct - left_ms), "change", wait=1.0)


def clients_by_source():
    """{source: [client ids]} of the CLOCKs connected now."""
    out = {}
    for k, v in sb.keys("ScoreBoard.Clients.Device(").items():
        m = re.search(r"Device\(([^)]+)\)\.Client\(([^)]+)\)\.Source$", k)
        if m and str(v).startswith(("jamtimer", "boxtimer")):
            out.setdefault(v, []).append((m.group(1), m.group(2)))
    return out


def setup_game(port, prefix="g4"):
    """UseLT + UsePBT, two prepared teams with rosters 10-19, a new current game."""
    global sb
    sb = SB(port)
    sb.set(S + "Penalties.UseLT)", "true")
    sb.set(S + "Penalties.UsePBT)", "true")
    for i, name in enumerate(["Red", "Blue"]):
        P = f"ScoreBoard.PreparedTeam({prefix}t{i})"
        sb.set(P + ".Id", f"{prefix}t{i}", wait=0.1)
        sb.set(P + ".TeamName", f"{name} {time.strftime('%H%M')}", wait=0.1)
        for s in range(10):
            sb.set(P + f".Skater({prefix}t{i}s{s}).Name", f"{name} {s}", wait=0.03)
            sb.set(P + f".Skater({prefix}t{i}s{s}).RosterNumber", str(10 + s), wait=0.03)
    gid = prefix + "-" + time.strftime("%H%M%S")
    sb.set(f"ScoreBoard.Game({gid}).Id", gid, wait=0.3)
    sb.set(f"ScoreBoard.Game({gid}).Team(1).PreparedTeam", f"{prefix}t0", wait=0.3)
    sb.set(f"ScoreBoard.Game({gid}).Team(2).PreparedTeam", f"{prefix}t1", wait=0.5)
    sb.set(G + "Game", gid, wait=2.5)
    check("setup: game is current, UsePBT on", gg("Game") == gid and sb.g(S + "Penalties.UsePBT)") == "true")
    return gid


# ---------------- a TCP proxy per CLOCK ----------------

class Proxy:
    """Between one CLOCK and the scoreboard. Modes:
    pass       forward both ways
    blackhole  sockets stay open, every byte is dropped (Wi-Fi gone, no FIN/RST)
    down()     the port refuses connections (scoreboard unreachable); up() undoes it
    cut()      closes the open connections (as a scoreboard restart or a reset)
    """

    def __init__(self, target_port):
        self.target = target_port
        self.mode = "pass"
        self.pairs, self.lock = [], threading.Lock()
        self.ls = None
        self.port = 0
        self.up()

    def up(self):
        if self.ls:
            return
        ls = socket.socket()
        ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ls.bind(("127.0.0.1", self.port))
        ls.listen(8)
        self.port = ls.getsockname()[1]
        self.ls = ls
        threading.Thread(target=self._accept, args=(ls,), daemon=True).start()

    def down(self):
        ls, self.ls = self.ls, None
        if ls:
            try:
                ls.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            ls.close()

    def _accept(self, ls):
        while True:
            try:
                c, _ = ls.accept()
            except OSError:
                return
            try:
                u = socket.create_connection(("127.0.0.1", self.target), timeout=2)
                u.settimeout(None)
            except OSError:
                c.close()
                continue
            with self.lock:
                self.pairs.append((c, u))
            threading.Thread(target=self._pump, args=(c, u), daemon=True).start()

    def _pump(self, c, u):
        socks = [c, u]
        try:
            while True:
                r, _, _ = select.select(socks, [], [], 0.5)
                for s in r:
                    d = s.recv(65536)
                    if not d:
                        raise EOFError
                    if self.mode == "pass":
                        (u if s is c else c).sendall(d)
        except (OSError, EOFError):
            pass
        for s in socks:
            try:
                s.close()
            except OSError:
                pass
        with self.lock:
            if (c, u) in self.pairs:
                self.pairs.remove((c, u))

    def cut(self):
        with self.lock:
            pairs = list(self.pairs)
        for c, u in pairs:
            for s in (c, u):
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

    def open_count(self):
        with self.lock:
            return len(self.pairs)


# ---------------- simulated CLOCKs ----------------

# Which board the simulated CLOCKs stand in for: CLOCK_SIM_BOARD=stopwatch
# runs every test on the M5Stack StopWatch's 466 x 466 screens (touch
# positions stay in design pixels, sim --ctl scales them).
BOARD = os.environ.get("CLOCK_SIM_BOARD", "")


class Clock:
    def __init__(self, sim, role, name, port, out, env=None, home=None):
        self.sim, self.role, self.name, self.port, self.env = sim, role, name, port, env or {}
        self.home = home or os.path.join(out, "home-" + role)
        os.makedirs(os.path.join(self.home, ".cache"), exist_ok=True)  # cookie file
        self.log = []
        self.start()

    def start(self):
        env = dict(os.environ, HOME=self.home, **self.env)
        args = [self.sim, "--ctl", "--role", self.role] + (["--crg", f"localhost:{self.port}"] if self.port else [])
        if BOARD:
            args += ["--board", BOARD]
        self.p = subprocess.Popen(args,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env, bufsize=1)
        line = self.p.stdout.readline()
        assert line.startswith("READY"), line

    def cmd(self, c):
        self.p.stdin.write(c + "\n")
        self.p.stdin.flush()
        out = []
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError(f"{self.name} exited")
            line = line.rstrip("\n")
            if line == "OK":
                return out
            if line.startswith(("TEXT ", "ERR ")):
                out.append(line)
            else:
                self.log.append(f"{time.strftime('%H:%M:%S')} {line}")

    def text(self):
        for l in self.cmd("text"):
            if l.startswith("TEXT "):
                return l[5:]
        return ""

    def has(self, *words, timeout=3.0):
        return wait_for(lambda: all(w in self.text() for w in words), timeout)

    def press(self, b, hold=0.08):
        self.cmd("down " + b)
        time.sleep(hold)
        self.cmd("up " + b)

    def touch(self, x, y):
        self.cmd(f"touch {x} {y}")

    def shot(self, path):
        self.cmd("shot " + path)

    def cookie(self):
        try:
            return open(os.path.join(self.home, ".cache", f"crg-sim-{self.role}.cookie")).read().strip()
        except OSError:
            return ""

    def logged(self, what):
        return sum(what in l for l in self.log)

    def stop(self):
        try:
            self.p.stdin.close()
            self.p.wait(3)
        except Exception:
            self.p.kill()
        with open(os.path.join(OUT, "log-" + os.path.basename(self.home)[5:] + ".txt"), "w") as f:
            f.write("\n".join(self.log) + "\n")


def online(c):
    t = c.text().lower()
    return not any(w in t for w in ("standalone", "connecting", "no scoreboard", "no conn", "crg back"))


def back(c):
    """Running locally and the scoreboard answers again (the quiet word)."""
    return "crg back" in c.text().lower()


YES, NO = (78, 154), (162, 154)  # "Swap sides?" and "Rejoin scoreboard?" buttons


def no_conn(c):
    return "NO CONN" in c.text()


try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = None

_step = [0]


def shots(clocks, label):
    """All screens side by side, one file per step."""
    _step[0] += 1
    files = []
    for c in clocks:
        f = os.path.join(OUT, f"_{c.role}.ppm")
        c.shot(f)
        files.append(f)
    base = os.path.join(OUT, f"{_step[0]:02d}-{label}")
    if Image:
        sheet = Image.new("RGB", (len(clocks) * 250 + 10, 270), (40, 40, 40))
        d = ImageDraw.Draw(sheet)
        for i, (c, f) in enumerate(zip(clocks, files)):
            sheet.paste(Image.open(f), (10 + i * 250, 25))
            d.text((10 + i * 250, 6), c.name, fill=(220, 220, 220))
        sheet.save(base + ".png")
    else:
        for c, f in zip(clocks, files):
            shutil.copy(f, f"{base}-{c.role}.ppm")


def texts(clocks):
    for c in clocks:
        print(f"   {c.name:11} {c.text()}", flush=True)


# PBT picker (boxctl pickHit): pivot, blocker 1, 2, 3
PICK = {"Pivot": (80, 93), "Blocker1": (160, 93), "Blocker2": (80, 148), "Blocker3": (160, 148)}
# Box clock menu (boxctl menuHit)
MENU = {"release": (120, 111), "penalty": (120, 153), "change": (85, 191), "cancel": (154, 191)}
# Clock rows: 1 clock fills the screen, 2: top/bottom, 3: thirds
ROW = {1: [(120, 120)], 2: [(120, 80), (120, 160)], 3: [(120, 50), (120, 120), (120, 200)]}
# JT timeout type picker (jtctl pickerHit)
TO = {"T1 TO": (70, 70), "T1 OR": (70, 170), "T2 TO": (170, 70), "T2 OR": (170, 170), "OTO": (120, 120)}
