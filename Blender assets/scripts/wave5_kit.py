"""B-15 Wave 5: more environment (new assets, placed by hand).

A-064 Tavern, A-065 Temple / shrine, A-066 Keep, A-067 Ruins, A-068 Bridge and
river bank, A-069 Cave kit, A-071 Interior props, A-072 Camp props,
A-073 Foliage variety, A-074 Desert variety.

Every piece follows the art bible: 1 Blender unit = 1 game metre (the glTF
importer converts to cm), pivot on the ground at the footprint centre, walls on
the 64 cm grid (VB_ = vision blockers, x along the wall, y through it), props at
~0.68 x real size next to a 122 cm character. Slot names are the Unreal
material instances (bound by valhalla_tools.import_kit).

Run in Blender: exec, then build_set("Tavern") / build_all().
"""

import importlib.util
import math
import os
import random

import bmesh
import bpy
import mathutils
from mathutils import Matrix, Vector, noise

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


kit = _load("valhalla_kit")
nat = _load("wave1_nature")
Geo = nat.Geo
MB = kit.MeshBuilder

ENV = os.path.join(kit.IMPORT, "Environment")

# Materials (Unreal MI names).
STONE, TIMBER, PLASTER, THATCH = "MI_StoneWall", "MI_Timber", "MI_Plaster", "MI_Thatch"
DOOR, IRON, GLASS, GOLD, STEEL = "MI_DoorPlanks", "M_IronDark", "MI_WindowGlass", "MI_Gold", "MI_Steel"
SLATE, KEEP, TEMPLE, MARBLE, TFLOOR = "MI_SlateRoof", "MI_KeepStone", "MI_TempleStone", "MI_Marble", "MI_TempleFloor"
RUIN, MOSSY, OAK, DARKPL, LOG = "MI_RuinStone", "MI_MossyRock", "MI_OakPlanks", "MI_DarkPlanks", "MI_LogBark"
CAVE, CAVEFLOOR, CRYSTAL = "MI_CaveRock", "MI_CaveFloor", "MI_Crystal"
FIRE, WAX, POTTERY, BREAD = "MI_Fire", "MI_Wax", "MI_Pottery", "MI_Bread"
CANVAS, CANVASRED, ROPE, LEATHER, WOOL = "MI_CanvasCream", "MI_CanvasRed", "M_Rope", "MI_Leather", "MI_ClothBrown"
CLOTHBLUE, CLOTHCREAM, BONE, SROCK, SBLOCK = "MI_ClothBlue", "MI_ClothCream", "MI_Bone", "MI_SandstoneRock", "MI_SandstoneBlocks"
BARK, MASS, CARD, WATERM = "MI_Bark", "MI_LeafMass", "MI_LeafCluster", "MI_Water"
GRASSC, REEDS, FERN, FLOWA, FLOWB = "MI_GrassTuft", "MI_Reeds", "MI_Fern", "MI_FlowersA", "MI_FlowersB"
BANK, BOTTLE = "MI_DirtPath", "MI_LanternGlass"

kit.PREVIEW.update({
    STONE: (0.45, 0.43, 0.4), TIMBER: (0.28, 0.2, 0.14), PLASTER: (0.85, 0.8, 0.7), THATCH: (0.65, 0.55, 0.33),
    DOOR: (0.4, 0.3, 0.2), IRON: (0.12, 0.12, 0.13), GLASS: (0.3, 0.4, 0.45), GOLD: (0.8, 0.6, 0.2), STEEL: (0.7, 0.7, 0.72),
    ROPE: (0.6, 0.5, 0.3), LEATHER: (0.4, 0.26, 0.15), WOOL: (0.35, 0.24, 0.16), CLOTHBLUE: (0.15, 0.22, 0.5),
    CLOTHCREAM: (0.85, 0.8, 0.65), BONE: (0.85, 0.82, 0.7), SROCK: (0.75, 0.6, 0.42), SBLOCK: (0.78, 0.65, 0.45),
    BARK: (0.3, 0.22, 0.15), MASS: (0.2, 0.35, 0.12), CARD: (0.25, 0.42, 0.15), WATERM: (0.1, 0.2, 0.25),
    GRASSC: (0.3, 0.45, 0.15), REEDS: (0.35, 0.45, 0.2), FERN: (0.2, 0.4, 0.12), FLOWA: (0.8, 0.7, 0.2), FLOWB: (0.5, 0.35, 0.7),
    BANK: (0.35, 0.3, 0.2), BOTTLE: (0.6, 0.7, 0.6),
    SLATE: (0.25, 0.27, 0.3), KEEP: (0.5, 0.48, 0.44), TEMPLE: (0.78, 0.74, 0.66), MARBLE: (0.85, 0.83, 0.78),
    TFLOOR: (0.6, 0.55, 0.5), RUIN: (0.42, 0.44, 0.36), MOSSY: (0.4, 0.45, 0.32), OAK: (0.55, 0.36, 0.2),
    DARKPL: (0.3, 0.22, 0.15), LOG: (0.3, 0.24, 0.17), CAVE: (0.2, 0.2, 0.2), CAVEFLOOR: (0.25, 0.24, 0.23),
    CRYSTAL: (0.3, 0.7, 1.0), FIRE: (1.0, 0.6, 0.15), WAX: (0.9, 0.87, 0.75), POTTERY: (0.6, 0.35, 0.2),
    BREAD: (0.7, 0.5, 0.25), CANVAS: (0.85, 0.8, 0.7), CANVASRED: (0.6, 0.12, 0.1),
})


def folder(set_name):
    return os.path.join(ENV, set_name)


# ── Shared shapes ────────────────────────────────────────────────────────────

def rock(g, centre, size, mat, seed, rough=0.2, subdiv=3, chisel=6, squash=1.0):
    """A chiselled boulder: noisy sphere cut by a few planes (facets), ``size``
    = (rx, ry, rz) radii. UVs box-projected in metres."""
    rng = random.Random(seed)
    before = set(g.bm.verts)
    verts = g.blob((0, 0, 0), 1.0, mat, squash=squash, seed=seed, rough=rough, subdiv=subdiv)
    planes = []
    for _ in range(chisel):
        d = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 1))).normalized()
        planes.append((d, rng.uniform(0.7, 0.9)))
    c = Vector(centre)
    for v in verts:
        p = v.co.copy()
        for d, k in planes:
            h = p.dot(d)
            if h > k:
                p -= d * (h - k)
        v.co = c + Vector((p.x * size[0], p.y * size[1], p.z * size[2]))
    faces = {f for v in verts for f in v.link_faces}
    for f in faces:
        f.normal_update()
        ax = max(range(3), key=lambda i: abs(f.normal[i]))
        for loop in f.loops:
            co = loop.vert.co
            loop[g.uv].uv = (co.x, co.y) if ax == 2 else ((co.y, co.z) if ax == 0 else (co.x, co.z))
    return verts


def flame(g, centre, height, width, seed=0):
    """Fire: five vertical cards (MI_Fire, instance of M_ValhallaFire) turned
    36 degrees apart and nudged off the centre so they do not all meet in one
    line. Each card carries the full 0..1 UV, u across and v up; the material
    cuts the flame shape out of it and animates it with rising 3D noise sampled
    in object space, so the cards read as one moving volume of flame."""
    rng = random.Random(seed)
    cx, cy, cz = centre
    spin = rng.uniform(0.0, math.pi / 5)
    for i in range(5):
        a = spin + math.pi * i / 5
        w = width * rng.uniform(1.15, 1.45)
        h = height * rng.uniform(0.9, 1.2)
        d = Vector((math.cos(a), math.sin(a), 0.0)) * (w / 2)
        off = Vector((-math.sin(a), math.cos(a), 0.0)) * width * rng.uniform(-0.12, 0.12)
        base = Vector((cx, cy, cz - height * 0.05)) + off
        vs = [g.bm.verts.new(p) for p in (base - d, base + d, base + d + Vector((0, 0, h)), base - d + Vector((0, 0, h)))]
        g.face(vs, ((0, 0), (1, 0), (1, 1), (0, 1)), FIRE, smooth=False)


def cards(g, centre, width, height, n, mat, rng, lean=0.12, spin=0.0):
    """Crossed vertical cards through ``centre`` (foliage clumps, flames),
    full texture on each (base at v=0)."""
    c = Vector(centre)
    for i in range(n):
        a = spin + math.pi * i / n + rng.uniform(-0.15, 0.15)
        d = Vector((math.cos(a), math.sin(a), 0))
        lv = Vector((rng.uniform(-lean, lean), rng.uniform(-lean, lean), 1.0)).normalized()
        w = width * rng.uniform(0.85, 1.15)
        h = height * rng.uniform(0.85, 1.1)
        p0, p1 = c - d * w / 2, c + d * w / 2
        vs = [g.bm.verts.new(p) for p in (p0, p1, p1 + lv * h, p0 + lv * h)]
        g.face(vs, ((0, 0), (1, 0), (1, 1), (0, 1)), mat, smooth=False)


def fit_bottom(obj):
    """Move the mesh so its lowest point is at z=0 (pivot on the ground)."""
    zs = [v.co.z for v in obj.data.vertices]
    obj.data.transform(Matrix.Translation((0, 0, -min(zs))))
    obj.data.update()
    return obj


def _lathe(b, profile, segs, m, cx=0.0, cy=0.0, cap_bottom=True, cap_top=True):
    return b.lathe(profile, segs, m, cx=cx, cy=cy, cap_bottom=cap_bottom, cap_top=cap_top)


def rot_mesh(obj, deg, axis="Z"):
    obj.data.transform(Matrix.Rotation(math.radians(deg), 4, axis))
    obj.data.update()
    return obj


def join(name, objs):
    """Join several built objects into one (the first keeps the name)."""
    for k, o in enumerate(objs):                   # free the name for the result
        o.name = "_join_part%d" % k
        o.data.name = "_join_part%d" % k
    mats = []
    bm = bmesh.new()
    for o in objs:
        me = o.data
        offset = len(mats)
        for m in me.materials:
            mats.append(m)
        tmp = bmesh.new()
        tmp.from_mesh(me)
        for f in tmp.faces:
            f.material_index += offset
        tmp_me = bpy.data.meshes.new("_tmp")
        tmp.to_mesh(tmp_me)
        tmp.free()
        bm.from_mesh(tmp_me)
        bpy.data.meshes.remove(tmp_me)
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    # Merge duplicate material slots by name.
    names = []
    remap = []
    for m in mats:
        if m.name not in names:
            names.append(m.name)
        remap.append(names.index(m.name))
    for p in me.polygons:
        p.material_index = remap[p.material_index]
    for n in names:
        me.materials.append(bpy.data.materials[n])
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if ob.name != name:                            # a stale object holds the name
        bpy.data.objects[name].name = name + "_stale"
        ob.name = name
    me.name = name
    return ob


# ═════════════════════════════════════════════════════════════════════════════
#  A-064 Tavern: a two-storey building kit. Stone ground floor, a jettied
#  timber-and-plaster upper floor, slate roof. Walls 64 cm (door 128 cm) wide,
#  25 cm thick, 3.6 m high (two of the 1.8 m storeys the art bible sets).
# ═════════════════════════════════════════════════════════════════════════════

TW = 0.125          # half thickness
TH = 3.6            # wall height
STOREY = 1.8


def _tavern_upper(b, x0, x1, window=False):
    """Upper storey from 1.8 to 3.6: a moulded jetty beam, sill, posts, top
    plate, a brace, plaster panels 2.5 cm behind the timbers."""
    b.box((x0, -TW - 0.05, 1.74), (x1, TW + 0.05, 1.88), TIMBER, bevel=0.01)       # jetty beam
    b.box((x0, -TW + 0.025, 1.88), (x1, TW - 0.025, TH - 0.1), PLASTER)            # plaster core
    b.box((x0, -TW, 1.88), (x1, TW, 1.98), TIMBER, bevel=0.006)                    # sill
    b.box((x0, -TW, TH - 0.12), (x1, TW, TH), TIMBER, bevel=0.006)                 # top plate
    for x in (x0, x1 - 0.07):
        b.box((x, -TW, 1.98), (x + 0.07, TW, TH - 0.12), TIMBER, bevel=0.005)       # posts
    if window:
        wx0, wx1 = (x0 + x1) / 2 - 0.16, (x0 + x1) / 2 + 0.16
        b.box((wx0 - 0.05, -TW - 0.01, 2.36), (wx1 + 0.05, TW + 0.01, 2.42), TIMBER)   # sill
        b.box((wx0 - 0.05, -TW - 0.01, 3.02), (wx1 + 0.05, TW + 0.01, 3.08), TIMBER)   # lintel
        b.box((wx0, -0.02, 2.42), (wx1, 0.02, 3.02), GLASS)
        for x in (wx0 - 0.04, (wx0 + wx1) / 2 - 0.02, wx1):
            b.box((x, -TW - 0.005, 2.42), (x + 0.04, TW + 0.005, 3.02), TIMBER)         # mullions
        b.box((wx0, -TW - 0.005, 2.7), (wx1, TW + 0.005, 2.74), TIMBER)               # transom
    else:
        # A St Andrew's cross brace.
        for sgn in (1, -1):
            xa, xb = (x0 + 0.07, x1 - 0.07) if sgn > 0 else (x1 - 0.07, x0 + 0.07)
            for side in (-1, 1):
                y0, y1 = (TW - 0.025, TW) if side > 0 else (-TW, -TW + 0.025)
                b.prism_xz([(xa, 2.0), (xa + 0.06 * sgn, 2.0), (xb, TH - 0.14), (xb - 0.06 * sgn, TH - 0.14)], y0, y1, TIMBER)


def _tavern_lower(b, x0, x1):
    b.box((x0, -TW - 0.02, 0.0), (x1, TW + 0.02, 0.2), STONE, bevel=0.012)        # footing
    b.box((x0, -TW, 0.2), (x1, TW, STOREY - 0.06), STONE, bevel=0.006)
    b.box((x0, -TW - 0.015, STOREY - 0.06), (x1, TW + 0.015, 1.74), STONE, bevel=0.01)   # string course


def tavern_wall():
    b = MB("VB_TavernWall")
    _tavern_lower(b, -0.32, 0.32)
    _tavern_upper(b, -0.32, 0.32)
    return b.finish()


def tavern_wall_window():
    b = MB("VB_TavernWall_Window")
    # Ground floor: a small, deep-set window with a stone sill.
    b.box((-0.32, -TW - 0.02, 0.0), (0.32, TW + 0.02, 0.2), STONE, bevel=0.012)
    b.box((-0.32, -TW, 0.2), (-0.13, TW, STOREY - 0.06), STONE, bevel=0.006)
    b.box((0.13, -TW, 0.2), (0.32, TW, STOREY - 0.06), STONE, bevel=0.006)
    b.box((-0.13, -TW, 0.2), (0.13, TW, 0.72), STONE, bevel=0.006)
    b.box((-0.13, -TW, 1.2), (0.13, TW, STOREY - 0.06), STONE, bevel=0.006)
    b.box((-0.16, -TW - 0.04, 0.68), (0.16, TW + 0.04, 0.74), STONE, bevel=0.008)    # sill
    b.box((-0.13, -0.02, 0.72), (0.13, 0.02, 1.2), GLASS)
    b.box((-0.015, -0.03, 0.72), (0.015, 0.03, 1.2), IRON)
    b.box((-0.13, -0.03, 0.95), (0.13, 0.03, 0.98), IRON)
    b.box((-0.32, -TW - 0.015, STOREY - 0.06), (0.32, TW + 0.015, 1.74), STONE, bevel=0.01)
    _tavern_upper(b, -0.32, 0.32, window=True)
    return b.finish()


def tavern_wall_door():
    """128 cm wide: a round-arched doorway in the stone storey, a studded
    plank door, a lantern bracket, and the timber storey above."""
    b = MB("VB_TavernWall_Door")
    b.box((-0.64, -TW - 0.02, 0.0), (-0.34, TW + 0.02, 0.2), STONE, bevel=0.012)
    b.box((0.34, -TW - 0.02, 0.0), (0.64, TW + 0.02, 0.2), STONE, bevel=0.012)
    b.box((-0.64, -TW, 0.2), (-0.34, TW, STOREY - 0.06), STONE, bevel=0.006)
    b.box((0.34, -TW, 0.2), (0.64, TW, STOREY - 0.06), STONE, bevel=0.006)
    # Arch: voussoirs in a half-ring over the 68 cm opening, springing at 1.2 m.
    r_in, r_out, zc = 0.34, 0.46, 1.2
    n = 7
    for i in range(n):
        a0, a1 = math.pi * i / n + 0.01, math.pi * (i + 1) / n - 0.01
        pts = [(r_in * math.cos(a0), zc + r_in * math.sin(a0)), (r_out * math.cos(a0), zc + r_out * math.sin(a0)),
               (r_out * math.cos(a1), zc + r_out * math.sin(a1)), (r_in * math.cos(a1), zc + r_in * math.sin(a1))]
        b.prism_xz(pts[::-1], -TW - 0.01, TW + 0.01, STONE)
    # Masonry filling round the arch up to the string course.
    for x0, x1 in ((-0.34, -0.2), (0.2, 0.34)):
        b.box((x0, -TW, zc + 0.2), (x1, TW, STOREY - 0.06), STONE)
    b.box((-0.2, -TW, zc + 0.46), (0.2, TW, STOREY - 0.06), STONE)
    b.box((-0.64, -TW - 0.015, STOREY - 0.06), (0.64, TW + 0.015, 1.74), STONE, bevel=0.01)
    # Door leaf (planks, iron straps and studs), set 6 cm back.
    pts = [(-0.34, 0.0), (0.34, 0.0), (0.34, zc)] + \
          [(r_in * math.cos(math.pi * k / 10), zc + r_in * math.sin(math.pi * k / 10)) for k in range(1, 10)] + [(-0.34, zc)]
    b.prism_xz(pts[::-1], 0.02, 0.07, DOOR)
    for z in (0.3, 0.95):
        b.box((-0.32, 0.01, z), (0.32, 0.02, z + 0.05), IRON)
    b.box((0.2, 0.0, 0.62), (0.25, 0.02, 0.72), IRON)                               # handle
    _tavern_upper(b, -0.64, 0.0, window=True)
    _tavern_upper(b, 0.0, 0.64, window=False)
    return b.finish()


def tavern_corner():
    """An outside corner: a stone quoin pier and a corner post above, so two
    runs of wall meet cleanly. Footprint 25 x 25 cm."""
    b = MB("VB_TavernCorner")
    b.box((-TW - 0.03, -TW - 0.03, 0.0), (TW + 0.03, TW + 0.03, 0.2), STONE, bevel=0.012)
    for i in range(8):
        z0 = 0.2 + i * 0.19
        w = 0.02 if i % 2 else 0.0
        b.box((-TW - 0.01 - w, -TW - 0.01, z0), (TW + 0.01, TW + 0.01 + w, z0 + 0.19), STONE, bevel=0.01)
    b.box((-TW - 0.05, -TW - 0.05, 1.74), (TW + 0.05, TW + 0.05, 1.88), TIMBER, bevel=0.01)
    b.box((-TW, -TW, 1.88), (TW, TW, TH), TIMBER, bevel=0.008)
    return b.finish()


def tavern_roof():
    """128 x 128 slate gable, ridge along x, sitting on the 3.6 m wall top
    (pivot at the roof's base). Overhang 12 cm, bargeboards, ridge tiles."""
    b = MB("SM_TavernRoof")
    x0, x1 = -0.66, 0.66
    slope, thick, eave = 0.9, 0.07, 0.76

    def outer(y):
        return 0.08 + (0.64 - abs(y)) * slope

    profile = [(-eave, outer(-eave) - thick), (-eave, outer(-eave)), (0.0, outer(0.0)),
               (eave, outer(eave)), (eave, outer(eave) - thick), (0.0, outer(0.0) - thick)]
    b.prism_yz(profile, x0, x1, SLATE)
    for gx0, gx1 in ((-0.64, -0.62), (0.62, 0.64)):
        b.prism_yz([(-0.62, 0.0), (0.62, 0.0), (0.0, 0.6)], gx0, gx1, PLASTER)
        b.box((gx0, -0.62, 0.0), (gx1, 0.62, 0.06), TIMBER)
    top = outer(0.0)
    b.box((x0, -0.05, top - 0.03), (x1, 0.05, top + 0.05), SLATE, bevel=0.02)       # ridge tiles
    angle = math.atan(slope)
    for sgn in (-1, 1):
        # Bargeboards along each gable's edge.
        for gx in (x0 - 0.01, x1 - 0.02):
            pts = [(0.0, top + 0.02), (sgn * eave, outer(eave) + 0.02), (sgn * eave, outer(eave) - 0.08), (0.0, top - 0.08)]
            if sgn > 0:
                pts = pts[::-1]
            b.prism_yz(pts, gx, gx + 0.03, TIMBER)

    def uv(face, co):
        if abs(face.normal.z) > 0.2 and abs(face.normal.x) < 0.5:
            return (co.x, abs(co.y) / math.cos(angle))
        return None
    return b.finish(uv_fn=uv)


def tavern_chimney():
    b = MB("SM_TavernChimney")
    b.box((-0.2, -0.16, 0.0), (0.2, 0.16, 1.5), STONE, bevel=0.01)
    b.box((-0.23, -0.19, 1.5), (0.23, 0.19, 1.58), STONE, bevel=0.012)
    for x in (-0.08, 0.08):
        _lathe(b, [(0.05, 1.58), (0.055, 1.72), (0.065, 1.74), (0.065, 1.77)], 10, POTTERY, cx=x, cy=0.0)
    return b.finish()


def tavern_sign():
    """A hanging sign on an iron bracket: an oak board with a gilded tankard.
    Pivot at the wall fixing (the bracket reaches out along -y)."""
    b = MB("SM_TavernSign")
    b.box((-0.03, -0.62, 0.0), (0.03, 0.0, 0.035), IRON)                          # arm
    b.prism_yz([(0.0, 0.0), (-0.3, 0.0), (0.0, -0.3)], -0.012, 0.012, IRON)        # stay
    for y in (-0.58, -0.18):
        b.box((-0.006, y - 0.006, -0.1), (0.006, y + 0.006, 0.0), IRON)             # chains
    b.box((-0.025, -0.64, -0.46), (0.025, -0.12, -0.1), OAK, bevel=0.008)          # board
    for side in (-1, 1):
        x0, x1 = (0.025, 0.035) if side > 0 else (-0.035, -0.025)
        b.box((x0, -0.5, -0.4), (x1, -0.3, -0.18), GOLD, bevel=0.004)             # tankard body
        b.box((x0, -0.52, -0.2), (x1, -0.28, -0.16), CLOTHCREAM)                  # froth
        b.prism_yz([(-0.3, -0.24), (-0.23, -0.24), (-0.23, -0.34), (-0.3, -0.34), (-0.3, -0.31), (-0.26, -0.31), (-0.26, -0.27), (-0.3, -0.27)],
                   x0, x1, GOLD)                                                   # handle
    return b.finish()


def tavern_floor():
    """A 128 x 128 plank floor for interiors, 6 cm top like the ground tiles."""
    b = MB("SM_TavernFloor")
    b.box((-0.64, -0.64, 0.0), (0.64, 0.64, 0.06), DARKPL)
    return b.finish()


TAVERN = [tavern_wall, tavern_wall_window, tavern_wall_door, tavern_corner, tavern_roof, tavern_chimney,
          tavern_sign, tavern_floor]


# ═════════════════════════════════════════════════════════════════════════════
#  A-071 Interior props (tavern and houses). Real sizes x 0.68.
# ═════════════════════════════════════════════════════════════════════════════

def bar_counter():
    """128 cm of bar: a panelled front, an oak top, a foot rail."""
    b = MB("SM_BarCounter")
    b.box((-0.64, -0.22, 0.0), (0.64, 0.22, 0.05), DARKPL)
    b.box((-0.62, -0.2, 0.05), (0.62, 0.18, 0.66), DARKPL, bevel=0.006)
    for x in (-0.42, 0.0, 0.42):
        b.box((x - 0.17, -0.205, 0.12), (x + 0.17, -0.2, 0.58), OAK, bevel=0.004)     # panels
    b.box((-0.64, -0.26, 0.66), (0.64, 0.24, 0.72), OAK, bevel=0.012)                # top
    b.box((-0.62, -0.3, 0.1), (0.62, -0.28, 0.12), IRON)                              # foot rail
    for x in (-0.55, 0.0, 0.55):
        b.box((x - 0.01, -0.3, 0.05), (x + 0.01, -0.2, 0.12), IRON)
    return b.finish()


def table():
    b = MB("SM_Table")
    b.box((-0.45, -0.3, 0.46), (0.45, 0.3, 0.52), OAK, bevel=0.01)
    for x in (-0.38, 0.38):
        for y in (-0.23, 0.23):
            b.box((x - 0.035, y - 0.035, 0.0), (x + 0.035, y + 0.035, 0.46), OAK, bevel=0.006)
    b.box((-0.38, -0.02, 0.1), (0.38, 0.02, 0.14), OAK)                               # stretcher
    return b.finish()


def chair():
    b = MB("SM_Chair")
    b.box((-0.16, -0.16, 0.28), (0.16, 0.16, 0.32), OAK, bevel=0.006)
    for x in (-0.13, 0.13):
        for y in (-0.13, 0.13):
            top = 0.66 if y > 0 else 0.28
            b.box((x - 0.022, y - 0.022, 0.0), (x + 0.022, y + 0.022, top), OAK, bevel=0.004)
    for z in (0.44, 0.58):
        b.box((-0.13, 0.11, z), (0.13, 0.15, z + 0.05), OAK, bevel=0.004)             # back slats
    return b.finish()


def bench():
    b = MB("SM_Bench")
    b.box((-0.5, -0.12, 0.26), (0.5, 0.12, 0.31), OAK, bevel=0.008)
    for x in (-0.4, 0.4):
        b.prism_xz([(x - 0.03, 0.0), (x + 0.03, 0.0), (x + 0.03, 0.26), (x - 0.03, 0.26)], -0.1, 0.1, OAK)
    b.box((-0.4, -0.02, 0.08), (0.4, 0.02, 0.12), OAK)
    return b.finish()


def bed():
    """A single bed, 136 x 64: posts, headboard, straw mattress, wool blanket, pillow."""
    b = MB("SM_Bed")
    b.box((-0.68, -0.32, 0.12), (0.68, 0.32, 0.2), OAK, bevel=0.008)                 # frame
    for x in (-0.66, 0.66):
        for y in (-0.3, 0.3):
            b.box((x - 0.03, y - 0.03, 0.0), (x + 0.03, y + 0.03, 0.62 if x < 0 else 0.34), OAK, bevel=0.006)
    b.box((-0.66, -0.3, 0.34), (-0.63, 0.3, 0.56), OAK, bevel=0.006)                 # headboard
    b.box((-0.62, -0.29, 0.2), (0.64, 0.29, 0.3), CANVAS, bevel=0.03)                # mattress
    b.box((-0.2, -0.305, 0.25), (0.65, 0.305, 0.32), CLOTHBLUE, bevel=0.025)         # blanket
    b.box((-0.58, -0.2, 0.29), (-0.36, 0.2, 0.36), CLOTHCREAM, bevel=0.03)           # pillow
    return b.finish()


def shelf():
    """A wall shelf unit with bottles, jars and books. Back to +y."""
    b = MB("SM_Shelf")
    for x in (-0.4, 0.37):
        b.box((x, -0.13, 0.0), (x + 0.03, 0.13, 1.2), OAK, bevel=0.004)
    for z in (0.02, 0.4, 0.78, 1.16):
        b.box((-0.4, -0.13, z), (0.4, 0.13, z + 0.03), OAK, bevel=0.004)
    b.box((-0.4, 0.11, 0.0), (0.4, 0.13, 1.2), DARKPL)
    rng = random.Random(8)
    x = -0.34
    while x < 0.3:                                                              # books, middle shelf
        w = rng.uniform(0.03, 0.05)
        h = rng.uniform(0.18, 0.28)
        col = rng.choice((LEATHER, CLOTHBLUE, "MI_ClothGreen", CANVASRED))
        b.box((x, -0.07, 0.81), (x + w, 0.09, 0.81 + h), col, bevel=0.004)
        x += w + 0.005
    for i, x in enumerate((-0.3, -0.18, -0.05, 0.1, 0.24)):                       # bottles and jars, top
        if i % 2:
            _lathe(b, [(0.045, 0.43), (0.05, 0.47), (0.05, 0.58), (0.03, 0.6), (0.001, 0.61)], 10, POTTERY, cx=x, cy=0.0)
        else:
            _lathe(b, [(0.035, 0.43), (0.04, 0.46), (0.04, 0.56), (0.015, 0.62), (0.015, 0.68), (0.001, 0.69)], 10, BOTTLE, cx=x, cy=0.0)
    for i, x in enumerate((-0.25, 0.05, 0.25)):                                  # crocks, bottom
        _lathe(b, [(0.07, 0.05), (0.09, 0.1), (0.085, 0.22), (0.06, 0.26), (0.001, 0.27)], 12, POTTERY, cx=x, cy=0.0)
    return b.finish()


def books():
    b = MB("SM_Books")
    rng = random.Random(3)
    z = 0.0
    for i in range(4):
        w, d, h = rng.uniform(0.14, 0.2), rng.uniform(0.1, 0.14), rng.uniform(0.025, 0.04)
        a = rng.uniform(-0.3, 0.3)
        col = rng.choice((LEATHER, CLOTHBLUE, CANVASRED, "MI_ClothGreen"))
        b.box((-w / 2, -d / 2, z), (w / 2, d / 2, z + h), col, bevel=0.004)
        b.box((-w / 2 + 0.005, -d / 2 - 0.001, z + 0.004), (w / 2 - 0.004, d / 2 + 0.001, z + h - 0.004), CLOTHCREAM)
        z += h
    ob = b.finish()
    return ob


def candle():
    """A pewter candlestick with a lit candle."""
    g = Geo("SM_Candle")
    b = MB("_candle")
    _lathe(b, [(0.06, 0.0), (0.06, 0.012), (0.02, 0.03), (0.015, 0.1), (0.035, 0.12), (0.035, 0.13), (0.001, 0.13)], 14, STEEL)
    _lathe(b, [(0.018, 0.13), (0.018, 0.26), (0.001, 0.265)], 10, WAX)
    ob1 = b.finish()
    flame(g, (0.0, 0.0, 0.268), 0.06, 0.02, seed=1)
    ob2 = g.finish()
    return join("SM_Candle", [ob1, ob2])


def chest():
    """A banded strongbox with a domed lid, 60 x 40 x 40."""
    b = MB("SM_Chest")
    b.box((-0.3, -0.2, 0.0), (0.3, 0.2, 0.26), OAK, bevel=0.008)
    prof = [(-0.2, 0.26)] + [(-0.2 * math.cos(math.pi * k / 8), 0.26 + 0.12 * math.sin(math.pi * k / 8)) for k in range(1, 8)] + [(0.2, 0.26)]
    b.prism_yz(prof, -0.3, 0.3, OAK)
    for x in (-0.24, 0.0, 0.24):
        b.box((x - 0.02, -0.205, 0.0), (x + 0.02, 0.205, 0.265), IRON)
        pb = [(-0.205, 0.26)] + [(-0.205 * math.cos(math.pi * k / 8), 0.26 + 0.125 * math.sin(math.pi * k / 8)) for k in range(1, 8)] + [(0.205, 0.26)]
        b.prism_yz(pb, x - 0.02, x + 0.02, IRON)
    b.box((-0.04, -0.215, 0.16), (0.04, -0.2, 0.26), GOLD)                           # lock plate
    return b.finish()


def keg():
    """A small barrel on its side on a cradle, with a tap."""
    b = MB("SM_Keg")
    prof = [(0.13, -0.2), (0.16, -0.1), (0.165, 0.0), (0.16, 0.1), (0.13, 0.2)]
    _lathe(b, [(r, z) for r, z in prof], 16, OAK)
    for z in (-0.16, 0.16):
        _lathe(b, [(0.153, z - 0.012), (0.157, z), (0.153, z + 0.012)], 16, IRON, cap_bottom=False, cap_top=False)
    ob = b.finish()
    rot_mesh(ob, 90, "Y")
    ob.data.transform(Matrix.Translation((0, 0, 0.26)))
    c = MB("_cradle")
    for x in (-0.14, 0.14):
        c.prism_yz([(-0.16, 0.0), (0.16, 0.0), (0.16, 0.08), (0.08, 0.14), (-0.08, 0.14), (-0.16, 0.08)], x - 0.025, x + 0.025, DARKPL)
    c.box((0.2, -0.015, 0.2), (0.26, 0.015, 0.23), STEEL)                            # tap
    return join("SM_Keg", [ob, c.finish()])


def fireplace():
    """A stone hearth 128 wide with a timber mantel and a fire. Back to +y."""
    b = MB("SM_Fireplace")
    b.box((-0.64, -0.3, 0.0), (0.64, 0.25, 0.08), STONE, bevel=0.01)                # hearth stone
    for x0, x1 in ((-0.62, -0.34), (0.34, 0.62)):
        b.box((x0, -0.22, 0.08), (x1, 0.25, 0.9), STONE, bevel=0.008)                # jambs
    b.box((-0.62, 0.1, 0.08), (0.62, 0.25, 0.9), STONE)                             # back
    b.box((-0.68, -0.28, 0.9), (0.68, 0.25, 0.98), TIMBER, bevel=0.01)               # mantel beam
    b.box((-0.56, -0.18, 0.98), (0.56, 0.25, 1.9), STONE, bevel=0.008)               # chimney breast
    ob = b.finish()
    g = Geo("_fire")
    for i, (x, a) in enumerate(((-0.12, 0.3), (0.12, -0.3), (0.0, 1.57))):
        path = [(x - 0.18 * math.cos(a), -0.02 - 0.18 * math.sin(a) * 0.3, 0.12), (x + 0.18 * math.cos(a), -0.02 + 0.18 * math.sin(a) * 0.3, 0.12 + (0.05 if i == 2 else 0))]
        g.tube([(p[0], p[1], p[2], 0.035) for p in path], 7, LOG, seed=i, gnarl=0.1, cap_start=True, tip=False)
    flame(g, (0.0, -0.02, 0.14), 0.36, 0.2, seed=4)
    return join("SM_Fireplace", [ob, g.finish()])


def table_clutter():
    """Tabletop dressing: two tankards, a plate with bread, a jug."""
    b = MB("SM_TableClutter")
    for x, y in ((-0.18, 0.05), (0.14, -0.1)):
        _lathe(b, [(0.045, 0.0), (0.05, 0.1), (0.048, 0.11), (0.001, 0.11)], 10, OAK, cx=x, cy=y)
        b.box((x + 0.045, y - 0.008, 0.03), (x + 0.075, y + 0.008, 0.08), IRON)
    _lathe(b, [(0.12, 0.0), (0.13, 0.015), (0.001, 0.015)], 16, POTTERY, cx=0.0, cy=0.1)
    _lathe(b, [(0.07, 0.015), (0.08, 0.04), (0.06, 0.07), (0.001, 0.075)], 10, BREAD, cx=0.0, cy=0.1)
    _lathe(b, [(0.06, 0.0), (0.075, 0.08), (0.05, 0.15), (0.045, 0.19), (0.001, 0.19)], 12, POTTERY, cx=0.2, cy=0.12)
    return b.finish()


INTERIOR = [bar_counter, table, chair, bench, bed, shelf, books, candle, chest, keg, fireplace, table_clutter]


# ═════════════════════════════════════════════════════════════════════════════
#  A-065 Temple / shrine: pale limestone, marble, gilt. Walls 2.4 m.
# ═════════════════════════════════════════════════════════════════════════════

def temple_wall():
    b = MB("VB_TempleWall")
    b.box((-0.32, -0.15, 0.0), (0.32, 0.15, 0.22), TEMPLE, bevel=0.012)           # base course
    b.box((-0.32, -0.125, 0.22), (0.32, 0.125, 2.2), TEMPLE, bevel=0.004)
    b.box((-0.32, -0.16, 2.2), (0.32, 0.16, 2.28), MARBLE, bevel=0.01)             # cornice
    b.box((-0.32, -0.14, 2.28), (0.32, 0.14, 2.4), TEMPLE, bevel=0.01)
    b.box((-0.32, -0.145, 1.2), (0.32, 0.145, 1.26), MARBLE, bevel=0.006)          # string course
    return b.finish()


def temple_wall_pilaster():
    b = MB("VB_TempleWall_Pilaster")
    b.box((-0.32, -0.15, 0.0), (0.32, 0.15, 0.22), TEMPLE, bevel=0.012)
    b.box((-0.32, -0.125, 0.22), (0.32, 0.125, 2.2), TEMPLE, bevel=0.004)
    for side in (-1, 1):
        y0, y1 = (0.125, 0.19) if side > 0 else (-0.19, -0.125)
        b.box((-0.11, y0, 0.22), (0.11, y1, 2.2), MARBLE, bevel=0.01)
        for x in (-0.06, 0.0, 0.06):
            b.box((x - 0.012, y0 if side > 0 else y1 - 0.01, 0.4), (x + 0.012, (y1 + 0.005) if side > 0 else y1 + 0.0, 2.0), TEMPLE)
    b.box((-0.32, -0.21, 2.2), (0.32, 0.21, 2.28), MARBLE, bevel=0.01)
    b.box((-0.32, -0.14, 2.28), (0.32, 0.14, 2.4), TEMPLE, bevel=0.01)
    return b.finish()


def temple_column():
    """A fluted marble column, 2.4 m: square plinth, torus base, shaft with
    entasis and 16 flutes, a moulded capital and abacus."""
    b = MB("SM_TempleColumn")
    b.box((-0.2, -0.2, 0.0), (0.2, 0.2, 0.1), MARBLE, bevel=0.01)
    _lathe(b, [(0.17, 0.1), (0.18, 0.13), (0.16, 0.16), (0.15, 0.17)], 24, MARBLE)
    segs, rings = 32, 12
    before = set(b.bm.faces)
    ring_vs = []
    for i in range(rings + 1):
        t = i / rings
        z = 0.17 + 1.9 * t
        r = 0.14 * (1.0 - 0.12 * t * t) + 0.004 * math.sin(math.pi * t)
        ring = []
        for k in range(segs):
            a = 2 * math.pi * k / segs
            flute = 1.0 - 0.06 * (0.5 + 0.5 * math.cos(a * 16))
            ring.append(b.bm.verts.new((r * flute * math.cos(a), r * flute * math.sin(a), z)))
        ring_vs.append(ring)
    for ra, rb in zip(ring_vs, ring_vs[1:]):
        for k in range(segs):
            b.bm.faces.new((ra[k], ra[(k + 1) % segs], rb[(k + 1) % segs], rb[k]))
    new = b._tag(before, MARBLE)
    for f in new:
        b.lathe_faces.add(f)
        b.lathe_axis[f] = (0.0, 0.0)
    _lathe(b, [(0.125, 2.07), (0.14, 2.1), (0.17, 2.16), (0.19, 2.2)], 24, MARBLE, cap_bottom=False)
    b.box((-0.21, -0.21, 2.2), (0.21, 0.21, 2.28), MARBLE, bevel=0.01)
    b.box((-0.24, -0.24, 2.28), (0.24, 0.24, 2.4), TEMPLE, bevel=0.01)
    return b.finish()


def temple_altar():
    """A marble altar with a runner cloth, two candles and a gilt sun disc."""
    b = MB("SM_TempleAltar")
    b.box((-0.5, -0.3, 0.0), (0.5, 0.3, 0.08), TEMPLE, bevel=0.01)
    b.box((-0.44, -0.24, 0.08), (0.44, 0.24, 0.6), MARBLE, bevel=0.01)
    b.box((-0.48, -0.28, 0.6), (0.48, 0.28, 0.66), MARBLE, bevel=0.012)
    b.box((-0.12, -0.285, 0.2), (0.12, 0.285, 0.661), CLOTHBLUE)                    # runner
    b.box((-0.1, -0.29, 0.2), (0.1, -0.285, 0.26), GOLD)
    _lathe(b, [(0.09, 0.66), (0.09, 0.68), (0.03, 0.7), (0.03, 0.72)], 16, GOLD, cx=0.0, cy=0.12)
    b.prism_xz([(math.cos(a) * r, 0.9 + math.sin(a) * r) for k in range(24) for a, r in
                [(2 * math.pi * k / 24, 0.16 if k % 2 == 0 else 0.12)]], 0.105, 0.125, GOLD)
    ob = b.finish()
    g = Geo("_candles")
    c2 = MB("_sticks")
    for x in (-0.34, 0.34):
        _lathe(c2, [(0.05, 0.66), (0.02, 0.7), (0.02, 0.8), (0.04, 0.82), (0.001, 0.82)], 10, GOLD, cx=x, cy=0.05)
        _lathe(c2, [(0.02, 0.82), (0.02, 0.96), (0.001, 0.965)], 10, WAX, cx=x, cy=0.05)
        flame(g, (x, 0.05, 0.968), 0.07, 0.022, seed=int(x * 10) + 20)
    return join("SM_TempleAltar", [ob, c2.finish(), g.finish()])


def temple_steps():
    """Three steps, 128 wide x 64 deep, 30 cm rise. Top step at +y."""
    b = MB("SM_TempleSteps")
    for i in range(3):
        b.box((-0.64, -0.32 + i * 0.2133, 0.0), (0.64, 0.32, 0.1 * (i + 1)), TEMPLE, bevel=0.008)
    return b.finish()


def temple_floor():
    b = MB("SM_TempleFloor")
    b.box((-0.64, -0.64, 0.0), (0.64, 0.64, 0.06), TFLOOR)
    return b.finish()


def brazier():
    """An iron bowl on three legs, burning. 90 cm tall."""
    b = MB("SM_Brazier")
    _lathe(b, [(0.04, 0.62), (0.18, 0.68), (0.24, 0.8), (0.25, 0.82), (0.2, 0.81), (0.001, 0.72)], 16, IRON)
    ob = b.finish()
    g = Geo("_legs")
    for k in range(3):
        a = 2 * math.pi * k / 3
        g.tube([(0.26 * math.cos(a), 0.26 * math.sin(a), 0.0, 0.02), (0.18 * math.cos(a), 0.18 * math.sin(a), 0.4, 0.02),
                (0.12 * math.cos(a), 0.12 * math.sin(a), 0.7, 0.018)], 6, IRON, seed=k, gnarl=0.0, cap_start=True)
    rng = random.Random(2)
    for k in range(6):
        rock(g, (rng.uniform(-0.1, 0.1), rng.uniform(-0.1, 0.1), 0.8), (0.06, 0.06, 0.04), CAVE, seed=k, subdiv=1, chisel=3)
    flame(g, (0.0, 0.0, 0.8), 0.42, 0.26, seed=9)
    return join("SM_Brazier", [ob, g.finish()])


def statue():
    """A hooded, robed figure with hands folded, on a plinth: a nameless
    saint, not any real deity. 1.9 m with the plinth."""
    b = MB("_plinth")
    b.box((-0.3, -0.3, 0.0), (0.3, 0.3, 0.12), TEMPLE, bevel=0.012)
    b.box((-0.25, -0.25, 0.12), (0.25, 0.25, 0.4), MARBLE, bevel=0.01)
    b.box((-0.28, -0.28, 0.4), (0.28, 0.28, 0.46), TEMPLE, bevel=0.01)
    ob = b.finish()
    g = Geo("_figure")
    # Robe: a flared, folded cone.
    rings = []
    for i in range(14):
        t = i / 13
        z = 0.46 + 1.05 * t
        r = 0.2 * (1 - t) + 0.11 * t + 0.03 * (1 - t) ** 3
        rings.append((z, r))
    path = [(0.0, 0.0, z, r) for z, r in rings]
    g.tube(path, 16, MARBLE, seed=3, gnarl=0.08, cap_start=True, tip=False)
    g.blob((0.0, 0.0, 1.47), 0.16, MARBLE, squash=0.7, seed=5, rough=0.1, subdiv=2)       # shoulders
    g.blob((0.0, -0.01, 1.62), 0.095, MARBLE, squash=1.2, seed=6, rough=0.06, subdiv=2)   # hood
    g.blob((0.0, -0.08, 1.6), 0.05, MARBLE, squash=1.1, seed=7, rough=0.02, subdiv=1)     # face in shadow
    for side in (-1, 1):                                                                  # sleeves to folded hands
        g.tube([(side * 0.14, -0.02, 1.42, 0.05), (side * 0.12, -0.1, 1.2, 0.05), (side * 0.03, -0.15, 1.12, 0.045)],
               8, MARBLE, seed=10 + side, gnarl=0.05, cap_start=True)
    g.blob((0.0, -0.16, 1.13), 0.05, MARBLE, squash=0.8, seed=12, rough=0.05, subdiv=1)   # hands
    return join("SM_Statue", [ob, g.finish()])


TEMPLE_SET = [temple_wall, temple_wall_pilaster, temple_column, temple_altar, temple_steps, temple_floor, brazier, statue]


# ── Build ────────────────────────────────────────────────────────────────────

SETS = {}


def build_set(set_name, export=True, review=None):
    fns = SETS[set_name]
    kit.reset_scene()
    out, objs = [], []
    for fn in fns:
        obj = fn()
        if export:
            kit.export_glb(obj, os.path.join(folder(set_name), obj.name + ".glb"))
        bb = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
        mn = [round(min(v[k] for v in bb), 3) for k in range(3)]
        mx = [round(max(v[k] for v in bb), 3) for k in range(3)]
        out.append((obj.name, kit.tri_count(obj), mn, mx, [m.name for m in obj.data.materials]))
        objs.append(obj)
    if review:
        rv = {}
        exec(open(os.path.join(_here, "wave5_review.py")).read(), rv)
        rv["review"](objs, review, preview=kit.PREVIEW)
    return out


SETS.update({"Tavern": TAVERN, "Interior": INTERIOR, "Temple": TEMPLE_SET})


# ═════════════════════════════════════════════════════════════════════════════
#  Shared helpers for the sets below.
# ═════════════════════════════════════════════════════════════════════════════

def xform(obj, rot=(0.0, 0.0, 0.0), loc=(0.0, 0.0, 0.0), scale=None):
    """Bake a rotation (degrees, XYZ), optional scale, then a move into the mesh."""
    m = mathutils.Euler([math.radians(a) for a in rot]).to_matrix().to_4x4()
    if scale is not None:
        s = scale if isinstance(scale, (tuple, list)) else (scale, scale, scale)
        m = m @ Matrix.Diagonal((s[0], s[1], s[2], 1.0))
    obj.data.transform(Matrix.Translation(loc) @ m)
    obj.data.update()
    return obj


def _n1(x, seed, f=3.0):
    return noise.noise(Vector((x * f, seed * 1.37, 0.5)))


def _sector(b, a0, a1, r0, r1, z0, z1, mat, steps=3):
    """An annular sector (merlons, slits on round towers)."""
    angs = [a0 + (a1 - a0) * i / steps for i in range(steps + 1)]
    pts = [(r1 * math.cos(a), r1 * math.sin(a)) for a in angs] + \
          [(r0 * math.cos(a), r0 * math.sin(a)) for a in reversed(angs)]
    return b.prism_xy(pts, z0, z1, mat)


def _arch(b, cx, zc, r_in, r_out, a0, a1, n, y0, y1, mat):
    """Voussoirs from angle a0 to a1 (radians, 0 = +x) round (cx, zc)."""
    for i in range(n):
        s0 = a0 + (a1 - a0) * i / n + 0.003
        s1 = a0 + (a1 - a0) * (i + 1) / n - 0.003
        pts = [(cx + r_in * math.cos(s0), zc + r_in * math.sin(s0)), (cx + r_out * math.cos(s0), zc + r_out * math.sin(s0)),
               (cx + r_out * math.cos(s1), zc + r_out * math.sin(s1)), (cx + r_in * math.cos(s1), zc + r_in * math.sin(s1))]
        b.prism_xz(pts[::-1], y0, y1, mat)


def _block_wall(b, x0, x1, t, top, mat, seed, course=0.2, min_len=0.18, max_len=0.42, loose=0.45):
    """Dry-laid block masonry from x0..x1, y -t..t, each column capped by
    ``top(x)``. Blocks at the broken top survive at random so the edge is
    ragged. Blocks only ever shrink inside the box, so VB_ bounds stay exact."""
    rng = random.Random(seed)
    zmax = max(top(x0 + (x1 - x0) * k / 24) for k in range(25))
    z, i = 0.0, 0
    below = [(x0, x1)]
    while z < zmax - 0.02:
        h = course * rng.uniform(0.85, 1.15)
        x = x0 - (rng.uniform(0.05, min_len) if i % 2 else 0.0)
        kept = []
        while x < x1 - 0.001:
            L = rng.uniform(min_len, max_len)
            a, c = max(x, x0), min(x + L, x1)
            if x1 - c < 0.06:
                c = x1
            if c - a > 0.03:
                lim = top((a + c) / 2)
                support = sum(max(0.0, min(c, q) - max(a, p)) for p, q in below) / (c - a)
                if support > 0.55 and (z + h <= lim or (z < lim - 0.03 and rng.random() < loose)):
                    b.box((a, -t + rng.uniform(0, 0.018), z), (c, t - rng.uniform(0, 0.018), z + h), mat, bevel=0.016)
                    kept.append((a, c))
            x = c
        below = kept
        z += h
        i += 1


def _shaft(b, z0, z1, r0, r1, mat, segs=24, flutes=12, depth=0.06, rings=8, jag=0.0, seed=0, cx=0.0, cy=0.0):
    """A column shaft (fluted when ``flutes``), optionally snapped off at the
    top: ``jag`` metres of broken, tilted, uneven stone."""
    rng = random.Random(seed)
    phase = rng.uniform(0, 2 * math.pi)
    before = set(b.bm.faces)
    rows = []
    for i in range(rings + 1):
        t = i / rings
        r = r0 + (r1 - r0) * t
        ring = []
        for k in range(segs):
            a = 2 * math.pi * k / segs
            z = z0 + (z1 - z0) * t
            if jag and i == rings:
                z -= jag * (0.5 + 0.5 * math.sin(a + phase)) + rng.uniform(0, jag * 0.35)
            f = 1.0 - (depth * (0.5 + 0.5 * math.cos(a * flutes)) if flutes else 0.0)
            ring.append(b.bm.verts.new((cx + r * f * math.cos(a), cy + r * f * math.sin(a), z)))
        rows.append(ring)
    for ra, rb in zip(rows, rows[1:]):
        for k in range(segs):
            b.bm.faces.new((ra[k], ra[(k + 1) % segs], rb[(k + 1) % segs], rb[k]))
    for f in b._tag(before, mat):
        b.lathe_faces.add(f)
        b.lathe_axis[f] = (cx, cy)
    before = set(b.bm.faces)
    b.bm.faces.new(list(reversed(rows[0])))
    top = rows[-1]
    if jag:
        c = b.bm.verts.new((cx, cy, z1 - jag * 0.45))
        for k in range(segs):
            b.bm.faces.new((top[k], top[(k + 1) % segs], c))
    else:
        b.bm.faces.new(top)
    b._tag(before, mat)


def _rock_wall(b, w, h, t, mat, seed, amp=0.1, step=0.08, crown=0.25):
    """A rough rock wall x -w/2..w/2, y -t/2..t/2, 0..h with a rounded crown.
    Noise only pushes the surface inward, so it never leaves its box (VB
    bounds); the ends settle to a fixed depth so neighbours line up."""
    before = set(b.bm.faces)
    hw, ht = w / 2, t / 2
    nz = max(4, int((h - crown) / step))
    prof = [((-ht, (h - crown) * i / nz), Vector((0, 1, 0))) for i in range(nz + 1)]
    for k in range(1, 8):
        a = math.pi * k / 8
        n = Vector((0, math.cos(a), -math.sin(a)))
        prof.append(((-ht * math.cos(a), (h - crown) + crown * math.sin(a)), n))
    prof += [((ht, (h - crown) * i / nz), Vector((0, -1, 0))) for i in range(nz, -1, -1)]
    nx = max(4, int(w / step))
    off = Vector((seed * 5.3, seed * 2.1, seed * 3.7))
    cols = []
    for i in range(nx + 1):
        x = -hw + w * i / nx
        edge = min(1.0, (hw - abs(x)) / 0.18)
        col = []
        for (y, z), n in prof:
            p = Vector((x, y, z))
            v = 0.5 + 0.5 * (noise.noise(p * 2.2 + off) * 0.55 + noise.noise(p * 6.0 + off) * 0.3)
            v += 0.35 * (1.0 - abs(noise.noise(p * 3.5 + off * 1.3))) - 0.2                  # ridges
            strata = ((z * 2.6 + 0.4 * noise.noise(p * 1.2 + off)) % 1.0)                     # ledges
            v += 0.45 * strata - 0.2
            d = amp * edge * max(0.0, min(1.0, v))
            col.append(b.bm.verts.new(p + n * d))
        cols.append(col)
    m = len(prof)
    for ca, cb in zip(cols, cols[1:]):
        for j in range(m - 1):
            b.bm.faces.new((ca[j], cb[j], cb[j + 1], ca[j + 1]))
    b.bm.faces.new(cols[0])
    b.bm.faces.new(list(reversed(cols[-1])))
    b._tag(before, mat)


def flat(ob):
    """Flat shading: chiselled rock reads as facets, not blobs."""
    for p in ob.data.polygons:
        p.use_smooth = False
    return ob


def _lie(obj, length_axis_up=True, r=0.0, yaw=0.0, loc=(0.0, 0.0, 0.0)):
    """Lay a piece built standing along +z down along +x, resting on the
    ground at height ``r`` (its radius), then turn and move it."""
    xform(obj, rot=(0.0, 90.0, 0.0))
    xform(obj, rot=(0.0, 0.0, yaw), loc=(loc[0], loc[1], loc[2] + r))
    return obj


def _ground_disc(b, r, z, mat, segs=16, cx=0.0, cy=0.0):
    _lathe(b, [(r, 0.0), (r * 0.96, z), (0.001, z + 0.002)], segs, mat, cx=cx, cy=cy)


# ═════════════════════════════════════════════════════════════════════════════
#  A-066 Keep / castle walls and gate. 50 cm curtain walls, 1.28 m modules
#  (two grid cells), a 3.6 m wall walk with a crenellated outer parapet
#  (outside = -y), battered footing. Towers and gate are the corners/breaks.
# ═════════════════════════════════════════════════════════════════════════════

KT, KH = 0.25, 3.6


def _merlons(b, x0, x1, y0, y1, z, mat, h=0.45, gap=0.16):
    width = x1 - x0
    n = max(1, round(width / 0.42))
    mw = (width - gap * n) / n
    x = x0 + gap / 2
    for _ in range(n):
        b.box((x, y0, z), (x + mw, y1, z + h), mat, bevel=0.012)
        x += mw + gap


def _keep_body(b, x0, x1, ztop=KH):
    prof = [(-KT - 0.1, 0.0), (KT + 0.1, 0.0), (KT + 0.1, 0.3), (KT, 0.75), (KT, ztop), (-KT, ztop), (-KT, 0.75), (-KT - 0.1, 0.3)]
    b.prism_yz(prof, x0, x1, KEEP)


def _keep_top(b, x0, x1, merlons=True):
    b.box((x0, -KT - 0.05, KH - 0.14), (x1, KT + 0.05, KH), KEEP, bevel=0.012)       # string course
    b.box((x0, -KT - 0.05, KH), (x1, -KT + 0.17, KH + 0.2), KEEP, bevel=0.008)      # outer parapet
    b.box((x0, KT - 0.12, KH), (x1, KT + 0.05, KH + 0.12), KEEP, bevel=0.008)       # inner kerb
    if merlons:
        _merlons(b, x0, x1, -KT - 0.05, -KT + 0.17, KH + 0.2, KEEP)


def keep_wall():
    b = MB("VB_KeepWall")
    _keep_body(b, -0.64, 0.64)
    _keep_top(b, -0.64, 0.64)
    return b.finish()


def keep_wall_slit():
    """The same wall with an arrow slit, recessed 12 cm on both faces."""
    b = MB("VB_KeepWall_Slit")
    _keep_body(b, -0.64, -0.05)
    _keep_body(b, 0.05, 0.64)
    _keep_body(b, -0.05, 0.05, ztop=1.5)
    b.box((-0.05, -KT, 2.4), (0.05, KT, KH), KEEP)
    b.box((-0.05, -KT + 0.12, 1.5), (0.05, KT - 0.12, 2.4), IRON)
    b.box((-0.1, -KT - 0.04, 1.44), (0.1, -KT + 0.02, 1.5), KEEP, bevel=0.008)      # sill
    b.box((-0.1, -KT - 0.04, 2.4), (0.1, -KT + 0.02, 2.48), KEEP, bevel=0.008)      # head
    _keep_top(b, -0.64, 0.64)
    return b.finish()


def keep_corner():
    """A square corner pier, 70 x 70 cm, one crenel higher than the walls."""
    b = MB("VB_KeepCorner")
    s = 0.35
    b.box((-s - 0.08, -s - 0.08, 0.0), (s + 0.08, s + 0.08, 0.3), KEEP, bevel=0.015)
    b.box((-s, -s, 0.3), (s, s, KH + 0.2), KEEP, bevel=0.01)
    b.box((-s - 0.05, -s - 0.05, KH + 0.06), (s + 0.05, s + 0.05, KH + 0.2), KEEP, bevel=0.012)
    for (x, y) in ((-s + 0.02, -s + 0.02), (s - 0.18, -s + 0.02), (-s + 0.02, s - 0.18), (s - 0.18, s - 0.18)):
        b.box((x - 0.07, y - 0.07, KH + 0.2), (x + 0.23, y + 0.23, KH + 0.7), KEEP, bevel=0.012)
    return b.finish()


def keep_tower():
    """A round tower, 2.16 m across, 4.8 m to the merlon tops: battered base,
    arrow slits, corbelled parapet with ten merlons."""
    b = MB("VB_KeepTower")
    _lathe(b, [(1.08, 0.0), (1.08, 0.3), (1.0, 0.75), (1.0, 3.9), (1.08, 4.0), (1.08, 4.35),
               (0.92, 4.35), (0.92, 4.1), (0.001, 4.1)], 40, KEEP, cap_top=False)
    for i in range(10):
        a = 2 * math.pi * i / 10
        _sector(b, a, a + 2 * math.pi / 10 * 0.62, 0.92, 1.08, 4.35, 4.8, KEEP, steps=2)
    for a, z0 in ((-math.pi / 2, 1.6), (-math.pi / 2 + 1.2, 2.7), (-math.pi / 2 - 1.2, 2.7), (math.pi / 2, 2.0)):
        _sector(b, a - 0.025, a + 0.025, 0.985, 1.006, z0, z0 + 0.7, IRON, steps=1)
    return b.finish()


def keep_gate():
    """A 2.56 m gatehouse front: a 1.1 m round-arched passage with a raised
    portcullis and machicolations over it. Walk-through, so SM_ not VB_."""
    b = MB("SM_KeepGate")
    _keep_body(b, -1.28, -0.55)
    _keep_body(b, 0.55, 1.28)
    zc, r_in, r_out = 1.5, 0.55, 0.78
    _arch(b, 0.0, zc, r_in, r_out, 0.0, math.pi, 9, -KT - 0.03, KT + 0.03, KEEP)
    for x0, x1 in ((-0.55, -0.3), (0.3, 0.55)):
        b.box((x0, -KT, 2.0), (x1, KT, KH), KEEP)
    b.box((-0.3, -KT, 2.2), (0.3, KT, KH), KEEP)
    for x in (-0.6, -0.3, 0.0, 0.3, 0.6):                                          # machicolation corbels
        b.box((x - 0.06, -KT - 0.16, KH - 0.5), (x + 0.06, -KT, KH - 0.14), KEEP, bevel=0.01)
    b.box((-0.72, -KT - 0.18, KH - 0.16), (0.72, -KT, KH), KEEP, bevel=0.01)
    # Portcullis in its slot, raised: bars hang down to 1.62 m.
    for i in range(8):
        x = -0.49 + 0.14 * i
        b.box((x - 0.018, -0.02, 1.62), (x + 0.018, 0.02, 2.3), IRON)
        b.loft([(1.62, [(x - 0.018, -0.02), (x + 0.018, -0.02), (x + 0.018, 0.02), (x - 0.018, 0.02)])], IRON,
               cap_bottom=False, tip=(x, 0.0, 1.54))
    for z in (1.7, 1.92):
        b.box((-0.52, -0.025, z), (0.52, 0.025, z + 0.035), IRON)
    _keep_top(b, -1.28, 1.28)
    return b.finish()


def keep_banner():
    """A swallow-tailed banner on an iron arm. Pivot at the wall fixing; it
    hangs out along -y like the tavern sign."""
    b = MB("SM_KeepBanner")
    b.box((-0.3, -0.16, -0.02), (0.3, -0.12, 0.02), IRON)                            # pole
    for x in (-0.3, 0.3):
        b.box((x - 0.02, -0.14, -0.04), (x + 0.02, 0.0, 0.0), IRON)                  # brackets
    b.prism_xz([(-0.26, 0.0), (0.26, 0.0), (0.26, -1.1), (0.0, -0.92), (-0.26, -1.1)][::-1], -0.145, -0.135, CANVASRED)
    for side, y0, y1 in ((-1, -0.152, -0.145), (1, -0.135, -0.128)):
        b.prism_xz([(0.0, -0.3), (0.13, -0.48), (0.0, -0.66), (-0.13, -0.48)][::-1], y0, y1, GOLD)
        b.box((-0.26, y0, -0.1), (0.26, y1, -0.06), GOLD)
    return b.finish()


KEEP_SET = [keep_wall, keep_wall_slit, keep_corner, keep_tower, keep_gate, keep_banner]


# ═════════════════════════════════════════════════════════════════════════════
#  A-067 Ruins: broken block walls, snapped columns, a half arch, a toppled
#  statue, rubble. Mossy stone; the column drums are the temple limestone.
# ═════════════════════════════════════════════════════════════════════════════

RT = 0.14


def ruin_wall():
    b = MB("VB_RuinWall")
    top = lambda x: 2.0 - 1.25 * kit.smoothstep(-0.25, 0.64, x) + 0.12 * _n1(x, 1)
    _block_wall(b, -0.64, 0.64, RT, top, RUIN, seed=1)
    return b.finish()


def ruin_wall_broken():
    b = MB("VB_RuinWall_Broken")
    top = lambda x: 0.4 + 1.4 * kit.smoothstep(0.1, 0.45, abs(x + 0.05)) + 0.12 * _n1(x, 2)
    _block_wall(b, -0.64, 0.64, RT, top, RUIN, seed=2)
    return b.finish()


def ruin_wall_low():
    b = MB("SM_RuinWall_Low")
    top = lambda x: 0.6 + 0.2 * _n1(x, 3, f=2.0)
    _block_wall(b, -0.64, 0.64, RT, top, RUIN, seed=3)
    return b.finish()


def ruin_corner():
    b1 = MB("VB_RuinCorner")
    _block_wall(b1, -RT, 0.64, RT, lambda x: 1.7 - 0.9 * kit.smoothstep(0.1, 0.64, x) + 0.1 * _n1(x, 4), RUIN, seed=4)
    o1 = b1.finish()
    b2 = MB("_leg")
    _block_wall(b2, RT, 0.64, RT, lambda x: 1.7 - 1.2 * kit.smoothstep(0.2, 0.64, x) + 0.1 * _n1(x, 5), RUIN, seed=5)
    o2 = rot_mesh(b2.finish(), 90)
    return join("VB_RuinCorner", [o1, o2])


def _column_base(b, mat_plinth, mat_col, r=0.14):
    b.box((-0.2, -0.2, 0.0), (0.2, 0.2, 0.1), mat_plinth, bevel=0.012)
    _lathe(b, [(r + 0.03, 0.1), (r + 0.04, 0.13), (r + 0.02, 0.16), (r + 0.01, 0.17)], 24, mat_col)


def ruin_column():
    b = MB("SM_RuinColumn")
    _column_base(b, RUIN, TEMPLE)
    _shaft(b, 0.17, 1.4, 0.14, 0.132, TEMPLE, jag=0.18, seed=6)
    return b.finish()


def ruin_column_fallen():
    """Two drums and a capital lying where they fell."""
    b1 = MB("_d1")
    _shaft(b1, 0.0, 0.9, 0.14, 0.136, TEMPLE, jag=0.1, seed=7, rings=6)
    d1 = _lie(b1.finish(), r=0.128, yaw=12, loc=(-0.55, -0.05, 0.0))
    b2 = MB("_d2")
    _shaft(b2, 0.0, 0.46, 0.136, 0.133, TEMPLE, rings=3)
    d2 = _lie(b2.finish(), r=0.125, yaw=-30, loc=(0.35, 0.2, 0.0))
    b3 = MB("_cap")
    b3.box((-0.21, -0.21, 0.0), (0.21, 0.21, 0.1), TEMPLE, bevel=0.012)
    _lathe(b3, [(0.19, 0.1), (0.17, 0.14), (0.14, 0.18)], 24, TEMPLE)
    cap = xform(b3.finish(), rot=(0.0, 0.0, 20), loc=(0.62, -0.32, -0.01))
    g = Geo("_chips")
    rng = random.Random(8)
    for k in range(5):
        rock(g, (rng.uniform(-0.8, 0.8), rng.uniform(-0.4, 0.4), 0.02), (0.05, 0.04, 0.03), TEMPLE, seed=40 + k, subdiv=1, chisel=4)
    return join("SM_RuinColumnFallen", [d1, d2, cap, g.finish()])


def ruin_arch():
    """Two block piers and what is left of the arch between them (1.28 m)."""
    b = MB("SM_RuinArch")
    for x0, x1, s in ((-0.64, -0.4, 9), (0.4, 0.64, 10)):
        _block_wall(b, x0, x1, 0.18, lambda x: 1.4, RUIN, seed=s, loose=0.0)
    _arch(b, 0.0, 1.4, 0.4, 0.6, 0.42 * math.pi, math.pi, 6, -0.18, 0.18, RUIN)
    _arch(b, 0.0, 1.4, 0.4, 0.6, 0.0, 0.14 * math.pi, 1, -0.18, 0.18, RUIN)
    ob = b.finish()
    b2 = MB("_fallen")
    b2.box((-0.1, -0.16, 0.0), (0.1, 0.16, 0.18), RUIN, bevel=0.016)
    fallen = xform(b2.finish(), rot=(0.0, 0.0, 25), loc=(0.62, -0.42, 0.0))           # beside the pier, not in the way
    return join("SM_RuinArch", [ob, fallen])


def ruin_statue():
    """The robed saint again, snapped at the knees; the head lies at its feet."""
    b = MB("SM_RuinStatue")
    b.box((-0.3, -0.3, 0.0), (0.3, 0.3, 0.12), RUIN, bevel=0.012)
    b.box((-0.25, -0.25, 0.12), (0.25, 0.25, 0.4), TEMPLE, bevel=0.01)
    b.box((-0.28, -0.28, 0.4), (0.28, 0.28, 0.46), TEMPLE, bevel=0.01)
    _shaft(b, 0.46, 1.0, 0.2, 0.17, MARBLE, segs=20, flutes=7, depth=0.1, rings=5, jag=0.2, seed=11)
    ob = b.finish()
    g = Geo("_head")
    g.blob((0.1, -0.52, 0.09), 0.095, MARBLE, squash=1.0, seed=6, rough=0.06, subdiv=2)
    g.blob((0.04, -0.58, 0.07), 0.05, MARBLE, squash=1.0, seed=7, rough=0.02, subdiv=1)
    rock(g, (-0.3, -0.45, 0.03), (0.08, 0.06, 0.05), MARBLE, seed=12, subdiv=1, chisel=4)
    return join("SM_RuinStatue", [ob, g.finish()])


def rubble_pile():
    """Fallen blocks and broken stone in a heap."""
    rng = random.Random(13)
    parts = []
    for k in range(8):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.0, 0.4) if k else 0.0
        w, dp, h = rng.uniform(0.2, 0.34), rng.uniform(0.16, 0.24), rng.uniform(0.14, 0.2)
        bb = MB("_blk%d" % k)
        bb.box((-w / 2, -dp / 2, -h / 2), (w / 2, dp / 2, h / 2), RUIN, bevel=0.02)
        tilt = (rng.uniform(-18, 18), rng.uniform(-18, 18), rng.uniform(0, 180))
        z = h * 0.4 + (0.12 if k in (1, 2) and d < 0.25 else 0.0)
        parts.append(xform(bb.finish(), rot=tilt, loc=(d * math.cos(a), d * math.sin(a) * 0.8, z)))
    g = Geo("_chips")
    for k in range(6):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.2, 0.55)
        s = rng.uniform(0.04, 0.08)
        rock(g, (d * math.cos(a), d * math.sin(a) * 0.8, 0.015), (s * 1.3, s, s * 0.7), MOSSY if k % 2 else RUIN,
             seed=60 + k, rough=0.08, subdiv=1, chisel=6)
    parts.append(g.finish())
    return join("SM_RubblePile", parts)


RUINS = [ruin_wall, ruin_wall_broken, ruin_wall_low, ruin_corner, ruin_column, ruin_column_fallen, ruin_arch,
         ruin_statue, rubble_pile]


# ═════════════════════════════════════════════════════════════════════════════
#  A-068 Bridge and river bank. Water tiles are 3 cm slabs, ground tiles 6 cm;
#  bank strips sit over the land/water boundary (land at -y, water at +y).
# ═════════════════════════════════════════════════════════════════════════════

def bridge_stone():
    """A humped stone bridge, 3.84 m long (x), 1.0 m wide, deck 0.7 m at the
    crown, one arch with a dressed voussoir ring and parapets."""
    b = MB("SM_BridgeStone")
    L, W, crown = 1.92, 0.52, 0.62                  # 80 cm between parapets (capsule is 60)

    def deck(x):
        return 0.07 + crown * math.cos(0.5 * math.pi * x / L) ** 1.2

    xs = [-L + 2 * L * i / 24 for i in range(25)]
    arch = [(1.0 * math.cos(math.pi * k / 12), 0.46 * math.sin(math.pi * k / 12)) for k in range(13)]
    elev = [(x, deck(x)) for x in reversed(xs)] + [(-L, 0.0), (-1.0, 0.0)] + arch[::-1][1:-1] + [(1.0, 0.0), (L, 0.0)]
    b.prism_xz(elev, -W, W, STONE)
    ring = [(1.18 * math.cos(math.pi * k / 12), 0.6 * math.sin(math.pi * k / 12)) for k in range(13)]
    for y0, y1 in ((-W - 0.02, -W + 0.01), (W - 0.01, W + 0.02)):
        for k in range(12):
            pts = [arch[k], ring[k], ring[k + 1], arch[k + 1]]
            b.prism_xz(pts[::-1], y0, y1, KEEP)
    for y0, y1 in ((-W - 0.03, -W + 0.1), (W - 0.1, W + 0.03)):
        top = [(x, deck(x) + 0.26) for x in xs]
        bot = [(x, deck(x) - 0.02) for x in reversed(xs)]
        b.prism_xz((top + bot)[::-1], y0, y1, KEEP)
        cap = [(x, deck(x) + 0.3) for x in xs] + [(x, deck(x) + 0.25) for x in reversed(xs)]
        b.prism_xz(cap[::-1], y0 - 0.015, y1 + 0.015, STONE)
    for xc in (-L + 0.08, L - 0.08):                                                   # end posts
        for y0, y1 in ((-W - 0.06, -W + 0.1), (W - 0.1, W + 0.06)):
            b.box((xc - 0.08, y0, 0.0), (xc + 0.08, y1, 0.45), KEEP, bevel=0.012)
    return b.finish()


def bridge_wood():
    """A plank footbridge, 2.56 m (x), deck 12 cm up on log posts, rope rails."""
    b = MB("SM_BridgeWood")
    rng = random.Random(21)
    for y in (-0.28, 0.28):
        b.box((-1.28, y - 0.04, 0.04), (1.28, y + 0.04, 0.1), DARKPL, bevel=0.008)          # stringers
    x = -1.26
    while x < 1.26:
        w = rng.uniform(0.11, 0.14)
        dz = rng.uniform(-0.006, 0.006)
        b.box((x, -0.36 + rng.uniform(-0.02, 0.0), 0.1), (min(x + w, 1.28), 0.36 + rng.uniform(0.0, 0.02), 0.13 + dz), OAK, bevel=0.006)
        x += w + 0.012
    ob = b.finish()
    g = Geo("_posts")
    posts = [(x, y) for x in (-1.2, 0.0, 1.2) for y in (-0.4, 0.4)]
    for i, (x, y) in enumerate(posts):
        g.tube([(x, y, -0.02, 0.045), (x, y, 0.6, 0.04)], 8, LOG, seed=i, gnarl=0.08, cap_start=True, tip=False)
        g.blob((x, y, 0.6), 0.04, LOG, squash=0.5, seed=i, rough=0.05, subdiv=1)
    for y in (-0.4, 0.4):
        for x0, x1 in ((-1.2, 0.0), (0.0, 1.2)):
            pts = [(x0 + (x1 - x0) * t, y, 0.55 - 0.08 * math.sin(math.pi * t), 0.012) for t in (0, 0.25, 0.5, 0.75, 1.0)]
            g.tube(pts, 6, ROPE, seed=3, gnarl=0.0, tip=False)
    return join("SM_BridgeWood", [ob, g.finish()])


def _bank_surface(b, x0, x1, fade_ends=True, seed=0):
    ys = [-0.2 + 0.34 * j / 8 for j in range(9)]
    nx = 10
    grid = []
    for i in range(nx + 1):
        x = x0 + (x1 - x0) * i / nx
        row = []
        for y in ys:
            s = (y + 0.2) / 0.34
            z = 0.062 * (1 - kit.smoothstep(0.0, 1.0, s)) - 0.004
            z += 0.012 * noise.noise(Vector((x * 6 + seed, y * 6, 0.3))) * 4 * s * (1 - s)
            row.append(b.bm.verts.new((x, y, z)))
        grid.append(row)
    before = set(b.bm.faces)
    for ra, rb in zip(grid, grid[1:]):
        for j in range(len(ys) - 1):
            b.bm.faces.new((ra[j], rb[j], rb[j + 1], ra[j + 1]))
    b._tag(before, BANK)


def _bank_dressing(g, rng, xs, seed):
    for k, x in enumerate(xs):
        s = rng.uniform(0.025, 0.055)
        rock(g, (x, rng.uniform(-0.04, 0.06), 0.02), (s * 1.3, s, s * 0.6), MOSSY, seed=seed + k, rough=0.1, subdiv=1, chisel=4)


def river_bank():
    b = MB("SM_RiverBank_Edge")
    _bank_surface(b, -0.32, 0.32, seed=1)
    ob = b.finish()
    g = Geo("_dress")
    rng = random.Random(31)
    _bank_dressing(g, rng, [rng.uniform(-0.28, 0.28) for _ in range(6)], 300)
    cards(g, (0.14, 0.05, 0.0), 0.22, 0.42, 3, REEDS, rng)
    return join("SM_RiverBank_Edge", [ob, g.finish()])


def river_bank_corner():
    """Covers the point where two bank strips meet at a corner: a mud and
    pebble mound with reeds."""
    b = MB("SM_RiverBank_Corner")
    _lathe(b, [(0.26, -0.004), (0.2, 0.03), (0.1, 0.06), (0.001, 0.068)], 16, BANK, cap_bottom=False)
    ob = b.finish()
    g = Geo("_dress")
    rng = random.Random(32)
    for k in range(7):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.08, 0.22)
        s = rng.uniform(0.025, 0.05)
        rock(g, (d * math.cos(a), d * math.sin(a), 0.02 + 0.03 * (1 - d / 0.26)), (s * 1.3, s, s * 0.6), MOSSY, seed=320 + k, subdiv=1, chisel=4)
    cards(g, (0.04, 0.03, 0.02), 0.24, 0.5, 3, REEDS, rng)
    return join("SM_RiverBank_Corner", [ob, g.finish()])


def reeds():
    g = Geo("SM_Reeds")
    rng = random.Random(33)
    for (x, y, h) in ((0.0, 0.0, 0.62), (0.16, 0.08, 0.5), (-0.12, 0.1, 0.46), (0.05, -0.14, 0.4)):
        cards(g, (x, y, -0.02), 0.26, h, 3, REEDS, rng, lean=0.1)
    return g.finish()


def stepping_stones():
    g = Geo("SM_SteppingStones")
    rng = random.Random(34)
    for k, x in enumerate((-0.5, -0.12, 0.26, 0.6)):
        s = rng.uniform(0.13, 0.16)
        rock(g, (x, rng.uniform(-0.06, 0.06), 0.0), (s * 1.2, s, 0.07), MOSSY, seed=340 + k, rough=0.08, subdiv=2, chisel=5)
    return g.finish()


RIVER = [bridge_stone, bridge_wood, river_bank, river_bank_corner, reeds, stepping_stones]


# ═════════════════════════════════════════════════════════════════════════════
#  A-069 Cave kit (Greyfell Cave, B-06). Rock walls 1.28 x 0.64 x 2.4 m,
#  pillars to hide joints and turn corners, a ceiling lip that overhangs the
#  floor without roofing it (top-down camera), floor tiles, stalagmites,
#  stalactites and glowing crystals.
# ═════════════════════════════════════════════════════════════════════════════

def cave_wall():
    b = MB("VB_CaveWall")
    _rock_wall(b, 1.28, 2.4, 0.64, CAVE, seed=1, amp=0.2, step=0.06)
    return flat(b.finish())


def cave_wall_low():
    b = MB("VB_CaveWall_Low")
    _rock_wall(b, 1.28, 1.4, 0.64, CAVE, seed=2, amp=0.2, step=0.06, crown=0.3)
    return flat(b.finish())


def cave_pillar():
    """A rough rock column, 2.5 m, ledged like the walls: hides wall joints
    and turns corners."""
    b = MB("SM_CavePillar")
    segs, nz, h = 18, 34, 2.5
    off = Vector((7.1, 3.3, 5.9))
    before = set(b.bm.faces)
    rings = []
    for i in range(nz + 1):
        t = i / nz
        z = h * t
        R = 0.46 - 0.1 * t
        if t > 0.85:
            R *= math.sqrt(max(0.0, 1 - ((t - 0.85) / 0.15) ** 2)) * 0.8 + 0.2
        ring = []
        for k in range(segs):
            a = 2 * math.pi * k / segs
            d = Vector((math.cos(a), math.sin(a), 0.0))
            p = d * R + Vector((0, 0, z))
            v = 0.5 + 0.5 * (noise.noise(p * 2.2 + off) * 0.55 + noise.noise(p * 6.0 + off) * 0.3)
            v += 0.35 * (1.0 - abs(noise.noise(Vector((p.x * 3.0, p.y * 3.0, p.z * 1.2)) + off * 1.3))) - 0.15     # vertical ribs
            v += 0.22 * ((z * 1.7 + 1.1 * noise.noise(p * 0.8 + off)) % 1.0) - 0.1               # broken ledges
            r = R * (1.0 - 0.34 * max(0.0, min(1.0, v)))
            ring.append(b.bm.verts.new((r * math.cos(a), r * math.sin(a), z)))
        rings.append(ring)
    for ra, rb in zip(rings, rings[1:]):
        for k in range(segs):
            b.bm.faces.new((ra[k], ra[(k + 1) % segs], rb[(k + 1) % segs], rb[k]))
    c = b.bm.verts.new((0.0, 0.0, h))
    for k in range(segs):
        b.bm.faces.new((rings[-1][k], rings[-1][(k + 1) % segs], c))
    b.bm.faces.new(list(reversed(rings[0])))
    b._tag(before, CAVE)
    ob = flat(b.finish())
    g = Geo("_foot")
    rock(g, (0.34, -0.26, 0.14), (0.24, 0.2, 0.2), CAVE, seed=9, rough=0.3, subdiv=2, chisel=7)
    rock(g, (-0.3, 0.3, 0.1), (0.18, 0.16, 0.14), CAVE, seed=10, rough=0.3, subdiv=2, chisel=7)
    return join("SM_CavePillar", [ob, flat(g.finish())])


def cave_ceiling_edge():
    """A rock lip on top of a cave wall, reaching 0.9 m out over the floor at
    -y. Same pivot as the wall under it."""
    b = MB("SM_CaveCeilingEdge")
    _rock_wall(b, 1.28, 1.22, 0.42, CAVE, seed=5, amp=0.16, step=0.06, crown=0.2)
    ob = flat(b.finish())
    return xform(ob, rot=(90.0, 0.0, 0.0), loc=(0.0, 0.32, 2.56))


def cave_floor():
    ob = kit.ground_tile("SM_CaveFloor", CAVEFLOOR, top=0.06, size=1.28, res=16)
    for v in ob.data.vertices:
        if v.co.z > 0.03:
            fade = min(1.0, (0.64 - max(abs(v.co.x), abs(v.co.y))) / 0.16)
            v.co.z = 0.06 + fade * 0.025 * noise.noise(Vector((v.co.x * 3.0, v.co.y * 3.0, 7.0)))
    ob.data.update()
    return ob


def _spike(g, base, h, r, mat, seed, down=False):
    x, y, z = base
    sgn = -1.0 if down else 1.0
    path = [(x, y, z - sgn * 0.03, r), (x, y, z + sgn * h * 0.3, r * 0.7), (x + 0.01, y, z + sgn * h * 0.7, r * 0.35),
            (x, y + 0.01, z + sgn * h, r * 0.08)]
    g.tube(path, 8, mat, seed=seed, gnarl=0.2, cap_start=True, tip=True)


def stalagmites():
    g = Geo("SM_Stalagmites")
    for i, (x, y, h, r) in enumerate(((0.0, 0.0, 1.05, 0.13), (0.2, 0.1, 0.62, 0.09), (-0.16, 0.14, 0.45, 0.07),
                                      (0.1, -0.18, 0.34, 0.06), (-0.2, -0.1, 0.25, 0.05))):
        _spike(g, (x, y, 0.0), h, r, CAVE, seed=i)
    rock(g, (0.0, 0.0, 0.02), (0.3, 0.26, 0.08), CAVE, seed=9, subdiv=2, chisel=4)
    return g.finish()


def stalactites():
    """Hanging from 2.56 m, under SM_CaveCeilingEdge (pivot still on the floor)."""
    g = Geo("SM_Stalactites")
    for i, (x, y, h, r) in enumerate(((0.0, 0.0, 0.7, 0.1), (0.2, 0.08, 0.45, 0.07), (-0.18, 0.1, 0.35, 0.06),
                                      (0.08, -0.16, 0.28, 0.05))):
        _spike(g, (x, y, 2.56), h, r, CAVE, seed=10 + i, down=True)
    return g.finish()


def crystals():
    """A cluster of glowing hexagonal crystals growing from a rock."""
    b = MB("_crystals")
    rng = random.Random(41)
    specs = [(0.0, 0.0, 0.7, 0.07, 0.0, 0.0), (0.12, 0.05, 0.45, 0.05, 25, 30), (-0.1, 0.08, 0.5, 0.05, -20, 140),
             (0.05, -0.12, 0.35, 0.04, 30, 250), (-0.12, -0.08, 0.3, 0.035, -30, 200), (0.16, -0.06, 0.25, 0.03, 40, 300)]
    for x, y, L, r, tilt, yaw in specs:
        sec = [(r * math.cos(math.pi * k / 3), r * math.sin(math.pi * k / 3)) for k in range(6)]
        before = set(b.bm.faces)
        b.loft([(-0.05, sec), (L * 0.78, [(p[0] * 0.92, p[1] * 0.92) for p in sec])], CRYSTAL, tip=(0.0, 0.0, L))
        new_verts = {v for f in b.bm.faces if f not in before for v in f.verts}
        m = Matrix.Translation((x, y, 0.02)) @ mathutils.Euler((0.0, math.radians(tilt), math.radians(yaw)), "ZXY").to_matrix().to_4x4()
        for v in new_verts:
            v.co = m @ v.co
    ob = b.finish()
    g = Geo("_base")
    rock(g, (0.0, 0.0, 0.03), (0.24, 0.2, 0.09), CAVE, seed=42, subdiv=2, chisel=5)
    return join("SM_Crystals", [ob, g.finish()])


def cave_rubble():
    g = Geo("SM_CaveRubble")
    rng = random.Random(43)
    for k in range(9):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.0, 0.4)
        s = rng.uniform(0.05, 0.14)
        rock(g, (d * math.cos(a), d * math.sin(a), s * 0.4), (s * 1.2, s, s * 0.75), CAVE, seed=430 + k, rough=0.3, subdiv=2, chisel=8)
    return flat(g.finish())


CAVE_SET = [cave_wall, cave_wall_low, cave_pillar, cave_ceiling_edge, cave_floor, stalagmites, stalactites, crystals, cave_rubble]


# ═════════════════════════════════════════════════════════════════════════════
#  A-072 Camp props (EQ-style NPC camps).
# ═════════════════════════════════════════════════════════════════════════════

def tent_a():
    """An A-frame canvas tent, ridge along x, door at -x with one flap tied back."""
    b = MB("SM_TentA")
    sect = [(-0.56, 0.0), (-0.535, 0.0), (0.0, 0.93), (0.535, 0.0), (0.56, 0.0), (0.0, 0.97)]
    b.prism_yz(sect, -0.62, 0.62, CANVAS)
    b.prism_yz([(-0.535, 0.0), (0.535, 0.0), (0.0, 0.93)], 0.6, 0.62, CANVAS)                 # back wall
    b.prism_yz([(-0.535, 0.0), (-0.2, 0.0), (0.0, 0.93)], -0.64, -0.62, CANVAS)              # closed flap
    b.prism_yz([(0.2, 0.0), (0.45, 0.02), (0.0, 0.93)], -0.66, -0.64, CANVAS)                 # tied-back flap
    b.box((-0.6, -0.4, 0.0), (0.58, 0.4, 0.012), WOOL)                                       # groundsheet
    for x in (-0.63, 0.63):
        b.box((x - 0.02, -0.02, 0.0), (x + 0.02, 0.02, 1.02), TIMBER)
    b.box((-0.66, -0.02, 0.96), (0.66, 0.02, 1.0), TIMBER)                                   # ridge pole
    ob = b.finish()
    g = Geo("_ropes")
    for x, xe in ((-0.63, -1.0), (0.63, 1.0)):
        g.tube([(x, 0.0, 0.98, 0.008), (xe, 0.0, 0.02, 0.008)], 5, ROPE, gnarl=0.0, tip=False)
        g.tube([(xe, 0.0, -0.05, 0.015), (xe - 0.02 * (1 if xe > 0 else -1), 0.0, 0.08, 0.012)], 5, TIMBER, gnarl=0.0, cap_start=True)
    return join("SM_TentA", [ob, g.finish()])


def tent_b():
    """A round bell tent in red and cream with a dark doorway and guy ropes."""
    b = MB("SM_TentB")
    _lathe(b, [(0.72, 0.0), (0.72, 0.34)], 24, CANVASRED, cap_bottom=False, cap_top=False)
    _lathe(b, [(0.72, 0.34), (0.75, 0.37), (0.12, 1.2)], 24, CANVAS, cap_bottom=False, cap_top=False)
    _lathe(b, [(0.12, 1.2), (0.03, 1.28)], 24, CANVASRED, cap_bottom=False)
    _lathe(b, [(0.02, 1.28), (0.02, 1.5), (0.001, 1.52)], 6, TIMBER)
    b.prism_xz([(0.0, 1.5), (0.22, 1.45), (0.0, 1.4)], -0.004, 0.004, CANVASRED)            # pennant

    def r_at(z):
        return 0.72 if z <= 0.34 else 0.75 - (z - 0.37) / 0.83 * 0.63
    before = set(b.bm.faces)
    zs = [0.0, 0.17, 0.34, 0.5, 0.65, 0.8]
    rows = []
    for z in zs:
        hw = 0.3 * (1 - z / 0.9)
        r = r_at(z) + 0.012
        rows.append([b.bm.verts.new((r * math.cos(-math.pi / 2 + s * hw), r * math.sin(-math.pi / 2 + s * hw), z)) for s in (-1, 1)])
    for ra, rb in zip(rows, rows[1:]):
        b.bm.faces.new((ra[0], ra[1], rb[1], rb[0]))
    b._tag(before, WOOL)
    ob = b.finish()
    g = Geo("_ropes")
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        g.tube([(0.5 * math.cos(a), 0.5 * math.sin(a), 0.66, 0.008), (1.05 * math.cos(a), 1.05 * math.sin(a), 0.02, 0.008)], 5, ROPE, gnarl=0.0, tip=False)
    return join("SM_TentB", [ob, g.finish()])


def _fire_ring(g, rng, r=0.28, seed=0):
    for k in range(10):
        a = 2 * math.pi * k / 10 + rng.uniform(-0.1, 0.1)
        s = rng.uniform(0.06, 0.08)
        rock(g, (r * math.cos(a), r * math.sin(a), 0.03), (s * 1.2, s, s * 0.8), CAVE, seed=seed + k, subdiv=1, chisel=4)
    for i, a in enumerate((0.3, 1.9, 3.5, 5.0)):
        d = Vector((math.cos(a), math.sin(a), 0.0))
        p0, p1 = d * 0.2, d * -0.02 + Vector((0, 0, 0.1))
        g.tube([(p0.x, p0.y, 0.03, 0.03), (p1.x, p1.y, p1.z, 0.025)], 7, LOG, seed=seed + 20 + i, gnarl=0.1, cap_start=True, tip=False)
    flame(g, (0.0, 0.0, 0.05), 0.52, 0.26, seed=seed + 5)


def campfire():
    b = MB("_ash")
    _ground_disc(b, 0.24, 0.015, IRON)
    g = Geo("_fire")
    _fire_ring(g, random.Random(51), seed=510)
    return join("SM_Campfire", [b.finish(), g.finish()])


def campfire_cooking():
    """The campfire with a tripod and a pot hanging over it."""
    b = MB("_pot")
    _ground_disc(b, 0.24, 0.015, IRON)
    _lathe(b, [(0.06, 0.42), (0.11, 0.46), (0.12, 0.54), (0.1, 0.6), (0.11, 0.61), (0.09, 0.6), (0.001, 0.56)], 14, IRON)
    b.box((-0.004, -0.004, 0.61), (0.004, 0.004, 0.93), IRON)                                 # chain
    g = Geo("_fire")
    _fire_ring(g, random.Random(52), seed=520)
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.4
        g.tube([(0.45 * math.cos(a), 0.45 * math.sin(a), -0.02, 0.022), (0.0, 0.0, 0.98, 0.018)], 6, LOG, seed=k, gnarl=0.06, cap_start=True)
    return join("SM_CampfireCooking", [b.finish(), g.finish()])


def bedroll():
    """An unrolled bedroll with a rolled blanket for a pillow."""
    b = MB("SM_Bedroll")
    b.box((-0.45, -0.2, 0.0), (0.45, 0.2, 0.035), WOOL, bevel=0.012)
    b.box((-0.3, -0.195, 0.03), (0.44, 0.195, 0.05), CLOTHBLUE, bevel=0.01)                    # blanket
    ob = b.finish()
    b2 = MB("_roll")
    _lathe(b2, [(0.07, -0.18), (0.075, 0.0), (0.07, 0.18)], 12, CLOTHCREAM)
    for z in (-0.1, 0.1):
        _lathe(b2, [(0.077, z - 0.012), (0.077, z + 0.012)], 12, LEATHER, cap_bottom=False, cap_top=False)
    roll = xform(b2.finish(), rot=(90.0, 0.0, 0.0), loc=(-0.38, 0.0, 0.1))
    return join("SM_Bedroll", [ob, roll])


def _sword(name, blade=0.5):
    b = MB(name)
    b.loft([(0.0, [(-0.025, -0.006), (0.025, -0.006), (0.025, 0.006), (-0.025, 0.006)]),
            (blade * 0.9, [(-0.02, -0.005), (0.02, -0.005), (0.02, 0.005), (-0.02, 0.005)])], STEEL, tip=(0.0, 0.0, blade))
    b.box((-0.08, -0.015, -0.03), (0.08, 0.015, 0.0), IRON, bevel=0.004)
    b.box((-0.014, -0.014, -0.15), (0.014, 0.014, -0.03), LEATHER)
    _lathe(b, [(0.025, -0.19), (0.028, -0.17), (0.02, -0.15)], 8, IRON)
    ob = b.finish()
    return xform(ob, loc=(0.0, 0.0, 0.19))


def weapon_rack():
    """A trestle rack: two spears, two swords and a round shield leaning on it."""
    b = MB("SM_WeaponRack")
    for x in (-0.5, 0.5):
        for s in (-1, 1):
            b.prism_yz([(s * 0.24, 0.0), (s * 0.18, 0.0), (0.0, 0.68), (s * 0.05, 0.68)][:: (1 if s > 0 else -1)], x - 0.025, x + 0.025, OAK)
    b.box((-0.56, -0.03, 0.62), (0.56, 0.03, 0.68), OAK, bevel=0.006)
    b.box((-0.5, -0.02, 0.18), (0.5, 0.02, 0.22), OAK)
    ob = b.finish()
    parts = [ob]
    g = Geo("_spears")
    for i, x in enumerate((-0.35, -0.22)):
        top = Vector((x, 0.05, 1.3))
        base = Vector((x, -0.34, 0.0))
        d = (top - base).normalized()
        g.tube([(base.x, base.y, base.z, 0.014), (top.x, top.y, top.z, 0.012)], 6, LOG, seed=i, gnarl=0.0, cap_start=True, tip=False)
        t2 = top + d * 0.16
        g.tube([(top.x, top.y, top.z, 0.028), (t2.x, t2.y, t2.z, 0.002)], 4, STEEL, gnarl=0.0, cap_start=True)
    parts.append(g.finish())
    for i, x in enumerate((0.0, 0.12)):
        parts.append(xform(_sword("_sw%d" % i), rot=(-18.0, 0.0, 0.0), loc=(x, -0.24, 0.0)))
    b3 = MB("_shield")
    _lathe(b3, [(0.24, 0.0), (0.24, 0.03), (0.001, 0.03)], 20, OAK)
    _lathe(b3, [(0.245, -0.002), (0.25, 0.035), (0.23, 0.036)], 20, IRON, cap_bottom=False, cap_top=False)
    _lathe(b3, [(0.06, 0.03), (0.05, 0.06), (0.001, 0.07)], 12, IRON)
    parts.append(xform(b3.finish(), rot=(-72.0, 0.0, 0.0), loc=(0.36, -0.12, 0.24)))
    return join("SM_WeaponRack", parts)


def log_seat():
    b = MB("SM_LogSeat")
    _lathe(b, [(0.12, -0.4), (0.125, -0.2), (0.13, 0.2), (0.12, 0.4)], 12, LOG, cap_bottom=False, cap_top=False)
    _lathe(b, [(0.12, 0.4), (0.001, 0.402)], 12, OAK, cap_bottom=False)
    _lathe(b, [(0.001, -0.402), (0.12, -0.4)], 12, OAK, cap_top=False)
    return _lie(b.finish(), r=0.11)


def supply_pile():
    """Sacks, a banded crate and a small barrel."""
    b = MB("SM_SupplyPile")
    b.box((0.05, -0.2, 0.0), (0.45, 0.16, 0.3), OAK, bevel=0.01)
    for x in (0.1, 0.4):
        b.box((x - 0.02, -0.205, 0.0), (x + 0.02, 0.165, 0.305), IRON)
    _lathe(b, [(0.11, 0.0), (0.13, 0.12), (0.13, 0.2), (0.11, 0.32)], 14, OAK, cx=-0.35, cy=0.15)
    for z in (0.05, 0.27):
        _lathe(b, [(0.123, z - 0.012), (0.128, z), (0.123, z + 0.012)], 14, IRON, cx=-0.35, cy=0.15, cap_bottom=False, cap_top=False)
    ob = b.finish()
    g = Geo("_sacks")
    for i, (x, y, z, r) in enumerate(((-0.2, -0.12, 0.1, 0.14), (-0.02, 0.18, 0.1, 0.13), (-0.1, 0.02, 0.26, 0.12))):
        g.blob((x, y, z), r, CANVAS, squash=0.75, seed=60 + i, rough=0.12, subdiv=2)
        g.tube([(x, y, z + r * 0.6, r * 0.25), (x, y, z + r * 0.95, r * 0.14)], 8, ROPE, gnarl=0.2, tip=True)
    return join("SM_SupplyPile", [ob, g.finish()])


CAMP = [tent_a, tent_b, campfire, campfire_cooking, bedroll, weapon_rack, log_seat, supply_pile]


# ═════════════════════════════════════════════════════════════════════════════
#  A-073 Foliage variety (grasslands): bushes, flowers, grass, ferns, a dead
#  tree, a stump and a fallen log. Card materials are M_ValhallaFoliage.
# ═════════════════════════════════════════════════════════════════════════════

def bush_a():
    g = Geo("SM_BushA")
    rng = random.Random(61)
    for i, (x, y, z, r) in enumerate(((0.0, 0.0, 0.3, 0.3), (0.24, 0.08, 0.22, 0.22), (-0.2, 0.12, 0.2, 0.2), (0.05, -0.2, 0.2, 0.2))):
        nat._clump(g, (x, y, z), r, rng, 610 + i, cards_per_m2=55.0)
    return g.finish()


def bush_b():
    """A lower, wider flowering bush."""
    g = Geo("SM_BushB")
    rng = random.Random(62)
    clumps = ((0.0, 0.0, 0.2, 0.24), (0.28, 0.0, 0.16, 0.18), (-0.26, 0.05, 0.15, 0.18))
    for i, (x, y, z, r) in enumerate(clumps):
        nat._clump(g, (x, y, z), r, rng, 620 + i, cards_per_m2=50.0)
    for k in range(14):
        x, y, z, r = clumps[k % 3]
        a = rng.uniform(0, 2 * math.pi)
        c = (x + r * 0.6 * math.cos(a), y + r * 0.6 * math.sin(a), z + r * 0.55)
        g.card(c, (rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), 1.0), 0.14, rng.uniform(0, 6.28), FLOWB)
    return g.finish()


def grass_tuft():
    g = Geo("SM_GrassTuft")
    rng = random.Random(63)
    cards(g, (0.0, 0.0, -0.01), 0.34, 0.26, 3, GRASSC, rng)
    cards(g, (0.12, 0.08, -0.01), 0.24, 0.2, 2, GRASSC, rng, spin=0.6)
    return g.finish()


def _flower_patch(name, flower, seed):
    g = Geo(name)
    rng = random.Random(seed)
    for k in range(5):
        a = 2 * math.pi * k / 5 + rng.uniform(-0.3, 0.3)
        d = rng.uniform(0.08, 0.26) if k else 0.0
        c = (d * math.cos(a), d * math.sin(a), -0.01)
        cards(g, c, 0.26, 0.2, 2, GRASSC, rng, spin=rng.uniform(0, 3))
        cards(g, c, 0.2, 0.24, 2, flower, rng, spin=rng.uniform(0, 3))
    return g.finish()


def flowers_a():
    return _flower_patch("SM_FlowersA", FLOWA, 64)


def flowers_b():
    return _flower_patch("SM_FlowersB", FLOWB, 65)


def fern():
    """Arched fronds round a centre; each frond is a bent strip, full texture."""
    g = Geo("SM_Fern")
    rng = random.Random(66)
    n = 8
    for i in range(n):
        yaw = 2 * math.pi * i / n + rng.uniform(-0.2, 0.2)
        L = rng.uniform(0.34, 0.44)
        rise = rng.uniform(0.2, 0.28)
        d = Vector((math.cos(yaw), math.sin(yaw), 0.0))
        side = Vector((-math.sin(yaw), math.cos(yaw), 0.0))
        w = 0.14
        prev = None
        for s in range(6):
            t = s / 5
            p = d * L * t + Vector((0.0, 0.0, rise * math.sin(math.pi * t * 0.85) - 0.05 * t * t))
            va, vb = g.bm.verts.new(p - side * w / 2), g.bm.verts.new(p + side * w / 2)
            if prev:
                g.face((prev[0], prev[1], vb, va), ((0, prev[2]), (1, prev[2]), (1, t), (0, t)), FERN, smooth=False)
            prev = (va, vb, t)
    return g.finish()


def dead_tree():
    """A bare, weathered tree, 2.1 m, no leaves."""
    g = Geo("SM_DeadTree")
    trunk = [(0.0, 0.0, -0.04, 0.12), (0.0, 0.0, 0.12, 0.085), (0.03, 0.01, 0.6, 0.065), (0.06, 0.0, 1.1, 0.05),
             (0.04, -0.02, 1.55, 0.035), (0.02, -0.03, 1.95, 0.015)]
    g.tube(trunk, 9, LOG, seed=71, gnarl=0.18, cap_start=True, tip=True)
    limbs = [((0.03, 0.01, 0.75), (0.5, 0.2, 1.3), 0.035, 0.008, (0.0, 0.0, 0.1)),
             ((0.05, 0.0, 1.0), (-0.45, 0.15, 1.55), 0.03, 0.006, (0.0, 0.0, 0.08)),
             ((0.05, -0.01, 1.25), (0.25, -0.45, 1.8), 0.025, 0.005, (0.0, 0.0, 0.08)),
             ((0.35, 0.14, 1.15), (0.55, -0.05, 1.5), 0.015, 0.004, (0.0, 0.0, 0.04)),
             ((-0.3, 0.1, 1.38), (-0.5, -0.12, 1.75), 0.014, 0.004, (0.0, 0.0, 0.04))]
    for i, l in enumerate(limbs):
        g.tube(nat._limb(*l), 6, LOG, seed=72 + i, gnarl=0.12)
    for a in (0.3, 1.9, 3.3, 4.8):
        g.tube(nat._limb((0, 0, 0.12), (0.3 * math.cos(a), 0.3 * math.sin(a), -0.07), 0.055, 0.025, (0, 0, 0.04), n=4), 6, LOG, seed=79, gnarl=0.1, tip=False)
    return g.finish()


def stump():
    b = MB("_stump")
    _lathe(b, [(0.2, -0.02), (0.18, 0.06), (0.15, 0.16), (0.145, 0.3)], 14, LOG, cap_bottom=False, cap_top=False)
    _lathe(b, [(0.145, 0.3), (0.1, 0.31), (0.001, 0.305)], 14, OAK, cap_bottom=False)
    ob = b.finish()
    g = Geo("_roots")
    for i, a in enumerate((0.2, 1.5, 2.9, 4.2, 5.4)):
        g.tube(nat._limb((0, 0, 0.1), (0.34 * math.cos(a), 0.34 * math.sin(a), -0.07), 0.07, 0.03, (0, 0, 0.05), n=4), 6, LOG, seed=80 + i, gnarl=0.12, tip=False)
    return join("SM_Stump", [ob, g.finish()])


def log_fallen():
    b = MB("_log")
    _lathe(b, [(0.13, -0.7), (0.14, -0.3), (0.135, 0.3), (0.12, 0.7)], 12, LOG, cap_bottom=False, cap_top=False)
    _lathe(b, [(0.12, 0.7), (0.001, 0.705)], 12, OAK, cap_bottom=False)
    _lathe(b, [(0.001, -0.705), (0.13, -0.7)], 12, OAK, cap_top=False)
    ob = _lie(b.finish(), r=0.12)
    g = Geo("_stubs")
    for i, (x, a) in enumerate(((-0.3, 1.2), (0.2, 2.2), (0.45, 0.8))):
        d = Vector((0.0, math.cos(a), math.sin(a)))
        p0 = Vector((x, 0.0, 0.12)) + d * 0.1
        p1 = p0 + d * 0.16 + Vector((0.05, 0.0, 0.0))
        g.tube([(p0.x, p0.y, p0.z, 0.035), (p1.x, p1.y, p1.z, 0.024)], 6, LOG, seed=90 + i, gnarl=0.1)
    g.blob((0.05, 0.02, 0.22), 0.08, MOSSY, squash=0.3, seed=91, rough=0.2, subdiv=2)
    return join("SM_LogFallen", [ob, g.finish()])


FOLIAGE = [bush_a, bush_b, grass_tuft, flowers_a, flowers_b, fern, dead_tree, stump, log_fallen]


# ═════════════════════════════════════════════════════════════════════════════
#  A-074 Desert variety: bones, a horned skull, sandstone ruins, a nomad
#  awning, pottery.
# ═════════════════════════════════════════════════════════════════════════════

def bones():
    """A half-buried ribcage and two long bones."""
    g = Geo("SM_Bones")
    g.tube([(-0.36, 0.0, 0.03, 0.03), (-0.1, 0.0, 0.07, 0.028), (0.2, 0.01, 0.06, 0.025), (0.4, 0.0, 0.02, 0.02)], 7, BONE, seed=1, gnarl=0.25, cap_start=True)
    for i in range(6):
        x = -0.22 + 0.08 * i
        k = 1.0 - abs(i - 2) * 0.12
        for s in (-1, 1):
            g.tube([(x, 0.0, 0.07, 0.014), (x + 0.02, s * 0.13 * k, 0.15 * k, 0.012), (x + 0.04, s * 0.24 * k, 0.08, 0.01),
                    (x + 0.05, s * 0.27 * k, -0.01, 0.008)], 5, BONE, seed=10 + i, gnarl=0.1)
    for (x0, y0, x1, y1, sd) in ((0.2, -0.38, 0.55, -0.25, 3), (-0.52, 0.28, -0.2, 0.4, 4)):
        g.tube([(x0, y0, 0.025, 0.02), (x1, y1, 0.025, 0.018)], 6, BONE, seed=sd, gnarl=0.05, tip=False)
        for x, y in ((x0, y0), (x1, y1)):
            g.blob((x, y, 0.03), 0.035, BONE, squash=0.8, seed=sd, rough=0.2, subdiv=1)
    return g.finish()


def skull():
    """A long-horned animal skull, lying in the sand."""
    g = Geo("SM_Skull")
    g.blob((0.0, 0.05, 0.09), 0.1, BONE, squash=0.85, seed=1, rough=0.1, subdiv=2)
    g.tube([(0.0, 0.0, 0.08, 0.075), (0.0, -0.12, 0.06, 0.06), (0.0, -0.22, 0.045, 0.04)], 10, BONE, seed=2, gnarl=0.1, tip=False)
    g.blob((0.0, -0.23, 0.045), 0.045, BONE, squash=0.8, seed=3, rough=0.1, subdiv=1)
    for s in (-1, 1):
        g.blob((s * 0.055, -0.03, 0.12), 0.028, IRON, squash=1.0, seed=4, rough=0.0, subdiv=1)     # eye sockets
        g.blob((s * 0.018, -0.25, 0.05), 0.012, IRON, squash=1.0, seed=5, rough=0.0, subdiv=1)    # nostrils
        g.tube([(s * 0.07, 0.08, 0.13, 0.035), (s * 0.24, 0.07, 0.15, 0.028), (s * 0.38, 0.02, 0.24, 0.018),
                (s * 0.44, -0.04, 0.34, 0.006)], 8, BONE, seed=6, gnarl=0.05, cap_start=True)
    ob = g.finish()
    return xform(ob, rot=(-8.0, 0.0, 0.0), loc=(0.0, 0.0, -0.015))


def desert_ruin_wall():
    b = MB("VB_DesertRuinWall")
    top = lambda x: 1.75 - 1.25 * kit.smoothstep(-0.35, 0.6, x) + 0.15 * _n1(x, 7)
    _block_wall(b, -0.64, 0.64, RT, top, SBLOCK, seed=7, course=0.24, min_len=0.24, max_len=0.5)
    return b.finish()


def desert_ruin_column():
    b = MB("SM_DesertRuinColumn")
    _column_base(b, SBLOCK, SROCK, r=0.15)
    _shaft(b, 0.17, 1.1, 0.15, 0.145, SROCK, flutes=0, rings=6, jag=0.16, seed=8)
    ob = b.finish()
    b2 = MB("_drum")
    _shaft(b2, 0.0, 0.4, 0.15, 0.15, SROCK, flutes=0, rings=2, jag=0.06, seed=9)
    drum = _lie(b2.finish(), r=0.14, yaw=40, loc=(0.4, -0.42, 0.0))
    return join("SM_DesertRuinColumn", [ob, drum])


def desert_tent():
    """A nomad awning: striped roof on four poles, a back cloth, a rug and cushions."""
    b = MB("SM_DesertTent")
    for x in (-0.7, 0.7):
        for y, h in ((-0.5, 0.92), (0.5, 1.17)):
            b.box((x - 0.025, y - 0.025, 0.0), (x + 0.025, y + 0.025, h), TIMBER)
    n = 7
    for i in range(n):
        x0, x1 = -0.78 + 1.56 * i / n, -0.78 + 1.56 * (i + 1) / n
        b.prism_yz([(-0.6, 0.9), (0.56, 1.17), (0.56, 1.19), (-0.6, 0.92)], x0, x1, CANVASRED if i % 2 else CANVAS)
    for i in range(10):
        x0 = -0.78 + 0.156 * i
        b.prism_xz([(x0, 0.9), (x0 + 0.156, 0.9), (x0 + 0.078, 0.8)][::-1], -0.61, -0.6, CANVASRED if i % 2 else CANVAS)
    b.box((-0.72, 0.5, 0.0), (0.72, 0.52, 1.14), CANVAS)                                          # back cloth
    b.box((-0.62, -0.42, 0.0), (0.62, 0.42, 0.01), CLOTHCREAM)
    b.box((-0.56, -0.36, 0.0), (0.56, 0.36, 0.014), CANVASRED)
    for x in (-0.35, 0.0, 0.35):
        b.box((x - 0.13, 0.22, 0.014), (x + 0.13, 0.42, 0.12), CLOTHBLUE, bevel=0.035)
    ob = b.finish()
    g = Geo("_ropes")
    for x in (-0.7, 0.7):
        g.tube([(x, -0.5, 0.9, 0.007), (x * 1.15, -0.95, 0.02, 0.007)], 5, ROPE, gnarl=0.0, tip=False)
    return join("SM_DesertTent", [ob, g.finish()])


def _amphora(b, cx, cy, h=1.0, mat=POTTERY):
    prof = [(0.001, 0.0), (0.04, 0.0), (0.07, 0.05), (0.13, 0.19), (0.135, 0.27), (0.08, 0.4), (0.045, 0.44),
            (0.045, 0.5), (0.06, 0.515), (0.05, 0.53), (0.036, 0.5)]
    _lathe(b, [(r * h, z * h) for r, z in prof], 16, mat, cx=cx, cy=cy, cap_bottom=False)


def pottery():
    """Two amphorae, a squat urn, one amphora on its side and some shards."""
    b = MB("SM_Pottery")
    _amphora(b, 0.0, 0.0)
    _amphora(b, 0.24, 0.1, h=0.8)
    _lathe(b, [(0.001, 0.0), (0.08, 0.0), (0.15, 0.1), (0.15, 0.17), (0.09, 0.25), (0.1, 0.27), (0.08, 0.26)], 16, POTTERY, cx=-0.24, cy=0.12, cap_bottom=False)
    ob = b.finish()
    b2 = MB("_lying")
    _amphora(b2, 0.0, 0.0, h=0.85)
    lying = _lie(b2.finish(), r=0.11, yaw=-150, loc=(0.1, -0.28, 0.0))
    g = Geo("_bits")
    for s, (x, y) in enumerate(((0.0, 0.0), (0.24, 0.1))):
        hh = 1.0 if s == 0 else 0.8
        for side in (-1, 1):
            g.tube([(x + side * 0.045 * hh, y, 0.47 * hh, 0.012), (x + side * 0.1 * hh, y, 0.45 * hh, 0.012),
                    (x + side * 0.11 * hh, y, 0.37 * hh, 0.012)], 6, POTTERY, gnarl=0.0, tip=False)
    rng = random.Random(95)
    for k in range(5):
        rock(g, (rng.uniform(-0.4, 0.4), rng.uniform(-0.45, -0.15), 0.008), (0.045, 0.035, 0.012), POTTERY, seed=950 + k, rough=0.05, subdiv=1, chisel=4)
    return join("SM_Pottery", [ob, lying, g.finish()])


DESERT_VAR = [bones, skull, desert_ruin_wall, desert_ruin_column, desert_tent, pottery]


SETS.update({"Keep": KEEP_SET, "Ruins": RUINS, "River": RIVER, "Cave": CAVE_SET, "Camp": CAMP,
             "Foliage": FOLIAGE, "DesertVariety": DESERT_VAR})


def build_all(export=True):
    return {name: build_set(name, export=export) for name in SETS}
