"""B-15 Wave 3: the Desert kit.

A-038 SM_Sand_A / B / C, A-039 SM_Dune, A-040 SM_CrackedEarth,
A-041 SM_Cactus_A / B, A-042 SM_Palm, A-043 SM_Boulder_A,
A-044 VB_SandstoneWall_Straight / Corner / End, A-057 SM_PortalMarker.

Every piece replaces its first-pass mesh in place (same name, pivot at the
ground at the footprint centre, same bounds for the VB_ walls), so the Desert
level needs no edits. Ground tiles keep the first-pass heights (sand 7.3 cm,
cracked earth 6.2 cm) and are flat at their edges, so any tile meets any other.

Materials (slot name = Unreal MI name, bound by valhalla_tools.import_kit):
  MI_SandBlend     M_ValhallaGroundBlend: DesertSand (A) / SandRipples (B) by vertex red
  MI_CrackedBlend  M_ValhallaGroundBlend: DesertSand (A) / CrackedEarth (B)
  MI_SandstoneBlocks, MI_SandstoneRock, MI_PalmBark   M_ValhallaPBR texture sets
  MI_PalmFrond     M_ValhallaFoliage (procedural frond, wave3_palm_texture.py)
  MI_Cactus        M_ValhallaPBR (procedural rib texture, wave3_cactus_texture.py)
  MI_CactusFlower, MI_Coconut   flat M_ValhallaPBR
  MI_StoneWall, MI_Rock (Wave 1), M_PortalGlow (first pass, emissive)

Run in Blender: exec, then build_all().
"""

import importlib.util
import math
import os
import random

import bmesh
import bpy
import mathutils
from mathutils import Vector, noise

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


kit = _load("valhalla_kit")
nat = _load("wave1_nature")          # Geo (tubes, blobs, cards) and _fit
Geo = nat.Geo

DESERT = os.path.join(kit.IMPORT, "Environment", "Desert")
PROPS = os.path.join(kit.IMPORT, "Props")

SAND, CRACKED = "MI_SandBlend", "MI_CrackedBlend"
BLOCKS, SROCK, BARK, FROND = "MI_SandstoneBlocks", "MI_SandstoneRock", "MI_PalmBark", "MI_PalmFrond"
CACTUS, FLOWER, NUT = "MI_Cactus", "MI_CactusFlower", "MI_Coconut"
STONE, ROCK, GLOW = "MI_StoneWall", "MI_Rock", "M_PortalGlow"

kit.PREVIEW.update({SAND: (0.76, 0.66, 0.47), CRACKED: (0.62, 0.52, 0.40), BLOCKS: (0.74, 0.60, 0.42),
                    SROCK: (0.70, 0.55, 0.38), BARK: (0.30, 0.24, 0.17), FROND: (0.22, 0.34, 0.12),
                    CACTUS: (0.24, 0.36, 0.18), FLOWER: (0.85, 0.25, 0.45), NUT: (0.25, 0.17, 0.08),
                    STONE: (0.45, 0.44, 0.42), ROCK: (0.4, 0.4, 0.38), GLOW: (0.5, 0.3, 1.0)})

SAND_TOP, CRACK_TOP = 0.073, 0.062


# ── Ground ───────────────────────────────────────────────────────────────────

def _edge_fade(x, y, width=0.07, size=0.64):
    """0 at the tile edge, 1 once ``width`` inside: blend masks go to layer A
    at every edge, so a tile meets its neighbours without a seam."""
    h = size / 2.0
    d = min(h - abs(x), h - abs(y))
    return kit.smoothstep(0.0, width, d)


def _patch(x, y, cx, cy, rx, ry, seed, rough=0.35):
    """A soft, noisy blob mask (0..1) centred on (cx, cy)."""
    dx, dy = (x - cx) / rx, (y - cy) / ry
    n = noise.noise(Vector((x * 7.0 + seed, y * 7.0, seed * 0.37)))
    r = math.hypot(dx, dy) * (1.0 + rough * n)
    return 1.0 - kit.smoothstep(0.55, 1.0, r)


def tile(name, mat, top, height=None, mask=None, res=16, size=0.64):
    """kit.ground_tile with an optional height field ``height(x, y) -> z``.
    Edges stay at ``top`` when the height field does."""
    bm = bmesh.new()
    h = size / 2.0
    grid = []
    for j in range(res + 1):
        row = []
        for i in range(res + 1):
            x = -h + size * i / res
            y = -h + size * j / res
            z = height(x, y) if height else top
            row.append(bm.verts.new((x, y, z)))
        grid.append(row)
    for j in range(res):
        for i in range(res):
            # Split each quad along the shorter diagonal (smoother dunes).
            a, b, c, d = grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]
            if abs(a.co.z - c.co.z) <= abs(b.co.z - d.co.z):
                bm.faces.new((a, b, c)); bm.faces.new((a, c, d))
            else:
                bm.faces.new((a, b, d)); bm.faces.new((b, c, d))
    ring = [grid[0][i] for i in range(res + 1)] + [grid[j][res] for j in range(1, res + 1)] + \
           [grid[res][i] for i in range(res - 1, -1, -1)] + [grid[j][0] for j in range(res - 1, 0, -1)]
    # The skirt gets its own copy of the rim vertices, so the top's smooth
    # normals don't bend towards the vertical sides (which drew a light/dark
    # line round every dune in the game).
    rim = [bm.verts.new(v.co) for v in ring]
    low = [bm.verts.new((v.co.x, v.co.y, 0.0)) for v in ring]
    n = len(ring)
    for k in range(n):
        bm.faces.new((rim[(k + 1) % n], rim[k], low[k], low[(k + 1) % n]))
    bm.faces.new(list(reversed(low)))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    uv = bm.loops.layers.uv.new("UVMap")
    col = bm.loops.layers.color.new("Col")
    for f in bm.faces:
        side = abs(f.normal.z) < 0.5
        f.smooth = not side
        for loop in f.loops:
            x, y, z = loop.vert.co
            loop[uv].uv = ((x + y) / size + 0.5, z / size) if side else (x / size + 0.5, y / size + 0.5)
            r = mask(x, y) if mask else 0.0
            loop[col] = (max(0.0, min(1.0, r)), 0.0, 0.0, 1.0)
    return kit.mesh_object(name, bm, [mat])


def sand_a():
    return tile("SM_Sand_A", SAND, SAND_TOP, res=1)


def sand_b():
    # One wind-rippled patch, stretched across the wind (world-aligned ripples
    # run the same way on every tile, as if one wind shaped the whole desert).
    return tile("SM_Sand_B", SAND, SAND_TOP,
                mask=lambda x, y: _patch(x, y, 0.05, -0.04, 0.26, 0.16, 3.0) * _edge_fade(x, y))


def sand_c():
    def m(x, y):
        a = _patch(x, y, -0.12, 0.10, 0.15, 0.10, 7.0)
        b = _patch(x, y, 0.14, -0.13, 0.12, 0.09, 11.0)
        return max(a, b) * _edge_fade(x, y)
    return tile("SM_Sand_C", SAND, SAND_TOP, mask=m)


# The first-pass dune rose from the tile's -X edge to a 25 cm crest at +X. In
# the Desert every dune is its own actor standing on a sand tile, scaled 1.1-1.6
# and turned a few degrees, in diagonal chains (both diagonals occur). So this
# is only the mound, not a tile: a round barchan-like mound 0.96 m across, so
# neighbours along either diagonal (0.9 m apart) overlap into one bumpy ridge.
# Gentle on the windward side, steep on the slip face (the same wind for every
# dune). Its rim sits 2 cm up, below the sand tile's 7.3 cm top at any placed
# scale, so it rises out of the sand with no edge.
DUNE_PEAK, DUNE_RIM = 0.22, 0.02
DUNE_RU, DUNE_RW = 0.48, 0.48          # footprint half-axes (u across the wind, w along it)
_S2 = math.sqrt(0.5)


def _dune_shape(u, w):
    """(height 0..1, on the windward side?) at ridge coords u (along), w (across,
    + = downwind)."""
    nu = u / DUNE_RU
    wc = 0.06 + 0.03 * math.sin(nu * 2.6)          # the crest wanders a little
    if w <= wc:
        t = (w + DUNE_RW) / (wc + DUNE_RW)          # windward: gentle, convex
        k = kit.smoothstep(0.0, 1.0, t) ** 1.3
    else:
        t = (w - wc) / (DUNE_RW - wc)               # slip face: steep, then a toe
        k = 1.0 - kit.smoothstep(0.0, 0.9, t)
    crest = 0.85 + 0.15 * math.cos(nu * 3.3)        # saddles along the ridge
    r = math.hypot(nu, w / DUNE_RW)
    env = 1.0 - kit.smoothstep(0.35, 1.0, r)
    return k * env * crest, w <= wc - 0.02


def _to_uw(x, y):
    # Blender +Y is Unreal -Y (glTF), so the Unreal +X+Y diagonal is Blender X-Y.
    return (x - y) * _S2, (x + y) * _S2


def dune(rings=12, segs=48):
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    col = bm.loops.layers.color.new("Col")

    def vert(u, w):
        k, _ = _dune_shape(u, w)
        return bm.verts.new(((u + w) * _S2, (w - u) * _S2, DUNE_RIM + (DUNE_PEAK - DUNE_RIM) * k))

    centre = vert(0.0, 0.0)
    grid = []
    for i in range(1, rings + 1):
        r = i / rings
        grid.append([vert(DUNE_RU * r * math.cos(2 * math.pi * j / segs),
                          DUNE_RW * r * math.sin(2 * math.pi * j / segs)) for j in range(segs)])
    for j in range(segs):
        bm.faces.new((centre, grid[0][j], grid[0][(j + 1) % segs]))
    for i in range(rings - 1):
        for j in range(segs):
            j2 = (j + 1) % segs
            bm.faces.new((grid[i][j], grid[i + 1][j], grid[i + 1][j2], grid[i][j2]))
    bm.faces.new(list(reversed(grid[-1])))       # underside, buried
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    for f in bm.faces:
        f.smooth = f.normal.z > 0.0
        for loop in f.loops:
            x, y, z = loop.vert.co
            loop[uv].uv = (x / 0.64 + 0.5, y / 0.64 + 0.5)
            k, windward = _dune_shape(*_to_uw(x, y))
            loop[col] = (kit.smoothstep(0.3, 0.7, k) if windward else 0.0, 0.0, 0.0, 1.0)
    return kit.mesh_object("SM_Dune", bm, [SAND])


def cracked_earth():
    # A dry pan: cracked in the middle, drifted sand creeping in at the edges
    # (half and half at the very edge, so a cluster of pans still reads as one).
    def m(x, y):
        n = noise.noise(Vector((x * 9.0, y * 9.0, 1.7)))
        return 0.45 + 0.55 * _edge_fade(x, y, 0.16) + 0.25 * n
    return tile("SM_CrackedEarth", CRACKED, CRACK_TOP, mask=m)


# ── Walls ────────────────────────────────────────────────────────────────────
# Dressed sandstone: a projecting plinth course, a slightly battered body and a
# coping of individual capstones (a few worn lower), all inside the first-pass
# bounds (x -0.32..0.32, y -0.125..0.125, z 0..1.80).

T = 0.125
WALL_H = 1.80


def _caps(b, x0, x1, y0, y1, seed, along_x=True):
    rng = random.Random(seed)
    n = max(1, round(((x1 - x0) if along_x else (y1 - y0)) / 0.16))
    for i in range(n):
        if along_x:
            a, c = x0 + (x1 - x0) * i / n, x0 + (x1 - x0) * (i + 1) / n
            lo, hi = (a + 0.004, y0), (c - 0.004, y1)
        else:
            a, c = y0 + (y1 - y0) * i / n, y0 + (y1 - y0) * (i + 1) / n
            lo, hi = (x0, a + 0.004), (x1, c - 0.004)
        top = WALL_H - (0.0 if rng.random() > 0.3 else rng.uniform(0.015, 0.04))
        b.box((lo[0], lo[1], 1.70), (hi[0], hi[1], top), BLOCKS, bevel=0.012)


def _run(b, x0, x1, seed, y0=-T, y1=T):
    b.box((x0, y0, 0.0), (x1, y1, 0.14), BLOCKS, bevel=0.01)                   # plinth
    b.box((x0, y0 + 0.018, 0.14), (x1, y1 - 0.018, 1.62), BLOCKS, bevel=0.004)  # body
    b.box((x0, y0 + 0.008, 1.62), (x1, y1 - 0.008, 1.70), BLOCKS, bevel=0.008)  # string course
    _caps(b, x0, x1, y0 + 0.004, y1 - 0.004, seed)


def _run_y(b, y0, y1, seed, x0=-T, x1=T):
    b.box((x0, y0, 0.0), (x1, y1, 0.14), BLOCKS, bevel=0.01)
    b.box((x0 + 0.018, y0, 0.14), (x1 - 0.018, y1, 1.62), BLOCKS, bevel=0.004)
    b.box((x0 + 0.008, y0, 1.62), (x1 - 0.008, y1, 1.70), BLOCKS, bevel=0.008)
    _caps(b, x0 + 0.004, x1 - 0.004, y0, y1, seed, along_x=False)


def wall_straight():
    b = kit.MeshBuilder("VB_SandstoneWall_Straight")
    _run(b, -0.32, 0.32, 1)
    return b.finish()


def wall_corner():
    # First-pass corner: x -0.32..0.125, Blender y -0.32..0.125.
    b = kit.MeshBuilder("VB_SandstoneWall_Corner")
    _run(b, -0.32, T, 2)
    _run_y(b, -0.32, -T, 3)
    return b.finish()


def wall_end():
    # First pass: the run to x 0.30, and an end pier x 0.02..0.32, y +-0.17, 2.04 high.
    b = kit.MeshBuilder("VB_SandstoneWall_End")
    _run(b, -0.32, 0.03, 4)
    b.box((0.02, -0.17, 0.0), (0.32, 0.17, 0.16), BLOCKS, bevel=0.012)            # pier base
    b.box((0.035, -0.155, 0.16), (0.305, 0.155, 1.84), BLOCKS, bevel=0.006)       # shaft
    b.box((0.02, -0.17, 1.84), (0.32, 0.17, 1.92), BLOCKS, bevel=0.01)            # cap moulding
    # A low pyramidal cap stone, the Desert's one flourish.
    top = b.bm.verts.new((0.17, 0.0, 2.04))
    before = set(b.bm.faces)
    ring = [b.bm.verts.new(p) for p in ((0.05, -0.14, 1.92), (0.29, -0.14, 1.92), (0.29, 0.14, 1.92), (0.05, 0.14, 1.92))]
    for k in range(4):
        b.bm.faces.new((ring[k], ring[(k + 1) % 4], top))
    b._tag(before, BLOCKS)
    return b.finish()


# ── Boulder ──────────────────────────────────────────────────────────────────

def boulder():
    """A wind-carved sandstone boulder: bedding planes eroded into ledges,
    undercut at the base, flattened on top. First-pass bounds kept."""
    rng = random.Random(9)
    g = Geo("SM_Boulder_A")
    verts = g.blob((0, 0, 0), 1.0, SROCK, squash=1.0, seed=6, rough=0.16, subdiv=4)
    beds = [rng.uniform(0.0, 1.0) for _ in range(5)]
    for v in verts:
        p = v.co.copy()
        z01 = (p.z + 1.0) / 2.0
        # Ledges: each bed recesses the rock just below it.
        rec = 0.0
        for bz in beds:
            dz = z01 - bz
            if -0.07 < dz < 0.0:
                rec = max(rec, 0.09 * (1.0 + dz / 0.07))
        # Undercut near the base (wind-blown sand abrades the foot).
        rec += 0.12 * (1.0 - kit.smoothstep(0.0, 0.3, z01))
        s = 1.0 - rec
        p.x *= s
        p.y *= s
        if p.z > 0.55:                          # a flat, weathered top
            p.z = 0.55 + (p.z - 0.55) * 0.35
        v.co = p
    for v in verts:
        v.co = Vector((v.co.x * 0.44, v.co.y * 0.32, v.co.z * 0.30 + 0.28))
        if v.co.z < -0.04:
            v.co.z = -0.04
    for f in g.bm.faces:
        f.normal_update()
        ax = max(range(3), key=lambda i: abs(f.normal[i]))
        for loop in f.loops:
            co = loop.vert.co
            loop[g.uv].uv = (co.x, co.y) if ax == 2 else ((co.y, co.z) if ax == 0 else (co.x, co.z))
    obj = g.finish()
    # first-pass bounds (Blender): x -0.44..0.44, y -0.324..0.324, z 0..0.497
    return nat._fit(obj, (-0.44, -0.324, -0.04), (0.44, 0.324, 0.497), uniform_xy=False)


# ── Cacti ────────────────────────────────────────────────────────────────────

RIBS_A, RIBS_B = 10, 16


def _ribbed(g, path, ribs, m, depth=0.12, segs_per_rib=2, cap=True, seed=0):
    """A fluted stem along ``path`` [(x, y, z, r), ...]. UV u = rib index
    (one texture repeat per rib: the areoles sit on each rib's crest),
    v = distance along the stem in metres. The top closes in a rounded dome."""
    pts = [Vector(p[:3]) for p in path]
    tang = [((pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)])).normalized() for i in range(len(pts))]
    ref = Vector((1, 0, 0)) if abs(tang[0].x) < 0.9 else Vector((0, 1, 0))
    nrm = tang[0].cross(ref).normalized()
    segs = ribs * segs_per_rib
    rings, vs = [], 0.0
    for i, (p, t) in enumerate(zip(pts, tang)):
        if i:
            nrm = (tang[i - 1].rotation_difference(t) @ nrm).normalized()
            vs += (pts[i] - pts[i - 1]).length
        bn = t.cross(nrm).normalized()
        r = path[i][3]
        ring = []
        for k in range(segs):
            a = 2 * math.pi * k / segs
            rib = 0.5 + 0.5 * math.cos(a * ribs)             # 1 on a crest, 0 in a groove
            rr = r * (1.0 - depth * (1.0 - rib))
            ring.append(g.bm.verts.new(p + (nrm * math.cos(a) + bn * math.sin(a)) * rr))
        rings.append((ring, vs))
    for (ra, va), (rb, vb) in zip(rings, rings[1:]):
        for k in range(segs):
            k2 = (k + 1) % segs
            u0 = k / segs_per_rib
            u1 = (k + 1) / segs_per_rib
            g.face((ra[k], ra[k2], rb[k2], rb[k]), ((u0, va), (u1, va), (u1, vb), (u0, vb)), m)
    if cap:
        ring, v_end = rings[-1]
        tip = g.bm.verts.new(pts[-1] + tang[-1] * path[-1][3] * 0.35)
        for k in range(segs):
            k2 = (k + 1) % segs
            g.face((ring[k], ring[k2], tip), ((k / segs_per_rib, v_end), ((k + 1) / segs_per_rib, v_end),
                                              ((k + 0.5) / segs_per_rib, v_end + 0.05)), m)
    return rings


def _arm(base, out, up, r):
    """An arm leaving the trunk sideways, turning up at an elbow."""
    bx, by, bz = base
    ox, oy = out
    pts = [(bx, by, bz, r * 0.8)]
    for i in range(1, 4):
        t = i / 3
        pts.append((bx + ox * t, by + oy * t, bz + 0.03 * t, r * (0.85 + 0.15 * t)))
    ex, ey, ez = bx + ox, by + oy, bz + 0.03
    for i in range(1, 6):
        t = i / 5
        ang = t * math.pi / 2
        pts.append((ex + ox * 0.35 * math.sin(ang) * 0.4, ey + oy * 0.35 * math.sin(ang) * 0.4,
                    ez + 0.10 * (1 - math.cos(ang)) + up * t, r))
    return pts


def cactus_a():
    """A saguaro-like column, 1.40 m, with two arms (first-pass bounds kept)."""
    g = Geo("SM_Cactus_A")
    trunk = [(0.0, 0.0, -0.03, 0.105), (0.0, 0.0, 0.2, 0.11), (0.01, 0.0, 0.6, 0.108),
             (0.01, 0.0, 1.0, 0.10), (0.0, 0.0, 1.22, 0.092)]
    _ribbed(g, trunk, RIBS_A, CACTUS)
    _ribbed(g, _arm((0.07, 0.0, 0.52), (0.20, 0.0), 0.30, 0.062), RIBS_A - 2, CACTUS, segs_per_rib=2)
    _ribbed(g, _arm((-0.07, 0.01, 0.70), (-0.17, 0.02), 0.22, 0.055), RIBS_A - 2, CACTUS, segs_per_rib=2)
    obj = g.finish()
    # first-pass bounds (Blender): x -0.41..0.37, y -0.14..0.14, z 0..1.4
    return nat._fit(obj, (-0.41, -0.14, 0.0), (0.37, 0.14, 1.40), uniform_xy=True)


def _flower(g, c, r, seed):
    """A ring of petals round a yellow-green centre, facing up."""
    rng = random.Random(seed)
    n = 9
    cv = Vector(c)
    for i in range(n):
        a = 2 * math.pi * (i + rng.uniform(-0.1, 0.1)) / n
        d = Vector((math.cos(a), math.sin(a), 0.0))
        s = Vector((-d.y, d.x, 0.0))
        base = cv + d * r * 0.15
        tip = cv + d * r + Vector((0, 0, r * 0.45))
        mid = (base + tip) / 2
        vs = [g.bm.verts.new(p) for p in (base, mid + s * r * 0.22, tip, mid - s * r * 0.22)]
        g.face(vs, ((0, 0), (1, 0.5), (0, 1), (-1, 0.5)), FLOWER, smooth=False)
    g.blob(tuple(cv + Vector((0, 0, r * 0.12))), r * 0.22, FLOWER, squash=0.6, seed=seed, rough=0.1, subdiv=1)


def cactus_b():
    """A barrel cactus with a pup and a crown of flowers, 0.51 m."""
    g = Geo("SM_Cactus_B")
    body = []
    for i in range(9):
        t = i / 8
        r = 0.17 * (0.78 + 0.35 * math.sin(math.pi * (0.15 + 0.7 * t)) - 0.12 * t)
        body.append((0.0, 0.0, -0.03 + 0.43 * t, r))
    _ribbed(g, body, RIBS_B, CACTUS, depth=0.14)
    pup = [(0.17, -0.10, -0.02 + 0.16 * i / 5, 0.075 * (0.8 + 0.3 * math.sin(math.pi * (0.2 + 0.6 * i / 5))))
           for i in range(6)]
    _ribbed(g, pup, 12, CACTUS, depth=0.14)
    _flower(g, (0.0, 0.0, 0.44), 0.06, 1)
    _flower(g, (0.045, 0.035, 0.425), 0.045, 2)
    _flower(g, (-0.04, 0.03, 0.43), 0.04, 3)
    obj = g.finish()
    # first-pass bounds (Blender): x -0.233..0.233, y -0.245..0.245, z 0..0.509
    return nat._fit(obj, (-0.233, -0.245, 0.0), (0.233, 0.245, 0.509), uniform_xy=True)


# ── Palm ─────────────────────────────────────────────────────────────────────

def _frond(g, base, direction, length, lift, droop, width, twist=0.0, segs=8):
    """A frond card that arches out and droops: 3 vertices across (the rachis
    raised, so the two halves fold up into a shallow V), UV v along the frond
    (0 = base, 1 = tip), u across (0..1), matching T_PalmFrond."""
    d = Vector((direction[0], direction[1], 0.0)).normalized()
    side = Vector((-d.y, d.x, 0.0))
    b = Vector(base)
    rows = []
    for i in range(segs + 1):
        t = i / segs
        # Height: rises by ``lift``, then droops by ``droop`` (a parabola).
        z = lift * math.sin(t * math.pi * 0.5) - droop * t * t
        p = b + d * (length * t) + Vector((0, 0, z))
        w = width * (0.35 + 0.65 * math.sin(math.pi * min(1.0, 0.1 + t * 0.95)))
        # The halves fold up along the rachis; the fold relaxes at the tip.
        fold = 0.30 * (1.0 - 0.6 * t)
        rot = mathutils.Matrix.Rotation(twist * t, 3, d)
        s = rot @ side
        up = rot @ Vector((0, 0, 1))
        left = p - s * w * 0.5 + up * w * fold * 0.5
        right = p + s * w * 0.5 + up * w * fold * 0.5
        rows.append([g.bm.verts.new(v) for v in (left, p, right)] + [t])
    for ra, rb in zip(rows, rows[1:]):
        va, vb = ra[3], rb[3]
        for k in range(2):
            ua, ub = k * 0.5, (k + 1) * 0.5
            g.face((ra[k], ra[k + 1], rb[k + 1], rb[k]), ((ua, va), (ub, va), (ub, vb), (ua, vb)), FROND, smooth=True)


def palm():
    """A date-style palm, 2.56 m: a leaning, ringed trunk and a crown of 13
    arching fronds over a cluster of dates/coconuts. First-pass bounds kept."""
    rng = random.Random(17)
    g = Geo("SM_Palm")
    # Trunk: a gentle S-lean, flared foot, leaf-scar rings every ~5 cm.
    n = 30
    path = []
    for i in range(n + 1):
        t = i / n
        x = 0.16 * t * t - 0.03 * math.sin(t * math.pi)
        y = 0.02 * math.sin(t * math.pi * 1.5)
        z = -0.04 + 2.02 * t
        r = 0.085 - 0.022 * t + 0.05 * max(0.0, 1.0 - t / 0.08) ** 2
        r *= 1.0 + 0.07 * (0.5 + 0.5 * math.cos(t * n * math.pi))      # rings
        path.append((x, y, z, r))
    g.tube(path, 10, BARK, seed=17, gnarl=0.03, cap_start=True, tip=False)
    top = Vector(path[-1][:3])
    # Crown boot: old frond bases round the top of the trunk.
    g.blob(tuple(top + Vector((0, 0, 0.05))), 0.12, BARK, squash=0.9, seed=3, rough=0.25, subdiv=2)
    # Fruit hanging under the crown.
    for i in range(5):
        a = rng.uniform(0, 2 * math.pi)
        c = top + Vector((0.08 * math.cos(a), 0.08 * math.sin(a), -0.06 - rng.uniform(0, 0.05)))
        g.blob(tuple(c), 0.035, NUT, squash=1.0, seed=20 + i, rough=0.05, subdiv=1)
    # Fronds: an outer ring drooping, an inner ring rising.
    for i in range(9):
        a = 2 * math.pi * i / 9 + rng.uniform(-0.15, 0.15)
        _frond(g, top + Vector((0, 0, 0.06)), (math.cos(a), math.sin(a)), rng.uniform(0.82, 0.95),
               rng.uniform(0.18, 0.28), rng.uniform(0.62, 0.8), 0.40, twist=rng.uniform(-0.3, 0.3))
    for i in range(4):
        a = 2 * math.pi * (i + 0.5) / 4 + rng.uniform(-0.2, 0.2)
        _frond(g, top + Vector((0, 0, 0.10)), (math.cos(a), math.sin(a)), rng.uniform(0.55, 0.65),
               rng.uniform(0.40, 0.50), rng.uniform(0.18, 0.28), 0.34, twist=rng.uniform(-0.3, 0.3))
    obj = g.finish()
    # first-pass bounds (Blender): x/y -0.967..0.967, z 0..2.564
    return nat._fit(obj, (-0.967, -0.967, 0.0), (0.967, 0.967, 2.564), uniform_xy=True)


# ── Portal marker ────────────────────────────────────────────────────────────

def portal_marker():
    """A rune-stone: a two-step octagonal plinth, a tapering obelisk with a
    pyramidion, glowing rune channels on its four faces and a glowing ring
    inlaid in the plinth. 1.21 m, as the first pass; same footprint."""
    b = kit.MeshBuilder("SM_PortalMarker")
    before = set(b.bm.faces)

    def octagon(r, z0, z1, m, r_top=None):
        r_top = r if r_top is None else r_top
        bot = [b.bm.verts.new((r * math.cos(math.pi / 8 + k * math.pi / 4),
                               r * math.sin(math.pi / 8 + k * math.pi / 4), z0)) for k in range(8)]
        top = [b.bm.verts.new((r_top * math.cos(math.pi / 8 + k * math.pi / 4),
                               r_top * math.sin(math.pi / 8 + k * math.pi / 4), z1)) for k in range(8)]
        fb = set(b.bm.faces)
        for k in range(8):
            b.bm.faces.new((bot[k], bot[(k + 1) % 8], top[(k + 1) % 8], top[k]))
        b.bm.faces.new(list(reversed(bot)))
        b.bm.faces.new(top)
        b._tag(fb, m)

    octagon(0.345, 0.0, 0.07, STONE, 0.335)
    octagon(0.27, 0.07, 0.13, STONE, 0.26)
    # Glowing ring inlay: eight thin segments just proud of the upper step.
    for k in range(8):
        a0, a1 = math.pi / 8 + k * math.pi / 4 + 0.08, math.pi / 8 + (k + 1) * math.pi / 4 - 0.08
        r0, r1 = 0.20, 0.225
        pts = [(r0 * math.cos(a0), r0 * math.sin(a0)), (r1 * math.cos(a0), r1 * math.sin(a0)),
               (r1 * math.cos(a1), r1 * math.sin(a1)), (r0 * math.cos(a1), r0 * math.sin(a1))]
        b.prism_xy(pts, 0.125, 0.134, GLOW)
    # Obelisk: a square shaft tapering from 20 to 13 cm, then a pyramidion.
    z0, z1, w0, w1 = 0.13, 1.08, 0.10, 0.065
    loft = [(z0, [(-w0, -w0), (w0, -w0), (w0, w0), (-w0, w0)]),
            (z1, [(-w1, -w1), (w1, -w1), (w1, w1), (-w1, w1)])]
    b.loft(loft, ROCK, cap_bottom=True, tip=(0.0, 0.0, 1.206))
    # Rune channels: short glowing bars stacked up the middle of each face,
    # set 3 mm proud so they read from any angle.
    rng = random.Random(4)
    for face in range(4):
        rot = mathutils.Matrix.Rotation(face * math.pi / 2, 3, "Z")
        z = 0.24
        while z < 0.98:
            hgt = rng.uniform(0.035, 0.07)
            wid = rng.uniform(0.012, 0.035)
            off = rng.uniform(-0.015, 0.015)
            t = (z - z0) / (z1 - z0)
            half = w0 + (w1 - w0) * t
            y_out = -(half + 0.003)
            corners = [(off - wid, y_out, z), (off + wid, y_out, z), (off + wid, y_out, z + hgt), (off - wid, y_out, z + hgt)]
            fb = set(b.bm.faces)
            vs = [b.bm.verts.new(rot @ Vector(c)) for c in corners]
            back = [b.bm.verts.new(rot @ Vector((c[0], c[1] + 0.006, c[2]))) for c in corners]
            b.bm.faces.new(vs)
            for k in range(4):
                b.bm.faces.new((back[k], back[(k + 1) % 4], vs[(k + 1) % 4], vs[k]))
            b._tag(fb, GLOW)
            z += hgt + rng.uniform(0.03, 0.06)
    return b.finish()


PIECES = [
    (sand_a, DESERT), (sand_b, DESERT), (sand_c, DESERT), (dune, DESERT), (cracked_earth, DESERT),
    (wall_straight, DESERT), (wall_corner, DESERT), (wall_end, DESERT),
    (boulder, DESERT), (cactus_a, DESERT), (cactus_b, DESERT), (palm, DESERT),
    (portal_marker, PROPS),
]


def build_all(export=True, only=None):
    kit.reset_scene()
    out = []
    for i, (fn, folder) in enumerate(PIECES):
        if only and fn.__name__ not in only:
            continue
        obj = fn()
        if export:
            kit.export_glb(obj, os.path.join(folder, obj.name + ".glb"))
        bb = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
        mn = [round(min(v[k] for v in bb), 3) for k in range(3)]
        mx = [round(max(v[k] for v in bb), 3) for k in range(3)]
        obj.location = ((i % 5) * 1.4, -(i // 5) * 1.6, 0.0)     # laid out for review
        out.append((obj.name, kit.tri_count(obj), mn, mx, [m.name for m in obj.data.materials]))
    return out
