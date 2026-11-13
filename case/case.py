"""Parametric stopwatch case for the CRG jam timer, v1 (fit test).

Base board: Waveshare ESP32-C6-Touch-LCD-1.28. Battery: 803040 LiPo.
Run:  python3 case.py   -> writes STL and STEP files into ./out

Coordinates: Z=0 is the front face (screen side), +Z goes to the back.
+Y is 12 o'clock (crown). Seen from the front, +X is 9 o'clock and -X is
3 o'clock. USB-C sits at 3 o'clock (see USB_ROT). Angles are math angles
in the model: 90 = crown, and seen from the front 150 = 2 o'clock,
30 = 10 o'clock, 180 = 3 o'clock (USB), 0 = 9 o'clock.
(v0's comment had 30 and 150 the wrong way round; the geometry was right.)
"""
import math
import os
import cadquery as cq

# ---- measured / published -------------------------------------------------
BOARD_R = 18.75        # Waveshare drawing
USB_TAB_END = 39.49 - BOARD_R  # distance from centre to the USB end of the board
SHOULDER_W = 25.28     # board width where the USB tab starts
WINDOW_D = 33.0        # visible screen, taken from Waveshare's own housing
BATT = (30.0, 40.0, 8.0)  # 803040 cell: width, height, thickness
BATT_Y = -1.2          # v1: battery moved towards 6 o'clock, clear of the crown switch

# ---- estimates, check on the first print ----------------------------------
STACK = 8.5            # glass front to tallest part on the back of the board
USB_Z = 8.3            # centre of the USB-C connector, measured from Z=0
FIT = 0.3              # clearance around board and battery

# ---- case -----------------------------------------------------------------
WALL = 2.5
FACE = 1.5             # front and back wall thickness
RI = 26.5              # inner radius, set by the battery corners (25.0)
RO = RI + WALL
SPLIT = FACE + STACK   # front shell is Z 0..SPLIT
DEPTH = SPLIT + 0.5 + BATT[2] + 0.5 + FACE
EDGE = 2.0             # outer edge rounding

SCREW_ANGLES = (70, 110, 220, 320)
SCREW_R = 24.4          # column overlaps the wall by 0.4 so they fuse cleanly
BOSS_D = 5.0
PILOT_D = 1.7          # M2 self-tapping screw into the front shell
CLEAR_D = 2.3
HEAD_D, HEAD_DEPTH = 4.4, 2.2

CROWN = dict(ang=90, shaft=6.0, flange=9.0, head=9.6, head_len=4.2)
SIDE = dict(shaft=4.0, flange=7.0, head=7.0, head_len=3.0)
SIDE_ANGLES = (30, 150)
HOLE_FIT = 0.4
FLANGE_T = 1.2
COLLAR_OD, COLLAR_LEN = 13.0, 3.5

USB_W, USB_H = 13.0, 7.5   # opening large enough for a plug's overmould
# Board rotation about Z. USB features are modelled at 6 o'clock and turned by
# this angle: 0 = port at 6 o'clock, -90 = port at 3 o'clock seen from the
# front. At 3 o'clock the chip antenna lands at about 1 to 1:30, above the
# palm (antenna position estimated from Waveshare's photos, not measured).
# Only 90 degree steps: the display can only be rotated in quarter turns.
USB_ROT = -90
LUG = dict(ang=270, w=12.0, t=4.5, reach=9.0, hole=5.0)

# ---- v1: inside parts ----------------------------------------------------
# 6x6x4.3 mm tactile switch: 6 mm square body 3.5 mm high, plunger to 4.3 mm,
# through-hole legs at the body's corners (6.5 x 4.5 mm pitch).
SW = dict(body=6.0, body_h=3.5, total_h=4.3, fit=0.2)
SW_WALL = 1.6          # holder sleeve wall
SW_PRELOAD = 0.1       # plunger presses the cap flange onto the wall: no rattle
SW_STOP = 1.4          # back stop behind the switch body
# Chip antenna: the ceramic part marked "C3" on Waveshare's dimension drawing
# (github.com/waveshareteam/ESP32-C6-Touch-LCD-1.28, hardware/dimensions),
# on the board edge. With USB at 3 o'clock it sits at about 1:36 seen from
# the front. Position read off the drawing, not measured on a board; Z is a
# guess (component side of the board).
ANTENNA = dict(ang=138.0, r=18.0, z=FACE + 4.5)
# 1027 coin vibration motor (10 x 2.7 mm, adhesive back), later version.
MOTOR = dict(ang=0, d=10.0, t=2.7, z=SPLIT + 4.5)
# Cable-tie anchor for the 12-pin header cable (header position unknown).
CLAMP = dict(ang=270, tie_w=3.0, tie_t=1.6)
# Pads on the back shell that press on the board edge beside the battery
# (with a 1 mm foam dot) so the board stays against the front glass.
PAD = dict(x0=16.6, x1=17.9, y=5.0, w=3.0, gap=0.3)


def radial_cyl(d, r0, r1, ang, z):
    """Cylinder of diameter d on a radial axis, from radius r0 to r1."""
    c = cq.Workplane("XY").circle(d / 2).extrude(r1 - r0)
    c = c.rotate((0, 0, 0), (1, 0, 0), -90).translate((0, r0, z))
    return c.rotate((0, 0, 0), (0, 0, 1), ang - 90)


def polar(r, ang):
    a = math.radians(ang)
    return r * math.cos(a), r * math.sin(a)


def disc(r, z0, z1):
    return cq.Workplane("XY").workplane(offset=z0).circle(r).extrude(z1 - z0)


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def usb_rot(shape):
    return shape.rotate((0, 0, 0), (0, 0, 1), USB_ROT)


def at_angle(shape, ang):
    """Parts are built along +Y (12 o'clock) and turned to their angle."""
    return shape.rotate((0, 0, 0), (0, 0, 1), ang - 90)


def switch_tip_r():
    return RI - FLANGE_T + SW_PRELOAD


def switch_holder(ang, flange_d):
    """Square sleeve around the switch, on the split plane, so each shell
    holds half of it: the switch drops into the front half, the back half
    closes over it. Back stop behind the body, slots there for the legs."""
    tip = switch_tip_r()
    base = tip - SW["total_h"]
    half = (SW["body"] + SW["fit"]) / 2
    out = half + SW_WALL
    # a full-height column, so both halves grow from a face when printed
    # (front shell screen down, back shell outer face down): no overhangs
    z0, z1 = FACE, DEPTH - FACE
    sleeve = box(-out, out, base, RI + 0.4, z0, z1).cut(box(-half, half, base - 0.01, RI + 1, SPLIT - half, SPLIT + half))
    # room for the cap flange to move in when pressed
    sleeve = sleeve.cut(radial_cyl(flange_d + 0.8, tip - 0.4, RI + 1, 90, SPLIT))
    stop = box(-out, out, base - SW_STOP, base, z0, z1)
    for s in (-1, 1):
        x0, x1 = sorted((s * 2.4, s * 4.1))
        stop = stop.cut(box(x0, x1, base - SW_STOP - 0.01, base + 0.01, SPLIT - half, SPLIT + half))
    return at_angle(sleeve.union(stop), ang).intersect(disc(RI + 0.4, FACE, DEPTH - FACE))


def switch_envelope(ang):
    out = (SW["body"] + SW["fit"]) / 2 + SW_WALL
    base = switch_tip_r() - SW["total_h"] - SW_STOP
    return at_angle(box(-out - 0.2, out + 0.2, base - 0.2, RO, FACE, DEPTH - FACE), ang)


def switch_part(ang):
    """Stand-in for the switch itself (checks only)."""
    tip = switch_tip_r()
    base = tip - SW["total_h"]
    h = SW["body"] / 2
    body = box(-h, h, base, base + SW["body_h"], SPLIT - h, SPLIT + h)
    plunger = radial_cyl(3.5, base + SW["body_h"], tip, 90, SPLIT)
    legs = box(-3.45, -3.05, base - 1.2, base, SPLIT - 2.45, SPLIT + 2.45).union(
        box(3.05, 3.45, base - 1.2, base, SPLIT - 2.45, SPLIT + 2.45))
    return at_angle(body.union(plunger).union(legs), ang)


def motor_mount():
    """Flat pad with a locating rim on the inside wall of the back shell.
    The motor sticks on with its own adhesive. Back shell only (Z >= SPLIT)."""
    back = math.sqrt(RI ** 2 - (MOTOR["d"] / 2 + 1.2) ** 2) - 0.3   # flat face, inside the curve
    pad = box(-MOTOR["d"] / 2 - 1.2, MOTOR["d"] / 2 + 1.2, back, RI + 0.4,
              MOTOR["z"] - MOTOR["d"] / 2 - 1.2, MOTOR["z"] + MOTOR["d"] / 2 + 1.2)
    rim = radial_cyl(MOTOR["d"] + 0.4 + 1.6, back - 1.0, back, 90, MOTOR["z"]).cut(
        radial_cyl(MOTOR["d"] + 0.4, back - 1.1, back, 90, MOTOR["z"]))
    m = at_angle(pad.union(rim), MOTOR["ang"])
    return m.intersect(disc(RI + 0.4, SPLIT, DEPTH - FACE + 0.01))


def motor_part():
    back = math.sqrt(RI ** 2 - (MOTOR["d"] / 2 + 1.2) ** 2) - 0.3
    return at_angle(radial_cyl(MOTOR["d"], back - MOTOR["t"], back, 90, MOTOR["z"]), MOTOR["ang"])


def motor_clearance():
    back = math.sqrt(RI ** 2 - (MOTOR["d"] / 2 + 1.2) ** 2) - 0.3
    return at_angle(radial_cyl(MOTOR["d"] + 0.4, back - MOTOR["t"] - 0.3, back, 90, MOTOR["z"]), MOTOR["ang"])


def battery_space():
    """Battery plus its fit: kept free of holders and the alignment tongue."""
    bw, bh = BATT[0] / 2 + FIT, BATT[1] / 2 + FIT
    z0 = SPLIT + 0.5 - 0.2
    return box(-bw, bw, -bh + BATT_Y, bh + BATT_Y, z0, z0 + BATT[2] + 0.4)


def cable_anchor():
    """Block on the front shell's inner wall with a tunnel for a cable tie."""
    w, t = CLAMP["tie_w"], CLAMP["tie_t"]
    blk = box(-4.0, 4.0, RI - 3.2, RI + 0.4, FACE, FACE + 6.0)
    tunnel = box(-5, 5, RI - 2.4, RI - 2.4 + t, FACE + 3.0 - w / 2, FACE + 3.0 + w / 2)
    return at_angle(blk.cut(tunnel), CLAMP["ang"])


def board_pads():
    """Two ribs from the back face down to just above the board's rear plane."""
    z0 = SPLIT + PAD["gap"]
    pads = None
    for s in (-1, 1):
        y = s * PAD["y"]
        p = box(PAD["x0"], PAD["x1"], y - PAD["w"] / 2, y + PAD["w"] / 2, z0, DEPTH - FACE + 0.01)
        pads = p if pads is None else pads.union(p)
    return pads


def build_body():
    body = disc(RO, 0, DEPTH).edges().fillet(EDGE)

    # crown guard collar and lanyard lug
    body = body.union(radial_cyl(COLLAR_OD, RO - 1.5, RO + COLLAR_LEN, 90, SPLIT))
    lug = box(-LUG["w"] / 2, LUG["w"] / 2, RO - 3, RO + LUG["reach"], DEPTH - LUG["t"], DEPTH)
    lug = lug.edges("|Z").fillet(3.0)
    lug = lug.cut(disc(LUG["hole"] / 2, DEPTH - LUG["t"], DEPTH).translate((0, RO + LUG["reach"] - 4.5, 0)))
    # lug sits at 6 o'clock on the back shell
    lug = lug.rotate((0, 0, 0), (0, 0, 1), LUG["ang"] - 90)
    body = body.union(lug)

    # main cavity and screen window
    body = body.cut(disc(RI, FACE, DEPTH - FACE))
    body = body.cut(disc(WINDOW_D / 2, 0, FACE))

    # board locating ring on the inside of the front, open towards the USB tab
    ring = disc(BOARD_R + FIT + 1.2, FACE, FACE + 3.0).cut(disc(BOARD_R + FIT, FACE, FACE + 3.0))
    ring = ring.cut(usb_rot(box(-SHOULDER_W / 2 - 1, SHOULDER_W / 2 + 1, -RI, 0, FACE, FACE + 3.0)))
    body = body.union(ring)

    # battery frame on the inside of the back
    bw, bh = BATT[0] / 2 + FIT, BATT[1] / 2 + FIT
    z0 = DEPTH - FACE - 4.0
    frame = box(-bw - 1.2, bw + 1.2, -bh - 1.2, bh + 1.2, z0, DEPTH - FACE).cut(
        box(-bw, bw, -bh, bh, z0, DEPTH - FACE))
    # leave the frame open on the right for the battery lead
    frame = frame.cut(box(bw - 0.1, bw + 1.3, -6, 6, z0, DEPTH - FACE))
    frame = frame.translate((0, BATT_Y, 0))
    body = body.union(frame.intersect(disc(RI, FACE, DEPTH - FACE)))

    # screw columns, full height, drilled below
    for a in SCREW_ANGLES:
        x, y = polar(SCREW_R, a)
        body = body.union(disc(BOSS_D / 2, FACE, DEPTH - FACE).translate((x, y, 0)))

    # v1: switch holders, motor mount, cable-tie anchor, board pads
    body = body.union(switch_holder(CROWN["ang"], CROWN["flange"]))
    for a in SIDE_ANGLES:
        body = body.union(switch_holder(a, SIDE["flange"]))
    body = body.union(motor_mount())
    body = body.union(cable_anchor())
    body = body.union(board_pads())
    # the motor is taller than the back shell inside: recess the back face
    body = body.cut(motor_clearance())
    body = body.cut(battery_space())
    return body


def drill(body):
    for a in SCREW_ANGLES:
        x, y = polar(SCREW_R, a)
        body = body.cut(disc(PILOT_D / 2, SPLIT - 6.5, SPLIT).translate((x, y, 0)))
        body = body.cut(disc(CLEAR_D / 2, SPLIT, DEPTH).translate((x, y, 0)))
        body = body.cut(disc(HEAD_D / 2, DEPTH - HEAD_DEPTH, DEPTH).translate((x, y, 0)))

    # button bores on the split plane
    body = body.cut(radial_cyl(CROWN["shaft"] + HOLE_FIT, RI - 1, RO + COLLAR_LEN + 1, 90, SPLIT))
    body = body.cut(radial_cyl(CROWN["head"] + 0.8, RO + 0.6, RO + COLLAR_LEN + 1, 90, SPLIT))
    for a in SIDE_ANGLES:
        body = body.cut(radial_cyl(SIDE["shaft"] + HOLE_FIT, RI - 1, RO + 1, a, SPLIT))

    # USB-C opening
    usb = (cq.Workplane("XZ").rect(USB_W, USB_H).extrude(RO + 1 - (USB_TAB_END - 0.5))
           .edges("|Y").fillet(2.0))
    usb = usb.translate((0, -(USB_TAB_END - 0.5), USB_Z))
    return body.cut(usb_rot(usb))


def split(body):
    big = 200
    front = body.intersect(box(-big, big, -big, big, -big, SPLIT))
    back = body.intersect(box(-big, big, -big, big, SPLIT, big))

    # alignment tongue on the back shell that drops into the front shell
    t = 1.5
    # the part inside the back shell overlaps its wall so the two fuse
    tongue = disc(RI - 0.15, SPLIT - t, SPLIT).union(disc(RI + 0.2, SPLIT, SPLIT + 3.0))
    tongue = tongue.cut(disc(RI - 1.35, SPLIT - t, SPLIT + 3.0))
    for a in SCREW_ANGLES:
        x, y = polar(SCREW_R, a)
        tongue = tongue.cut(disc(BOSS_D / 2 + 0.4, SPLIT - t, SPLIT + 3.0).translate((x, y, 0)))
    tongue = tongue.cut(radial_cyl(CROWN["flange"] + 1.5, RI - 3, RO, 90, SPLIT))
    for a in SIDE_ANGLES:
        tongue = tongue.cut(radial_cyl(SIDE["flange"] + 1.5, RI - 3, RO, a, SPLIT))
    tongue = tongue.cut(usb_rot(box(-USB_W / 2 - 1, USB_W / 2 + 1, -RO, 0, SPLIT - t, SPLIT + 3.0)))
    # the tongue must not run through the switch holders (they are in both shells)
    for a in (CROWN["ang"],) + SIDE_ANGLES:
        tongue = tongue.cut(switch_envelope(a))
    tongue = tongue.cut(motor_clearance())
    tongue = tongue.cut(battery_space())
    return front, back.union(tongue)


def cap(spec, wall_out):
    """Push button cap, built along +Z: flange, shaft, head."""
    shaft_len = (wall_out - (RI - FLANGE_T)) + 1.0  # 1 mm of travel outside the wall
    c = cq.Workplane("XY").circle(spec["flange"] / 2).extrude(FLANGE_T)
    c = c.union(cq.Workplane("XY").workplane(offset=FLANGE_T).circle(spec["shaft"] / 2).extrude(shaft_len))
    head = (cq.Workplane("XY").workplane(offset=FLANGE_T + shaft_len)
            .circle(spec["head"] / 2).extrude(spec["head_len"]).faces(">Z").edges().fillet(0.8))
    return c.union(head)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    os.makedirs(out, exist_ok=True)
    front, back = split(drill(build_body()))
    parts = {
        "front": front,
        # flip so the back prints with its flat outer face on the bed
        "back": back.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, DEPTH)),
        "cap_crown": cap(CROWN, RO),
        "cap_side": cap(SIDE, RO),
    }
    for name, p in parts.items():
        cq.exporters.export(p, os.path.join(out, f"jamtimer_{name}.stl"), tolerance=0.02, angularTolerance=0.1)
        cq.exporters.export(p, os.path.join(out, f"jamtimer_{name}.step"))
        bb = p.val().BoundingBox()
        print(f"{name:10s} {bb.xlen:6.1f} x {bb.ylen:6.1f} x {bb.zlen:6.1f} mm  solids={p.solids().size()} "
              f"volume={p.val().Volume() / 1000:.1f} cm3")
    print(f"case: {2 * RO:.0f} mm diameter, {DEPTH:.1f} mm deep")
    # how close the 2 o'clock switch comes to the (estimated) chip antenna
    ax, ay = polar(ANTENNA["r"], ANTENNA["ang"])
    sw = switch_part(150).val()
    import OCP.BRepExtrema as ext
    from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeVertex
    from OCP.gp import gp_Pnt
    v = BRepBuilderAPI_MakeVertex(gp_Pnt(ax, ay, ANTENNA["z"])).Vertex()
    d = ext.BRepExtrema_DistShapeShape(v, sw.wrapped).Value()
    print(f"2 o'clock switch to estimated antenna: {d:.1f} mm "
          f"({abs(150 - ANTENNA['ang']):.0f} degrees apart; antenna position not measured)")
    return front, back


if __name__ == "__main__":
    main()
