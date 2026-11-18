"""Writes the release files next to the built firmware.

Usage: make_release.py VERSION OUTDIR
OUTDIR holds a folder per board (BOARDS below) with firmware.bin and
firmware.factory.bin. Written:
  OUTDIR/<board>/latest.json   what that board's devices check (firmware/lib/update)
  OUTDIR/manifest.json         ESP Web Tools: one install button, the build
                               picked by the chip it finds
  OUTDIR/latest.json, OUTDIR/firmware.bin
                               the Waveshare's, where 0.1.0 devices look
"""
import hashlib, json, os, shutil, sys

# board id (as in firmware/lib/update, upd::Board) -> ESP Web Tools chip family
BOARDS = {"waveshare-c6": "ESP32-C6", "m5-stopwatch": "ESP32-S3"}
LEGACY = "waveshare-c6"

version, out = sys.argv[1], sys.argv[2]
builds = []
for board, chip in BOARDS.items():
    app = os.path.join(out, board, "firmware.bin")
    if not os.path.exists(app):
        sys.exit(f"missing {app}")
    data = open(app, "rb").read()
    if f"CLOCK-BOARD={board};".encode() not in data:
        sys.exit(f"{app} has no CLOCK-BOARD={board}; marker")
    digest = hashlib.sha256(data).hexdigest()
    json.dump({"version": version, "app": "firmware.bin", "size": len(data), "sha256": digest},
              open(os.path.join(out, board, "latest.json"), "w"), indent=1)
    builds.append({"chipFamily": chip, "parts": [{"path": f"{board}/firmware.factory.bin", "offset": 0}]})
    print(version, board, digest)
json.dump({"name": "CLOCK", "version": version, "new_install_prompt_erase": True, "builds": builds},
          open(os.path.join(out, "manifest.json"), "w"), indent=1)
# 0.1.0 checks https://clock.quagga.rocks/latest.json and a firmware.bin next to it
shutil.copy(os.path.join(out, LEGACY, "firmware.bin"), os.path.join(out, "firmware.bin"))
shutil.copy(os.path.join(out, LEGACY, "latest.json"), os.path.join(out, "latest.json"))
