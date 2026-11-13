"""Top-down views of the inside of both shells, coloured by height. Run after case.py."""
import os
import numpy as np
import trimesh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
fig, axes = plt.subplots(1, 2, figsize=(14, 7.4))
for ax, (name, title, flip) in zip(axes, (("front", "Front shell, inside", False),
                                           ("back", "Back shell, inside (x-ray, from the back)", True))):
    m = trimesh.load(os.path.join(out, f"jamtimer_{name}.stl"))
    v = m.vertices.copy()
    if flip:  # the back STL is stored flipped for printing; show its inside from the front
        v[:, 1] = -v[:, 1]
    tri = v[m.faces]
    up = m.face_normals[:, 2] > 0.3          # faces looking at the viewer
    tri, n = tri[up], m.face_normals[up]
    z = tri[:, :, 2].mean(axis=1)
    order = np.argsort(z)                    # paint low first, high on top
    tri, n, z = tri[order], n[order], z[order]
    zn = (z - z.min()) / max(1e-6, z.max() - z.min())
    light = np.clip(n @ np.array([0.3, 0.4, 0.86]), 0, 1)
    col = plt.cm.viridis(zn)[:, :3] * (0.55 + 0.45 * light[:, None])
    ax.add_collection(PolyCollection(tri[:, :, :2], facecolors=col, edgecolors="none"))
    ax.set_xlim(-36, 36); ax.set_ylim(-40, 36); ax.set_aspect("equal"); ax.set_title(title)
    ax.set_xticks([]); ax.set_yticks([])
    # model X to the right: seen from the back, so 3 o'clock (USB, -X) is on the left
    for txt, (x, y) in (("12 crown", (0, 33)), ("USB\n3 o'clock", (-31.5, 6)), ("9 o'clock", (31.5, 6))):
        ax.text(x, y, txt, ha="center", va="center", fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(out, "inside_top.png"), dpi=100)
print("wrote out/inside_top.png")
