"""Firmware update end to end in the simulator (about 1 minute).

    python3 update.py SIM FIRMWARE_BIN OUTDIR [OTHER_BOARD_BIN]

FIRMWARE_BIN is a real build for the board the simulator stands in for
(firmware/.pio/build/c6/firmware.bin; with CLOCK_SIM_BOARD=stopwatch
firmware/.pio/build/stopwatch/firmware.bin), so the image header and the
board marker checks see a real image. OTHER_BOARD_BIN, the other board's
build, adds the checks that it is refused. The script serves release files
the way the install site does (web-installer/make_release.py) and runs a
simulated CLOCK through the menu's Update and the setup page upload. The
simulator's "slot" is ~/.cache/crg-sim-update.bin in its HOME; "restart"
makes it exit.
"""
import hashlib, http.server, json, os, re, shutil, sys, threading, time, urllib.request, urllib.error
import common as C
from common import check, note, wait_for

SIM, FW, OUT = sys.argv[1:4]
OTHER = sys.argv[4] if len(sys.argv) > 4 else None
os.makedirs(OUT, exist_ok=True)
C.OUT = OUT
fw = open(FW, "rb").read()
sha = hashlib.sha256(fw).hexdigest()
other = open(OTHER, "rb").read() if OTHER else None
BOARD_ID = "m5-stopwatch" if C.BOARD in ("stopwatch", "m5-stopwatch") else "waveshare-c6"


def latest(version, data=fw, digest=None, app="firmware.bin"):
    return json.dumps({"version": version, "app": app, "size": len(data),
                       "sha256": digest or hashlib.sha256(data).hexdigest()}).encode()


# path -> (body, bytes per 50 ms or None)
FILES = {
    "/ok/latest.json": (latest("0.2.0"), None),
    "/ok/firmware.bin": (fw, None),
    "/same/latest.json": (latest("0.1.0"), None),
    "/bad/latest.json": (latest("0.2.0", digest="0" * 64), None),
    "/bad/firmware.bin": (fw, None),
    "/slow/latest.json": (latest("0.2.0"), None),
    "/slow/firmware.bin": (fw, 16384),
    "/junk/latest.json": (b"<html>not json</html>", None),
    # the install site's layout: the device adds <board>/latest.json
    f"/site/{BOARD_ID}/latest.json": (latest("0.2.0"), None),
    f"/site/{BOARD_ID}/firmware.bin": (fw, None),
}
if other:
    FILES["/other/latest.json"] = (latest("0.2.0", data=other), None)
    FILES["/other/firmware.bin"] = (other, None)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        f = FILES.get(self.path)
        if not f:
            self.send_error(404)
            return
        body, rate = f
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            if rate is None:
                self.wfile.write(body)
            else:
                for i in range(0, len(body), rate):
                    self.wfile.write(body[i:i + rate])
                    time.sleep(0.05)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
BASE = f"http://127.0.0.1:{srv.server_address[1]}"

CONNECTION, UPDATE_ROW, SETUP_ROW = (60, 170), (120, 175), (120, 107)
INSTALL, CANCEL = (78, 180), (162, 180)
n_shot = [0]


def shot(c, label):
    n_shot[0] += 1
    f = os.path.join(OUT, f"_{c.role}.ppm")
    c.shot(f)
    if C.Image:
        C.Image.open(f).save(os.path.join(OUT, f"{n_shot[0]:02d}-{label}.png"))


def device(name, url, **env):
    """A fresh standalone jam timer with this release address."""
    home = os.path.join(OUT, "home-" + name)
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(os.path.join(home, ".cache"))
    with open(os.path.join(home, ".cache", "crg-sim-settings.txt"), "w") as f:
        f.write(f"version=1\nrole=jt\nupdateUrl={url}\n")
    return C.Clock(SIM, "jt", name, None, OUT, env=dict({"CLOCK_SIM_VERSION": "0.1.0"}, **env), home=home)


def setup_port(c):
    """The simulator prints its setup page address."""
    for _ in range(40):
        c.text()
        for l in c.log:
            m = re.search(r"setup page: http://localhost:(\d+)/", l)
            if m:
                return m.group(1)
        time.sleep(0.05)
    return None


def open_menu_item(c, row):
    c.press("left", hold=3.3)  # 3 s hold: the device menu (no clock runs)
    wait_for(lambda: "Connection" in c.text(), 2)
    c.touch(*CONNECTION)
    wait_for(lambda: "Update" in c.text(), 2)
    c.touch(*row)


def exited(c, timeout):
    return wait_for(lambda: c.p.poll() is not None, timeout)


def slot(c, ext=".bin"):
    p = os.path.join(c.home, ".cache", "crg-sim-update" + ext)
    return open(p, "rb").read() if os.path.exists(p) else None


def finish(c):
    try:
        c.stop()
    except Exception:
        pass


try:
    print("\n== 1. same version: up to date")
    c = device("same", BASE + "/same/latest.json")
    open_menu_item(c, UPDATE_ROW)
    check("shows 'Up to date'", c.has("Up to date", timeout=5), c.text())
    shot(c, "up-to-date")
    c.press("crown")
    check("any button closes it", wait_for(lambda: "Up to date" not in c.text(), 2), c.text())
    finish(c)

    print("\n== 2. newer version: offered, installed, restart")
    c = device("ok", BASE + "/ok/latest.json")
    open_menu_item(c, UPDATE_ROW)
    check("offers 0.2.0", c.has("Update to 0.2.0?", timeout=5), c.text())
    note(c.text())
    shot(c, "offer")
    c.touch(*INSTALL)
    seen = wait_for(lambda: re.search(r"Updating \| (\d+) %", c.text()), 5)
    check("shows progress", seen, c.text())
    if seen:
        shot(c, "downloading")
    check("installs and restarts (the simulator exits)", exited(c, 20))
    check("the slot holds exactly firmware.bin", slot(c) == fw, f"{len(slot(c) or b'')} bytes")
    check("no partial file left", slot(c, ".part") is None)
    finish(c)

    print("\n== 3. damaged download (sha256 does not match)")
    c = device("bad", BASE + "/bad/latest.json")
    open_menu_item(c, UPDATE_ROW)
    c.has("Update to 0.2.0?", timeout=5)
    c.touch(*INSTALL)
    check("fails with 'Checksum wrong'", c.has("Update failed", "Checksum wrong", timeout=15), c.text())
    shot(c, "checksum-wrong")
    check("nothing installed, nothing left over", slot(c) is None and slot(c, ".part") is None)
    check("the device keeps running", c.p.poll() is None)
    finish(c)

    print("\n== 4. cancel during a slow download")
    c = device("slow", BASE + "/slow/latest.json")
    open_menu_item(c, UPDATE_ROW)
    c.has("Update to 0.2.0?", timeout=5)
    c.touch(*INSTALL)
    check("download running", wait_for(lambda: re.search(r"Updating \| [1-9]\d* %", c.text()), 10), c.text())
    shot(c, "slow-download")
    c.press("crown")
    check("a button cancels it", wait_for(lambda: "Updating" not in c.text(), 2), c.text())
    time.sleep(0.5)
    check("nothing installed, nothing left over", slot(c) is None and slot(c, ".part") is None)
    check("the screen shows the jam timer again", "READY" in c.text(), c.text())
    finish(c)

    print("\n== 5. low battery without USB: refused, the offer stays")
    c = device("battery", BASE + "/ok/latest.json", CLOCK_SIM_BATTERY="12")
    open_menu_item(c, UPDATE_ROW)
    c.has("Update to 0.2.0?", timeout=5)
    c.touch(*INSTALL)
    check("refused with the reason", c.has("Battery 12 %: charge or connect USB first", timeout=2), c.text())
    check("still offered", "Install" in c.text() and "Update to 0.2.0?" in c.text(), c.text())
    shot(c, "battery-low")
    finish(c)
    c = device("battery-usb", BASE + "/ok/latest.json", CLOCK_SIM_BATTERY="12", CLOCK_SIM_USB="1")
    open_menu_item(c, UPDATE_ROW)
    c.has("Update to 0.2.0?", timeout=5)
    c.touch(*INSTALL)
    check("low battery on USB power: installs", exited(c, 20) and slot(c) == fw)
    finish(c)

    print("\n== 6. release address problems")
    for name, url, want in (("missing", BASE + "/nope/latest.json", "server answered 404"),
                            ("junk", BASE + "/junk/latest.json", "not valid JSON"),
                            ("refused", "http://127.0.0.1:9/latest.json", "Check failed")):
        c = device(name, url)
        open_menu_item(c, UPDATE_ROW)
        check(f"{name}: '{want}'", c.has("Update failed", want, timeout=8), c.text())
        if name == "missing":
            shot(c, "check-404")
        finish(c)

    print("\n== 6b. the install site as the address: this board's folder")
    c = device("site", BASE + "/site/")
    open_menu_item(c, UPDATE_ROW)
    check(f"offers 0.2.0 from /site/{BOARD_ID}/latest.json", c.has("Update to 0.2.0?", timeout=5), c.text())
    c.touch(*INSTALL)
    check("installs and restarts", exited(c, 20) and slot(c) == fw)
    finish(c)

    if other:
        print("\n== 6c. the other board's firmware: refused")
        c = device("other", BASE + "/other/latest.json")
        open_menu_item(c, UPDATE_ROW)
        c.has("Update to 0.2.0?", timeout=5)
        c.touch(*INSTALL)
        check("fails with 'This firmware is for the ...'", c.has("Update failed", "This firmware is for the", timeout=15),
              c.text())
        shot(c, "other-board")
        check("nothing installed, nothing left over", slot(c) is None and slot(c, ".part") is None)
        finish(c)

    print("\n== 7. setup page upload")
    c = device("upload", "")
    open_menu_item(c, SETUP_ROW)
    port = setup_port(c)
    check("setup page open", port, " / ".join(c.log))
    page = urllib.request.urlopen(f"http://localhost:{port}/").read().decode()
    check("page shows the running version and the upload", "Running: 0.1.0" in page and "/update" in page)

    def post(data):
        r = urllib.request.Request(f"http://localhost:{port}/update", data=data,
                                   headers={"Content-Type": "application/octet-stream"})
        try:
            with urllib.request.urlopen(r, timeout=30) as resp:
                return resp.status, resp.read().decode()
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode()
        except Exception as e:
            return 0, str(e)

    res = {}
    t = threading.Thread(target=lambda: res.update(r=post(b"\x89PNG\r\n\x1a\n" + b"x" * 5000)))
    t.start()
    while t.is_alive():
        c.text()  # keeps the simulator's loop answering
        time.sleep(0.05)
    check("a wrong file is refused (400, 'not a firmware file')", res["r"][0] == 400 and "not a firmware file" in res["r"][1],
          str(res["r"]))
    check("the device says so", c.has("Update failed", "not a firmware file", timeout=2), c.text())
    shot(c, "upload-wrong-file")
    c.press("crown")  # closes the update screen; setup stays open
    if other:
        res.clear()
        t = threading.Thread(target=lambda: res.update(r=post(other)))
        t.start()
        while t.is_alive():
            c.text()
            time.sleep(0.05)
        check("the other board's firmware.bin is refused (400)", res["r"][0] == 400 and "This firmware is for the" in res["r"][1],
              str(res["r"]))
        c.has("Update failed", timeout=2)
        c.press("crown")
    res.clear()
    t = threading.Thread(target=lambda: res.update(r=post(fw)))
    t.start()
    t0 = time.time()
    got_progress = False
    while t.is_alive() and time.time() - t0 < 30:
        try:
            if "Receiving update" in c.text() and not got_progress:
                got_progress = True
                shot(c, "upload-receiving")
        except RuntimeError:
            break
        time.sleep(0.05)
    t.join(30)
    check("firmware.bin upload answered 200 'Installed'", res.get("r", (0,))[0] == 200 and "Installed" in res["r"][1],
          str(res.get("r")))
    check("the device restarts", exited(c, 5))
    check("the slot holds exactly firmware.bin", slot(c) == fw)
    note(f"progress shown on the device: {got_progress}")
    finish(c)
finally:
    srv.shutdown()

sys.exit(C.summary())
