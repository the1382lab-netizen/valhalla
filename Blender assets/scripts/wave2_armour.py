"""B-15 Wave 2, Stage B: the 16 skinned equipment pieces (A-005..A-020),
fitted to the Wave 2 body.

Every piece keeps its first-pass name (``items.json`` meshIds map to them),
its slot (chest / legs / boots / gloves / head / back) and lives in
``Blender assets/Characters/valhalla_equipment.blend`` on a copy of
``ARM_Valhalla``; each is exported alone as ``Import/Characters/Equipment/
SK_<name>.glb`` (armature + mesh, deform bones only, no animations — the
first-pass file layout) and imported onto ``SK_Valhalla_Skeleton``.

How a piece is fitted: ``Fit`` wraps the rest-pose body in a BVH and answers
"how far is the skin from this bone line in this direction" by ray cast. A
``tube`` is rings of those answers (plus an offset) lofted along a bone line
— sleeves, trousers, boot shafts, gauntlets, the torso of a jerkin, a helm
around the head axis — so the piece hugs the new body by construction and an
offset of 8 mm or more keeps the skin inside through the animations, because
the piece is weighted with the *same* ``wave2_body.skin`` falloff as the
body. Partial tubes (an angle range) make plates and cops, ``band`` makes
belts and straps, ``box`` makes buckles and studs, ``hang`` drops a sheet
from a ring (tabard, cloak, robe skirt). UVs are metres (u = arc length,
v = length along the axis), so a texture set at ``UVScale`` 2–3 tiles at
its real size.

Materials are the CC0 sets of Stage B (``wave2_ambientcg.py``): MI_Leather /
MI_LeatherDark (ambientCG Leather028), MI_Chainmail (Chainmail004),
MI_ClothBlue / Cream / Brown / Green (Fabric032 tinted), MI_IronPlate
(Metal009), plus the existing MI_Steel, MI_Gold, M_IronDark and M_Rope. The
slot name is the instance name; ``w2b_equipment_import.py`` binds by name.

Race shape keys ``Race_Stocky`` / ``Race_Slender`` are added to every piece
with ``wave2_body.race_keys`` so they follow the body's.

Run in Blender: exec, then ``build_all()``.
"""

import importlib.util
import math
import os

import bmesh
import bpy
import mathutils
import numpy as np

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


kit = _load("valhalla_kit")
w2 = _load("wave2_body")

BODY_BLEND = w2.BLEND
EQUIP_BLEND = os.path.join(kit.REPO, "Blender assets", "Characters", "valhalla_equipment.blend")
EQUIP_DIR = os.path.join(kit.IMPORT, "Characters", "Equipment")

# Materials (slot names = Unreal instance names)
LEATHER, LEATHER_D = "MI_Leather", "MI_LeatherDark"
CHAIN, PLATE, STEEL, GOLD, IRON_D = "MI_Chainmail", "MI_IronPlate", "MI_Steel", "MI_Gold", "M_IronDark"
BLUE, CREAM, BROWN, GREEN, ROPE = "MI_ClothBlue", "MI_ClothCream", "MI_ClothBrown", "MI_ClothGreen", "M_Rope"

PREVIEW = {LEATHER: (0.30, 0.17, 0.09), LEATHER_D: (0.12, 0.07, 0.04), CHAIN: (0.45, 0.47, 0.5), PLATE: (0.6, 0.62, 0.65),
           STEEL: (0.55, 0.57, 0.6), GOLD: (0.8, 0.6, 0.2), IRON_D: (0.1, 0.11, 0.12), BLUE: (0.1, 0.18, 0.45),
           CREAM: (0.75, 0.68, 0.5), BROWN: (0.3, 0.2, 0.12), GREEN: (0.15, 0.25, 0.14), ROPE: (0.5, 0.38, 0.18)}
kit.PREVIEW.update(PREVIEW)

# Bone landmarks (metres), from wave2_body
SX, HX = w2.SHOULDER_X, w2.HIP_X
SHOULDER = 0.655
ELBOW = mathutils.Vector((0.0, w2.ELBOW[0], w2.ELBOW[1]))
WRIST_Z, ANKLE_Z = w2.WRIST_Z, w2.ANKLE_Z
KNEE = mathutils.Vector((0.0, w2.KNEE[0], w2.KNEE[1]))


# ── Fitting against the body ─────────────────────────────────────────────────

class Fit:
    """Ray-cast distances from bone lines to the rest-pose body surface."""

    def __init__(self, body):
        dg = bpy.context.evaluated_depsgraph_get()
        self.bvh = mathutils.bvhtree.BVHTree.FromObject(body, dg)
        self.body = body

    def radius(self, origin, direction, max_r=0.5, fallback=None):
        """Distance from ``origin`` to the skin along ``direction`` (the first
        surface crossed, cast from inside the body), clamped to ``max_r``.
        None (or ``fallback``) when the ray meets nothing."""
        loc, nrm, idx, dist = self.bvh.ray_cast(origin, direction, max_r + 0.1)
        if loc is None:
            return fallback
        return min(dist, max_r)

    def ring(self, center, axis, offset, segs=24, max_r=0.5, ref=None, a0=0.0, a1=2 * math.pi,
             closed=True, max_dir=None, shape=None):
        """Points on a circle of rays around ``center`` in the plane ⟂ ``axis``,
        each at skin distance + ``offset``. ``ref`` fixes where angle 0 points
        (default: the character's front, -Y). ``max_dir`` = {angle: max_r}
        overrides let a ring ignore a neighbouring limb. ``shape(angle, r)``
        can modify the radius (flare, brim)."""
        axis = mathutils.Vector(axis).normalized()
        if ref is None:
            ref = mathutils.Vector((0, -1, 0))
        u = (mathutils.Vector(ref) - axis * mathutils.Vector(ref).dot(axis)).normalized()
        v = axis.cross(u).normalized()
        n = segs if closed else segs + 1
        c = mathutils.Vector(center)
        dirs, raw = [], []
        for k in range(n):
            a = a0 + (a1 - a0) * (k / segs)
            d = u * math.cos(a) + v * math.sin(a)
            mr = max_r
            if max_dir:
                for ang, lim in max_dir.items():
                    if abs((a - ang + math.pi) % (2 * math.pi) - math.pi) < 0.5:
                        mr = lim
            dirs.append((a, d))
            raw.append(self.radius(c, d, mr))
        # A ray that meets nothing (cast from just outside the skin, e.g. the
        # chin line or between the legs) takes the nearest hit neighbours'
        # distance instead of a spike to max_r.
        hits = [r for r in raw if r is not None]
        if not hits:
            raw = [max_r * 0.5] * n
        else:
            for k in range(n):
                if raw[k] is None:
                    left = right = None
                    for j in range(1, n):
                        if left is None and raw[(k - j) % n] is not None:
                            left = raw[(k - j) % n]
                        if right is None and raw[(k + j) % n] is not None:
                            right = raw[(k + j) % n]
                    raw[k] = min(left if left is not None else right, right if right is not None else left)
        pts = []
        for (a, d), r in zip(dirs, raw):
            r = r + offset
            if shape:
                r = shape(a, r)
            pts.append(c + d * r)
        return pts


# ── Piece builder ────────────────────────────────────────────────────────────

class Piece:
    """One equipment mesh: parts with their own material slots and metre UVs."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.mats = []

    def _mat(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def loft(self, rings, m, closed=True, cap_a=None, cap_b=None, rim=0.0, flip=False, v0=0.0, orient=None):
        """Faces between consecutive rings (all the same length). ``rim`` folds
        the open ends inward by that much so they read as thick from outside.
        Every face is turned to point away from its ring's centre (or along
        ``orient`` when given), because Unreal culls back faces and bmesh's
        normal recalculation guesses wrong on open tubes."""
        idx = self._mat(m)
        if rim > 0.0:
            rings = [self._shrunk(rings[0], rings[1], rim)] + list(rings) + [self._shrunk(rings[-1], rings[-2], rim)]
        vr = [[self.bm.verts.new(p) for p in ring] for ring in rings]
        n = len(rings[0])
        centres = [sum(r, mathutils.Vector()) / n for r in rings]

        def turn(f, hint):
            f.normal_update()
            if f.normal.dot(hint) < 0:
                f.normal_flip()
        # v along the loft, u around, both in metres
        vlen = [0.0]
        for a, b in zip(rings, rings[1:]):
            vlen.append(vlen[-1] + (sum((p - q).length for p, q in zip(a, b)) / n))
        faces = []
        for i, (r0, r1) in enumerate(zip(vr, vr[1:])):
            ulen = 0.0
            for k in range(n if closed else n - 1):
                k2 = (k + 1) % n
                quad = (r0[k], r0[k2], r1[k2], r1[k]) if not flip else (r0[k2], r0[k], r1[k], r1[k2])
                try:
                    f = self.bm.faces.new(quad)
                except ValueError:
                    continue
                f.material_index = idx
                turn(f, orient if orient is not None else (f.calc_center_median() - (centres[i] + centres[i + 1]) / 2))
                seg = (rings[i][k2] - rings[i][k]).length
                uvs = [(ulen, vlen[i] + v0), (ulen + seg, vlen[i] + v0), (ulen + seg, vlen[i + 1] + v0), (ulen, vlen[i + 1] + v0)]
                if flip:
                    uvs = [uvs[1], uvs[0], uvs[3], uvs[2]]
                for loop, uv in zip(f.loops, uvs):
                    loop[self.uv].uv = uv
                ulen += seg
                faces.append(f)
        for cap, ring, rev in ((cap_a, vr[0], True), (cap_b, vr[-1], False)):
            if cap is None:
                continue
            c = self.bm.verts.new(cap)
            for k in range(n if closed else n - 1):
                k2 = (k + 1) % n
                tri = (ring[k2], ring[k], c) if rev else (ring[k], ring[k2], c)
                try:
                    f = self.bm.faces.new(tri)
                except ValueError:
                    continue
                f.material_index = idx
                turn(f, orient if orient is not None else (mathutils.Vector(cap) - (centres[0] if rev else centres[-1])))
                for loop in f.loops:
                    p = loop.vert.co
                    loop[self.uv].uv = (p.x + p.y, p.z)
        return faces

    @staticmethod
    def _shrunk(ring, towards, amount):
        """The ring pulled ``amount`` toward its centre and slightly toward the
        next ring: the fold that fakes thickness at an open end."""
        c = sum(ring, mathutils.Vector()) / len(ring)
        c2 = sum(towards, mathutils.Vector()) / len(towards)
        step = (c2 - c).normalized() * (amount * 0.5)
        out = []
        for p in ring:
            d = (p - c)
            L = d.length
            out.append(c + d * max(0.0, (L - amount) / L) + step)
        return out

    def box(self, mn, mx, m):
        idx = self._mat(m)
        geom = bmesh.ops.create_cube(self.bm, size=1.0)
        c = [(a + b) / 2.0 for a, b in zip(mn, mx)]
        s = [b - a for a, b in zip(mn, mx)]
        for v in geom["verts"]:
            v.co = mathutils.Vector((c[0] + v.co.x * s[0], c[1] + v.co.y * s[1], c[2] + v.co.z * s[2]))
        for f in self.bm.faces:
            if f.material_index == 0 and any(v in geom["verts"] for v in f.verts):
                f.material_index = idx
                for loop in f.loops:
                    p = loop.vert.co
                    loop[self.uv].uv = (p.x + p.y, p.z)
        return geom["verts"]

    def stud(self, p, r, m, segs=8):
        """A low dome at point ``p`` (a rivet or a buckle boss)."""
        idx = self._mat(m)
        p = mathutils.Vector(p)
        n = p.copy()
        n.z = 0
        n = n.normalized() if n.length > 1e-6 else mathutils.Vector((0, -1, 0))
        u = n.cross(mathutils.Vector((0, 0, 1))).normalized()
        v = n.cross(u)
        ring = [p + u * (r * math.cos(2 * math.pi * k / segs)) + v * (r * math.sin(2 * math.pi * k / segs)) for k in range(segs)]
        top = p + n * (r * 0.6)
        verts = [self.bm.verts.new(q) for q in ring]
        t = self.bm.verts.new(top)
        for k in range(segs):
            f = self.bm.faces.new((verts[k], verts[(k + 1) % segs], t))
            f.material_index = idx
            f.normal_update()
            if f.normal.dot(n) < 0:
                f.normal_flip()
            for loop in f.loops:
                q = loop.vert.co
                loop[self.uv].uv = (q.x + q.y, q.z)

    def finish(self):
        bmesh.ops.remove_doubles(self.bm, verts=self.bm.verts, dist=1e-5)
        me = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(me)
        self.bm.free()
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        for m in self.mats:
            mat = kit.material(m)
            c = PREVIEW.get(m, (0.5, 0.5, 0.5))
            mat.diffuse_color = (c[0], c[1], c[2], 1.0)      # Workbench review colour
            me.materials.append(mat)
        for p in me.polygons:
            p.use_smooth = True
        return ob


# ── Fitted primitives ────────────────────────────────────────────────────────

def _lerp(a, b, t):
    return a + (b - a) * t


def tube(piece, fit, a, b, offset, m, n=8, segs=24, max_r=0.5, ref=None, a0=0.0, a1=2 * math.pi,
         closed=True, rim=0.006, cap_a=False, cap_b=False, max_dir=None, offset_b=None, flare=None, shape=None,
         orient=None):
    """Rings from bone point ``a`` to ``b`` (the axis), each fitted to the skin
    plus ``offset`` (linear to ``offset_b`` at the far end). ``flare(t)`` adds
    an extra radius along the way (cuffs, skirts)."""
    a, b = mathutils.Vector(a), mathutils.Vector(b)
    axis = (b - a).normalized()
    rings = []
    for i in range(n + 1):
        t = i / n
        off = _lerp(offset, offset if offset_b is None else offset_b, t) + (flare(t) if flare else 0.0)
        rings.append(fit.ring(a + (b - a) * t, axis, off, segs, max_r, ref, a0, a1, closed, max_dir, shape))
    ca = (a - axis * (offset * 0.8)) if cap_a else None
    cb = (b + axis * (offset * 0.8)) if cap_b else None
    piece.loft(rings, m, closed=closed, cap_a=ca, cap_b=cb, rim=0.0 if (cap_a and cap_b) else rim, orient=orient)
    return rings


def band(piece, fit, center, axis, offset, height, m, segs=24, max_r=0.5, ref=None, a0=0.0, a1=2 * math.pi, closed=True,
         rim=0.004, max_dir=None):
    """A belt or strap: a short tube ``height`` tall around ``center``."""
    axis = mathutils.Vector(axis).normalized()
    c = mathutils.Vector(center)
    return tube(piece, fit, c - axis * (height / 2), c + axis * (height / 2), offset, m, n=1, segs=segs, max_r=max_r,
                ref=ref, a0=a0, a1=a1, closed=closed, rim=rim, max_dir=max_dir)


def hang(piece, top_ring, bottom_z, m, n=6, spread=1.0, sway=None, rim=0.004, closed=True, taper=None):
    """A sheet that hangs from ``top_ring`` straight down to ``bottom_z``,
    spreading outward from the ring's centre by ``spread`` per metre of drop
    (a skirt, a tabard, a cloak). ``sway(t)`` shifts the whole ring (metres)."""
    c = sum(top_ring, mathutils.Vector()) / len(top_ring)
    z0 = c.z
    rings = []
    for i in range(n + 1):
        t = i / n
        z = _lerp(z0, bottom_z, t)
        drop = z0 - z
        k = 1.0 + spread * drop * (taper(t) if taper else 1.0)
        ring = []
        for p in top_ring:
            q = mathutils.Vector((c.x + (p.x - c.x) * k, c.y + (p.y - c.y) * k, z))
            if sway:
                q += sway(t)
            ring.append(q)
        rings.append(ring)
    piece.loft(rings, m, closed=closed, rim=rim)
    return rings


def mirror_x(pts):
    return [mathutils.Vector((-p.x, p.y, p.z)) for p in pts]


def arm_axis(side, t0, t1):
    """Points along the upper arm (t in 0..1, shoulder->elbow) for the given side."""
    a = mathutils.Vector((side * SX, 0.0, SHOULDER))
    e = mathutils.Vector((side * SX, ELBOW.y, ELBOW.z))
    return a + (e - a) * t0, a + (e - a) * t1


def forearm_axis(side, t0, t1):
    e = mathutils.Vector((side * SX, ELBOW.y, ELBOW.z))
    w = mathutils.Vector((side * SX, 0.0, WRIST_Z))
    return e + (w - e) * t0, e + (w - e) * t1


def thigh_axis(side, t0, t1):
    h = mathutils.Vector((side * HX, 0.0, 0.385))
    k = mathutils.Vector((side * HX, KNEE.y, KNEE.z))
    return h + (k - h) * t0, h + (k - h) * t1


def calf_axis(side, t0, t1):
    k = mathutils.Vector((side * HX, KNEE.y, KNEE.z))
    a = mathutils.Vector((side * HX, 0.0, ANKLE_Z))
    return k + (a - k) * t0, k + (a - k) * t1


# ── The pieces ───────────────────────────────────────────────────────────────

PI = math.pi
# Ring angles (axis pointing up, ref = front): 0 is the front (-Y), PI/2 is
# the character's left (+X), PI the back, 3PI/2 the right.
FRONT_HALF = (-PI / 2, PI / 2)
BACK_HALF = (PI / 2, 3 * PI / 2)


def _outer(side):
    """Angle range of the half of an arm/leg ring that faces away from the
    body (rings around a downward axis with the front as angle 0)."""
    return (-PI, 0.0) if side > 0 else (0.0, PI)


def chest_travelers_jerkin(fit):
    p = Piece("SK_chest_travelers_jerkin")
    # leather body, arm holes under the deltoid
    tube(p, fit, (0, 0, 0.37), (0, 0, 0.655), 0.010, LEATHER, n=9, segs=28, max_r=0.19)
    # shoulder caps over the deltoids (outer half of the upper arm)
    for s in (1, -1):
        a, b = arm_axis(s, -0.08, 0.32)
        a0, a1 = _outer(s)
        tube(p, fit, a, b, 0.012, LEATHER, n=4, segs=10, max_r=0.09, a0=a0, a1=a1, closed=False, rim=0.005)
    # collar
    band(p, fit, (0, 0, 0.685), (0, 0, 1), 0.012, 0.035, LEATHER_D, segs=20, max_r=0.09)
    # belt + buckle
    band(p, fit, (0, 0, 0.455), (0, 0, 1), 0.018, 0.036, LEATHER_D, segs=28, max_r=0.19)
    fy = -fit.radius(mathutils.Vector((0, 0, 0.455)), mathutils.Vector((0, -1, 0))) - 0.018
    p.box((-0.022, fy - 0.010, 0.437), (0.022, fy + 0.006, 0.473), STEEL)
    # two front straps with studs
    for sx in (-0.42, 0.42):
        tube(p, fit, (0, 0, 0.475), (0, 0, 0.645), 0.016, LEATHER_D, n=4, segs=2, max_r=0.19,
             a0=sx - 0.12, a1=sx + 0.12, closed=False, rim=0.004)
        for z in (0.50, 0.56, 0.62):
            d = mathutils.Vector((math.sin(sx), -math.cos(sx), 0))     # ring angle -> direction
            r = fit.radius(mathutils.Vector((0, 0, z)), d)
            p.stud(mathutils.Vector((0, 0, z)) + d * (r + 0.018), 0.008, STEEL)
    return p.finish()


def chest_priests_chain(fit):
    p = Piece("SK_chest_priests_chain")
    tube(p, fit, (0, 0, 0.35), (0, 0, 0.66), 0.012, CHAIN, n=9, segs=28, max_r=0.19)
    for s in (1, -1):
        a, b = arm_axis(s, -0.05, 0.55)
        tube(p, fit, a, b, 0.009, CHAIN, n=5, segs=16, max_r=0.075, flare=lambda t: 0.006 * t)
    band(p, fit, (0, 0, 0.69), (0, 0, 1), 0.014, 0.045, CHAIN, segs=20, max_r=0.09)        # coif collar
    # tabard: front and back panels hanging from the chest, with a hem trim
    for (a0, a1) in ((-0.62, 0.62), (PI - 0.62, PI + 0.62)):
        top = fit.ring((0, 0, 0.64), (0, 0, 1), 0.024, 8, 0.19, a0=a0, a1=a1, closed=False)
        hang(p, top, 0.27, CREAM, n=6, spread=0.35, closed=False)
        hem = [mathutils.Vector((q.x, q.y, 0.27)) for q in hang_ring(top, 0.27, 0.35)]
        hang(p, hem, 0.245, LEATHER_D, n=1, spread=0.35, closed=False)
    band(p, fit, (0, 0, 0.455), (0, 0, 1), 0.034, 0.034, LEATHER_D, segs=28, max_r=0.19)   # belt over the tabard
    fy = -fit.radius(mathutils.Vector((0, 0, 0.58)), mathutils.Vector((0, -1, 0))) - 0.03
    p.stud((0, fy, 0.58), 0.02, LEATHER_D)                                                 # holy symbol boss
    return p.finish()


def hang_ring(top_ring, z, spread, extra=0.03):
    """Where ``hang`` puts the ring at height ``z``, scaled out by ``extra``
    so a trim stacked on the sheet sits just proud of it (no z-fighting)."""
    c = sum(top_ring, mathutils.Vector()) / len(top_ring)
    k = 1.0 + spread * (c.z - z) + extra
    return [mathutils.Vector((c.x + (q.x - c.x) * k, c.y + (q.y - c.y) * k, z)) for q in top_ring]


def chest_apprentice_robe(fit):
    p = Piece("SK_chest_apprentice_robe")
    tube(p, fit, (0, 0, 0.40), (0, 0, 0.665), 0.012, BLUE, n=8, segs=28, max_r=0.19)
    for s in (1, -1):
        a, b = arm_axis(s, -0.05, 1.10)
        tube(p, fit, a, b, 0.009, BLUE, n=6, segs=16, max_r=0.075)
        a, b = forearm_axis(s, -0.12, 0.97)
        tube(p, fit, a, b, 0.012, BLUE, n=6, segs=16, max_r=0.07, flare=lambda t: 0.028 * t ** 3)   # bell cuff
    top = fit.ring((0, 0, 0.42), (0, 0, 1), 0.020, 28, 0.19)
    hang(p, top, 0.22, BLUE, n=6, spread=0.9)                                              # skirt to the knee
    hem = hang_ring(top, 0.245, 0.9)
    hang(p, hem, 0.215, CREAM, n=1, spread=0.9)                                            # hem trim
    band(p, fit, (0, 0, 0.45), (0, 0, 1), 0.026, 0.020, ROPE, segs=28, max_r=0.19)         # rope belt
    band(p, fit, (0, 0, 0.685), (0, 0, 1), 0.016, 0.05, CREAM, segs=20, max_r=0.09)        # cowl collar
    return p.finish()


def legs_travelers(fit):
    p = Piece("SK_legs_travelers")
    tube(p, fit, (0, 0, 0.335), (0, 0, 0.47), 0.012, BROWN, n=4, segs=28, max_r=0.19)
    for s in (1, -1):
        a, b = thigh_axis(s, -0.15, 1.12)
        tube(p, fit, a, b, 0.008, BROWN, n=6, segs=18, max_r=0.12)
        a, b = calf_axis(s, -0.12, 0.92)
        tube(p, fit, a, b, 0.0105, BROWN, n=6, segs=18, max_r=0.09)
        # leather knee patch
        band(p, fit, (s * HX, KNEE.y, KNEE.z), (0, 0, 1), 0.016, 0.07, LEATHER, segs=10, max_r=0.09,
             a0=-1.1, a1=1.1, closed=False)
    band(p, fit, (0, 0, 0.455), (0, 0, 1), 0.016, 0.03, LEATHER_D, segs=28, max_r=0.19)
    fy = -fit.radius(mathutils.Vector((0, 0, 0.455)), mathutils.Vector((0, -1, 0))) - 0.016
    p.stud((0, fy, 0.455), 0.014, LEATHER_D)
    return p.finish()


def legs_iron_plate(fit):
    p = Piece("SK_legs_iron_plate")
    band(p, fit, (0, 0, 0.445), (0, 0, 1), 0.014, 0.06, PLATE, segs=28, max_r=0.19)        # fauld
    band(p, fit, (0, 0, 0.39), (0, 0, 1), 0.019, 0.05, PLATE, segs=28, max_r=0.19)         # second lame
    for s in (1, -1):
        # cuisse and greave overlap the knee (the joint bends ~50 deg in Walk/Run
        # and ~120 deg in Sit); the cop sits further out so the kneecap stays under it
        a, b = thigh_axis(s, 0.05, 0.98)
        tube(p, fit, a, b, 0.014, PLATE, n=5, segs=18, max_r=0.12)                         # cuisse
        band(p, fit, (s * HX, KNEE.y, KNEE.z), (0, 0, 1), 0.028, 0.08, PLATE, segs=12, max_r=0.09,
             a0=-1.3, a1=1.3, closed=False)                                                # knee cop
        band(p, fit, (s * HX, KNEE.y, KNEE.z), (0, 0, 1), 0.006, 0.03, LEATHER_D, segs=18, max_r=0.09)  # strap
        a, b = calf_axis(s, 0.06, 0.92)
        tube(p, fit, a, b, 0.010, PLATE, n=5, segs=18, max_r=0.09)                         # greave
    return p.finish()


def legs_apprentice_robe(fit):
    p = Piece("SK_legs_apprentice_robe")
    tube(p, fit, (0, 0, 0.34), (0, 0, 0.46), 0.008, BLUE, n=3, segs=28, max_r=0.19)
    top = fit.ring((0, 0, 0.40), (0, 0, 1), 0.018, 28, 0.19)
    hang(p, top, 0.10, BLUE, n=8, spread=0.6)
    hem = hang_ring(top, 0.125, 0.6)
    hang(p, hem, 0.095, CREAM, n=1, spread=0.6)
    return p.finish()


def _foot(p, fit, s, offset, m, segs=16, n=9):
    heel = mathutils.Vector((s * HX, 0.082, 0.05))
    toe = mathutils.Vector((s * HX, -0.138, 0.032))       # body toe reaches y=-0.14; the cap sits 0.8*offset past this
    rings = tube(p, fit, heel, toe, offset, m, n=n, segs=segs, max_r=0.09, ref=(0, 0, 1), cap_a=True, cap_b=True)
    return rings


def _sole(p, s, m, y0=-0.145, y1=0.095, w=0.056, h=0.012):
    p.box((s * HX - w, y0, 0.0), (s * HX + w, y1, h), m)


def boots_ranger(fit):
    p = Piece("SK_boots_ranger")
    for s in (1, -1):
        a, b = calf_axis(s, 0.2, 1.0)
        tube(p, fit, a, b, 0.011, LEATHER, n=5, segs=18, max_r=0.09)                       # shaft
        band(p, fit, calf_axis(s, 0.2, 0.2)[0], (0, 0, 1), 0.020, 0.035, LEATHER_D, segs=18, max_r=0.09)  # cuff
        _foot(p, fit, s, 0.014, LEATHER)
        band(p, fit, (s * HX, -0.03, 0.058), (0, -1, -0.12), 0.014, 0.02, LEATHER_D, segs=16, max_r=0.09, ref=(0, 0, 1))
        _sole(p, s, LEATHER_D)
    return p.finish()


def boots_iron_sabatons(fit):
    p = Piece("SK_boots_iron_sabatons")
    for s in (1, -1):
        _foot(p, fit, s, 0.016, PLATE)
        for i, y in enumerate((-0.015, -0.055, -0.095)):
            band(p, fit, (s * HX, y, 0.05 - 0.004 * i), (0, -1, -0.12), 0.015 - 0.001 * i, 0.028, PLATE, segs=16,
                 max_r=0.09, ref=(0, 0, 1))
        a, b = calf_axis(s, 0.8, 1.05)
        tube(p, fit, a, b, 0.012, PLATE, n=2, segs=18, max_r=0.09)                         # ankle greave
        _sole(p, s, IRON_D)
    return p.finish()


def boots_cloth_slippers(fit):
    p = Piece("SK_boots_cloth_slippers")
    for s in (1, -1):
        _foot(p, fit, s, 0.012, CREAM)
        band(p, fit, (s * HX, 0.0, ANKLE_Z + 0.01), (0, 0, 1), 0.008, 0.022, BROWN, segs=16, max_r=0.09)
        _sole(p, s, BROWN, h=0.008)
    return p.finish()


def _hand(p, fit, s, offset, m, segs=16, n=6):
    wrist = mathutils.Vector((s * SX, 0.0, WRIST_Z + 0.01))
    tip = mathutils.Vector((s * SX, -0.004, 0.283))
    return tube(p, fit, wrist, tip, offset, m, n=n, segs=segs, max_r=0.075, cap_b=True)


def gloves_ranger_bracers(fit):
    p = Piece("SK_gloves_ranger_bracers")
    for s in (1, -1):
        a, b = forearm_axis(s, 0.12, 0.98)
        tube(p, fit, a, b, 0.009, LEATHER, n=5, segs=16, max_r=0.07)
        for t in (0.3, 0.55, 0.8):
            band(p, fit, forearm_axis(s, t, t)[0], (0, 0, -1), 0.013, 0.018, LEATHER_D, segs=16, max_r=0.07)
        _hand(p, fit, s, 0.008, LEATHER)
    return p.finish()


def gloves_iron_gauntlets(fit):
    p = Piece("SK_gloves_iron_gauntlets")
    for s in (1, -1):
        a, b = forearm_axis(s, 0.2, 0.98)
        tube(p, fit, a, b, 0.011, PLATE, n=5, segs=16, max_r=0.07, flare=lambda t: 0.022 * (1 - t) ** 2)   # flared cuff
        band(p, fit, forearm_axis(s, 0.98, 0.98)[0], (0, 0, -1), 0.008, 0.03, LEATHER_D, segs=16, max_r=0.07)
        _hand(p, fit, s, 0.010, PLATE)
        for z, h in ((0.335, 0.03), (0.305, 0.022)):                                       # knuckle lames
            band(p, fit, (s * SX, 0.0, z), (0, 0, 1), 0.016, h, PLATE, segs=10, max_r=0.075, a0=-1.3, a1=1.3, closed=False)
    return p.finish()


def gloves_cloth_wraps(fit):
    p = Piece("SK_gloves_cloth_wraps")
    for s in (1, -1):
        _hand(p, fit, s, 0.007, CREAM)
        for i, t in enumerate((0.16, 0.34, 0.52, 0.70, 0.88)):
            band(p, fit, forearm_axis(s, t, t)[0], (0, 0, -1), 0.008 + 0.003 * (i % 2), 0.032, CREAM, segs=16, max_r=0.07)
        band(p, fit, forearm_axis(s, 0.97, 0.97)[0], (0, 0, -1), 0.012, 0.016, BROWN, segs=16, max_r=0.07)
    return p.finish()


HEAD_AXIS = (0.0, 0.005, 1.0)


def _head_tube(p, fit, z0, z1, offset, m, n, segs=28, a0=0.0, a1=2 * PI, closed=True, cap_b=False, rim=0.006, flare=None):
    return tube(p, fit, (HEAD_AXIS[0], HEAD_AXIS[1], z0), (HEAD_AXIS[0], HEAD_AXIS[1], z1), offset, m, n=n, segs=segs,
                max_r=0.3, a0=a0, a1=a1, closed=closed, cap_b=cap_b, rim=rim, flare=flare)


def helm_iron_full(fit):
    p = Piece("SK_helm_iron_full")
    _head_tube(p, fit, 0.80, 0.865, 0.016, PLATE, 2, flare=lambda t: 0.012 * (1 - t))                 # chin / neck guard
    _head_tube(p, fit, 0.865, 1.04, 0.014, PLATE, 3, a0=0.6, a1=2 * PI - 0.6, closed=False)          # face opening
    _head_tube(p, fit, 1.04, 1.205, 0.014, PLATE, 5, cap_b=True)                                    # skull
    fy = -fit.radius(mathutils.Vector((0, 0.005, 0.95)), mathutils.Vector((0, -1, 0))) - 0.02
    p.box((-0.012, fy - 0.012, 0.87), (0.012, fy + 0.03, 1.045), IRON_D)                             # nasal bar
    p.box((-0.075, fy + 0.005, 0.955), (0.075, fy + 0.04, 0.972), IRON_D)                            # brow bar
    p.box((-0.010, -0.12, 1.19), (0.010, 0.16, 1.24), IRON_D)                                        # crest
    return p.finish()


def hood_scout(fit):
    p = Piece("SK_hood_scout")
    _head_tube(p, fit, 0.80, 1.235, 0.020, LEATHER, 8, a0=0.95, a1=2 * PI - 0.95, closed=False, cap_b=True)  # hood
    neck = fit.ring((0, 0.0, 0.70), (0, 0, 1), 0.022, 24, 0.12)
    hang(p, neck, 0.545, LEATHER, n=4, spread=2.6)                                                  # shoulder mantle
    hem = hang_ring(neck, 0.565, 2.6)
    hang(p, hem, 0.54, LEATHER_D, n=1, spread=2.6)
    band(p, fit, (0, 0, 0.71), (0, 0, 1), 0.016, 0.03, LEATHER_D, segs=20, max_r=0.09)
    return p.finish()


def hat_adept(fit):
    p = Piece("SK_hat_adept")
    inner = fit.ring((0, 0.005, 1.055), (0, 0, 1), 0.014, 28, 0.3)
    outer = [q + (q - mathutils.Vector((0, 0.005, 1.055))).normalized() * 0.19 - mathutils.Vector((0, 0, 0.025)) for q in inner]
    mid = [(a + b) / 2 + mathutils.Vector((0, 0, 0.012)) for a, b in zip(inner, outer)]
    p.loft([inner, mid, outer], BLUE, rim=0.0, orient=mathutils.Vector((0, 0, 1)))                 # brim, top
    p.loft([inner, mid, outer], BLUE, rim=0.0, orient=mathutils.Vector((0, 0, -1)))                # brim, underside
    _head_tube(p, fit, 1.055, 1.20, 0.014, BLUE, 3)
    top = fit.ring((0, 0.005, 1.20), (0, 0, 1), 0.014, 28, 0.3)
    c = mathutils.Vector((0, 0.005, 1.20))
    rings = [top]
    for i in range(1, 7):
        t = i / 6
        k = (1 - t) ** 1.3
        rings.append([c + (q - c) * k + mathutils.Vector((0, 0.09 * t * t, 0.38 * t)) for q in top])
    p.loft(rings, BLUE, rim=0.0, cap_b=c + mathutils.Vector((0, 0.09, 0.39)))                       # bent cone
    band(p, fit, (0, 0.005, 1.085), (0, 0, 1), 0.024, 0.035, LEATHER_D, segs=28, max_r=0.3)
    return p.finish()


def cloak_warden(fit):
    p = Piece("SK_cloak_warden")
    neck = fit.ring((0, 0, 0.705), (0, 0, 1), 0.022, 24, 0.12)
    hang(p, neck, 0.575, GREEN, n=3, spread=2.4)                                                    # shoulder cape
    back = fit.ring((0, 0, 0.635), (0, 0, 1), 0.030, 16, 0.19, a0=PI - 1.45, a1=PI + 1.45, closed=False)
    hang(p, back, 0.115, GREEN, n=8, spread=0.35, closed=False, sway=lambda t: mathutils.Vector((0, 0.045 * t * t, 0)))
    hem = [q + mathutils.Vector((0, 0.045, 0)) for q in hang_ring(back, 0.14, 0.35)]
    hang(p, hem, 0.11, LEATHER_D, n=1, spread=0.35, closed=False)
    fy = -fit.radius(mathutils.Vector((0, 0, 0.66)), mathutils.Vector((0, -1, 0))) - 0.024
    p.stud((0, fy, 0.66), 0.016, STEEL)                                                             # clasp
    return p.finish()


PIECES = {
    "SK_chest_travelers_jerkin": chest_travelers_jerkin, "SK_chest_priests_chain": chest_priests_chain,
    "SK_chest_apprentice_robe": chest_apprentice_robe, "SK_legs_travelers": legs_travelers,
    "SK_legs_iron_plate": legs_iron_plate, "SK_legs_apprentice_robe": legs_apprentice_robe,
    "SK_boots_ranger": boots_ranger, "SK_boots_iron_sabatons": boots_iron_sabatons,
    "SK_boots_cloth_slippers": boots_cloth_slippers, "SK_gloves_ranger_bracers": gloves_ranger_bracers,
    "SK_gloves_iron_gauntlets": gloves_iron_gauntlets, "SK_gloves_cloth_wraps": gloves_cloth_wraps,
    "SK_helm_iron_full": helm_iron_full, "SK_hood_scout": hood_scout, "SK_hat_adept": hat_adept,
    "SK_cloak_warden": cloak_warden,
}

# ── Weights ──────────────────────────────────────────────────────────────────

NO_ARMS = {k: v for k, v in w2.BONE_INFLUENCE.items() if not (k.startswith("upperarm") or k.startswith("lowerarm")
                                                             or k.startswith("hand") or k.startswith("clavicle"))}
HEAD_ONLY = {"head": dict(R=0.6, head=0.35, tail=0.5), "neck": dict(R=0.12, head=0.03, tail=0.02),
             "spine_02": dict(R=0.3, head=0.04, tail=0.02)}
CLOAK = dict(NO_ARMS)
CLOAK.update({"thigh_l": dict(R=0.24, head=0.05, tail=0.03), "thigh_r": dict(R=0.24, head=0.05, tail=0.03),
              "calf_l": dict(R=0.16, head=0.03, tail=0.03), "calf_r": dict(R=0.16, head=0.03, tail=0.03)})
INFLUENCE = {"SK_helm_iron_full": HEAD_ONLY, "SK_hat_adept": HEAD_ONLY, "SK_hood_scout": HEAD_ONLY,
             "SK_cloak_warden": CLOAK, "SK_legs_apprentice_robe": CLOAK}


def weight(ob, arm):
    inf = INFLUENCE.get(ob.name, w2.BONE_INFLUENCE)
    w2.skin(ob, arm, influence=inf)
    w2.race_keys(ob, arm)
    return ob


# ── Build, save, export ──────────────────────────────────────────────────────

def build(names=None, body=None, arm=None):
    body = body or bpy.data.objects[w2.BODY]
    arm = arm or bpy.data.objects[w2.ARMATURE]
    fit = Fit(body)
    out = []
    for name in (names or PIECES):
        if name in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[name])
        ob = PIECES[name](fit)
        weight(ob, arm)
        out.append(ob)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    return out


def export(ob, path=None):
    """One glb per piece: armature + the piece, deform bones, no animations."""
    arm = bpy.data.objects[w2.ARMATURE]
    path = path or os.path.join(EQUIP_DIR, ob.name + ".glb")
    w2._deselect_all()
    arm.select_set(True)
    ob.hide_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = arm
    os.makedirs(os.path.dirname(path), exist_ok=True)
    kwargs = dict(filepath=path, export_format="GLB", use_selection=True, export_yup=True, export_apply=False,
                  export_materials="EXPORT", export_normals=True, export_texcoords=True, export_skins=True,
                  export_def_bones=True, export_animations=False, export_morph=True, export_morph_normal=False,
                  export_image_format="NONE")
    with bpy.context.temp_override(**w2._ctx()):
        try:
            bpy.ops.export_scene.gltf(export_vertex_color="NONE", **kwargs)
        except TypeError:
            bpy.ops.export_scene.gltf(export_colors=False, **kwargs)
    return path


def report(ob):
    return dict(name=ob.name, tris=kit.tri_count(ob), verts=len(ob.data.vertices), slots=[m.name for m in ob.data.materials],
                groups=len(ob.vertex_groups),
                bbox=[tuple(round(c, 3) for c in ob.bound_box[0]), tuple(round(c, 3) for c in ob.bound_box[6])])


def build_all(do_export=True, save_blend=True):
    """Open the body .blend (body + armature are the fitting reference), build
    the 16 pieces, save them with the body and armature as
    ``valhalla_equipment.blend`` (a copy; the body file is left as it is) and
    export one glb per piece."""
    arm = w2.open_blend()
    for n in ("SK_Hair_Brown_Short", "SK_Hair_Blonde", "SK_Hood_Bald_Cap"):
        if n in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[n])
    pieces = build(arm=arm)
    if save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=EQUIP_BLEND, copy=True)
    if do_export:
        for ob in pieces:
            export(ob)
    return [report(o) for o in pieces]
