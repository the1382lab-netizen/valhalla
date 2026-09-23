"""B-15 Wave 2, hair v2: layered hair cards on the realistic head
(SK_Hair_Brown_Short, SK_Hair_Blonde, SK_Hood_Bald_Cap redone for wave2_body2).

Kevin's note on the Stage A hair: "scale-like clumps". This version is the
usual game approach instead: a scalp cap that hides the skull, and layers of
thin, slightly curved cards that follow the cranium from the crown down to
the hairline (short) or on down to the shoulders (long), each card carrying
a slice of the tiling strand texture (``HairStrands``: u picks a strip of
strands, v runs root -> tip). Cards are built as two single-sided strips
1.5 mm apart, so they read from both sides without a two-sided material.
The tips taper to a point, so the outline is ragged, not scalloped. Colour is
still the per-corner vertex colour ``Col`` that the M_Hair instance
multiplies in, darker at the roots.

Everything sits on the cranium ellipsoid ``wave2_body2`` builds the head
from; ``wave2_hair.fit_to_body`` then lifts anything the real skull covers,
and ``transfer_weights`` copies the head/neck weights from the body.
"""

import importlib.util
import math
import os
import random

import bpy
import mathutils

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


h1 = _load("wave2_hair")
b2 = _load("wave2_body2")

HEAD_C = mathutils.Vector(b2.HEAD_C)
HEAD_R = mathutils.Vector(b2.HEAD_R)
h1.HEAD_C, h1.HEAD_R = HEAD_C, HEAD_R          # fit_to_body / _surface use these

BROWN, BLONDE = h1.BROWN, h1.BLONDE


def _hairline_z(phi):
    """Forehead 1.175, above the ears 1.135, nape 1.07 (azimuth -90 = face)."""
    f = (math.sin(phi) + 1.0) / 2.0          # 0 face, 1 back
    side = 1.0 - abs(math.cos(phi))          # 1 front/back, 0 sides
    z = 1.175 * (1 - f) + 1.07 * f
    return z - 0.02 * (1 - side) * (1 - f)


h1._hairline_z = _hairline_z
_surface = h1._surface
_theta_for_z = h1._theta_for_z


class CardBuilder(h1.HairBuilder):
    def card(self, phi0, theta0, length_deg, width, sweep=0.0, lift=0.006, tip_lift=0.010, drop_to=None,
             rows=6, curl=0.003, shade=1.0, thick=0.0015):
        """One card from (theta0, phi0) ``length_deg`` down the skull, drifting
        ``sweep`` degrees of azimuth per degree; with ``drop_to`` it carries on
        straight down past the hairline to that height."""
        pts, nrm = [], []
        theta_end = _theta_for_z(_hairline_z(math.radians(phi0 + sweep * length_deg)))
        theta1 = min(math.radians(theta0 + length_deg), theta_end)
        if drop_to is None and theta1 - math.radians(theta0) < math.radians(6):
            return
        n_skull = rows if drop_to is None else max(3, rows // 2)
        for i in range(n_skull + 1):
            s = i / n_skull
            th = math.radians(theta0) + (theta1 - math.radians(theta0)) * s
            ph = math.radians(phi0 + sweep * math.degrees(th - math.radians(theta0)))
            p, n = _surface(th, ph, lift)
            pts.append(p)
            nrm.append(n)
        if drop_to is not None:
            p0, n0 = pts[-1], nrm[-1]
            out = mathutils.Vector((p0.x - HEAD_C.x, p0.y - HEAD_C.y, 0.0)).normalized()
            n_drop = rows - n_skull
            for i in range(1, n_drop + 1):
                u = i / n_drop
                z = p0.z - (p0.z - drop_to) * u
                pts.append(mathutils.Vector((p0.x, p0.y, z)) + out * (0.006 * u * u))
                nrm.append(out)
        m = len(pts)
        for i in range(m):                     # the last third peels off the skull
            s = i / (m - 1)
            pts[i] = pts[i] + nrm[i] * (tip_lift * max(0.0, s - 0.5) / 0.5)
        # strip rows: 3 verts across (edges lower, centre raised = a curved card)
        u0 = self.rng.uniform(0.0, 0.75)
        tone = shade * (1.0 + self.rng.uniform(-0.10, 0.10))
        for side in (1, -1):                   # front and back strip, 'thick' apart
            rows_v = []
            for i in range(m):
                s = i / (m - 1)
                w = width * (0.55 + 0.45 * math.sin(math.pi * min(1.0, s * 0.85 + 0.15)))
                if i == m - 1:
                    w = 0.002
                t = (pts[min(i + 1, m - 1)] - pts[max(i - 1, 0)]).normalized()
                n = nrm[i]
                b = t.cross(n).normalized()
                n2 = b.cross(t).normalized()
                c = pts[i] + n2 * (side * thick * 0.5)
                rows_v.append([self.bm.verts.new(c - b * (w * 0.5)), self.bm.verts.new(c + n2 * curl),
                               self.bm.verts.new(c + b * (w * 0.5))])
            for i in range(m - 1):
                for k in range(2):
                    a, b_, c_, d = rows_v[i][k], rows_v[i][k + 1], rows_v[i + 1][k + 1], rows_v[i + 1][k]
                    quad = (a, b_, c_, d) if side > 0 else (a, d, c_, b_)
                    try:
                        f = self.bm.faces.new(quad)
                    except ValueError:
                        continue
                    uvs = [(u0 + 0.25 * k / 2, i / (m - 1) * 2.0), (u0 + 0.25 * (k + 1) / 2, i / (m - 1) * 2.0),
                           (u0 + 0.25 * (k + 1) / 2, (i + 1) / (m - 1) * 2.0), (u0 + 0.25 * k / 2, (i + 1) / (m - 1) * 2.0)]
                    if side < 0:
                        uvs = [uvs[0], uvs[3], uvs[2], uvs[1]]
                    for loop, uv in zip(f.loops, uvs):
                        loop[self.uv].uv = uv
                        s = uv[1] / 2.0
                        col = self.base * tone * (0.70 + 0.30 * s)
                        loop[self.col] = (col.x, col.y, col.z, 1.0)


def brown_short():
    hb = CardBuilder(BROWN, seed=11)
    hb.cap(lift=0.003, segs=32, rings=10)
    rng = hb.rng
    # inner layer: three rings of cards, crown to hairline; outer layer lifted, fewer, longer
    for lift, tip, rings in ((0.004, 0.006, ((4, 10, 40), (28, 14, 42), (54, 18, 36), (80, 18, 26))),
                             (0.009, 0.012, ((16, 12, 46), (44, 16, 40), (72, 16, 30)))):
        for theta0, count, length in rings:
            for k in range(count):
                phi = -90 + 360.0 * k / count + rng.uniform(-8, 8)
                front = max(0.0, math.cos(math.radians(phi + 90)))
                hb.card(phi, theta0 + rng.uniform(-4, 4), length + rng.uniform(-6, 6),
                        width=0.026 + rng.uniform(-0.005, 0.005), sweep=0.30 * front + rng.uniform(-0.12, 0.12),
                        lift=lift, tip_lift=tip + 0.006 * front, rows=5, curl=0.0025)
    return hb.finish("SK_Hair_Brown_Short", h1._material("M_Hair", (0.8, 0.8, 0.8)))


def blonde_long():
    hb = CardBuilder(BLONDE, seed=5)
    hb.cap(lift=0.003, segs=32, rings=10)
    rng = hb.rng
    # fringe over the forehead, parted to the right
    for k in range(8):
        phi = -128 + 76.0 * k / 7 + rng.uniform(-3, 3)
        hb.card(phi, 14 + rng.uniform(-3, 3), 62, width=0.024, sweep=0.22 + rng.uniform(-0.08, 0.08), lift=0.005,
                tip_lift=0.012, rows=5)
    # crown cover
    for theta0, count, length in ((4, 10, 40), (30, 14, 36)):
        for k in range(count):
            phi = -90 + 360.0 * k / count + rng.uniform(-6, 6)
            hb.card(phi, theta0 + rng.uniform(-3, 3), length, width=0.026 + rng.uniform(-0.004, 0.004),
                    sweep=rng.uniform(-0.1, 0.1), lift=0.004, tip_lift=0.005, rows=4)
    # long cards from the sides and back, over the skull and down to the shoulders
    for theta0, count, lift in ((22, 16, 0.006), (52, 20, 0.010)):
        for k in range(count):
            phi = -40 + 260.0 * k / (count - 1) + rng.uniform(-4, 4)
            hb.card(phi, theta0 + rng.uniform(-3, 3), 120, width=0.028 + rng.uniform(-0.005, 0.005),
                    sweep=rng.uniform(-0.06, 0.06), lift=lift, tip_lift=0.004, drop_to=0.955 + rng.uniform(-0.02, 0.03),
                    rows=8, curl=0.003)
    return hb.finish("SK_Hair_Blonde", h1._material("M_Hair", (0.8, 0.8, 0.8)))


def bald_cap(skin_material):
    hb = CardBuilder((1.0, 1.0, 1.0), seed=1)
    hb.cap(lift=0.0025, margin_deg=0.0, z_min=1.10, shade=1.0, segs=32, rings=10)
    ob = hb.finish("SK_Hood_Bald_Cap", skin_material)
    ob.data.color_attributes.remove(ob.data.color_attributes["Col"])
    return ob


def fit_to_body(ob, body, offset=0.003):
    """wave2_hair.fit_to_body with the lift measured in metres: what a vertex
    was meant to sit above the ideal cranium is kept above the real skull."""
    dg = bpy.context.evaluated_depsgraph_get()
    bvh = mathutils.bvhtree.BVHTree.FromObject(body, dg)
    for v in ob.data.vertices:
        loc, nrm, _, _ = bvh.find_nearest(v.co)
        if loc is None:
            continue
        rel = v.co - HEAD_C
        rho = math.sqrt((rel.x / HEAD_R.x) ** 2 + (rel.y / HEAD_R.y) ** 2 + (rel.z / HEAD_R.z) ** 2)
        want = max(offset, (rho - 1.0) * HEAD_R.x * 0.8) if v.co.z > 1.09 else offset
        depth = (v.co - loc).dot(nrm)
        if depth < want:
            v.co = loc + nrm * want
    ob.data.update()
    return ob


def build_all(body, arm):
    for n in ("SK_Hair_Brown_Short", "SK_Hair_Blonde", "SK_Hood_Bald_Cap"):
        if n in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[n])
    out = [brown_short(), blonde_long(), bald_cap(body.data.materials[0])]
    for ob in out:
        fit_to_body(ob, body, offset=0.0025 if ob.name == "SK_Hood_Bald_Cap" else 0.003)
        h1.transfer_weights(ob, body, arm)
    return out
