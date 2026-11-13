"""Writes manifest.json (ESP Web Tools) and latest.json (the timer's update
check) next to the built firmware. Usage: make_release.py VERSION OUTDIR"""
import hashlib, json, os, sys

version, out = sys.argv[1], sys.argv[2]
app = os.path.join(out, "firmware.bin")
digest = hashlib.sha256(open(app, "rb").read()).hexdigest()
json.dump({
    "name": "CLOCK", "version": version, "new_install_prompt_erase": True,
    "builds": [{"chipFamily": "ESP32-C6", "parts": [{"path": "firmware.factory.bin", "offset": 0}]}],
}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
json.dump({"version": version, "app": "firmware.bin", "size": os.path.getsize(app), "sha256": digest},
          open(os.path.join(out, "latest.json"), "w"), indent=1)
print(version, digest)
