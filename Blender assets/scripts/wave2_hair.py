"""B-15 Wave 2, Stage A: hair (A-002 SK_Hair_Blonde, A-003 SK_Hair_Brown_Short,
A-004 SK_Hood_Bald_Cap).

Sculpted-clump hair, the way NWN2-era characters wore it: a thin scalp cap
that hides the skull between the clumps, and rows of tapered clumps that lie
on the cranium and flow from the crown to the hairline (short brown) or on
past it and down to the shoulders (long blonde). Every clump is a lofted
tube: flat against the skull, widest in the middle, pointed at the tip, with
UVs that run *along* the strand (u around, v root -> tip) so one tiling strand
texture serves all three meshes. Colour is a per-corner vertex colour
(``Col``): the M_Hair instance multiplies it in (``UseVertexColor``), darker
at the roots, ±8% per clump. The bald cap is the scalp patch alone, on
M_Skin, for hoods.

The clumps are placed on the same cranium ellipsoid ``wave2_body`` builds the
head from (``HEAD_C`` and its radii), so they sit on the skin without any
fitting step. Skin weights are transferred from the body: each hair vertex
copies the groups of its nearest body vertex (KD-tree), which is the head
bone everywhere except the longest blonde strands, which pick up a little
neck and spine so they follow a bending back.
"""

import math
import os
import random

import bmesh
import bpy
import mathutils
import numpy as np

# The cranium the body's head is built on (wave2_body.shells).
HEAD_C = mathutils.Vector((0.0, 0.005, 1.0))
HEAD_R = mathutils.Vector((0.205, 0.215, 0.200))

# Colours are linear (glTF COLOR_0 is linear).
BROWN = (0.12, 0.055, 0.025)
BLONDE = (0.55, 0.38, 0.15)


def _surface(theta, phi, lift=0.0):
    """Point on the cranium ellipsoid at polar ``theta`` (0 = crown) and
    azimuth ``phi`` (-90° = face), pushed ``lift`` along the normal."""
    d = mathutils.Vector((math.sin(theta) * math.cos(phi), math.sin(theta) * math.sin(phi), math.cos(theta)))
    p = HEAD_C + mathutils.Vector((HEAD_R.x * d.x, HEAD_R.y * d.y, HEAD_R.z * d.z))
    n = mathutils.Vector((d.x / HEAD_R.x, d.y / HEAD_R.y, d.z / HEAD_R.z)).normalized()
    return p + n * lift, n


def _hairline_z(phi):
    """Height of the hairline around the head: forehead 1.06, above the ears
    0.985, nape 0.86 (azimuth -90° is the face, +90° the back)."""
    f = (math.sin(phi) + 1.0) / 2.0          # 0 at the face, 1 at the back
    side = 1.0 - abs(math.cos(phi))          # 1 at front/back, 0 at the sides
    z = 1.06 * (1 - f) + 0.86 * f
    return z - 0.03 * (1 - side) * (1 - f)   # dip over the ears


def _theta_for_z(z):
    return math.acos(max(-1.0, min(1.0, (z - HEAD_C.z) / HEAD_R.z)))


class HairBuilder:
    def __init__(self, base_color, seed=3):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.col = self.bm.loops.layers.float_color.new("Col")   # linear floats
        self.base = mathutils.Vector(base_color)
        self.rng = random.Random(seed)

    def clump(self, phi0, theta0, length_deg, width, thick=0.55, sweep=0.0, lift=0.006,
              tip_lift=0.012, drop_to=None, segs=6, rings=6, shade=1.0):
        """One clump from (theta0, phi0), ``length_deg`` of arc down the skull
        drifting ``sweep`` degrees of azimuth per degree of polar angle. With
        ``drop_to`` (a z) it carries on straight down past the hairline."""
        pts, nrm = [], []
        theta_end = _theta_for_z(_hairline_z(math.radians(phi0 + sweep * length_deg)))
        theta1 = min(math.radians(theta0 + length_deg), theta_end)
        if drop_to is None and theta1 - math.radians(theta0) < math.radians(8):
            return []                        # below the hairline already
        n_skull = rings if drop_to is None else max(3, rings // 2)
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
            n_drop = rings - n_skull
            for i in range(1, n_drop + 1):
                u = i / n_drop
                z = p0.z - (p0.z - drop_to) * u
                pts.append(mathutils.Vector((p0.x, p0.y, z)) + out * (0.008 * u * u))
                nrm.append(out)
        # tip lift: the last third of the clump peels off the skull
        m = len(pts)
        for i in range(m):
            s = i / (m - 1)
            pts[i] = pts[i] + nrm[i] * (tip_lift * max(0.0, s - 0.55) / 0.45)
        # rings
        rows = []
        for i in range(m):
            s = i / (m - 1)
            w = width * (0.30 + 0.70 * math.sin(math.pi * min(1.0, s * 0.9 + 0.08)))
            if i == m - 1:
                w = 0.0
            t = (pts[min(i + 1, m - 1)] - pts[max(i - 1, 0)]).normalized()
            n = nrm[i]
            b = t.cross(n).normalized()
            n2 = b.cross(t).normalized()
            c = pts[i] + n2 * (w * thick * 0.5)
            ring = []
            for k in range(segs):
                a = 2 * math.pi * k / segs
                ring.append(c + b * (w * 0.5 * math.cos(a)) + n2 * (w * thick * 0.5 * math.sin(a)))
            rows.append((ring, s))
        bm = self.bm
        verts = []
        for ring, s in rows[:-1]:
            verts.append([bm.verts.new(p) for p in ring])
        tip = bm.verts.new(rows[-1][0][0])
        faces = []
        for i, (r0, r1) in enumerate(zip(verts, verts[1:])):
            for k in range(segs):
                k2 = (k + 1) % segs
                f = bm.faces.new((r0[k], r0[k2], r1[k2], r1[k]))
                faces.append((f, [(k, i), (k + 1, i), (k + 1, i + 1), (k, i + 1)]))
        last = len(verts) - 1
        for k in range(segs):
            k2 = (k + 1) % segs
            f = bm.faces.new((verts[last][k], verts[last][k2], tip))
            faces.append((f, [(k, last), (k + 1, last), (k + 0.5, last + 1)]))
        tone = shade * (1.0 + self.rng.uniform(-0.08, 0.08))
        n_rows = len(rows) - 1
        for f, uvs in faces:
            for loop, (ku, kv) in zip(f.loops, uvs):
                s = kv / n_rows
                loop[self.uv].uv = (ku / segs, s * 2.0)
                c = self.base * tone * (0.72 + 0.28 * s)
                loop[self.col] = (c.x, c.y, c.z, 1.0)
        return faces

    def cap(self, lift=0.004, margin_deg=4.0, segs=36, rings=14, shade=0.85, z_min=None):
        """The scalp patch under the clumps, out to the hairline."""
        bm = self.bm
        grid = []
        for j in range(rings + 1):
            row = []
            for k in range(segs):
                ph = 2 * math.pi * k / segs
                th_end = _theta_for_z(_hairline_z(ph)) + math.radians(margin_deg)
                if z_min is not None:
                    th_end = min(th_end, _theta_for_z(z_min))
                th = th_end * (j / rings) ** 0.85
                p, n = _surface(th, ph, lift)
                row.append((bm.verts.new(p), th, ph))
            grid.append(row)
        pole = bm.verts.new(_surface(0.0, 0.0, lift)[0])
        for j in range(rings):
            for k in range(segs):
                k2 = (k + 1) % segs
                if j == 0:
                    b_, c_ = grid[1][k], grid[1][k2]
                    f = bm.faces.new((pole, b_[0], c_[0]))
                    uvs = [((k + 0.5) / segs, 0.0), (k / segs, b_[1] / math.pi * 2), ((k + 1) / segs, c_[1] / math.pi * 2)]
                else:
                    a_, b_, c_, d_ = grid[j][k], grid[j][k2], grid[j + 1][k2], grid[j + 1][k]
                    f = bm.faces.new((a_[0], b_[0], c_[0], d_[0]))
                    uvs = [(k / segs, a_[1] / math.pi * 2), ((k + 1) / segs, b_[1] / math.pi * 2),
                           ((k + 1) / segs, c_[1] / math.pi * 2), (k / segs, d_[1] / math.pi * 2)]
                for loop, uv in zip(f.loops, uvs):
                    loop[self.uv].uv = uv
                    c = self.base * shade
                    loop[self.col] = (c.x, c.y, c.z, 1.0)

    def finish(self, name, material):
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        me = bpy.data.meshes.new(name)
        self.bm.to_mesh(me)
        self.bm.free()
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        me.materials.append(material)
        for p in me.polygons:
            p.use_smooth = True
        if me.color_attributes:
            me.color_attributes.active_color = me.color_attributes[0]
            me.color_attributes.render_color_index = 0
        return ob


def _material(name, color):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (color[0], color[1], color[2], 1.0)
        bsdf.inputs["Roughness"].default_value = 0.55
    return mat


def brown_short():
    hb = HairBuilder(BROWN, seed=11)
    hb.cap()
    rng = hb.rng
    # Rows of short clumps down the skull; the fringe sweeps to the right.
    for theta0, count, length in ((2, 12, 42), (26, 16, 42), (52, 18, 40), (78, 18, 34), (104, 18, 30)):
        for k in range(count):
            phi = -90 + 360.0 * k / count + rng.uniform(-6, 6)
            front = max(0.0, math.cos(math.radians(phi + 90)))    # 1 at the face
            hb.clump(phi, theta0 + rng.uniform(-3, 3), length + rng.uniform(-5, 5),
                     width=0.062 + rng.uniform(-0.008, 0.008), thick=0.30, sweep=0.35 * front + rng.uniform(-0.1, 0.1),
                     tip_lift=0.008 + 0.008 * front, segs=6, rings=5)
    return hb.finish("SK_Hair_Brown_Short", _material("M_Hair", (0.8, 0.8, 0.8)))


def blonde_long():
    hb = HairBuilder(BLONDE, seed=5)
    hb.cap()
    rng = hb.rng
    # Fringe: short clumps over the forehead.
    for k in range(7):
        phi = -125 + 70.0 * k / 6 + rng.uniform(-3, 3)
        hb.clump(phi, 12 + rng.uniform(-3, 3), 60, width=0.055, thick=0.30, sweep=0.25, tip_lift=0.012, segs=6, rings=5)
    # Crown and back: two rows of short clumps so the scalp never shows.
    for theta0, count, length in ((2, 12, 40), (30, 16, 40)):
        for k in range(count):
            phi = -90 + 360.0 * k / count + rng.uniform(-6, 6)
            hb.clump(phi, theta0 + rng.uniform(-3, 3), length, width=0.060 + rng.uniform(-0.006, 0.006), thick=0.30,
                     sweep=rng.uniform(-0.1, 0.1), tip_lift=0.006, segs=6, rings=4)
    # Long clumps: from the sides and back of the crown, over the skull, then down.
    for theta0, count in ((20, 14), (50, 18)):
        for k in range(count):
            phi = -40 + 260.0 * k / (count - 1) + rng.uniform(-4, 4)
            hb.clump(phi, theta0 + rng.uniform(-3, 3), 120, width=0.062 + rng.uniform(-0.006, 0.006), thick=0.30,
                     sweep=rng.uniform(-0.08, 0.08), tip_lift=0.004, drop_to=0.78 + rng.uniform(-0.02, 0.04),
                     segs=6, rings=7)
    return hb.finish("SK_Hair_Blonde", _material("M_Hair", (0.8, 0.8, 0.8)))


def bald_cap(skin_material):
    hb = HairBuilder((1.0, 1.0, 1.0), seed=1)
    hb.cap(lift=0.003, margin_deg=0.0, z_min=0.99, shade=1.0)
    ob = hb.finish("SK_Hood_Bald_Cap", skin_material)
    ob.data.color_attributes.remove(ob.data.color_attributes["Col"])
    return ob


def fit_to_body(ob, body, offset=0.004):
    """The head is not exactly the cranium ellipsoid (the back of the skull and
    the jaw masses stick out past it), so anything the ellipsoid put under the
    skin is pushed out to ``offset`` above the body. Vertices already above the
    surface — clump tips, hanging strands — stay where they are."""
    dg = bpy.context.evaluated_depsgraph_get()
    bvh = mathutils.bvhtree.BVHTree.FromObject(body, dg)
    moved = 0
    for v in ob.data.vertices:
        loc, nrm, _, _ = bvh.find_nearest(v.co)
        if loc is None:
            continue
        # How high the vertex was meant to sit over the ideal cranium (clump
        # tops, lifted tips) is kept over the real skull.
        rel = v.co - HEAD_C
        rho = math.sqrt((rel.x / HEAD_R.x) ** 2 + (rel.y / HEAD_R.y) ** 2 + (rel.z / HEAD_R.z) ** 2)
        want = max(offset, (rho - 1.0) * 0.2) if v.co.z > 0.8 else offset
        depth = (v.co - loc).dot(nrm)
        if depth < want:
            v.co = loc + nrm * want
            moved += 1
    ob.data.update()
    return ob


def transfer_weights(ob, body, arm):
    """Copy each hair vertex's groups from the nearest body vertex."""
    kd = mathutils.kdtree.KDTree(len(body.data.vertices))
    for v in body.data.vertices:
        kd.insert(v.co, v.index)
    kd.balance()
    body_groups = {g.index: g.name for g in body.vertex_groups}
    for g in list(ob.vertex_groups):
        ob.vertex_groups.remove(g)
    groups = {}
    for v in ob.data.vertices:
        _, idx, _ = kd.find(v.co)
        for g in body.data.vertices[idx].groups:
            name = body_groups[g.group]
            vg = groups.get(name) or ob.vertex_groups.new(name=name)
            groups[name] = vg
            vg.add([v.index], g.weight, "REPLACE")
    mod = ob.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    ob.parent = arm
    ob.matrix_parent_inverse = arm.matrix_world.inverted()
    return ob


def build_all(body, arm):
    for n in ("SK_Hair_Brown_Short", "SK_Hair_Blonde", "SK_Hood_Bald_Cap"):
        if n in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[n])
    out = [brown_short(), blonde_long(), bald_cap(body.data.materials[0])]
    for ob in out:
        fit_to_body(ob, body, offset=0.003 if ob.name == "SK_Hood_Bald_Cap" else 0.004)
        transfer_weights(ob, body, arm)
    return out
