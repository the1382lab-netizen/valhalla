"""B-15 Wave 1: town props (A-049 market stall, A-050 well, A-051 lamp post,
A-052 fence, A-053 signpost, A-054 barrel, A-055 crate, A-056 loot bag) and the
roof gable panels.

Each prop keeps its first-pass footprint, pivot at the ground centre, name and
import path. Blender coordinates; first-pass glTF (x, y, z) = Blender (x, -z, y).

Run in Blender: exec, then build_all().
"""

import importlib.util
import math
import os

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"
_spec = importlib.util.spec_from_file_location("valhalla_kit", os.path.join(_here, "valhalla_kit.py"))
kit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(kit)

TOWN = os.path.join(kit.IMPORT, "Environment", "Town")
PROPS = os.path.join(kit.IMPORT, "Props")

WOOD, TIMBER, STONE, IRON = "MI_PropWood", "MI_Timber", "MI_StoneWall", "M_IronDark"
RED, CREAM, BURLAP, ROPE = "MI_CanvasRed", "MI_CanvasCream", "MI_Burlap", "M_Rope"
LANTERN, WATER, THATCH, FENCE = "MI_LanternGlass", "MI_WindowGlass", "MI_Thatch", "MI_FenceWood"


def barrel(name="SM_Barrel", h=0.70, r0=0.21, bulge=0.04, cx=0.0, cy=0.0, z0=0.0, b=None):
    own = b is None
    b = b or kit.MeshBuilder(name)
    prof = [(r0 + bulge * math.sin(math.pi * (i / 8.0)), z0 + h * i / 8.0) for i in range(9)]
    b.lathe(prof, 18, WOOD, cx=cx, cy=cy, cap_top=False)
    # Recessed lid a little below the rim.
    b.lathe([(r0 - 0.005, z0 + h - 0.025), (r0 - 0.005, z0 + h - 0.02)], 18, WOOD, cx=cx, cy=cy,
            cap_bottom=False)
    for z in (0.08 * h / 0.7, 0.36 * h / 0.7, 0.62 * h / 0.7):
        rr = r0 + bulge * math.sin(math.pi * z / h) + 0.006
        hb = 0.018 * max(h / 0.7, 0.4)
        b.lathe([(rr, z0 + z - hb), (rr, z0 + z + hb)], 18, IRON, cx=cx, cy=cy, cap_bottom=False, cap_top=False)
    return b.finish() if own else None


def crate(name="SM_Crate", s=0.52, h=0.50, cx=0.0, cy=0.0, b=None):
    own = b is None
    b = b or kit.MeshBuilder(name)
    hs = s / 2.0
    inset = 0.012
    b.box((cx - hs + inset, cy - hs + inset, 0.0), (cx + hs - inset, cy + hs - inset, h - inset), WOOD)
    t = 0.045
    # Edge battens: four verticals, and a frame at top and bottom.
    for sx in (-1, 1):
        for sy in (-1, 1):
            x0, x1 = sorted((cx + sx * hs, cx + sx * (hs - t)))
            y0, y1 = sorted((cy + sy * hs, cy + sy * (hs - t)))
            b.box((x0, y0, 0.0), (x1, y1, h), TIMBER, bevel=0.004)
    for z0, z1 in ((0.0, t), (h - t, h)):
        for sy in (-1, 1):
            y0, y1 = sorted((cy + sy * hs, cy + sy * (hs - t)))
            b.box((cx - hs, y0, z0), (cx + hs, y1, z1), TIMBER, bevel=0.004)
        for sx in (-1, 1):
            x0, x1 = sorted((cx + sx * hs, cx + sx * (hs - t)))
            b.box((x0, cy - hs, z0), (x1, cy + hs, z1), TIMBER, bevel=0.004)
    # A diagonal brace on two faces.
    return b.finish() if own else None


def sack(b, cx, cy, s=1.0, mat=BURLAP, z0=0.0):
    prof = [(0.0, 0.0), (0.10 * s, 0.01 * s), (0.14 * s, 0.07 * s), (0.135 * s, 0.16 * s),
            (0.09 * s, 0.22 * s), (0.04 * s, 0.25 * s), (0.04 * s, 0.27 * s), (0.065 * s, 0.30 * s)]
    b.lathe([(r, z0 + z) for r, z in prof], 12, mat, cx=cx, cy=cy, cap_bottom=False)
    b.lathe([(0.046 * s, z0 + 0.245 * s), (0.046 * s, z0 + 0.262 * s)], 12, ROPE, cx=cx, cy=cy,
            cap_bottom=False, cap_top=False)


def loot_bag():
    b = kit.MeshBuilder("SM_LootBag")
    prof = [(0.0, 0.0), (0.12, 0.015), (0.175, 0.08), (0.18, 0.15), (0.15, 0.23),
            (0.09, 0.285), (0.05, 0.30), (0.05, 0.315), (0.085, 0.34)]
    b.lathe(prof, 16, BURLAP, cap_bottom=False)
    b.lathe([(0.058, 0.292), (0.058, 0.312)], 16, ROPE, cap_bottom=False, cap_top=False)
    return b.finish()


def fence():
    """0.64 wide picket section: glTF z -0.04..0.068 is Blender y -0.068..0.04."""
    b = kit.MeshBuilder("SM_Fence")
    # Posts at both ends.
    for x0, x1 in ((-0.32, -0.265), (0.265, 0.32)):
        b.box((x0, -0.068, 0.0), (x1, 0.04, 0.86), FENCE, bevel=0.006)
        b.prism_xz([(x0, 0.86), (x1, 0.86), ((x0 + x1) / 2, 0.90)], -0.068, 0.04, FENCE)
    # Two rails behind the pickets.
    for z in (0.18, 0.62):
        b.box((-0.265, -0.068, z), (0.265, -0.032, z + 0.06), FENCE, bevel=0.004)
    # Pointed pickets.
    n, gap = 6, 0.012
    w = (0.53 - gap * (n + 1)) / n
    for i in range(n):
        x0 = -0.265 + gap + i * (w + gap)
        x1 = x0 + w
        top = 0.80 + 0.03 * (1 if i % 2 else 0)
        b.prism_xz([(x0, 0.05), (x1, 0.05), (x1, top), ((x0 + x1) / 2, top + 0.05), (x0, top)],
                   -0.032, 0.0, FENCE)
    return b.finish()


def signpost():
    """Post at the origin, one board pointing +x and one pointing Blender +y
    (glTF x -0.05..0.62, z -0.59..0.05)."""
    b = kit.MeshBuilder("SM_Signpost")
    b.box((-0.045, -0.045, 0.0), (0.045, 0.045, 1.52), TIMBER, bevel=0.008)
    b.prism_xz([(-0.05, 1.52), (0.05, 1.52), (0.0, 1.60)], -0.05, 0.05, TIMBER)
    # Board A along +x.
    b.prism_xz([(0.05, 1.26), (0.53, 1.26), (0.62, 1.335), (0.53, 1.41), (0.05, 1.41)], -0.018, 0.018, WOOD)
    # Board B along +y.
    b.prism_yz([(0.05, 1.04), (0.50, 1.04), (0.59, 1.115), (0.50, 1.19), (0.05, 1.19)], -0.018, 0.018, WOOD)
    # Iron pins.
    b.box((0.02, -0.024, 1.32), (0.07, 0.024, 1.35), IRON)
    b.box((-0.024, 0.02, 1.10), (0.024, 0.07, 1.13), IRON)
    return b.finish()


def lamp_post():
    """2.22 tall, 0.24 square. The lantern glass is emissive; the point light is
    a separate actor that valhalla_tools.lamp_lights attaches in the level."""
    b = kit.MeshBuilder("SM_LampPost")
    b.box((-0.12, -0.12, 0.0), (0.12, 0.12, 0.10), STONE, bevel=0.015)
    b.lathe([(0.06, 0.10), (0.045, 0.16), (0.032, 0.20), (0.028, 1.86), (0.04, 1.90)], 10, IRON,
            cap_bottom=False)
    # Lantern: iron frame, four glass panes, pyramid cap and finial.
    b.box((-0.085, -0.085, 1.90), (0.085, 0.085, 1.93), IRON)
    b.box((-0.07, -0.07, 1.93), (0.07, 0.07, 2.12), LANTERN)
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.box((sx * 0.07 - 0.012, sy * 0.07 - 0.012, 1.93), (sx * 0.07 + 0.012, sy * 0.07 + 0.012, 2.12), IRON)
    b.box((-0.09, -0.09, 2.12), (0.09, 0.09, 2.14), IRON)
    b.prism_xz([(-0.09, 2.14), (0.09, 2.14), (0.0, 2.20)], -0.09, 0.09, IRON)
    b.lathe([(0.012, 2.19), (0.012, 2.22)], 6, IRON, cap_bottom=False)
    return b.finish()


def market_stall():
    """x -0.68..0.68, Blender y -0.53..0.44 (glTF z -0.44..0.53); the striped
    awning overhangs the front (Blender -y), where Bjorn stands."""
    b = kit.MeshBuilder("SM_MarketStall")
    back_y, front_y = 0.40, -0.40
    # Four posts: taller at the back so the awning slopes to the front.
    for x in (-0.62, 0.62):
        b.box((x - 0.035, back_y - 0.035, 0.0), (x + 0.035, back_y + 0.035, 1.84), TIMBER, bevel=0.006)
        b.box((x - 0.035, front_y - 0.035, 0.0), (x + 0.035, front_y + 0.035, 1.55), TIMBER, bevel=0.006)
    # Counter: a plank top on two trestles, and a shelf below.
    b.box((-0.64, -0.40, 0.72), (0.64, 0.40, 0.77), WOOD, bevel=0.008)
    b.box((-0.58, -0.36, 0.25), (0.58, 0.36, 0.28), WOOD)
    for x in (-0.50, 0.50):
        b.box((x - 0.03, -0.36, 0.0), (x + 0.03, 0.36, 0.72), TIMBER, bevel=0.004)
    # Awning: six stripes alternating red and cream, sloping to a front valance.
    z_back, z_front = 1.88, 1.52
    yb, yf = 0.44, -0.53
    n = 6
    for i in range(n):
        x0 = -0.68 + i * (1.36 / n)
        x1 = x0 + 1.36 / n
        mat = RED if i % 2 == 0 else CREAM
        b.prism_yz([(yf, z_front - 0.012), (yb, z_back - 0.012), (yb, z_back), (yf, z_front)], x0, x1, mat)
        # Scalloped valance hanging from the front edge.
        b.box((x0 + 0.004, yf - 0.006, z_front - 0.12), (x1 - 0.004, yf + 0.004, z_front), mat)
    # Goods: two sacks, a small crate and a little barrel on the counter.
    sack(b, -0.42, -0.12, 0.8, z0=0.77)
    sack(b, -0.22, -0.20, 0.7, z0=0.77)
    top = 0.77
    b.box((0.12, -0.22, top), (0.36, 0.02, top + 0.18), WOOD, bevel=0.006)
    b.box((0.12, -0.22, top + 0.18), (0.36, 0.02, top + 0.195), TIMBER)
    barrel(b=b, h=0.30, r0=0.09, bulge=0.018, cx=0.50, cy=0.15, z0=top)
    return b.finish()


def well():
    """x -0.5..0.5, y -0.43..0.43, h 1.6."""
    b = kit.MeshBuilder("SM_Well")
    # Stone ring with a rim, and dark water inside.
    b.lathe([(0.40, 0.0), (0.41, 0.05), (0.40, 0.48)], 20, STONE, cap_bottom=False, cap_top=False)
    b.lathe([(0.43, 0.48), (0.43, 0.54), (0.30, 0.54), (0.30, 0.10)], 20, STONE, cap_bottom=False, cap_top=False)
    b.lathe([(0.30, 0.22), (0.30, 0.225)], 20, WATER, cap_bottom=False)
    # Posts, crossbeam windlass with a rope wrap and a crank, hanging bucket.
    for x in (-0.46, 0.46):
        b.box((x - 0.04, -0.04, 0.0), (x + 0.04, 0.04, 1.36), TIMBER, bevel=0.006)
    b.box((-0.5, -0.03, 1.12), (0.5, 0.03, 1.18), TIMBER, bevel=0.005)
    b.box((-0.16, -0.045, 1.105), (0.16, 0.045, 1.195), ROPE, bevel=0.02)
    b.box((-0.006, -0.006, 0.80), (0.006, 0.006, 1.11), ROPE)
    barrel(b=b, h=0.16, r0=0.075, bulge=0.01, cx=0.0, cy=0.0, z0=0.64)   # the bucket, on its rope
    # Plank roof over the well.
    slope_top, eave = 1.60, 1.34
    b.prism_yz([(-0.43, eave), (0.0, slope_top), (0.43, eave), (0.43, eave - 0.03), (0.0, slope_top - 0.03),
                (-0.43, eave - 0.03)], -0.5, 0.5, THATCH)
    return b.finish()


PIECES = [
    (barrel, TOWN), (crate, TOWN), (fence, TOWN), (signpost, TOWN),
    (lamp_post, TOWN), (market_stall, TOWN), (well, TOWN), (loot_bag, PROPS),
]


def build_all(export=True):
    kit.reset_scene()
    import mathutils
    out = []
    for i, (fn, folder) in enumerate(PIECES):
        obj = fn()
        if export:
            kit.export_glb(obj, os.path.join(folder, obj.name + ".glb"))
        bb = [obj.matrix_world @ mathutils.Vector(c) for c in obj.bound_box]
        mn = [round(min(v[k] for v in bb), 3) for k in range(3)]
        mx = [round(max(v[k] for v in bb), 3) for k in range(3)]
        obj.location.x = i * 1.6
        out.append((obj.name, kit.tri_count(obj), mn, mx, [m.name for m in obj.data.materials]))
    return out
