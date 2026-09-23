"""B-15 Wave 1: grassland nature pieces.

A-034 SM_Tree_A / SM_Tree_B, A-035 SM_Rock_A, A-036 SM_Water.

Trees are built the way the NWN-era trees read from above: a bark trunk that
forks into a few limbs, each limb ending in a canopy clump. A clump is an
opaque, noise-displaced core (MI_LeafMass, a tileable leaf mass) wrapped in
two-sided leaf-cluster cards (MI_LeafCluster, alpha-masked) that break up the
silhouette. Both leaf materials are instances of M_ValhallaFoliage.

Every piece is fitted back into its first-pass bounds (pivot at the trunk /
footprint centre on the ground), so the levels need no edits.

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
_spec = importlib.util.spec_from_file_location("valhalla_kit", os.path.join(_here, "valhalla_kit.py"))
kit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(kit)

GRASSLAND = os.path.join(kit.IMPORT, "Environment", "Grassland")
BARK, MASS, CARD, ROCK, WATER = "MI_Bark", "MI_LeafMass", "MI_LeafCluster", "MI_Rock", "MI_Water"

kit.PREVIEW.update({BARK: (0.25, 0.18, 0.12), MASS: (0.18, 0.32, 0.08), CARD: (0.25, 0.42, 0.1),
                    ROCK: (0.4, 0.4, 0.38), WATER: (0.05, 0.15, 0.2)})


class Geo:
    """A bmesh with UVs written as faces are made (tubes, blobs, cards)."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.mats = []

    def mat(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def face(self, verts, uvs, m, smooth=True):
        f = self.bm.faces.new(verts)
        f.material_index = self.mat(m)
        f.smooth = smooth
        for loop, uv in zip(f.loops, uvs):
            loop[self.uv].uv = uv
        return f

    # ── Tubes: trunk, roots, limbs ───────────────────────────────────────────
    def tube(self, path, segs, m, seed=0, gnarl=0.12, cap_start=False, tip=True):
        """``path`` = [(x, y, z, r), ...]. Rings follow the path with a
        parallel-transported frame; u runs round the bark in metres, v along it."""
        pts = [Vector(p[:3]) for p in path]
        tangents = []
        for i in range(len(pts)):
            a = pts[max(i - 1, 0)]
            b = pts[min(i + 1, len(pts) - 1)]
            tangents.append((b - a).normalized())
        ref = Vector((1, 0, 0)) if abs(tangents[0].x) < 0.9 else Vector((0, 1, 0))
        normal = tangents[0].cross(ref).normalized()
        rings, vs = [], 0.0
        for i, (p, t) in enumerate(zip(pts, tangents)):
            if i:
                # Parallel transport: rotate the previous normal onto the new tangent.
                rot = tangents[i - 1].rotation_difference(t)
                normal = (rot @ normal).normalized()
                vs += (pts[i] - pts[i - 1]).length
            binormal = t.cross(normal).normalized()
            r = path[i][3]
            ring = []
            for k in range(segs + 1):
                a = 2 * math.pi * (k % segs) / segs
                d = normal * math.cos(a) + binormal * math.sin(a)
                wob = 1.0 + gnarl * noise.noise(p * 6.0 + d * 1.5 + Vector((seed, 0, 0)))
                ring.append((self.bm.verts.new(p + d * r * wob) if k < segs else None, a, r))
            ring[segs] = (ring[0][0], 2 * math.pi, r)
            rings.append((ring, vs))
        for (ra, va), (rb, vb) in zip(rings, rings[1:]):
            for k in range(segs):
                ca = 2 * math.pi * ra[k][2]
                cb = 2 * math.pi * rb[k][2]
                u0a, u1a = ca * k / segs, ca * (k + 1) / segs
                u0b, u1b = cb * k / segs, cb * (k + 1) / segs
                self.face((ra[k][0], ra[k + 1][0], rb[k + 1][0], rb[k][0]),
                          ((u0a, va), (u1a, va), (u1b, vb), (u0b, vb)), m)
        if cap_start:
            ring = rings[0][0]
            vs0 = [ring[k][0] for k in range(segs)][::-1]
            self.face(vs0, [(v.co.x, v.co.y) for v in vs0], m)
        if tip:
            ring, v_end = rings[-1]
            end = pts[-1] + tangents[-1] * path[-1][3]
            c = self.bm.verts.new(end)
            for k in range(segs):
                self.face((ring[k][0], ring[k + 1][0], c),
                          ((k / segs * 0.3, v_end), ((k + 1) / segs * 0.3, v_end), (0.15, v_end + 0.05)), m)

    # ── Canopy cores ─────────────────────────────────────────────────────────
    def blob(self, centre, radius, m, squash=0.85, seed=0, rough=0.22, subdiv=2):
        before = set(self.bm.verts)
        geom = bmesh.ops.create_icosphere(self.bm, subdivisions=subdiv, radius=1.0)
        verts = geom["verts"]
        c = Vector(centre)
        off = Vector((seed * 3.1, seed * 1.7, seed * 2.3))
        for v in verts:
            d = v.co.normalized()
            n = noise.noise(d * 1.6 + off) * 0.6 + noise.noise(d * 3.5 + off) * 0.4
            r = radius * (1.0 + rough * n)
            v.co = c + Vector((d.x * r, d.y * r, d.z * r * squash))
        faces = {f for v in verts for f in v.link_faces}
        idx = self.mat(m)
        for f in faces:
            f.material_index = idx
            f.smooth = True
            f.normal_update()
            ax = max(range(3), key=lambda i: abs(f.normal[i]))
            for loop in f.loops:
                co = loop.vert.co
                loop[self.uv].uv = (co.x, co.y) if ax == 2 else ((co.y, co.z) if ax == 0 else (co.x, co.z))
        return verts

    # ── Leaf cards ───────────────────────────────────────────────────────────
    def card(self, centre, normal, size, spin, m):
        n = Vector(normal).normalized()
        ref = Vector((0, 0, 1)) if abs(n.z) < 0.95 else Vector((1, 0, 0))
        t = n.cross(ref).normalized()
        b = n.cross(t).normalized()
        rot = mathutils.Matrix.Rotation(spin, 3, n)
        t, b = rot @ t, rot @ b
        h = size / 2.0
        c = Vector(centre)
        corners = [c - t * h - b * h, c + t * h - b * h, c + t * h + b * h, c - t * h + b * h]
        vs = [self.bm.verts.new(p) for p in corners]
        # Cards keep flat normals pointing out of the canopy (a smooth, rounded
        # normal would be nicer for lighting; Two Sided Foliage hides most of it).
        self.face(vs, ((0, 0), (1, 0), (1, 1), (0, 1)), m, smooth=False)

    def finish(self):
        return kit.mesh_object(self.name, self.bm, self.mats)


def _clump(g, centre, radius, rng, seed, cards_per_m2=70.0):
    """A leaf clump: an opaque core plus cards spread over its surface."""
    core_r = radius * 0.78
    g.blob(centre, core_r, MASS, squash=0.82, seed=seed)
    c = Vector(centre)
    area = 4 * math.pi * radius * radius
    n_cards = max(8, int(area * cards_per_m2))
    for i in range(n_cards):
        # Fibonacci-ish spread with jitter, biased away from the underside.
        zc = 1.0 - 1.6 * (i + rng.random()) / n_cards
        phi = i * 2.399963 + rng.uniform(-0.3, 0.3)
        rr = math.sqrt(max(0.0, 1 - zc * zc))
        d = Vector((rr * math.cos(phi), rr * math.sin(phi), zc)).normalized()
        surface = c + Vector((d.x * core_r, d.y * core_r, d.z * core_r * 0.82))
        # The game camera looks down from ~50 degrees: face the cards mostly up
        # and outward so few are seen edge-on (the material fades those anyway).
        tilt = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * 0.45
        normal = (d * 0.6 + Vector((0, 0, 0.75)) + tilt).normalized()
        size = radius * rng.uniform(0.9, 1.3)
        g.card(surface + d * size * 0.12, normal, size, rng.uniform(0, 2 * math.pi), CARD)


def _limb(start, end, r0, r1, bend, n=5):
    s, e = Vector(start), Vector(end)
    mid = (s + e) / 2 + Vector(bend)
    pts = []
    for i in range(n):
        t = i / (n - 1)
        p = (1 - t) ** 2 * s + 2 * (1 - t) * t * mid + t * t * e
        pts.append((p.x, p.y, p.z, r0 + (r1 - r0) * t))
    return pts


def _fit(obj, mn, mx, uniform_xy=True):
    """Scale about the pivot so the bounds match the first-pass ones."""
    bb = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    cur_mn = [min(v[k] for v in bb) for k in range(3)]
    cur_mx = [max(v[k] for v in bb) for k in range(3)]
    sz = mx[2] / cur_mx[2]
    sx = (mx[0] - mn[0]) / (cur_mx[0] - cur_mn[0])
    sy = (mx[1] - mn[1]) / (cur_mx[1] - cur_mn[1])
    if uniform_xy:
        sx = sy = min(sx, sy)
    obj.data.transform(mathutils.Matrix.Diagonal((sx, sy, sz, 1.0)))
    obj.data.update()
    bb = [Vector(c) for c in obj.bound_box]
    cur_mn = [min(v[k] for v in bb) for k in range(3)]
    cur_mx = [max(v[k] for v in bb) for k in range(3)]
    # Re-centre the canopy on the old footprint centre (trunk stays near it).
    dx = (mn[0] + mx[0]) / 2 - (cur_mn[0] + cur_mx[0]) / 2
    dy = (mn[1] + mx[1]) / 2 - (cur_mn[1] + cur_mx[1]) / 2
    obj.data.transform(mathutils.Matrix.Translation((dx, dy, 0.0)))
    obj.data.update()
    return obj


def _tree(name, trunk, limbs, clumps, roots, seed, bounds):
    rng = random.Random(seed)
    g = Geo(name)
    g.tube(trunk, 9, BARK, seed=seed, gnarl=0.14, cap_start=True, tip=False)
    for a in roots:
        g.tube(_limb(*a, n=4), 6, BARK, seed=seed + 1, gnarl=0.1)
    for i, l in enumerate(limbs):
        g.tube(_limb(*l), 6, BARK, seed=seed + 10 + i, gnarl=0.1)
    for i, (x, y, z, r) in enumerate(clumps):
        _clump(g, (x, y, z), r, rng, seed * 10 + i)
    obj = g.finish()
    return _fit(obj, *bounds)


def tree_a():
    """Round broadleaf (oak), 2.15 m: short trunk, four limbs, eight clumps."""
    trunk = [(0, 0, -0.04, 0.125), (0.005, 0, 0.10, 0.09), (0.015, 0.01, 0.35, 0.074),
             (0.0, 0.02, 0.58, 0.068), (-0.01, 0.02, 0.78, 0.064)]
    roots = [((0, 0, 0.14), (0.24 * math.cos(a), 0.24 * math.sin(a), -0.02), 0.05, 0.012, (0, 0, 0.03))
             for a in (0.4, 2.0, 3.5, 5.1)]
    clumps = [(0.0, 0.0, 1.45, 0.45), (0.32, 0.10, 1.12, 0.32), (-0.32, 0.14, 1.15, 0.32),
              (0.06, -0.34, 1.10, 0.32), (-0.20, -0.22, 1.50, 0.32), (0.24, -0.10, 1.62, 0.30),
              (-0.06, 0.30, 1.55, 0.30), (0.0, 0.05, 1.85, 0.30)]
    limbs = [((-0.01, 0.02, 0.72), (x * 0.8, y * 0.8, z - r * 0.4), 0.05, 0.02, (0, 0, 0.05))
             for (x, y, z, r) in clumps[:4]]
    # first-pass bounds (Blender x, y, z): x -0.597..0.575, y -0.588..0.604, z 0..2.153
    return _tree("SM_Tree_A", trunk, limbs, clumps, roots, 11,
                 ((-0.597, -0.588, 0.0), (0.575, 0.604, 2.153)))


def tree_b():
    """Taller, leaning tree, 2.36 m: the trunk bends to +X, clumps stack up."""
    trunk = [(0, 0, -0.04, 0.11), (0.01, 0, 0.10, 0.08), (0.05, 0.0, 0.5, 0.066),
             (0.12, 0.01, 0.95, 0.058), (0.20, 0.02, 1.40, 0.05), (0.26, 0.02, 1.85, 0.04)]
    roots = [((0, 0, 0.12), (0.2 * math.cos(a), 0.2 * math.sin(a), -0.02), 0.045, 0.01, (0, 0, 0.03))
             for a in (0.9, 2.6, 4.2, 5.6)]
    clumps = [(0.26, 0.04, 2.02, 0.28), (0.02, 0.12, 1.55, 0.30), (0.36, -0.14, 1.62, 0.28),
              (0.12, -0.12, 1.22, 0.27), (0.40, 0.16, 1.28, 0.25), (-0.08, -0.05, 1.18, 0.22),
              (0.18, 0.18, 1.82, 0.25)]
    limbs = [((0.12, 0.01, 0.95), (0.12, -0.12, 1.1), 0.035, 0.015, (0, 0, 0.04)),
             ((0.10, 0.01, 0.9), (-0.06, -0.04, 1.08), 0.03, 0.012, (0, 0, 0.04)),
             ((0.16, 0.02, 1.15), (0.38, 0.15, 1.18), 0.035, 0.015, (0, 0, 0.05)),
             ((0.18, 0.02, 1.3), (0.02, 0.12, 1.45), 0.035, 0.015, (0, 0, 0.05)),
             ((0.22, 0.02, 1.5), (0.35, -0.13, 1.52), 0.03, 0.012, (0, 0, 0.05))]
    # first-pass bounds (Blender): x -0.418..0.722, y -0.452..0.556, z 0..2.363
    return _tree("SM_Tree_B", trunk, limbs, clumps, roots, 23,
                 ((-0.418, -0.452, 0.0), (0.722, 0.556, 2.363)))


def rock_a():
    """A weathered boulder: displaced icosphere, chiselled by a few planes so
    it has facets, bottom sunk 4 cm into the ground."""
    rng = random.Random(5)
    g = Geo("SM_Rock_A")
    verts = g.blob((0, 0, 0), 1.0, ROCK, squash=1.0, seed=4, rough=0.18, subdiv=4)
    planes = []
    for _ in range(9):
        d = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.2, 1))).normalized()
        planes.append((d, rng.uniform(0.72, 0.9)))
    for v in verts:
        p = v.co.copy()
        for d, k in planes:
            h = p.dot(d)
            if h > k:
                p -= d * (h - k)
        v.co = p
    for v in verts:
        v.co = Vector((v.co.x * 0.35, v.co.y * 0.30, v.co.z * 0.34 + 0.20))
        if v.co.z < -0.04:
            v.co.z = -0.04
    for f in g.bm.faces:
        f.normal_update()
        ax = max(range(3), key=lambda i: abs(f.normal[i]))
        for loop in f.loops:
            co = loop.vert.co
            loop[g.uv].uv = (co.x, co.y) if ax == 2 else ((co.y, co.z) if ax == 0 else (co.x, co.z))
    # Facets read better with split normals at the chiselled edges.
    obj = g.finish()
    # first-pass bounds (Blender): x -0.337..0.355, y -0.283..0.31, z 0..0.528
    return _fit(obj, (-0.337, -0.283, -0.04), (0.355, 0.31, 0.528), uniform_xy=False)


def water():
    """One 64 cm water tile, 3 cm deep, as in the first pass. The look lives in
    M_ValhallaWater (world-aligned ripples), so the mesh is a plain slab."""
    b = kit.MeshBuilder("SM_Water")
    b.box((-0.32, -0.32, 0.0), (0.32, 0.32, 0.03), WATER)
    return b.finish()


PIECES = [tree_a, tree_b, rock_a, water]


def build_all(export=True):
    kit.reset_scene()
    out = []
    for i, fn in enumerate(PIECES):
        obj = fn()
        if export:
            kit.export_glb(obj, os.path.join(GRASSLAND, obj.name + ".glb"))
        bb = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
        mn = [round(min(v[k] for v in bb), 3) for k in range(3)]
        mx = [round(max(v[k] for v in bb), 3) for k in range(3)]
        obj.location.x = i * 1.6
        out.append((obj.name, kit.tri_count(obj), mn, mx, [m.name for m in obj.data.materials]))
    return out
