"""B-15 Wave 2 (MetaHuman rework): the fitting reference and the shared
machinery for re-fitting the Wave 2 armour and hair to the MetaHuman body.

The Fable 5.1 pieces (wave2_armour.py, wave2_hair.py) were skinned to
SK_Valhalla_Skeleton and cannot follow metahuman_base_skel. This module brings
the MetaHuman into Blender and supplies what the per-piece scripts need:

* ``prep()`` imports ``Import/Characters/MetaHuman/MH_Body.fbx`` and
  ``MH_Face.fbx`` (exported from Unreal by ``Saved/ClaudeOps/mh_fbx_export.py``),
  keeps the body armature ``root`` (341 bones, metres, front = -Y, left = +X,
  the Fable convention) and builds ``MH_Fit``: body + face skin in the rest
  pose, the surface ``Fit`` ray-casts against.
* ``Z(old_z)`` maps a height on the 122 cm Fable body to the same landmark on
  the MetaHuman (ankle, knee, hip, shoulder, neck, chin, brow, crown), so each
  piece keeps its design and only its numbers move; ``O(x)`` scales an offset
  or thickness by 180.3 / 122 (the MetaHuman is drawn at 122/180.3 in game,
  so a piece keeps its apparent thickness).
* ``transfer_weights(ob)`` skins a piece with the MetaHuman's own weights:
  each vertex takes the interpolated weights of the nearest point on the body
  (or the face skin, whose facial bones fold into ``head``), optionally
  smoothed across the piece for hanging cloth, trimmed to 6 influences.
* ``export_fbx(ob)`` writes ``Import/Characters/MetaHuman/Equipment/SK_<name>.fbx``
  (armature + piece), imported onto metahuman_base_skel by
  ``Saved/ClaudeOps/mh_equipment_import.py``.

Run in Blender: exec this file, then ``prep()`` (once per session).
"""

import math
import os

import bmesh
import bpy
import mathutils
from mathutils.bvhtree import BVHTree

REPO = os.environ.get("VALHALLA_REPO", r"C:\Users\music\game-project\Valhalla2.0")
SRC = os.path.join(REPO, "Import", "Characters", "MetaHuman")
EQUIP_OUT = os.path.join(SRC, "Equipment")
HAIR_OUT = os.path.join(SRC, "Hair")
BLEND = os.path.join(REPO, "Blender assets", "Characters", "valhalla_mh_equipment.blend")

ARM, BODY, FACE, FIT = "root", "MH_Body", "MH_Face", "MH_Fit"
S = 180.3 / 122.0

# (Fable height, MetaHuman height) — the landmarks the two bodies share.
ZMAP = [
    (0.000, 0.000), (0.070, 0.0859), (0.215, 0.4916), (0.385, 0.9264), (0.655, 1.4257),
    (0.685, 1.500), (0.710, 1.530), (0.800, 1.552), (0.865, 1.580), (0.960, 1.712),
    (1.040, 1.742), (1.200, 1.802), (1.300, 1.870),
]


def Z(z):
    """A Fable-body height on the MetaHuman."""
    for (a, A), (b, B) in zip(ZMAP, ZMAP[1:]):
        if z <= b:
            return A + (B - A) * (z - a) / (b - a)
    (a, A), (b, B) = ZMAP[-2], ZMAP[-1]
    return A + (B - A) * (z - a) / (b - a)


def O(x):
    """A Fable-scale offset or thickness at MetaHuman scale."""
    return x * S


def ctx():
    win = bpy.context.window or bpy.context.window_manager.windows[0]
    for area in win.screen.areas:
        if area.type == "VIEW_3D":
            region = next(r for r in area.regions if r.type == "WINDOW")
            return dict(window=win, screen=win.screen, area=area, region=region,
                        scene=bpy.context.scene, view_layer=bpy.context.view_layer)
    return dict(window=win, screen=win.screen, scene=bpy.context.scene, view_layer=bpy.context.view_layer)


# ── Scene ────────────────────────────────────────────────────────────────────

def prep():
    """Empty scene -> MetaHuman armature, body, face skin and MH_Fit."""
    bpy.ops.wm.read_homefile(use_empty=True)
    with bpy.context.temp_override(**ctx()):
        bpy.ops.import_scene.fbx(filepath=os.path.join(SRC, "MH_Body.fbx"), use_anim=False,
                                 automatic_bone_orientation=False, ignore_leaf_bones=False)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    body = next(o for o in bpy.data.objects if o.type == "MESH")
    wrapper = arm.parent
    mw = arm.matrix_world.copy()
    arm.parent = None
    arm.matrix_world = mw
    if wrapper is not None:
        bpy.data.objects.remove(wrapper)
    arm.name, body.name = ARM, BODY

    before = set(bpy.data.objects)
    with bpy.context.temp_override(**ctx()):
        bpy.ops.import_scene.fbx(filepath=os.path.join(SRC, "MH_Face.fbx"), use_anim=False,
                                 automatic_bone_orientation=False, ignore_leaf_bones=False)
    new = [o for o in bpy.data.objects if o not in before]
    face_src = next(o for o in new if o.type == "MESH")
    # Face skin only (the biggest section; eyes, teeth, lashes are the rest),
    # baked to world space with its vertex groups, as a plain mesh.
    bm = bmesh.new()
    bm.from_mesh(face_src.data)
    bm.transform(face_src.matrix_world)
    sizes = {}
    for f in bm.faces:
        sizes[f.material_index] = sizes.get(f.material_index, 0) + 1
    skin = max(sizes, key=sizes.get)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index != skin], context="FACES")
    me = bpy.data.meshes.new(FACE)
    bm.to_mesh(me)
    bm.free()
    face = bpy.data.objects.new(FACE, me)
    bpy.context.scene.collection.objects.link(face)
    for g in face_src.vertex_groups:
        face.vertex_groups.new(name=g.name)
    for o in new:
        bpy.data.objects.remove(o)

    # MH_Fit: body (world space) + face skin, no groups.
    bm = bmesh.new()
    for ob in (body, face):
        tmp = bmesh.new()
        tmp.from_mesh(ob.data)
        tmp.transform(ob.matrix_world)
        m = bpy.data.meshes.new("_tmp")
        tmp.to_mesh(m)
        tmp.free()
        bm.from_mesh(m)
        bpy.data.meshes.remove(m)
    me = bpy.data.meshes.new(FIT)
    bm.to_mesh(me)
    bm.free()
    fit = bpy.data.objects.new(FIT, me)
    bpy.context.scene.collection.objects.link(fit)
    fit.hide_render = True
    for m in list(bpy.data.meshes):
        if m.users == 0:
            bpy.data.meshes.remove(m)
    return arm


# ── Landmarks ────────────────────────────────────────────────────────────────

def B(name):
    """World position of a MetaHuman bone's head."""
    arm = bpy.data.objects[ARM]
    return arm.matrix_world @ arm.data.bones[name].head_local


def side(s):
    return "l" if s > 0 else "r"


def lerp(a, b, t):
    return a + (b - a) * t


def spine_y(z):
    """The body's centre line (y) at height z: the spine and neck bones."""
    chain = [B(n) for n in ("pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05",
                            "neck_01", "neck_02", "head")]
    if z <= chain[0].z:
        return chain[0].y
    for a, b in zip(chain, chain[1:]):
        if z <= b.z:
            return lerp(a.y, b.y, (z - a.z) / (b.z - a.z))
    return chain[-1].y


def C(z):
    """A point on the torso axis at height z."""
    return mathutils.Vector((0.0, spine_y(z), z))


HEAD_Y = -0.027        # cranium centre line (between nose -0.15 and back +0.07)


def H(z):
    """A point on the head's vertical axis at height z."""
    return mathutils.Vector((0.0, HEAD_Y, z))


def arm_axis(s, t0, t1):
    a, e = B("upperarm_" + side(s)), B("lowerarm_" + side(s))
    return lerp(a, e, t0), lerp(a, e, t1)


def forearm_axis(s, t0, t1):
    e, w = B("lowerarm_" + side(s)), B("hand_" + side(s))
    return lerp(e, w, t0), lerp(e, w, t1)


def hand_tip(s):
    m2, m3 = B("middle_02_" + side(s)), B("middle_03_" + side(s))
    return m3 + (m3 - m2) * 1.1


def hand_axis(s, t0, t1):
    w, tip = B("hand_" + side(s)), hand_tip(s)
    return lerp(w, tip, t0), lerp(w, tip, t1)


def thigh_axis(s, t0, t1):
    h, k = B("thigh_" + side(s)), B("calf_" + side(s))
    return lerp(h, k, t0), lerp(h, k, t1)


def calf_axis(s, t0, t1):
    k, a = B("calf_" + side(s)), B("foot_" + side(s))
    return lerp(k, a, t0), lerp(k, a, t1)


# ── Weights ──────────────────────────────────────────────────────────────────

class _Source:
    def __init__(self, ob, arm_bones):
        self.ob = ob
        self.me = ob.data
        # Local space: MH_Body sits under the 0.01-scaled armature.
        self.bvh = BVHTree.FromPolygons([v.co for v in self.me.vertices], [p.vertices for p in self.me.polygons])
        self.mw = ob.matrix_world.copy()
        self.imw = self.mw.inverted()
        names = [g.name for g in ob.vertex_groups]
        # Unknown groups (the face's FACIAL_* bones) fold into the head.
        self.map = [n if n in arm_bones else "head" for n in names]
        self.w = [dict() for _ in self.me.vertices]
        for v in self.me.vertices:
            d = self.w[v.index]
            for g in v.groups:
                if g.weight > 0.0:
                    n = self.map[g.group]
                    d[n] = d.get(n, 0.0) + g.weight

    def nearest(self, p):
        loc, nrm, idx, dist = self.bvh.find_nearest(self.imw @ p)
        if loc is None:
            return None, 1e9
        dist = ((self.mw @ loc) - p).length
        poly = self.me.polygons[idx]
        cos = [self.me.vertices[i].co for i in poly.vertices]
        bw = mathutils.interpolate.poly_3d_calc(cos, loc)
        out = {}
        for i, b in zip(poly.vertices, bw):
            for n, w in self.w[i].items():
                out[n] = out.get(n, 0.0) + w * b
        return out, dist


def transfer_weights(ob, smooth=0, max_influences=6, only=None, sources=(BODY, FACE)):
    """Skin ``ob`` to the ``root`` armature with the MetaHuman's weights.

    ``smooth`` Laplacian passes average weights across the piece's own edges
    (cloth that hangs away from the skin, so a skirt does not split between
    the legs). ``only`` = {bone: weight} skips the transfer (rigid head
    pieces)."""
    arm = bpy.data.objects[ARM]
    bones = set(b.name for b in arm.data.bones)
    ob.vertex_groups.clear()
    n = len(ob.data.vertices)
    if only:
        W = [dict(only) for _ in range(n)]
    else:
        srcs = [_Source(bpy.data.objects[s], bones) for s in sources]
        mw = ob.matrix_world
        W = []
        for v in ob.data.vertices:
            p = mw @ v.co
            best, bd = {}, 1e9
            for s in srcs:
                w, d = s.nearest(p)
                if w is not None and d < bd:
                    best, bd = w, d
            W.append(best)
        if smooth:
            nb = [[] for _ in range(n)]
            for e in ob.data.edges:
                a, b = e.vertices
                nb[a].append(b)
                nb[b].append(a)
            for _ in range(smooth):
                W2 = []
                for i in range(n):
                    acc = dict(W[i])
                    for j in nb[i]:
                        for k, w in W[j].items():
                            acc[k] = acc.get(k, 0.0) + w
                    c = 1 + len(nb[i])
                    W2.append({k: w / c for k, w in acc.items()})
                W = W2
    groups = {}
    for i, d in enumerate(W):
        top = sorted(d.items(), key=lambda kv: -kv[1])[:max_influences]
        tot = sum(w for _, w in top) or 1.0
        for name, w in top:
            if w / tot < 0.01:
                continue
            g = groups.get(name) or ob.vertex_groups.new(name=name)
            groups[name] = g
            g.add([i], w / tot, "REPLACE")
    mod = ob.modifiers.get("Armature") or ob.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    ob.parent = arm
    ob.matrix_parent_inverse = arm.matrix_world.inverted()
    return ob


# ── Export ───────────────────────────────────────────────────────────────────

def export_fbx(ob, out_dir=EQUIP_OUT):
    arm = bpy.data.objects[ARM]
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, ob.name + ".fbx")
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    arm.select_set(True)
    ob.hide_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = arm
    with bpy.context.temp_override(**ctx()):
        bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"},
                                 add_leaf_bones=False, bake_anim=False, use_armature_deform_only=False,
                                 primary_bone_axis="Y", secondary_bone_axis="X", armature_nodetype="NULL",
                                 mesh_smooth_type="FACE", use_mesh_modifiers=False, apply_unit_scale=True,
                                 axis_forward="-Z", axis_up="Y")
    return path


def save():
    bpy.ops.wm.save_as_mainfile(filepath=BLEND)
