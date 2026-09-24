"""B-06 Phase 1 step 1.2: the Eldmoor Grasslands art kit.

Builds the art gaps in Docs/Zones/Eldmoor/EldmoorDesign.md section 12, in the
design's priority order:

1. Cliffs and crags: VB_CliffFace_Tall / _Mid / _Low, VB_CliffCorner, the
   unmarked cave cleft SM_CliffCleft, VB_BoulderLarge_A / _B, VB_RockOutcrop.
2. Palisade: VB_PalisadeWall (64 cm), VB_PalisadeWall_Long (256 cm),
   VB_PalisadeCorner, SM_PalisadeGate (2.56 m opening, open).
3. Thornwood thickets: VB_Thicket_A (128 x 128), VB_Thicket_B (256 x 128).
4. The rest: SM_Forge, SM_Anvil, SM_HayBale, SM_StrawPile, SM_TrainingPost,
   SM_CellBars, SM_StoneRamp, SM_KeepParapetLow, VB_ScorchedWall,
   SM_ScorchedPost, SM_ScorchedBeams, SM_Cart, SM_BindStone, SM_TorchSconce.

Conventions are wave5_kit.py's (Docs/ArtBible.md): 1 Blender unit = 1 game
metre, Z up, the front / outside of a piece faces -Y (Unreal +Y, south, at
yaw 0), pivot on the ground at the footprint centre, props ~0.68 x real size.
Material slot names are the Unreal MI_ instances (bound by
valhalla_tools.import_kit). New instances: MI_CliffRock (Poly Haven
rock_face_03, CC0), MI_CharredTimber, MI_Shroud, MI_BindGlow, MI_Embers.

VB_ pieces: the fog renderer takes a vision blocker's footprint from its
mesh bounding box (ValhallaFogRenderer::GatherBlockerSegments), so every VB_
piece fills its box and nothing a player walks through is VB_ (the gate, the
cleft and the ramp are SM_ with per-triangle collision, like SM_KeepGate).
The line-of-sight trace runs at 90 cm (ValhallaEyeHeight), so the thicket
stands 1.45 m, not waist high.

Run in Blender: exec(open(<this>).read()); build_all()
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


w5 = _load("wave5_kit")
kit, nat, Geo, MB = w5.kit, w5.nat, w5.Geo, w5.MB
rock, flame, xform, join, flat = w5.rock, w5.flame, w5.xform, w5.join, w5.flat

OUT = os.path.join(kit.IMPORT, "Environment", "Eldmoor")
REVIEW = os.path.join(kit.REPO, "Valhalla2", "Saved", "ArtReview", "eldmoor")

# Materials (Unreal MI names). Existing ones come from wave5_kit.
CLIFF, TOP, CHAR, SHROUD, BINDGLOW, EMBERS = "MI_CliffRock", "MI_MossyRock", "MI_CharredTimber", "MI_Shroud", "MI_BindGlow", "MI_Embers"
BURLAP, STONEFLOOR, WATER = "MI_Burlap", "MI_StoneFloor", "MI_Water"
LOG, OAK, DARKPL, IRON, KEEP, STONE = w5.LOG, w5.OAK, w5.DARKPL, w5.IRON, w5.KEEP, w5.STONE
THATCH, ROPE, CAVEFLOOR, FIRE, MASS, CARD = w5.THATCH, w5.ROPE, w5.CAVEFLOOR, w5.FIRE, w5.MASS, w5.CARD
DOOR, TIMBER, PLASTER, LEATHER, FERN, STEEL = w5.DOOR, w5.TIMBER, w5.PLASTER, w5.LEATHER, w5.FERN, w5.STEEL

kit.PREVIEW.update({
    CLIFF: (0.42, 0.41, 0.39), CHAR: (0.07, 0.06, 0.05), SHROUD: (0.0, 0.0, 0.0), BINDGLOW: (0.45, 1.0, 0.6),
    EMBERS: (1.0, 0.35, 0.05), BURLAP: (0.55, 0.45, 0.3), STONEFLOOR: (0.4, 0.38, 0.35), WATER: (0.1, 0.2, 0.25),
})


def _clamp01(v):
    return max(0.0, min(1.0, v))


def retag(obj, pred, mat_name):
    """Give every polygon matching ``pred`` the material ``mat_name``."""
    me = obj.data
    names = [m.name for m in me.materials]
    if mat_name not in names:
        me.materials.append(kit.material(mat_name))
        names.append(mat_name)
    idx = names.index(mat_name)
    for p in me.polygons:
        if pred(p):
            p.material_index = idx
    return obj


def centre_xy(obj):
    xs = [v.co.x for v in obj.data.vertices]
    ys = [v.co.y for v in obj.data.vertices]
    obj.data.transform(Matrix.Translation((-(min(xs) + max(xs)) / 2, -(min(ys) + max(ys)) / 2, 0.0)))
    obj.data.update()
    return obj


def settle(obj, z=0.0):
    zs = [v.co.z for v in obj.data.vertices]
    obj.data.transform(Matrix.Translation((0.0, 0.0, z - min(zs))))
    obj.data.update()
    return obj


def smooth(obj, on=True):
    for p in obj.data.polygons:
        p.use_smooth = on
    return obj


# ═════════════════════════════════════════════════════════════════════════════
#  1. Cliffs and crags (A-094). Greyfell Tor's 7 m S and E faces, the ridge's
#  north cliff (up to 4 m), its 1.5 m south scarp and the plateau's north edge.
#  Modules are 2.56 m (4 tiles) wide. Horizontal strata sit at the same
#  heights in every piece and the two ends of a module share one profile, so
#  modules butt together in any order and the ledges run on across joints.
# ═════════════════════════════════════════════════════════════════════════════

def _make_layers():
    rng = random.Random(4242)
    z, out = 0.0, [0.0]
    while z < 12.0:
        z += rng.uniform(0.55, 1.35)
        out.append(round(z, 3))
    return out


LAYERS = _make_layers()


def _layer(z):
    for i in range(len(LAYERS) - 1):
        if LAYERS[i] <= z < LAYERS[i + 1]:
            return i, (z - LAYERS[i]) / (LAYERS[i + 1] - LAYERS[i])
    return len(LAYERS), 0.0


def _h(*k):
    """A repeatable 0..1 value per integer key (per bed, per block)."""
    return random.Random(hash(k)).random()


def _detail(s, z, seed):
    """0..1 set-back of the rock face at (s along the face, z): jointed
    granite, as a tor weathers. Each bed is cut into blocks of its own width;
    every block sits back by its own amount, the joints between blocks are
    chamfered V grooves, some beds are undercut at the base and every block's
    top edge is rounded. Broad buttresses and grain on top."""
    i, f = _layer(z)
    off = Vector((seed * 7.3, seed * 3.1, seed * 1.9))
    bw = 0.7 + 1.0 * _h(i, 1, seed)
    q = (s + _h(i, 2, seed) * bw + 0.1 * noise.noise(Vector((s * 0.7, z * 0.9, 1.0)) + off)) / bw
    k = math.floor(q)
    u = q - k
    v = 0.62 * _h(i, k, 3, seed)                                                             # block setback
    v += 0.32 * (max(0.0, 1 - u / 0.1) + max(0.0, 1 - (1 - u) / 0.1))                        # joints
    v += 0.4 * _h(i, 4, seed) * max(0.0, 1 - f / 0.2)                                        # undercut
    v += 0.22 * max(0.0, (f - 0.84) / 0.16) ** 1.5                                           # rounded block tops
    v += 0.3 * (0.5 + 0.5 * noise.noise(Vector((s * 0.33, z * 0.1, 5.0)) + off))            # buttresses
    v += 0.1 * noise.noise(Vector((s * 3.1, z * 2.7, 9.0)) + off)                            # grain
    return _clamp01(v / 1.45)


def _end_blend(s, w):
    return kit.smoothstep(0.0, 0.5, w / 2 - abs(s))


def _inset(s, z, seed, amp, w, h, batter):
    """Set-back from the front plane. The ends of a module (|s| = w/2) use the
    seed-0 profile at s = 0, so any two modules meet flush."""
    e = _end_blend(s, w)
    d = (1 - e) * _detail(0.0, z, 0) + e * _detail(s, z, seed)
    d = amp * d + batter * max(0.0, z) / max(h, 1.0)
    if z < 0.55:                                               # talus foot
        d *= max(0.0, z) / 0.55
    return d


def _zs(h, rc, skirt, step):
    zs = {-skirt, 0.0, 0.25, h - rc}
    for b in LAYERS[1:]:
        if 0.3 < b < h - rc - 0.12:
            zs.update((b - 0.012, b + 0.012))
    zs = sorted(zs)
    out = [zs[0]]
    for z in zs[1:]:
        a = out[-1]
        n = max(1, int(math.ceil((z - a) / step)))
        for k in range(1, n + 1):
            out.append(a + (z - a) * k / n)
    return out


def _skyline(s, w, h, seed, drop):
    """Top height along the face: blocks of the top bed break the skyline."""
    e = _end_blend(s, w)
    off = Vector((seed * 1.1, 0.0, 4.0))
    bw = 0.8 + 0.6 * _h(99, 1, seed)
    k = math.floor((s + 7.0) / bw)
    v = 0.6 * _h(99, k, seed) + 0.4 * (0.5 + 0.5 * noise.noise(Vector((s * 0.9, 0.0, 0.0)) + off))
    return h - drop * e * v


def _cliff_mesh(b, w, h, d, seed, amp, batter, step=0.14, rc=0.28, skirt=0.3, top_amp=0.1, drop=None):
    """A closed cliff slab: x -w/2..w/2, y -d/2 (front plane) .. d/2, z -skirt..h."""
    y0, y1 = -d / 2, d / 2
    drop = min(0.55, h * 0.1) if drop is None else drop
    zs = _zs(h, rc, skirt, step)
    nx = max(6, int(math.ceil(w / 0.13)))
    rng = random.Random(seed)
    cols = []
    off = Vector((seed * 2.3, seed * 4.1, 0))
    for i in range(nx + 1):
        s = -w / 2 + w * i / nx
        x = s + (rng.uniform(-0.025, 0.025) if 0 < i < nx else 0.0)
        e = _end_blend(s, w)
        ht = _skyline(s, w, h, seed, drop)
        rcc = rc * (1.0 + 0.5 * e * noise.noise(Vector((s * 1.7, 2.0, 0.0)) + off))
        sc = (ht - rcc) / (h - rc)
        prof = []
        for z in zs:                                            # front face, bottom to rim
            prof.append((y0 + _inset(s, z, seed, amp, w, h, batter), z * sc if z > 0 else z))
        yf, zf = prof[-1]
        for k in (1, 2, 3):                                     # rounded, chipped rim
            a = math.pi - k * math.pi / 6
            j = 0.04 * e * noise.noise(Vector((s * 5.0, k, 1.0)) + off)
            prof.append((yf + rcc + rcc * math.cos(a) + j, zf + rcc * math.sin(a) - abs(j)))
        yt = yf + rcc
        nt = 5
        for k in range(1, nt + 1):                              # the top, sinking a little
            t = k / nt
            y = yt + (y1 - yt) * t
            dz = top_amp * e * (0.5 + 0.5 * noise.noise(Vector((s * 1.3, y * 1.3, 3.0)) + off))
            z_top = ht + (h - ht) * t * t                       # back edge meets the landscape at h
            prof.append((y, z_top - dz * (1.0 if k < nt else 0.0)))
        prof.append((y1, h * 0.5))                              # back (buried)
        prof.append((y1, -skirt))
        cols.append([b.bm.verts.new((x, y, z)) for (y, z) in prof])
    before = set(b.bm.faces)
    m = len(cols[0])
    for ca, cb in zip(cols, cols[1:]):
        for j in range(m):
            j2 = (j + 1) % m
            b.bm.faces.new((ca[j], ca[j2], cb[j2], cb[j]))
    caps = [b.bm.faces.new(cols[0]), b.bm.faces.new(list(reversed(cols[-1])))]
    bmesh.ops.triangulate(b.bm, faces=caps, quad_method="BEAUTY", ngon_method="EAR_CLIP")
    b._tag(before, CLIFF)


def _up(p, k=0.55):
    return p.normal.z > k


def _cliff(name, w, h, d, seed, amp=0.5, batter=0.3, step=0.14):
    b = MB(name)
    _cliff_mesh(b, w, h, d, seed, amp, batter, step=step)
    ob = flat(b.finish())
    return retag(ob, _up, TOP)


def cliff_face_tall():
    """Greyfell Tor's S and E faces: 2.56 m wide, 7 m high, 1.28 m deep."""
    return _cliff("VB_CliffFace_Tall", 2.56, 7.0, 1.28, seed=11)


def cliff_face_mid():
    """The ridge's north cliff and the keep plateau's north edge: 4 m."""
    return _cliff("VB_CliffFace_Mid", 2.56, 4.0, 1.28, seed=12)


def cliff_face_low():
    """The 1.5 m ridge scarp over Hask's Hold (1.8 m so the landscape can bury the foot)."""
    return _cliff("VB_CliffFace_Low", 2.56, 1.8, 0.96, seed=13, amp=0.34, batter=0.1, step=0.12)


def cliff_corner():
    """An outer corner / crag buttress, 1.28 x 1.28, 7 m: turns a cliff run
    through 90 degrees (the tor's SE corner) and ends a run. Scale Z 0.57 for
    the 4 m cliff, 0.26 for the scarp."""
    b = MB("VB_CliffCorner")
    R, h, rc, skirt, amp = 0.64, 7.0, 0.3, 0.3, 0.34
    n = 36
    zs = _zs(h, rc, skirt, 0.16)
    rings = []
    for z in zs:
        ring = []
        for k in range(n):
            a = 2 * math.pi * k / n
            c, s_ = math.cos(a), math.sin(a)
            r_se = R / (abs(c) ** 4 + abs(s_) ** 4) ** 0.25          # superellipse, fills the square
            s = ((a - math.pi / 2) % (2 * math.pi)) * R - math.pi * R   # seam at the buried back (+Y)
            d = _inset(s, z, 21, amp, 1e9, h, 0.25)                    # never "at an end"
            r = max(0.2, r_se - d)
            ring.append((r * c, r * s_, z))
        rings.append(ring)
    last = rings[-1]
    for k2, (fz, fr) in enumerate(((0.5, 0.87), (0.87, 0.5), (1.0, 0.0))):        # rim
        rings.append([(x * (1 - rc / max(0.3, math.hypot(x, y)) * (1 - fr)), y * (1 - rc / max(0.3, math.hypot(x, y)) * (1 - fr)),
                       h - rc + rc * fz) for (x, y, z) in last])
    top = rings[-1]
    for t in (0.66, 0.33):
        rings.append([(x * t, y * t, h - 0.06 * (1 - t) * (1 + noise.noise(Vector((x * 3, y * 3, 1.0))))) for (x, y, z) in top])
    before = set(b.bm.faces)
    vr = [[b.bm.verts.new(p) for p in ring] for ring in rings]
    for ra, rb in zip(vr, vr[1:]):
        for k in range(n):
            b.bm.faces.new((ra[k], ra[(k + 1) % n], rb[(k + 1) % n], rb[k]))
    c = b.bm.verts.new((0.0, 0.0, h - 0.05))
    for k in range(n):
        b.bm.faces.new((vr[-1][k], vr[-1][(k + 1) % n], c))
    b.bm.faces.new(list(reversed(vr[0])))
    b._tag(before, CLIFF)
    ob = flat(b.finish())
    return retag(ob, _up, TOP)


def cliff_cleft():
    """The unmarked Greyfell cleft (design section 8): a tall cliff module with
    a narrow opening at its foot, 1.5 m wide and 2.2 m tall, closing to a
    crack above; the passage runs 1.1 m in and ends in a black shroud so the
    portal trigger inside is never seen. Walk-in: SM_ with per-triangle
    collision. Keep the landscape at cliff-foot height under its footprint."""
    b = MB("SM_CliffCleft")
    _cliff_mesh(b, 2.56, 7.0, 1.28, 14, 0.5, 0.3)
    ob = b.finish()
    outline = [(-0.75, 0.0), (-0.74, 0.7), (-0.66, 1.35), (-0.5, 1.8), (-0.3, 2.08), (-0.12, 2.24), (-0.07, 2.9),
               (-0.035, 3.7), (0.015, 3.72), (0.05, 2.9), (0.12, 2.22), (0.33, 2.05), (0.53, 1.74), (0.68, 1.3),
               (0.75, 0.7), (0.75, 0.0)]
    rng = random.Random(7)
    g = MB("_cutter")
    rings = []
    for y, sx, sz, jit in ((-1.2, 1.0, 1.0, 0.0), (-0.45, 1.0, 1.0, 0.02), (-0.1, 0.95, 0.97, 0.04),
                           (0.2, 0.88, 0.94, 0.05), (0.45, 0.8, 0.9, 0.04)):
        ring = []
        for (x, z) in outline:
            jx = rng.uniform(-jit, jit)
            jz = rng.uniform(-jit, jit) if z > 0.05 else 0.0
            ring.append(g.bm.verts.new((x * sx + jx, y, max(0.0, z * sz + jz))))
        rings.append(ring)
    n = len(outline)
    fb = set(g.bm.faces)
    floor = []
    for ra, rb in zip(rings, rings[1:]):
        for k in range(n):
            f = g.bm.faces.new((ra[k], ra[(k + 1) % n], rb[(k + 1) % n], rb[k]))
            if k == n - 1:
                floor.append(f)
    g._tag(fb, CLIFF)
    fb = set(g.bm.faces)
    g.bm.faces.new(list(reversed(rings[0])))
    g._tag(fb, CLIFF)
    fb = set(g.bm.faces)
    g.bm.faces.new(rings[-1])
    g._tag(fb, SHROUD)
    idx = g._mat(CAVEFLOOR)
    for f in floor:
        f.material_index = idx
    cutter = g.finish()
    for m in cutter.data.materials:
        if m.name not in [x.name for x in ob.data.materials]:
            ob.data.materials.append(m)
    mod = ob.modifiers.new("cleft", "BOOLEAN")
    mod.operation, mod.object, mod.solver = "DIFFERENCE", cutter, "EXACT"
    mod.material_mode = "TRANSFER"
    with bpy.context.temp_override(object=ob, active_object=ob, selected_objects=[ob]):
        bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)
    flat(ob)
    names = [m.name for m in ob.data.materials]
    keep = {names.index(SHROUD), names.index(CAVEFLOOR)}
    return retag(ob, lambda p: _up(p) and p.material_index not in keep and p.center.z > 0.1, TOP)


def _granite(g, centre, size, seed, subdiv=3, n=3.2, cuts=9):
    """A weathered granite block: a rounded box (superellipsoid), chiselled by
    a few planes, with a little noise. ``size`` = half extents (x, y, z)."""
    rng = random.Random(seed)
    geom = bmesh.ops.create_icosphere(g.bm, subdivisions=subdiv, radius=1.0)
    verts = geom["verts"]
    planes = []
    for _ in range(cuts):
        d = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.2, 1))).normalized()
        planes.append((d, rng.uniform(0.72, 0.93)))
    off = Vector((seed * 1.3, seed * 2.9, seed * 0.7))
    c = Vector(centre)
    for v in verts:
        d = v.co.normalized()
        p = Vector([math.copysign(abs(a) ** (2.0 / n), a) for a in d])
        p *= 1.0 + 0.06 * noise.noise(d * 2.5 + off)
        for pd, k in planes:
            h = p.dot(pd)
            if h > k:
                p -= pd * (h - k)
        v.co = c + Vector((p.x * size[0], p.y * size[1], p.z * size[2]))
    idx = g.mat(CLIFF)
    faces = {f for v in verts for f in v.link_faces}
    for f in faces:
        f.material_index = idx
        f.smooth = False
        f.normal_update()
        ax = max(range(3), key=lambda i: abs(f.normal[i]))
        for loop in f.loops:
            co = loop.vert.co
            loop[g.uv].uv = (co.x, co.y) if ax == 2 else ((co.y, co.z) if ax == 0 else (co.x, co.z))


def _boulder(name, parts, seed, top=None):
    g = Geo(name)
    for i, (c, s, sub) in enumerate(parts):
        _granite(g, c, s, seed + i, subdiv=sub)
    ob = g.finish()
    if top is not None:
        for v in ob.data.vertices:
            if v.co.z > top:
                v.co.z = top + (v.co.z - top) * 0.06
    ob.data.update()
    flat(ob)
    settle(ob, -0.1)
    centre_xy(ob)
    return retag(ob, lambda p: p.normal.z > 0.72, TOP)


def boulder_large_a():
    """The big screen boulder (cleft screen at (3,30) and (9,31)): about
    2.4 x 1.8 x 2.1 m, a split block leaning on a smaller one."""
    return _boulder("VB_BoulderLarge_A", [((0.0, 0.0, 1.0), (1.0, 0.82, 1.1), 3),
                                          ((0.72, -0.28, 0.4), (0.52, 0.5, 0.5), 2),
                                          ((-0.72, 0.3, 0.3), (0.45, 0.42, 0.4), 2)], 31)


def boulder_large_b():
    """A lower, longer boulder, 1.9 x 1.3 x 1.5 m (Hask's rock at (31,42), the shelf)."""
    return _boulder("VB_BoulderLarge_B", [((0.0, 0.0, 0.7), (0.88, 0.6, 0.8), 3),
                                          ((0.6, 0.22, 0.28), (0.4, 0.38, 0.36), 2)], 41)


def rock_outcrop():
    """A flat-topped outcrop 1.2 m high a crossbowman can stand on
    (Hask's Hold (25,34)); 2.5 x 1.8 m."""
    return _boulder("VB_RockOutcrop", [((0.0, 0.0, 0.55), (1.15, 0.82, 0.8), 3),
                                       ((0.72, 0.38, 0.32), (0.52, 0.48, 0.45), 2),
                                       ((-0.78, -0.25, 0.28), (0.48, 0.42, 0.38), 2)], 51, top=1.15)


CLIFFS = [cliff_face_tall, cliff_face_mid, cliff_face_low, cliff_corner, cliff_cleft,
          boulder_large_a, boulder_large_b, rock_outcrop]


# ═════════════════════════════════════════════════════════════════════════════
#  2. Palisade (A-095): sharpened logs, 2.4 m, two back rails on the inside
#  (+Y). Outside faces -Y. Wall 64 x 26 cm (one tile), long 256 x 26.
# ═════════════════════════════════════════════════════════════════════════════

def _post(g, x, y, r, h, tip, seed, lean=(0.0, 0.0), bottom=-0.15, gnarl=0.06):
    base = Vector((x, y, bottom))
    topv = Vector((x + lean[0] * h, y + lean[1] * h, h))

    def at(z):
        return base.lerp(topv, (z - bottom) / (h - bottom))
    p8, p1, pm = at(0.8), at(h - tip), at(h - tip * 0.45)
    g.tube([(base.x, base.y, base.z, r * 1.03), (p8.x, p8.y, p8.z, r), (p1.x, p1.y, p1.z, r * 0.97)], 8, LOG,
           seed=seed, gnarl=gnarl, cap_start=True, tip=False)
    g.tube([(p1.x, p1.y, p1.z, r * 0.97), (pm.x, pm.y, pm.z, r * 0.5), (topv.x, topv.y, topv.z - 0.01, 0.01)], 8, OAK,
           seed=seed, gnarl=gnarl, tip=True)


def _rail(g, x0, x1, y, z, r, seed):
    g.tube([(x0, y, z, r), (x1, y, z, r)], 7, LOG, seed=seed, gnarl=0.04, cap_start=True, tip=False)


def _palisade(name, width, seed):
    g = Geo(name)
    rng = random.Random(seed)
    n = int(round(width / 0.16))
    for i in range(n):
        x = -width / 2 + 0.08 + i * 0.16 + rng.uniform(-0.006, 0.006)
        _post(g, x, -0.047, rng.uniform(0.074, 0.082), rng.uniform(2.24, 2.42), rng.uniform(0.2, 0.28),
              seed * 100 + i, lean=(rng.uniform(-0.01, 0.01), rng.uniform(-0.008, 0.004)))
    for z in (0.55, 1.72):
        _rail(g, -width / 2, width / 2, 0.083, z + rng.uniform(-0.03, 0.03), 0.045, seed + 7)
    return g.finish()


def palisade_wall():
    return _palisade("VB_PalisadeWall", 0.64, 61)


def palisade_wall_long():
    return _palisade("VB_PalisadeWall_Long", 2.56, 62)


def palisade_corner():
    """A heavy corner post, 36 cm, 2.75 m, lashed; stands on the corner point
    where two wall runs meet (footprint 36 x 36)."""
    g = Geo("_post")
    _post(g, 0.0, 0.0, 0.17, 2.75, 0.34, 901, gnarl=0.04)
    ob = g.finish()
    b = MB("_bands")
    for z in (1.9, 2.02):
        w5._lathe(b, [(0.176, z), (0.176, z + 0.05)], 12, ROPE, cap_bottom=False, cap_top=False)
    return join("VB_PalisadeCorner", [ob, b.finish()])


def _leaf(name, sign, width=1.24, height=2.1):
    """A planked gate leaf, hinge at x = 0, running along sign * x; ledges and
    a brace on the inside (+Y), iron straps outside."""
    b = MB(name)
    n = 7
    pw = width / n
    X = lambda x: x * sign
    for i in range(n):
        x0, x1 = sorted((X(i * pw + 0.004), X((i + 1) * pw - 0.004)))
        top = height - (0.05 if i % 2 else 0.0)
        b.box((x0, -0.03, 0.06), (x1, 0.03, top), OAK, bevel=0.006)
    for z in (0.35, 1.72):
        x0, x1 = sorted((X(0.03), X(width - 0.03)))
        b.box((x0, 0.03, z), (x1, 0.075, z + 0.14), OAK, bevel=0.01)
    pts = [(X(0.06), 0.49), (X(0.22), 0.49), (X(width - 0.06), 1.72), (X(width - 0.22), 1.72)]
    b.prism_xz(pts, 0.03, 0.07, OAK)
    for z in (0.42, 1.8):
        x0, x1 = sorted((X(-0.02), X(0.62)))
        b.box((x0, -0.045, z), (x1, -0.03, z + 0.06), IRON)
    return b.finish()


def palisade_gate():
    """The outpost gate: a 2.56 m clear opening between two 34 cm gate posts
    under a double log lintel (2.5 m clear), both leaves swung open inward
    (+Y), and one bay of logs each side. 3.84 m (6 tiles) along the wall:
    it replaces the wall modules for 6 tiles, the opening is the middle 4.
    Walk-through: SM_ with per-triangle collision, like SM_KeepGate."""
    g = Geo("_frame")
    rng = random.Random(71)
    for sx in (-1, 1):
        _post(g, sx * 1.45, 0.0, 0.17, 3.05, 0.34, 710 + sx, gnarl=0.04)
        for i, x in enumerate((1.715, 1.865)):
            _post(g, sx * x, -0.047, 0.078, rng.uniform(2.26, 2.4), 0.24, 720 + sx * 3 + i)
        for z in (0.55, 1.72):
            _rail(g, sx * 1.6, sx * 1.92, 0.083, z, 0.045, 730)
    for z, r in ((2.62, 0.11), (2.83, 0.1)):
        _rail(g, -1.82, 1.82, 0.0, z, r, 740 + int(z * 10))
    frame = g.finish()
    left = xform(_leaf("_leafL", 1), rot=(0.0, 0.0, 95.0), loc=(-1.36, 0.19, 0.0))
    right = xform(_leaf("_leafR", -1), rot=(0.0, 0.0, -95.0), loc=(1.36, 0.19, 0.0))
    return join("SM_PalisadeGate", [frame, left, right])


PALISADE = [palisade_wall, palisade_wall_long, palisade_corner, palisade_gate]


# ═════════════════════════════════════════════════════════════════════════════
#  3. Thornwood thickets (A-096): dense bush that fills its tile box (the fog
#  footprint), 1.45 m tall so the 90 cm sight line is blocked. Blocks
#  movement and vision (VisionBlocker profile, box collision).
# ═════════════════════════════════════════════════════════════════════════════

def _tclump(g, centre, radius, rng, seed, cards_per_m2=34.0):
    core = radius * 0.8
    g.blob(centre, core, MASS, squash=0.85, seed=seed, rough=0.28, subdiv=1)
    c = Vector(centre)
    n_cards = max(10, int(4 * math.pi * radius * radius * cards_per_m2))
    for i in range(n_cards):
        zc = 1.0 - 1.5 * (i + rng.random()) / n_cards
        phi = i * 2.399963 + rng.uniform(-0.3, 0.3)
        rr = math.sqrt(max(0.0, 1 - zc * zc))
        d = Vector((rr * math.cos(phi), rr * math.sin(phi), zc)).normalized()
        surface = c + Vector((d.x * core, d.y * core, d.z * core * 0.85))
        tilt = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * 0.45
        normal = (d * 0.6 + Vector((0, 0, 0.75)) + tilt).normalized()
        size = radius * rng.uniform(0.9, 1.25)
        g.card(surface + d * size * 0.1, normal, size, rng.uniform(0, 2 * math.pi), CARD)


def _thicket(name, W, D, seed, top=1.45):
    g = Geo(name)
    rng = random.Random(seed)
    cols, rows = max(2, round(W / 0.42)), max(2, round(D / 0.42))
    for i in range(cols):
        for j in range(rows):
            m = 0.26                                             # clumps bulge to the tile edge, not past it
            x = -W / 2 + m + (W - 2 * m) * (i / (cols - 1)) + rng.uniform(-0.06, 0.06)
            y = -D / 2 + m + (D - 2 * m) * (j / (rows - 1)) + rng.uniform(-0.06, 0.06)
            r = rng.uniform(0.3, 0.42)
            _tclump(g, (x, y, rng.uniform(0.55, 1.02)), r, rng, seed * 50 + i * 7 + j)
            ex, ey = x * 0.8, y * 0.8                            # the foot mass stays inside the fern skirt
            g.blob((ex, ey, 0.3), r * 0.85, MASS, squash=0.8, seed=seed * 70 + i * 5 + j, rough=0.4, subdiv=1)
            _tclump(g, (ex, ey, 0.34), r * 0.8, rng, seed * 90 + i * 5 + j, cards_per_m2=26.0)
    per = 2 * (W + D)
    for k in range(int(per / 0.16)):                            # a skirt of ferns right round the foot
        t = k * 0.16 + rng.uniform(0.0, 0.08)
        if t < W:
            x, y = -W / 2 + t, -D / 2 + 0.07
        elif t < W + D:
            x, y = W / 2 - 0.07, -D / 2 + (t - W)
        elif t < 2 * W + D:
            x, y = W / 2 - (t - W - D), D / 2 - 0.07
        else:
            x, y = -W / 2 + 0.07, D / 2 - (t - 2 * W - D)
        w5.cards(g, (x, y, -0.02), 0.36, rng.uniform(0.42, 0.6), 2, FERN, rng, spin=rng.uniform(0, math.pi))
    ob = g.finish()
    for v in ob.data.vertices:
        v.co.x = max(-W / 2, min(W / 2, v.co.x))
        v.co.y = max(-D / 2, min(D / 2, v.co.y))
        v.co.z = max(-0.03, min(top, v.co.z))
    ob.data.update()
    return ob


def thicket_a():
    return _thicket("VB_Thicket_A", 1.28, 1.28, 81)


def thicket_b():
    return _thicket("VB_Thicket_B", 2.56, 1.28, 82)


THICKET = [thicket_a, thicket_b]


# ═════════════════════════════════════════════════════════════════════════════
#  4. The rest (A-097 ..).
# ═════════════════════════════════════════════════════════════════════════════

def forge():
    """A smith's hearth: stone firebed with glowing coals, a hood and chimney
    at the back (+Y), bellows on the left, a quench tub on the right.
    Light: fire_lights.FIRES["SM_Forge"]."""
    b = MB("_hearth")
    b.box((-0.56, -0.36, 0.0), (0.56, 0.36, 0.5), STONE, bevel=0.02)
    for (x0, y0, x1, y1) in ((-0.6, -0.4, 0.6, -0.16), (-0.6, 0.16, 0.6, 0.4), (-0.6, -0.16, -0.2, 0.16), (0.2, -0.16, 0.6, 0.16)):
        b.box((x0, y0, 0.5), (x1, y1, 0.6), STONE, bevel=0.015)
    b.box((-0.2, -0.16, 0.45), (0.2, 0.16, 0.51), IRON)                                      # firepot
    b.box((-0.5, 0.18, 0.6), (0.5, 0.44, 1.05), STONE, bevel=0.015)                          # back wall
    b.loft([(1.05, [(-0.56, -0.02), (0.56, -0.02), (0.56, 0.46), (-0.56, 0.46)]),
            (1.2, [(-0.56, -0.02), (0.56, -0.02), (0.56, 0.46), (-0.56, 0.46)]),
            (1.62, [(-0.22, 0.2), (0.22, 0.2), (0.22, 0.46), (-0.22, 0.46)])], STONE)        # hood
    b.box((-0.2, 0.2, 1.62), (0.2, 0.46, 2.3), STONE, bevel=0.012)                           # chimney
    b.box((-0.23, 0.17, 2.3), (0.23, 0.49, 2.36), STONE, bevel=0.01)
    for x in (-0.53, 0.53):                                                                   # hood posts
        b.box((x - 0.04, -0.02, 0.6), (x + 0.04, 0.06, 1.05), OAK)
    # Bellows (left): two boards and a leather wedge, a nozzle into the hearth.
    b.prism_yz([(-0.22, 0.0), (0.22, 0.0), (0.12, 0.2), (-0.22, 0.2)], -1.0, -0.64, OAK)
    b.box((-1.02, -0.24, 0.0), (-0.62, 0.24, 0.04), OAK)
    b.box((-0.66, -0.03, 0.12), (-0.56, 0.03, 0.17), IRON)
    b.box((-1.0, -0.23, 0.2), (-0.64, 0.14, 0.24), OAK)
    b.box((-1.02, -0.02, 0.2), (-0.98, 0.02, 0.62), OAK)                                     # handle
    w5._lathe(b, [(0.17, 0.0), (0.19, 0.34), (0.17, 0.34), (0.16, 0.22), (0.001, 0.2)], 14, OAK, cx=0.85, cy=-0.05)  # quench tub
    w5._lathe(b, [(0.165, 0.3), (0.001, 0.3)], 14, IRON, cx=0.85, cy=-0.05, cap_bottom=False)   # dark water
    for z in (0.08, 0.26):
        w5._lathe(b, [(0.185, z), (0.195, z + 0.03)], 14, IRON, cx=0.85, cy=-0.05, cap_bottom=False, cap_top=False)
    ob = b.finish()
    g = Geo("_coals")
    rng = random.Random(91)
    for k in range(9):
        rock(g, (rng.uniform(-0.14, 0.14), rng.uniform(-0.11, 0.11), 0.52), (0.06, 0.05, 0.04), EMBERS, seed=900 + k, subdiv=1, chisel=3)
    flame(g, (0.0, 0.0, 0.54), 0.24, 0.18, seed=93)
    return centre_xy(join("SM_Forge", [ob, flat(g.finish())]))


def anvil():
    """An anvil on a log block with a hammer resting on it; face at 60 cm."""
    b = MB("_block")
    w5._lathe(b, [(0.19, -0.02), (0.17, 0.1), (0.165, 0.38)], 14, LOG, cap_bottom=False, cap_top=False)
    w5._lathe(b, [(0.165, 0.38), (0.001, 0.385)], 14, OAK, cap_bottom=False)
    b.box((-0.15, -0.1, 0.385), (0.15, 0.1, 0.44), IRON, bevel=0.01)
    b.box((-0.09, -0.06, 0.44), (0.09, 0.06, 0.52), IRON)
    b.box((-0.19, -0.075, 0.52), (0.17, 0.075, 0.6), IRON, bevel=0.008)
    b.box((0.17, -0.05, 0.55), (0.24, 0.05, 0.6), IRON, bevel=0.006)
    b.box((0.02, -0.04, 0.6), (0.14, 0.03, 0.635), IRON)                                      # hammer head
    ob = b.finish()
    g = Geo("_horn")
    g.tube([(-0.19, 0.0, 0.565, 0.042), (-0.28, 0.0, 0.575, 0.027), (-0.36, 0.0, 0.59, 0.006)], 8, IRON, gnarl=0.0, tip=True)
    g.tube([(0.08, -0.005, 0.617, 0.012), (0.1, 0.25, 0.605, 0.011)], 6, OAK, gnarl=0.0, tip=True)   # hammer haft
    return centre_xy(join("SM_Anvil", [ob, g.finish()]))


def hay_bale():
    """A rectangular bale, 62 x 40 x 36 cm, two twine bands."""
    b = MB("SM_HayBale")
    b.box((-0.31, -0.2, 0.0), (0.31, 0.2, 0.36), THATCH, bevel=0.04)
    for x in (-0.15, 0.15):
        b.box((x - 0.012, -0.206, -0.004), (x + 0.012, 0.206, 0.366), ROPE)
    ob = b.finish()
    rng = random.Random(3)
    for v in ob.data.vertices:
        v.co += Vector((rng.uniform(-0.008, 0.008), rng.uniform(-0.008, 0.008), rng.uniform(-0.006, 0.006)))
    ob.data.update()
    return ob


def straw_pile():
    """A loose heap of straw with a bale half buried in it (stables corner),
    1.4 x 1.1 x 0.55 m."""
    g = Geo("_heap")
    g.blob((0.0, 0.0, 0.05), 0.62, THATCH, squash=0.72, seed=5, rough=0.3, subdiv=3)
    g.blob((0.38, 0.22, 0.02), 0.34, THATCH, squash=0.6, seed=6, rough=0.3, subdiv=2)
    heap = g.finish()
    for v in heap.data.vertices:
        v.co.z = max(v.co.z, -0.04)
    heap.data.update()
    bale = xform(hay_bale(), rot=(0.0, 12.0, 30.0), loc=(-0.35, -0.3, 0.02))
    return settle(centre_xy(join("SM_StrawPile", [heap, bale])), -0.03)


def training_post():
    """A pell for the courtyard: a post on crossed feet with a stuffed
    sackcloth torso and a wooden arm; 1.5 m."""
    b = MB("_feet")
    b.box((-0.32, -0.05, 0.0), (0.32, 0.05, 0.08), OAK, bevel=0.01)
    b.box((-0.05, -0.32, 0.0), (0.05, 0.32, 0.08), OAK, bevel=0.01)
    ob = b.finish()
    g = Geo("_pell")
    g.tube([(0.0, 0.0, 0.0, 0.065), (0.0, 0.0, 1.35, 0.06)], 8, OAK, seed=3, gnarl=0.05, cap_start=True, tip=False)
    g.blob((0.0, 0.0, 1.08), 0.19, BURLAP, squash=1.45, seed=4, rough=0.12, subdiv=2)
    g.blob((0.0, 0.0, 1.43), 0.1, BURLAP, squash=1.1, seed=5, rough=0.1, subdiv=2)
    g.tube([(-0.36, 0.0, 1.14, 0.028), (0.36, 0.0, 1.14, 0.028)], 6, OAK, gnarl=0.0, cap_start=True, tip=True)
    for z in (0.86, 1.3):
        g.tube([(0.0, 0.0, z - 0.012, 0.17 if z < 1 else 0.13), (0.0, 0.0, z + 0.012, 0.17 if z < 1 else 0.13)], 10, ROPE, gnarl=0.0, tip=False)
    return join("SM_TrainingPost", [ob, g.finish()])


def cell_bars():
    """The undercroft gaol front: a 128 x 180 iron grille in a door-module
    width, with a locked barred door in the right half. Blocks movement, not
    sight (SM_)."""
    b = MB("_frame")
    b.box((-0.64, -0.035, 0.0), (0.64, 0.035, 0.07), IRON)
    b.box((-0.64, -0.035, 1.73), (0.64, 0.035, 1.8), IRON)
    for x in (-0.64, 0.64):
        b.box((x - 0.035 if x > 0 else x, -0.035, 0.0), (x if x > 0 else x + 0.035, 0.035, 1.8), IRON)
    for z in (0.62, 1.2):
        b.box((-0.6, -0.02, z), (0.0, 0.02, z + 0.04), IRON)
    b.box((0.04, -0.025, 0.07), (0.62, 0.025, 0.11), IRON)                                    # door frame
    b.box((0.04, -0.025, 1.46), (0.62, 0.025, 1.5), IRON)
    b.box((0.04, -0.025, 0.07), (0.08, 0.025, 1.5), IRON)
    b.box((0.58, -0.025, 0.07), (0.62, 0.025, 1.5), IRON)
    b.box((0.08, -0.025, 0.78), (0.58, 0.025, 0.82), IRON)
    b.box((0.44, -0.05, 0.72), (0.56, 0.05, 0.9), IRON, bevel=0.006)                          # lock
    b.box((0.0, -0.035, 0.07), (0.04, 0.035, 1.73), IRON)
    for z in (0.3, 1.3):
        b.box((0.0, -0.045, z), (0.1, 0.045, z + 0.06), IRON)                                 # hinges
    ob = b.finish()
    g = Geo("_bars")
    xs = [-0.55 + 0.11 * i for i in range(5)] + [0.15 + 0.11 * i for i in range(4)]
    for x in xs:
        g.tube([(x, 0.0, 0.05, 0.016), (x, 0.0, 1.76, 0.016)], 6, IRON, gnarl=0.0, tip=False)
    return join("SM_CellBars", [ob, g.finish()])


def stone_ramp():
    """The undercroft ramp (design section 7): 640 x 128 cm, drops 300 cm
    (25.1 deg). Pivot at hall-floor level (z = 0) on the footprint centre;
    the top edge is at +X, the foot at -X, 3 m down. A solid masonry wedge
    with a 12 cm kerb on its open (-Y) side. Per-triangle collision."""
    b = MB("SM_StoneRamp")
    b.prism_xz([(-3.2, -3.0), (3.2, -3.0), (3.2, 0.0)], -0.64, 0.64, KEEP)
    b.prism_xz([(-3.2, -3.0), (3.2, 0.0), (3.2, 0.12), (-3.2, -2.88)], -0.64, -0.54, KEEP)
    ob = b.finish()
    return retag(ob, lambda p: p.normal.z > 0.5 and p.center.y > -0.53, STONEFLOOR)


def keep_parapet_low():
    """A 90 cm parapet on the keep curtain's 128 cm module (the undercroft
    ramp slot's south edge). SM_: it stops feet, not sight."""
    b = MB("SM_KeepParapetLow")
    b.box((-0.64, -0.2, 0.0), (0.64, 0.2, 0.12), KEEP, bevel=0.01)
    b.box((-0.64, -0.17, 0.12), (0.64, 0.17, 0.78), KEEP)
    b.box((-0.64, -0.21, 0.78), (0.64, 0.21, 0.9), KEEP, bevel=0.015)
    return b.finish()


def _charred_board(b, x0, x1, y0, y1, z0, top, rng):
    """A vertical board burnt off at the top: a ragged three-point top edge."""
    zl, zm, zr = top + rng.uniform(-0.12, 0.05), top + rng.uniform(-0.05, 0.12), top + rng.uniform(-0.12, 0.05)
    xm = (x0 + x1) / 2 + rng.uniform(-0.02, 0.02)
    b.prism_xz([(x0, z0), (x1, z0), (x1, zr), (xm, zm), (x0, zl)], y0, y1, CHAR)


def scorched_wall():
    """A burnt timber-frame wall for the Burnt Steadings: 128 wide x 26
    deep, sill, three charred posts (one snapped), a broken head beam,
    charred boards and a daub fragment. Up to 1.85 m."""
    b = MB("VB_ScorchedWall")
    rng = random.Random(101)
    b.box((-0.64, -0.13, 0.0), (0.64, 0.13, 0.14), CHAR, bevel=0.012)
    b.box((-0.64, -0.08, 0.14), (-0.5, 0.08, 1.85), CHAR, bevel=0.01)
    b.prism_xz([(-0.07, 0.14), (0.07, 0.14), (0.07, 1.08), (0.0, 1.22), (-0.07, 1.12)], -0.08, 0.08, CHAR)
    b.prism_xz([(0.5, 0.14), (0.64, 0.14), (0.64, 1.6), (0.57, 1.72), (0.5, 1.64)], -0.08, 0.08, CHAR)
    b.prism_xz([(-0.64, 1.7), (-0.2, 1.7), (-0.12, 1.62), (-0.26, 1.58), (-0.64, 1.58)], -0.09, 0.09, CHAR)
    b.prism_xz([(-0.5, 0.86), (-0.08, 0.28), (-0.08, 0.4), (-0.5, 0.98)], -0.06, 0.06, CHAR)       # brace
    tops = [1.35, 1.1, 0.8, 0.5, None, 0.95, 0.7, 0.45, 0.6]
    x = -0.5
    for t in tops:
        x1 = x + 0.12 if x + 0.12 < 0.5 else 0.5
        if t is not None and not (-0.08 < (x + x1) / 2 < 0.08):
            _charred_board(b, x + 0.004, x1 - 0.004, 0.02, 0.06, 0.14, t, rng)
        x = x1
        if x >= 0.5:
            break
    b.box((0.08, 0.06, 0.14), (0.48, 0.1, 0.55), PLASTER, bevel=0.01)                            # daub remnant
    return b.finish()


def scorched_post():
    """A lone charred post with a snapped knee brace, 1.7 m."""
    b = MB("SM_ScorchedPost")
    b.box((-0.1, -0.1, 0.0), (0.1, 0.1, 0.1), CHAR, bevel=0.01)
    b.prism_xz([(-0.07, 0.1), (0.07, 0.1), (0.07, 1.55), (0.02, 1.72), (-0.07, 1.62)], -0.07, 0.07, CHAR)
    b.prism_xz([(0.07, 1.1), (0.07, 1.25), (0.32, 1.42), (0.34, 1.33)], -0.05, 0.05, CHAR)
    return b.finish()


def scorched_beams():
    """A fallen tangle of charred beams and boards with an ash bed,
    2.2 x 1.5 x 0.5 m."""
    b = MB("_ash")
    w5._ground_disc(b, 0.8, 0.02, CHAR, segs=14)
    parts = [b.finish()]
    rng = random.Random(111)
    for k, (L, rot, loc) in enumerate(((2.0, (0, 6, 12), (0.0, 0.0, 0.1)), (1.6, (0, -8, -40), (0.1, 0.25, 0.22)),
                                       (1.3, (4, 14, 75), (-0.35, -0.1, 0.18)), (1.1, (0, 0, -8), (0.25, -0.45, 0.08)),
                                       (0.9, (0, 3, 110), (0.55, 0.35, 0.09)))):
        bb = MB("_beam%d" % k)
        s = rng.uniform(0.07, 0.09)
        bb.prism_yz([(-s, -s), (s, -s), (s, s), (-s, s)], -L / 2, L / 2 - rng.uniform(0.0, 0.15), CHAR)
        bb.prism_xz([(L / 2 - 0.15, -s), (L / 2, -s * 0.2), (L / 2 - 0.08, s)], -s, s, CHAR)     # burnt end
        parts.append(xform(bb.finish(), rot=rot, loc=loc))
    for k in range(4):
        pb = MB("_plank%d" % k)
        pb.box((-0.35, -0.06, 0.0), (0.35, 0.06, 0.025), CHAR)
        parts.append(xform(pb.finish(), rot=(rng.uniform(-8, 8), rng.uniform(-8, 8), rng.uniform(0, 180)),
                           loc=(rng.uniform(-0.6, 0.6), rng.uniform(-0.5, 0.5), 0.03)))
    ob = join("SM_ScorchedBeams", parts)
    return settle(centre_xy(ob), -0.02)


def _wheel(name, r=0.33):
    b = MB(name)
    for i in range(12):
        a0, a1 = 2 * math.pi * i / 12, 2 * math.pi * (i + 1) / 12
        w5._sector(b, a0 + 0.004, a1 - 0.004, r - 0.06, r - 0.012, -0.035, 0.035, OAK, steps=2)
        w5._sector(b, a0, a1, r - 0.012, r, -0.04, 0.04, IRON, steps=2)
    for k in range(8):
        a = 2 * math.pi * k / 8
        c, s = math.cos(a), math.sin(a)
        p = lambda d, w: (c * d - s * w, s * d + c * w)
        b.prism_xy([p(0.05, -0.015), p(r - 0.05, -0.013), p(r - 0.05, 0.013), p(0.05, 0.015)], -0.018, 0.018, OAK)
    w5._lathe(b, [(0.07, -0.07), (0.075, 0.0), (0.07, 0.07)], 12, DARKPL)
    ob = b.finish()
    return xform(ob, rot=(90.0, 0.0, 0.0))


def cart():
    """The foragers' two-wheeled farm cart, at rest on its shafts, with a few
    sacks; 2.3 x 1.1 x 0.8 m."""
    b = MB("_bed")
    b.box((-0.66, -0.4, 0.42), (0.66, 0.4, 0.47), OAK)
    for y in (-0.4, 0.36):
        b.box((-0.66, y, 0.47), (0.66, y + 0.04, 0.72), DARKPL, bevel=0.006)
    b.box((-0.66, -0.36, 0.47), (-0.62, 0.36, 0.7), DARKPL, bevel=0.006)                      # tailboard
    b.box((0.62, -0.36, 0.47), (0.66, 0.36, 0.66), DARKPL, bevel=0.006)
    for x in (-0.64, 0.0, 0.64):
        for y in (-0.42, 0.42):
            b.box((x - 0.025, y - 0.025, 0.36), (x + 0.025, y + 0.025, 0.78), DARKPL)          # stakes
    for y in (-0.28, 0.28):
        b.box((-0.72, y - 0.035, 0.35), (0.72, y + 0.035, 0.42), DARKPL)                      # chassis
    parts = [b.finish()]
    g = Geo("_gear")
    g.tube([(0.0, -0.55, 0.33, 0.035), (0.0, 0.55, 0.33, 0.035)], 8, IRON, gnarl=0.0, cap_start=True, tip=False)
    for y in (-0.3, 0.3):
        g.tube([(0.7, y, 0.39, 0.035), (1.2, y * 1.05, 0.2, 0.03), (1.58, y * 1.12, 0.04, 0.028)], 7, DARKPL, gnarl=0.02, cap_start=True, tip=True)
    g.tube([(1.3, -0.33, 0.16, 0.022), (1.3, 0.33, 0.16, 0.022)], 6, DARKPL, gnarl=0.0, cap_start=True, tip=True)
    g.blob((-0.3, 0.1, 0.62), 0.2, BURLAP, squash=0.6, seed=7, rough=0.12, subdiv=2)
    g.blob((0.1, -0.12, 0.6), 0.19, BURLAP, squash=0.55, seed=8, rough=0.12, subdiv=2)
    g.blob((-0.18, -0.2, 0.74), 0.16, BURLAP, squash=0.55, seed=9, rough=0.12, subdiv=2)
    parts.append(g.finish())
    for y in (-0.49, 0.49):
        parts.append(xform(_wheel("_wheel%d" % (y > 0)), loc=(0.0, y, 0.33)))
    ob = join("SM_Cart", parts)
    return centre_xy(ob)


def bind_stone():
    """Harrow's Rest bind point: the rune-stone of SM_PortalMarker with a
    green-white glow instead of the portal's, on a round flagstone dais
    ringed by five low stones. No trigger."""
    w3 = _load("wave3_desert")
    ob = w3.portal_marker()
    ob.name = "_marker"
    for i, m in enumerate(ob.data.materials):
        if m.name == "M_PortalGlow":
            ob.data.materials[i] = kit.material(BINDGLOW)
    xform(ob, loc=(0.0, 0.0, 0.05))
    b = MB("_dais")
    w5._lathe(b, [(0.66, 0.0), (0.66, 0.035), (0.63, 0.05), (0.001, 0.05)], 20, STONE)
    g = Geo("_ring")
    for k in range(5):
        a = 2 * math.pi * k / 5 + 0.3
        rock(g, (0.55 * math.cos(a), 0.55 * math.sin(a), 0.1), (0.08, 0.07, 0.13), "MI_Rock", seed=120 + k, subdiv=2, chisel=5)
    return join("SM_BindStone", [ob, b.finish(), flat(g.finish())])


def torch_sconce():
    """A burning torch in an iron wall bracket. Pivot at the wall fixing; it
    stands out along -Y (like SM_TavernSign). Hang at ~150 cm.
    Light: fire_lights.FIRES["SM_TorchSconce"]."""
    b = MB("_bracket")
    b.box((-0.04, -0.012, -0.1), (0.04, 0.0, 0.1), IRON, bevel=0.004)
    w5._lathe(b, [(0.03, 0.02), (0.036, 0.06), (0.03, 0.065), (0.001, 0.03)], 10, IRON, cx=0.0, cy=-0.13)
    ob = b.finish()
    g = Geo("_torch")
    g.tube([(0.0, -0.006, -0.05, 0.011), (0.0, -0.08, 0.0, 0.011), (0.0, -0.13, 0.03, 0.01)], 6, IRON, gnarl=0.0, cap_start=True, tip=False)
    g.tube([(0.0, -0.12, -0.06, 0.018), (0.0, -0.14, 0.14, 0.022)], 7, LOG, seed=5, gnarl=0.05, cap_start=True, tip=False)
    g.tube([(0.0, -0.14, 0.13, 0.028), (0.0, -0.145, 0.19, 0.03), (0.0, -0.146, 0.21, 0.018)], 8, BURLAP, gnarl=0.1, tip=True)
    flame(g, (0.0, -0.146, 0.2), 0.24, 0.1, seed=7)
    return join("SM_TorchSconce", [ob, g.finish()])


REST = [forge, anvil, hay_bale, straw_pile, training_post, cell_bars, stone_ramp, keep_parapet_low,
        scorched_wall, scorched_post, scorched_beams, cart, bind_stone, torch_sconce]

ALL = CLIFFS + PALISADE + THICKET + REST


# ── Build ────────────────────────────────────────────────────────────────────

def _stats(obj):
    bb = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    mn = [round(min(v[k] for v in bb), 3) for k in range(3)]
    mx = [round(max(v[k] for v in bb), 3) for k in range(3)]
    return {"name": obj.name, "tris": kit.tri_count(obj), "min": mn, "max": mx,
            "mats": [m.name for m in obj.data.materials]}


def build(fns=None, export=True, reset=True):
    if reset:
        kit.reset_scene()
    out = []
    for fn in fns or ALL:
        obj = fn()
        if export:
            kit.export_glb(obj, os.path.join(OUT, obj.name + ".glb"))
        out.append(_stats(obj))
    return out


def build_all(export=True):
    return build(ALL, export=export)


def layout(gap=0.6, cols=7):
    """Spread the built pieces on a grid for viewport review, with a 122 cm
    character proxy beside the first."""
    objs = [o for o in bpy.data.objects if o.type == "MESH" and not o.name.startswith("_") and o.name != "ScaleRef"]
    order = {fn.__name__: i for i, fn in enumerate(ALL)}
    x = y = 0.0
    row_d = 0.0
    for i, o in enumerate(objs):
        o.location = (0, 0, 0)
        bpy.context.view_layer.update()
        bb = [Vector(c) for c in o.bound_box]
        w = max(v.x for v in bb) - min(v.x for v in bb)
        d = max(v.y for v in bb) - min(v.y for v in bb)
        if i and i % cols == 0:
            x, y, row_d = 0.0, y - row_d - gap, 0.0
        o.location = (x - min(v.x for v in bb), y - max(v.y for v in bb), 0.0)
        x += w + gap
        row_d = max(row_d, d)
    ref = bpy.data.objects.get("ScaleRef")
    if ref is None:
        me = bpy.data.meshes.new("ScaleRef")
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=0.3, radius2=0.3, depth=1.22)
        bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.61))
        bm.to_mesh(me)
        bm.free()
        ref = bpy.data.objects.new("ScaleRef", me)
        bpy.context.scene.collection.objects.link(ref)
    return [o.name for o in objs]
