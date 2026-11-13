"""Sanity checks and preview renders for the case. Run after case.py."""
import os
import numpy as np
import trimesh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import cadquery as cq
import case as c

front, back = c.split(c.drill(c.build_body()))
# the USB opening must pass through the wall at the port position
probe = c.usb_rot(c.box(-2, 2, -c.RO - 1, -c.RI + 1, c.USB_Z - 1, c.USB_Z + 1))
print("material left in USB opening (mm3):", round(probe.intersect(front.union(back)).val().Volume(), 3))
plug = c.usb_rot(c.box(-4.5, 4.5, -c.RO - 15, -c.USB_TAB_END, c.USB_Z - 1.6, c.USB_Z + 1.6))
print("plug path vs case (mm3):", round(plug.intersect(front.union(back)).val().Volume(), 3))
print("front/back overlap volume (mm3):", round(front.intersect(back).val().Volume(), 3))

# stand-ins for the parts that go inside
board = c.disc(c.BOARD_R, c.FACE, c.FACE + c.STACK).union(
    c.usb_rot(c.box(-4.96, 4.96, -c.USB_TAB_END, 0, c.FACE, c.FACE + c.STACK)))
bz = c.SPLIT + 0.5
batt = c.box(-15, 15, -20 + c.BATT_Y, 20 + c.BATT_Y, bz, bz + c.BATT[2])
for n, part in (("board", board), ("battery", batt)):
    for sn, shell in (("front", front), ("back", back)):
        print(f"{n} vs {sn} shell overlap (mm3):", round(part.intersect(shell).val().Volume(), 3))

# v1: switches, motor and their holders
ok = True
def vol(a, b):
    try:
        return round(a.intersect(b).val().Volume(), 3)
    except Exception:
        return 0.0
switches = [(90, "crown")] + [(a, "10 o'clock" if a == 30 else "2 o'clock") for a in c.SIDE_ANGLES]
for ang, name in switches:
    sw = c.switch_part(ang)
    for sn, shell in (("front", front), ("back", back)):
        v = vol(sw, shell)
        ok &= v == 0
        print(f"switch {name} vs {sn} shell (mm3):", v)
    for n, part in (("board", board), ("battery", batt)):
        v = vol(sw, part)
        ok &= v == 0
        print(f"switch {name} vs {n} (mm3):", v)
m = c.motor_part()
for sn, shell in (("front", front), ("back", back)):
    v = vol(m, shell)
    ok &= v == 0
    print(f"motor vs {sn} shell (mm3):", v)
for n, part in (("battery", batt), ("board", board)):
    v = vol(m, part)
    ok &= v == 0
    print(f"motor vs {n} (mm3):", v)
# the switch must drop into the front half from the split plane: nothing of
# the front shell above it
for ang, name in switches:
    sw = c.switch_part(ang)
    lift = sw.translate((0, 0, 7))   # pulled straight out along +Z
    sweep = sw.union(lift)
    for z in (1.5, 3, 4.5, 6):
        sweep = sweep.union(sw.translate((0, 0, z)))
    v = vol(sweep, front)
    ok &= v == 0
    print(f"switch {name} can be dropped into the front shell (blocked mm3):", v)
print("v1 checks:", "OK" if ok else "PROBLEM")

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
fig = plt.figure(figsize=(12, 6))
views = [("front", (35, -60), "Front shell, inside"), ("back", (35, -60), "Back shell, inside")]
for i, (name, (el, az), title) in enumerate(views):
    m = trimesh.load(os.path.join(out, f"jamtimer_{name}.stl"))
    print(name, "watertight:", m.is_watertight, "faces:", len(m.faces))
    ax = fig.add_subplot(1, 2, i + 1, projection="3d")
    tri = m.vertices[m.faces]
    light = np.array([0.3, -0.5, 0.8]); light /= np.linalg.norm(light)
    shade = 0.35 + 0.65 * np.clip(m.face_normals @ light, 0, 1)
    col = np.outer(shade, [0.45, 0.6, 0.8])
    ax.add_collection3d(Poly3DCollection(tri, facecolors=col, edgecolors="none"))
    lim = 36
    ax.set_xlim(-lim, lim); ax.set_ylim(-lim, lim); ax.set_zlim(-lim + 10, lim + 10)
    ax.set_box_aspect((1, 1, 1)); ax.view_init(el, az); ax.set_axis_off(); ax.set_title(title)
fig.tight_layout()
fig.savefig(os.path.join(out, "preview.png"), dpi=110)
