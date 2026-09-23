"""B-15 Wave 1: town buildings and stone walls.

A-045 VB_HouseWall / _Door / _Window, A-046 SM_Roof_Thatch,
A-037 VB_StoneWall_Straight / _Corner / _End.

Timber-frame walls in the NWN manner: a stone plinth, a sill beam, corner posts,
a top plate, a mid rail and a diagonal brace, with the plaster infill recessed
2 cm behind the timbers so the frame catches light. Every VB_ piece keeps the
first-pass bounds exactly (line of sight and fog depend on them).

Blender coordinates: x along the wall, y through it, z up. The first-pass glTF
bounds (x, y, z) are Blender (x, -z, y).

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
GRASSLAND = os.path.join(kit.IMPORT, "Environment", "Grassland")

STONE, TIMBER, PLASTER = "MI_StoneWall", "MI_Timber", "MI_Plaster"
THATCH, DOOR, IRON, GLASS = "MI_Thatch", "MI_DoorPlanks", "M_IronDark", "MI_WindowGlass"

H = 1.80            # wall height
T = 0.125           # half wall thickness (timbers flush with the old bounds)
P = 0.10            # plaster half thickness: 2.5 cm behind the timbers
PLINTH = 0.22       # stone plinth height
SILL = 0.32         # top of the sill beam
PLATE = 1.66        # underside of the top plate
POST = 0.07         # post width


def _frame(b, x0, x1, brace=True, rail=True):
    """Posts, sill, top plate, rail and brace for one bay between x0 and x1."""
    b.box((x0, -T, 0.0), (x1, T, PLINTH), STONE, bevel=0.012)
    b.box((x0, -0.12, PLINTH), (x1, 0.12, SILL), TIMBER, bevel=0.008)
    b.box((x0, -T, PLATE), (x1, T, H), TIMBER, bevel=0.008)
    b.box((x0, -0.12, SILL), (x0 + POST, 0.12, PLATE), TIMBER, bevel=0.006)
    b.box((x1 - POST, -0.12, SILL), (x1, 0.12, PLATE), TIMBER, bevel=0.006)
    if rail:
        b.box((x0 + POST, -0.115, 0.96), (x1 - POST, 0.115, 1.02), TIMBER, bevel=0.005)
    if brace:
        # Diagonal from the lower left to the rail, 6 cm wide.
        xa, xb = x0 + POST, x1 - POST
        za, zb = SILL, 0.96
        # Horizontal width of a 6 cm strip at angle theta is 6 cm / sin(theta).
        w = min(0.06 / math.sin(math.atan2(zb - za, xb - xa)), (xb - xa) / 2.0)
        b.prism_xz([(xa, za), (xa + w, za), (xb, zb), (xb - w, zb)], -0.113, 0.113, TIMBER)


def _plaster(b, x0, x1, z0, z1):
    b.box((x0, -P, z0), (x1, P, z1), PLASTER)


def house_wall():
    b = kit.MeshBuilder("VB_HouseWall")
    x0, x1 = -0.32, 0.32
    _frame(b, x0, x1)
    _plaster(b, x0 + POST, x1 - POST, SILL, PLATE)
    return b.finish()


def house_wall_window():
    b = kit.MeshBuilder("VB_HouseWall_Window")
    x0, x1 = -0.32, 0.32
    _frame(b, x0, x1, brace=False, rail=False)
    wx0, wx1, wz0, wz1 = -0.17, 0.17, 0.98, 1.40
    # Plaster around the opening.
    _plaster(b, x0 + POST, x1 - POST, SILL, wz0 - 0.04)
    _plaster(b, x0 + POST, wx0 - 0.04, wz0 - 0.04, wz1 + 0.05)
    _plaster(b, wx1 + 0.04, x1 - POST, wz0 - 0.04, wz1 + 0.05)
    _plaster(b, x0 + POST, x1 - POST, wz1 + 0.05, PLATE)
    # Window frame: sill (proud, the old bounds allow 16.5 cm), jambs, lintel.
    b.box((wx0 - 0.05, -0.16, wz0 - 0.04), (wx1 + 0.05, 0.16, wz0), TIMBER, bevel=0.006)
    b.box((wx0 - 0.04, -0.12, wz0), (wx0, 0.12, wz1), TIMBER)
    b.box((wx1, -0.12, wz0), (wx1 + 0.04, 0.12, wz1), TIMBER)
    b.box((wx0 - 0.05, -0.125, wz1), (wx1 + 0.05, 0.125, wz1 + 0.05), TIMBER, bevel=0.006)
    # Dark pane set back, with a mullion cross.
    b.box((wx0, -0.02, wz0), (wx1, 0.02, wz1), GLASS)
    b.box((-0.015, -0.045, wz0), (0.015, 0.045, wz1), TIMBER)
    b.box((wx0, -0.045, (wz0 + wz1) / 2 - 0.015), (wx1, 0.045, (wz0 + wz1) / 2 + 0.015), TIMBER)
    # Plank shutters folded back against the wall, both faces.
    for s in (-1.0, 1.0):
        y0, y1 = (0.100, 0.135) if s > 0 else (-0.135, -0.100)
        b.box((wx0 - 0.12, y0, wz0 + 0.01), (wx0 - 0.045, y1, wz1 - 0.01), DOOR, bevel=0.004)
        b.box((wx1 + 0.045, y0, wz0 + 0.01), (wx1 + 0.12, y1, wz1 - 0.01), DOOR, bevel=0.004)
    return b.finish()


def house_wall_door():
    b = kit.MeshBuilder("VB_HouseWall_Door")
    x0, x1 = -0.64, 0.64
    dx = 0.30                      # door half-width (old leaf: 0.90 wide, 1.50 tall)
    dz = 1.50
    # Two side bays, each a full frame with a brace.
    _frame(b, x0, -dx - 0.04)
    _frame(b, dx + 0.04, x1)
    _plaster(b, x0 + POST, -dx - 0.04 - POST, SILL, PLATE)
    _plaster(b, dx + 0.04 + POST, x1 - POST, SILL, PLATE)
    # Door posts, lintel, and the plaster over the door.
    b.box((-dx - 0.04, -0.13, 0.0), (-dx, 0.13, PLATE), TIMBER, bevel=0.006)
    b.box((dx, -0.13, 0.0), (dx + 0.04, 0.13, PLATE), TIMBER, bevel=0.006)
    b.box((-dx - 0.04, -0.13, dz), (dx + 0.04, 0.13, dz + 0.08), TIMBER, bevel=0.006)
    b.box((-dx - 0.04, -T, PLATE), (dx + 0.04, T, H), TIMBER, bevel=0.008)
    _plaster(b, -dx, dx, dz + 0.08, PLATE)
    # Stone step, proud of the wall on both sides (old bounds: 16 cm).
    b.box((-dx - 0.06, -0.16, 0.0), (dx + 0.06, 0.16, 0.05), STONE, bevel=0.01)
    # The door leaf: vertical planks, iron straps and a ring pull, both faces.
    b.box((-dx, -0.05, 0.05), (dx, 0.05, dz), DOOR)
    for side in (-1.0, 1.0):
        y0, y1 = (0.05, 0.058) if side > 0 else (-0.058, -0.05)
        for z in (0.30, 1.20):
            b.box((-dx + 0.02, y0, z), (dx - 0.02, y1, z + 0.04), IRON)
        ry0, ry1 = (0.058, 0.075) if side > 0 else (-0.075, -0.058)
        b.box((dx - 0.12, ry0, 0.74), (dx - 0.08, ry1, 0.80), IRON)
    return b.finish()


def roof_thatch():
    """128 x 128 gable, ridge along x. Thick thatch, 6 cm eave overhang, ridge roll."""
    b = kit.MeshBuilder("SM_Roof_Thatch")
    x0, x1 = -0.64, 0.64
    slope = 0.94                   # rise per metre (the first pass was ~46 degrees)
    thick = 0.10
    eave = 0.70

    def outer(y):
        return 0.10 + (0.64 - abs(y)) * slope

    profile = [(-eave, outer(-eave) - thick), (-eave, outer(-eave)), (0.0, outer(0.0)),
               (eave, outer(eave)), (eave, outer(eave) - thick), (0.0, outer(0.0) - thick)]
    b.prism_yz(profile, x0, x1, THATCH)
    # Gable panels at both ends: plaster under the thatch, with a timber tie
    # beam and king post. Where two modules meet these are hidden inside the
    # roof; at the end of a house they close the triangle the first pass left open.
    for gx0, gx1 in ((x0, x0 + 0.02), (x1 - 0.02, x1)):
        b.prism_yz([(-0.62, 0.0), (0.62, 0.0), (0.0, 0.58)], gx0, gx1, PLASTER)
    for gx0, gx1 in ((x0 - 0.001, x0 + 0.03), (x1 - 0.03, x1 + 0.001)):
        b.box((gx0, -0.62, 0.0), (gx1, 0.62, 0.05), TIMBER)
        b.box((gx0, -0.025, 0.05), (gx1, 0.025, 0.56), TIMBER)
    # Ridge roll: a bundle of straw capping the ridge.
    top = outer(0.0)
    b.box((x0, -0.09, top - 0.06), (x1, 0.09, top + 0.06), THATCH, bevel=0.035)
    angle = math.atan(slope)

    def uv(face, co):
        # Reeds run down the slope: u along the ridge, v down the slope.
        if abs(face.normal.z) > 0.2 and abs(face.normal.x) < 0.5:
            return (co.x, abs(co.y) / math.cos(angle))
        return None
    return b.finish(uv_fn=uv)


def _stone_run(b, x0, x1, y0, y1, height=1.62, cap=1.80):
    """A length of town wall: footing, rubble body, and a coping course."""
    b.box((x0, y0, 0.0), (x1, y1, 0.12), STONE, bevel=0.01)
    b.box((x0, y0 + 0.015, 0.12), (x1, y1 - 0.015, height), STONE, bevel=0.006)
    b.box((x0, y0, height), (x1, y1, cap), STONE, bevel=0.02)


def stone_straight():
    b = kit.MeshBuilder("VB_StoneWall_Straight")
    _stone_run(b, -0.32, 0.32, -T, T)
    return b.finish()


def stone_corner():
    b = kit.MeshBuilder("VB_StoneWall_Corner")
    # Arms along +x... the first-pass corner spans x -0.32..0.125 and
    # Blender y -0.32..0.125 (glTF z -0.125..0.32).
    _stone_run(b, -0.32, T, -T, T)
    _stone_run(b, -T, T, -0.32, T)
    return b.finish()


def stone_end():
    b = kit.MeshBuilder("VB_StoneWall_End")
    _stone_run(b, -0.32, 0.30, -T, T)
    # The end pillar on the +x half, as in the first pass (x 0.02..0.32).
    b.box((0.02, -0.17, 0.0), (0.32, 0.17, 1.86), STONE, bevel=0.012)
    b.box((0.0, -0.17, 1.86), (0.32, 0.17, 2.04), STONE, bevel=0.025)
    return b.finish()


PIECES = [
    (house_wall, TOWN), (house_wall_window, TOWN), (house_wall_door, TOWN), (roof_thatch, TOWN),
    (stone_straight, GRASSLAND), (stone_corner, GRASSLAND), (stone_end, GRASSLAND),
]


def build_all(export=True):
    kit.reset_scene()
    out = []
    for i, (fn, folder) in enumerate(PIECES):
        obj = fn()
        path = os.path.join(folder, obj.name + ".glb")
        if export:
            kit.export_glb(obj, path)
        dims = [round(v, 3) for v in obj.dimensions]
        bb = [obj.matrix_world @ __import__("mathutils").Vector(c) for c in obj.bound_box]
        mn = [round(min(v[k] for v in bb), 3) for k in range(3)]
        mx = [round(max(v[k] for v in bb), 3) for k in range(3)]
        obj.location.x = i * 1.6               # spread out for the review screenshot
        out.append((obj.name, kit.tri_count(obj), mn, mx, [m.name for m in obj.data.materials]))
    return out
