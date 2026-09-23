"""B-15 Wave 2, Stage A: the organic human base body (A-001, SK_Valhalla_Body).

Replaces the blocky first-pass body with an organic, better-proportioned one
on the *same* armature: ``ARM_Valhalla`` and its 7 actions are taken from
``Blender assets/Characters/valhalla_body.blend`` untouched (every bone,
socket and roll stays), so the 16 equipment pieces and the animations still
fit. Only the meshes are rebuilt. The silhouette is deliberately close to the
first pass — 122 cm tall, stylised big head, pivot at the feet, facing -Y —
because the armour is refitted to it only in Stage B.

How the body is made (all by script, no sculpting by hand):

1. ``shells()``   overlapping parametric shells — a lofted torso, tapered
                  capsules for the limbs, ellipsoids for the head, jaw, nose,
                  ears, deltoids, buttocks, mitten hands and feet. Each shell
                  is placed from the bone table below, so joints sit where
                  the armature bends.
2. ``union()``    voxel remesh (5 mm) welds the shells into one watertight
                  surface; a light smooth rounds the intersections into
                  natural transitions (deltoid into arm, jaw into neck).
3. ``sculpt()``   "grab brush" displacement fields on that dense mesh: eye
                  sockets and eyeballs, brow, cheekbones, mouth, collarbones,
                  chest, spine groove, shoulder blades, kneecaps, elbows,
                  ankle bones. This dense mesh is the *high-res* the normal
                  map is baked from.
4. ``retopo()``   QuadriFlow to ~4.6k quads (8,580 triangles, ArtBible
                  section 6: 8–12k), symmetric in X, shrink-wrapped back onto
                  the high-res, soles flattened onto z = 0.
5. ``unwrap()``   smart-projected UV islands (the skin texture is a neutral,
                  tint-multiplied detail map, so island seams do not show).
6. ``skin()``     weights: capsule falloff per deform bone with joint blends
                  tuned per limb (``BONE_INFLUENCE``), at most 4 bones per
                  vertex, normalised. Socket bones get no weights.
7. ``race_keys()`` shape keys ``Race_Stocky`` and ``Race_Slender`` (ArtBible
                  section 9); races can also be bone-scaled, since every
                  vertex is weighted to real limb bones.

Textures (``wave2_body_textures.py``) are baked from procedural node trees
into ``Import/Textures/SkinBase/`` and the hair is ``wave2_hair.py``. Export
is ``export()``: ``Import/Characters/SK_Valhalla_Body.glb`` with the body,
the three hair meshes and the seven actions, deform bones only — exactly how
the first pass was exported (checked against the old glb).

Bone table (metres, Blender Z up, character faces -Y):
  pelvis 0.36-0.46, spine_01 0.46-0.57, spine_02 0.57-0.675, neck 0.675-0.75,
  head 0.75-1.20; clavicle (±0.04..±0.19, z 0.655); upperarm (±0.235, 0.655)
  -> elbow (±0.235, y 0.012, 0.53) -> wrist (±0.235, 0.40) -> hand tip 0.29;
  thigh (±0.085, 0.385) -> knee (±0.085, y -0.012, 0.215) -> ankle
  (±0.085, 0.07) -> toe (±0.085, y -0.09, 0).

Run in Blender: exec, then ``build_all()`` (opens the .blend, rebuilds, saves,
exports) or the steps one at a time.
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

BLEND = os.path.join(kit.REPO, "Blender assets", "Characters", "valhalla_body.blend")
OUT_GLB = os.path.join(kit.IMPORT, "Characters", "SK_Valhalla_Body.glb")
REVIEW = os.path.join(kit.REPO, "Valhalla2", "Saved", "ArtReview", "wave2")

ARMATURE = "ARM_Valhalla"
BODY = "SK_Valhalla_Body"
HIGHRES = "_BodyHighRes"

# ── Measurements (metres). Everything below is derived from these. ──────────
SHOULDER_X = 0.235      # arm bone line
HIP_X = 0.085           # leg bone line
ELBOW = (0.012, 0.53)   # (y, z) of the elbow joint
KNEE = (-0.012, 0.215)
WRIST_Z, ANKLE_Z = 0.40, 0.07
HEAD_C = (0.0, 0.005, 1.0)   # cranium centre; top of head at 1.20


# ── 1. Shells ────────────────────────────────────────────────────────────────

def _frame(a, b):
    w = (mathutils.Vector(b) - mathutils.Vector(a))
    L = w.length
    w.normalize()
    helper = mathutils.Vector((0, 0, 1)) if abs(w.z) < 0.9 else mathutils.Vector((0, 1, 0))
    u = w.cross(helper).normalized()
    v = w.cross(u).normalized()
    return w, u, v, L


class ShellBuilder:
    """Collects closed shells (quads + fans) into one bmesh."""

    def __init__(self):
        self.bm = bmesh.new()

    def _rings_to_faces(self, rings, cap_a=None, cap_b=None):
        bm = self.bm
        vr = [[bm.verts.new(p) for p in ring] for ring in rings]
        n = len(rings[0])
        for r0, r1 in zip(vr, vr[1:]):
            for k in range(n):
                k2 = (k + 1) % n
                bm.faces.new((r0[k], r0[k2], r1[k2], r1[k]))
        if cap_a is not None:
            pa = bm.verts.new(cap_a)
            for k in range(n):
                bm.faces.new((vr[0][(k + 1) % n], vr[0][k], pa))
        if cap_b is not None:
            pb = bm.verts.new(cap_b)
            for k in range(n):
                bm.faces.new((vr[-1][k], vr[-1][(k + 1) % n], pb))

    def capsule(self, a, b, ra, rb, segs=28, along=8, bulge=None, ratio=1.0, cap_rings=4):
        """A tapered sausage from ``a`` to ``b``: radius ``ra`` -> ``rb``,
        times ``bulge(t)`` (t in 0..1) for muscle swell; ``ratio`` squashes the
        section across ``v`` (the direction facing the character's front)."""
        w, u, v, L = _frame(a, b)
        a, b = mathutils.Vector(a), mathutils.Vector(b)

        def r_at(t):
            r = ra + (rb - ra) * t
            return r * (bulge(t) if bulge else 1.0)

        rings = []

        def ring(center, r, rv):
            return [center + u * (r * math.cos(2 * math.pi * k / segs)) + v * (rv * math.sin(2 * math.pi * k / segs))
                    for k in range(segs)]
        for i in range(1, cap_rings + 1):
            phi = (math.pi / 2) * (1 - i / (cap_rings + 1.0))
            r = r_at(0)
            rings.append(ring(a - w * (r * math.sin(phi)), r * math.cos(phi), r * math.cos(phi) * ratio))
        for i in range(along + 1):
            t = i / along
            r = r_at(t)
            rings.append(ring(a + w * (L * t), r, r * ratio))
        for i in range(1, cap_rings + 1):
            phi = (math.pi / 2) * i / (cap_rings + 1.0)
            r = r_at(1)
            rings.append(ring(b + w * (r * math.sin(phi)), r * math.cos(phi), r * math.cos(phi) * ratio))
        self._rings_to_faces(rings, cap_a=a - w * r_at(0), cap_b=b + w * r_at(1))

    def ellipsoid(self, c, radii, segs=32, rings=18, rot=None):
        c = mathutils.Vector(c)
        m = rot.to_matrix() if rot is not None else mathutils.Matrix.Identity(3)
        rr = []
        for j in range(1, rings):
            th = math.pi * j / rings
            ring = []
            for k in range(segs):
                ph = 2 * math.pi * k / segs
                p = mathutils.Vector((radii[0] * math.sin(th) * math.cos(ph),
                                      radii[1] * math.sin(th) * math.sin(ph),
                                      radii[2] * math.cos(th)))
                ring.append(c + m @ p)
            rr.append(ring)
        self._rings_to_faces(rr, cap_a=c + m @ mathutils.Vector((0, 0, radii[2])),
                             cap_b=c + m @ mathutils.Vector((0, 0, -radii[2])))

    def loft(self, sections, segs=48):
        """``sections`` = [(z, cx, cy, rx, ry, n)] bottom to top; superellipse
        exponent ``n`` (2 = ellipse, 3 = boxier). Closed with fans."""
        rings = []
        for z, cx, cy, rx, ry, n in sections:
            ring = []
            for k in range(segs):
                a = 2 * math.pi * k / segs
                cs, sn = math.cos(a), math.sin(a)
                x = cx + rx * math.copysign(abs(cs) ** (2.0 / n), cs)
                y = cy + ry * math.copysign(abs(sn) ** (2.0 / n), sn)
                ring.append(mathutils.Vector((x, y, z)))
            rings.append(ring)
        s0, s1 = sections[0], sections[-1]
        self._rings_to_faces(rings, cap_a=(s0[1], s0[2], s0[0] - 0.02), cap_b=(s1[1], s1[2], s1[0] + 0.02))

    def finish(self, name):
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        me = bpy.data.meshes.new(name)
        self.bm.to_mesh(me)
        self.bm.free()
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        return ob


def shells():
    """The overlapping shells that make the body. Metres, facing -Y."""
    sb = ShellBuilder()
    sx, hx = SHOULDER_X, HIP_X

    # Torso: hips -> chest -> traps. Slightly boxy chest, narrower waist.
    sb.loft([
        (0.33, 0, 0.005, 0.105, 0.085, 2.2),
        (0.38, 0, 0.010, 0.145, 0.105, 2.4),
        (0.43, 0, 0.005, 0.150, 0.105, 2.4),
        (0.48, 0, 0.000, 0.138, 0.098, 2.3),
        (0.54, 0, -0.005, 0.145, 0.105, 2.4),
        (0.60, 0, -0.010, 0.160, 0.115, 2.6),
        (0.645, 0, -0.005, 0.188, 0.112, 2.6),
        (0.675, 0, 0.000, 0.150, 0.095, 2.4),
        (0.70, 0, 0.000, 0.090, 0.075, 2.2),
        (0.715, 0, 0.000, 0.065, 0.062, 2.0),
    ])
    # Buttocks and lower belly round the pelvis off.
    for s in (1, -1):
        sb.ellipsoid((s * 0.07, 0.045, 0.395), (0.085, 0.075, 0.075))
    # Neck
    sb.capsule((0, 0.01, 0.66), (0, 0.01, 0.84), 0.062, 0.058, segs=20, along=4, cap_rings=2)

    # Head: cranium, back of skull, face mass, jaw, chin, nose, ears. The
    # masses overlap a lot so the voxel union blends them instead of stepping.
    sb.ellipsoid(HEAD_C, (0.205, 0.215, 0.200), segs=48, rings=28)
    sb.ellipsoid((0, 0.04, 0.99), (0.185, 0.205, 0.185), segs=40, rings=24)
    sb.ellipsoid((0, -0.055, 0.93), (0.175, 0.160, 0.135), segs=40, rings=24)  # face mass / cheeks
    sb.ellipsoid((0, -0.03, 0.865), (0.145, 0.160, 0.095), segs=40, rings=20)  # jaw
    sb.ellipsoid((0, -0.095, 0.815), (0.075, 0.075, 0.055), segs=32, rings=16)  # chin
    sb.ellipsoid((0, -0.215, 0.925), (0.024, 0.030, 0.036), segs=20, rings=12)  # nose
    for s in (1, -1):
        sb.ellipsoid((s * 0.205, 0.015, 0.955), (0.012, 0.028, 0.040), segs=20, rings=12)  # ear

    for s in (1, -1):
        # Shoulder (deltoid) over the arm bone head: a teardrop stretched
        # down the arm, not a ball, so the upper arm flows into the shoulder
        # (Stage B fix: the ball read as a shoulder pad in game).
        sb.ellipsoid((s * (sx - 0.010), 0.0, 0.612), (0.058, 0.056, 0.086), segs=32, rings=18)
        # Upper arm: shoulder -> elbow, biceps swell.
        sb.capsule((s * sx, 0.0, 0.655), (s * sx, ELBOW[0], ELBOW[1]), 0.055, 0.046, segs=20, along=8,
                   bulge=lambda t: 1.0 + 0.10 * math.sin(math.pi * min(1.0, t * 1.3)) * (1 - t))
        # Forearm: elbow -> wrist, swell near the elbow, flattened wrist.
        sb.capsule((s * sx, ELBOW[0], ELBOW[1]), (s * sx, 0.0, WRIST_Z), 0.046, 0.033, segs=20, along=8,
                   bulge=lambda t: 1.0 + 0.16 * math.sin(math.pi * min(1.0, t * 1.4 + 0.1)) * (1 - t))
        # Hand: palm + four finger ridges + thumb (a mitten with grooves).
        sb.ellipsoid((s * sx, 0.0, 0.352), (0.042, 0.026, 0.062), segs=24, rings=14)
        for i, fx in enumerate((-0.027, -0.009, 0.009, 0.027)):
            ln = (0.040, 0.050, 0.046, 0.036)[i]
            sb.capsule((s * sx + fx, 0.0, 0.33), (s * sx + fx, -0.004, 0.33 - ln), 0.0115, 0.0100, segs=12, along=3, cap_rings=2)
        sb.capsule((s * (sx - 0.032), -0.012, 0.365), (s * (sx - 0.045), -0.030, 0.335), 0.011, 0.009, segs=10, along=3, cap_rings=2)

        # Hip / thigh: hip -> knee, thick at the top.
        sb.capsule((s * hx, 0.005, 0.40), (s * hx, KNEE[0], KNEE[1]), 0.078, 0.052, segs=22, along=8,
                   bulge=lambda t: 1.0 + 0.06 * math.sin(math.pi * t))
        # Calf: knee -> ankle, calf swell high and behind.
        sb.capsule((s * hx, KNEE[0], KNEE[1]), (s * hx, 0.0, ANKLE_Z), 0.050, 0.034, segs=20, along=8,
                   bulge=lambda t: 1.0 + 0.22 * math.sin(math.pi * min(1.0, t * 1.6 + 0.05)) * (1 - t))
        sb.ellipsoid((s * hx, 0.025, 0.165), (0.045, 0.048, 0.06), segs=16, rings=10)       # calf belly
        # Foot: heel -> toes, flattened, sits on z = 0 after flattening.
        sb.capsule((s * hx, 0.045, 0.035), (s * hx, -0.105, 0.028), 0.040, 0.036, segs=18, along=6, ratio=0.85)
        sb.ellipsoid((s * hx, -0.02, 0.05), (0.042, 0.06, 0.045), segs=16, rings=10)        # instep

    ob = sb.finish("_BodyShells")
    return ob


# ── 2. Union ─────────────────────────────────────────────────────────────────

def _ctx():
    """A 3D-view context for operators: after ``open_mainfile`` the context
    the MCP connector runs in has no area, and the remesh operators' poll
    fails without one."""
    win = bpy.context.window or bpy.context.window_manager.windows[0]
    for area in win.screen.areas:
        if area.type == "VIEW_3D":
            region = next(r for r in area.regions if r.type == "WINDOW")
            return dict(window=win, screen=win.screen, area=area, region=region,
                        scene=bpy.context.scene, view_layer=bpy.context.view_layer)
    return dict(window=win, screen=win.screen, scene=bpy.context.scene, view_layer=bpy.context.view_layer)


def _deselect_all():
    for o in bpy.data.objects:
        try:
            o.select_set(False)
        except RuntimeError:          # not in this view layer
            pass


def _activate(ob):
    _deselect_all()
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob


def union(ob, voxel=0.005, smooth_iters=6, smooth_factor=0.5, name=HIGHRES):
    """Voxel-remesh the shells into one surface and soften the intersections."""
    _activate(ob)
    ob.name = name
    ob.data.name = name
    ob.data.remesh_voxel_size = voxel
    ob.data.remesh_voxel_adaptivity = 0.0
    ob.data.use_remesh_fix_poles = True
    ob.data.use_remesh_preserve_volume = True
    with bpy.context.temp_override(**_ctx()):
        bpy.ops.object.voxel_remesh()
        mod = ob.modifiers.new("Smooth", "SMOOTH")
        mod.factor = smooth_factor
        mod.iterations = smooth_iters
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for p in ob.data.polygons:
        p.use_smooth = True
    return ob


# ── 3. Sculpt ────────────────────────────────────────────────────────────────

def _coords(ob):
    n = len(ob.data.vertices)
    co = np.empty(n * 3, dtype=np.float32)
    ob.data.vertices.foreach_get("co", co)
    return co.reshape(n, 3)


def _normals(ob):
    n = len(ob.data.vertices)
    nr = np.empty(n * 3, dtype=np.float32)
    ob.data.vertices.foreach_get("normal", nr)
    return nr.reshape(n, 3)


def _set_coords(ob, co):
    ob.data.vertices.foreach_set("co", co.astype(np.float32).ravel())
    ob.data.update()


def _falloff(d):
    """1 at d = 0, 0 at d >= 1, smooth (a sculpt brush curve)."""
    t = np.clip(d, 0.0, 1.0)
    return (1.0 - t * t) ** 2


#: Grab/inflate fields applied to the high-res mesh, mirrored in X where
#: ``mirror`` is true: (centre, radii, amount, direction or None = normal).
#: A negative amount along the normal pushes in (sockets, creases).
FEATURES = [
    # face
    dict(c=(0.075, -0.185, 0.965), r=(0.046, 0.040, 0.036), amt=-0.010, mirror=True),      # eye socket
    dict(c=(0.075, -0.195, 0.962), r=(0.025, 0.020, 0.022), amt=0.009, mirror=True),       # eyeball
    dict(c=(0.15, -0.13, 0.925), r=(0.05, 0.045, 0.04), amt=0.005, mirror=True),           # cheekbone
    dict(c=(0.0, -0.19, 1.005), r=(0.135, 0.045, 0.028), amt=0.008, mirror=False),         # brow ridge
    dict(c=(0.0, -0.205, 0.872), r=(0.052, 0.020, 0.008), amt=-0.006, mirror=False),       # mouth line
    dict(c=(0.0, -0.205, 0.86), r=(0.040, 0.020, 0.012), amt=0.005, mirror=False),         # lower lip
    dict(c=(0.17, -0.09, 1.03), r=(0.045, 0.06, 0.06), amt=-0.006, mirror=True),           # temple
    dict(c=(0.0, -0.215, 0.90), r=(0.024, 0.02, 0.012), amt=-0.003, mirror=False),         # under the nose
    dict(c=(0.028, -0.20, 0.915), r=(0.014, 0.018, 0.014), amt=0.004, mirror=True),        # nostril wing
    dict(c=(0.0, -0.20, 1.075), r=(0.16, 0.06, 0.06), amt=0.004, mirror=False),            # forehead
    dict(c=(0.0, 0.175, 0.83), r=(0.09, 0.05, 0.05), amt=-0.006, mirror=False),            # nape hollow
    # torso
    dict(c=(0.09, -0.10, 0.665), r=(0.075, 0.03, 0.018), amt=0.004, mirror=True),          # collarbone
    dict(c=(0.0, -0.11, 0.60), r=(0.012, 0.03, 0.06), amt=-0.004, mirror=False),           # sternum
    dict(c=(0.07, -0.11, 0.595), r=(0.065, 0.035, 0.05), amt=0.007, mirror=True),          # chest
    dict(c=(0.0, 0.10, 0.54), r=(0.02, 0.03, 0.14), amt=-0.006, mirror=False),             # spine groove
    dict(c=(0.08, 0.09, 0.60), r=(0.055, 0.04, 0.06), amt=0.005, mirror=True),             # shoulder blade
    dict(c=(0.0, -0.10, 0.50), r=(0.06, 0.03, 0.07), amt=0.004, mirror=False),             # belly
    # limbs
    dict(c=(SHOULDER_X, ELBOW[0] + 0.045, ELBOW[1]), r=(0.03, 0.025, 0.03), amt=0.006, mirror=True),   # elbow point
    dict(c=(SHOULDER_X, -0.02, 0.325), r=(0.05, 0.02, 0.012), amt=0.003, mirror=True),                 # knuckle ridge
    dict(c=(HIP_X, KNEE[0] - 0.05, KNEE[1] + 0.005), r=(0.03, 0.025, 0.035), amt=0.007, mirror=True),  # kneecap
    dict(c=(HIP_X, KNEE[0] + 0.04, KNEE[1] - 0.01), r=(0.03, 0.02, 0.03), amt=-0.004, mirror=True),    # back of knee
    dict(c=(HIP_X + 0.038, 0.0, 0.085), r=(0.016, 0.02, 0.018), amt=0.004, mirror=True),               # outer ankle
    dict(c=(HIP_X - 0.038, 0.0, 0.09), r=(0.016, 0.02, 0.018), amt=0.004, mirror=True),                # inner ankle
    dict(c=(HIP_X, 0.055, 0.12), r=(0.02, 0.02, 0.05), amt=-0.003, mirror=True),                       # achilles
]


def sculpt(ob, features=FEATURES):
    co = _coords(ob)
    nr = _normals(ob)
    for f in features:
        for s in ((1, -1) if f["mirror"] else (1,)):
            c = np.array(f["c"], dtype=np.float32) * np.array([s, 1, 1], dtype=np.float32)
            r = np.array(f["r"], dtype=np.float32)
            d = np.linalg.norm((co - c) / r, axis=1)
            w = _falloff(d)
            direction = nr if f.get("dir") is None else np.array(f["dir"], dtype=np.float32) * np.array([s, 1, 1])
            co = co + (w * f["amt"])[:, None] * direction
    _set_coords(ob, co)
    return ob


# ── 4. Retopo ────────────────────────────────────────────────────────────────

def retopo(high, faces=4600, name=BODY):
    """Copy the high-res, QuadriFlow it to ``faces`` quads, wrap it back on."""
    low = high.copy()
    low.data = high.data.copy()
    low.name = name
    low.data.name = name
    bpy.context.scene.collection.objects.link(low)
    _activate(low)
    low.data.use_mirror_x = True
    with bpy.context.temp_override(**_ctx()):
        bpy.ops.object.quadriflow_remesh(mode="FACES", target_faces=faces, use_mesh_symmetry=True,
                                         use_preserve_sharp=False, use_preserve_boundary=False,
                                         smooth_normals=True, seed=7)
        # QuadriFlow's symmetric output is two halves that meet at x = 0
        # without sharing vertices: weld the seam, or the mesh is open there
        # (rays leak through it, the normal bake shows a centre line).
        bm = bmesh.new()
        bm.from_mesh(low.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
        bm.to_mesh(low.data)
        bm.free()
        sw = low.modifiers.new("Wrap", "SHRINKWRAP")
        sw.target = high
        sw.wrap_method = "NEAREST_SURFACEPOINT"
        bpy.ops.object.modifier_apply(modifier=sw.name)
    flatten_soles(low)
    for p in low.data.polygons:
        p.use_smooth = True
    return low


def flatten_soles(ob, floor=0.0):
    co = _coords(ob)
    co[:, 2] = np.maximum(co[:, 2], floor)
    _set_coords(ob, co)


# ── 5. UVs ───────────────────────────────────────────────────────────────────

def unwrap(ob, angle=66.0, margin=0.015):
    _activate(ob)
    if not ob.data.uv_layers:
        ob.data.uv_layers.new(name="UVMap")
    with bpy.context.temp_override(**_ctx()):
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(angle), island_margin=margin,
                                 area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
        bpy.ops.object.mode_set(mode="OBJECT")
    return ob


# ── 6. Skinning ──────────────────────────────────────────────────────────────

#: Per deform bone: the radius around the bone line it owns (metres), and how
#: far past its head / tail its influence fades (the joint blend). Tuned by
#: rendering the seven actions (``pose_sheet``): elbows and knees blend over
#: 3 cm, the spine over 4 cm so the torso bends as a curve, the head takes
#: everything above the jaw, the foot takes the heel.
BONE_INFLUENCE = {
    "pelvis":     dict(R=0.24, head=0.05, tail=0.04),
    "spine_01":   dict(R=0.24, head=0.04, tail=0.04),
    "spine_02":   dict(R=0.26, head=0.04, tail=0.045),
    "neck":       dict(R=0.11, head=0.02, tail=0.03),
    "head":       dict(R=0.32, head=0.03, tail=0.05),
    "clavicle_l": dict(R=0.06, head=0.02, tail=0.04),
    "clavicle_r": dict(R=0.06, head=0.02, tail=0.04),
    # The deltoid belongs to the arm (gain, long head blend); the torso side
    # under the armpit does not (xmin gate), or a raised arm tears the chest.
    "upperarm_l": dict(R=0.085, head=0.08, tail=0.03, gain=2.0, xmin=0.165),
    "upperarm_r": dict(R=0.085, head=0.08, tail=0.03, gain=2.0, xmin=0.165),
    "lowerarm_l": dict(R=0.075, head=0.03, tail=0.025),
    "lowerarm_r": dict(R=0.075, head=0.03, tail=0.025),
    "hand_l":     dict(R=0.09, head=0.025, tail=0.05),
    "hand_r":     dict(R=0.09, head=0.025, tail=0.05),
    "thigh_l":    dict(R=0.115, head=0.05, tail=0.03, gain=1.5),
    "thigh_r":    dict(R=0.115, head=0.05, tail=0.03, gain=1.5),
    "calf_l":     dict(R=0.095, head=0.03, tail=0.03),
    "calf_r":     dict(R=0.095, head=0.03, tail=0.03),
    # The heel bump sits 9 cm off the foot bone line (the bone runs ankle ->
    # toe), so the foot needs a wide radius or the heel falls to no bone.
    "foot_l":     dict(R=0.14, head=0.12, tail=0.08),
    "foot_r":     dict(R=0.14, head=0.12, tail=0.08),
}
MAX_INFLUENCES = 4


def _bone_weights(co, arm, influence=BONE_INFLUENCE):
    """Raw (unnormalised) weight per bone for every vertex: axial window with
    soft ends times a radial falloff."""
    raw = {}
    for name, inf in influence.items():
        b = arm.data.bones[name]
        a = np.array(b.head_local, dtype=np.float32)
        t = np.array(b.tail_local, dtype=np.float32)
        axis = t - a
        L = float(np.linalg.norm(axis))
        axis /= L
        rel = co - a
        s = rel @ axis                       # metres along the bone
        radial = np.linalg.norm(rel - np.outer(s, axis), axis=1)
        over = np.where(s < 0, -s / inf["head"], np.where(s > L, (s - L) / inf["tail"], 0.0))
        axial = _falloff(over)
        w = axial * _falloff(radial / inf["R"]) * inf.get("gain", 1.0)
        if "xmin" in inf:                    # smooth gate on |x| (arms only)
            x0 = inf["xmin"]
            w = w * np.vectorize(kit.smoothstep)(x0 - 0.015, x0 + 0.015, np.abs(co[:, 0]))
        raw[name] = w
    return raw


def _line_distance(co, bone):
    a = np.array(bone.head_local, dtype=np.float32)
    b = np.array(bone.tail_local, dtype=np.float32)
    axis = b - a
    L2 = float(axis @ axis)
    t = np.clip(((co - a) @ axis) / max(L2, 1e-9), 0.0, 1.0)
    return np.linalg.norm(co - (a + np.outer(t, axis)), axis=1)


def skin(ob, arm, influence=BONE_INFLUENCE, max_influences=MAX_INFLUENCES):
    """Vertex groups from ``BONE_INFLUENCE``, ≤4 per vertex, normalised."""
    co = _coords(ob)
    raw = _bone_weights(co, arm, influence)
    names = list(raw)
    W = np.stack([raw[n] for n in names], axis=1)          # (verts, bones)
    # Keep the strongest ``max_influences``; a vertex nothing reaches goes to
    # the bone line it is nearest to (not the first bone in the table, which
    # is what an argmax over zeros would give).
    order = np.argsort(-W, axis=1)[:, :max_influences]
    keep = np.zeros_like(W)
    np.put_along_axis(keep, order, np.take_along_axis(W, order, axis=1), axis=1)
    total = keep.sum(axis=1)
    lost = total <= 1e-6
    if lost.any():
        dist = np.stack([_line_distance(co, arm.data.bones[n]) for n in names], axis=1)
        nearest = np.argmin(dist, axis=1)
        keep[lost] = 0.0
        keep[lost, nearest[lost]] = 1.0
        total = keep.sum(axis=1)
    keep /= total[:, None]

    for g in list(ob.vertex_groups):
        ob.vertex_groups.remove(g)
    for j, name in enumerate(names):
        vg = ob.vertex_groups.new(name=name)
        idx = np.nonzero(keep[:, j] > 1e-4)[0]
        for i in idx:
            vg.add([int(i)], float(keep[i, j]), "REPLACE")

    for m in list(ob.modifiers):
        if m.type == "ARMATURE":
            ob.modifiers.remove(m)
    mod = ob.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    ob.parent = arm
    ob.matrix_parent_inverse = arm.matrix_world.inverted()
    return keep, names


# ── 7. Race shape keys ───────────────────────────────────────────────────────

def race_keys(ob, arm):
    """``Race_Stocky`` / ``Race_Slender``: girth about each bone line, driven
    by the skin weights so the change follows the limbs. Height is a bone
    scale (ArtBible section 9), not a key."""
    if ob.data.shape_keys is None:
        ob.shape_key_add(name="Basis", from_mix=False)
    co = _coords(ob)
    W = {}
    for vg in ob.vertex_groups:
        w = np.zeros(len(co), dtype=np.float32)
        for v in ob.data.vertices:
            for g in v.groups:
                if g.group == vg.index:
                    w[v.index] = g.weight
        W[vg.name] = w
    for key_name, girth, head_scale in (("Race_Stocky", 1.16, 1.03), ("Race_Slender", 0.90, 0.98)):
        out = co.copy()
        for name, w in W.items():
            b = arm.data.bones[name]
            a = np.array(b.head_local, dtype=np.float32)
            axis = np.array(b.tail_local, dtype=np.float32) - a
            axis /= np.linalg.norm(axis)
            rel = co - a
            foot = a + np.outer(rel @ axis, axis)
            k = head_scale if name == "head" else girth
            out += (w * (k - 1.0))[:, None] * (co - foot)
        key = ob.data.shape_keys.key_blocks.get(key_name) or ob.shape_key_add(name=key_name, from_mix=False)
        key.data.foreach_set("co", out.astype(np.float32).ravel())
        key.value = 0.0
    return ob


# ── Materials, assembly, export ──────────────────────────────────────────────

def material(ob, name="M_Skin", color=(0.745, 0.44, 0.29)):
    """Slot name = Unreal instance name (character_import.consolidate_materials
    binds by it). Preview colour only; no image nodes, so the glb stays small."""
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (color[0], color[1], color[2], 1.0)
        bsdf.inputs["Roughness"].default_value = 0.6
    ob.data.materials.clear()
    ob.data.materials.append(mat)
    return mat


def build_body(arm, faces=4600, keep_highres=True):
    for n in (BODY, HIGHRES, "_BodyShells"):
        if n in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[n])
    hi = union(shells())
    sculpt(hi)
    low = retopo(hi, faces=faces, name=BODY)
    unwrap(low)
    material(low)
    skin(low, arm)
    race_keys(low, arm)
    if keep_highres:
        hi.hide_set(True)
        hi.hide_render = True
    else:
        bpy.data.objects.remove(hi)
    return low


def open_blend():
    bpy.ops.wm.open_mainfile(filepath=BLEND, load_ui=False)
    arm = bpy.data.objects[ARMATURE]
    # The file may have been saved in pose mode; the remesh operators need
    # object mode and a mesh as the active object.
    if bpy.context.mode != "OBJECT":
        with bpy.context.temp_override(**_ctx()):
            bpy.ops.object.mode_set(mode="OBJECT")
    arm.animation_data_create()
    arm.animation_data.action = None
    # Unassigning the action leaves the last evaluated pose on the bones, and
    # the hair is fitted against the *evaluated* body, so put every bone back
    # to rest explicitly.
    for pb in arm.pose.bones:
        pb.matrix_basis = mathutils.Matrix.Identity(4)
    bpy.context.scene.frame_set(0)
    bpy.context.view_layer.update()
    return arm


def clear_old():
    """Drop the first-pass meshes and their leftover icospheres."""
    for ob in list(bpy.data.objects):
        if ob.type == "MESH":
            bpy.data.objects.remove(ob)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)


def export(names=(BODY, "SK_Hair_Brown_Short", "SK_Hair_Blonde", "SK_Hood_Bald_Cap"), path=OUT_GLB):
    """The body glb as the first pass exported it: armature + skinned meshes,
    deform bones only, every action as an animation, vertex colours kept."""
    arm = bpy.data.objects[ARMATURE]
    arm.animation_data_create()
    _deselect_all()
    arm.select_set(True)
    for n in names:
        ob = bpy.data.objects[n]
        ob.hide_set(False)
        ob.select_set(True)
    bpy.context.view_layer.objects.active = arm
    os.makedirs(os.path.dirname(path), exist_ok=True)
    kwargs = dict(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                  export_apply=False, export_materials="EXPORT", export_normals=True,
                  export_texcoords=True, export_skins=True, export_def_bones=True,
                  export_animations=True, export_animation_mode="ACTIONS", export_morph=True,
                  export_morph_normal=False, export_image_format="NONE")
    with bpy.context.temp_override(**_ctx()):
        try:
            bpy.ops.export_scene.gltf(export_vertex_color="ACTIVE", **kwargs)
        except TypeError:
            bpy.ops.export_scene.gltf(export_colors=True, **kwargs)
    return path


def save():
    bpy.ops.wm.save_as_mainfile(filepath=BLEND)


def report(ob):
    return dict(name=ob.name, tris=kit.tri_count(ob), verts=len(ob.data.vertices),
                groups=len(ob.vertex_groups), slots=[m.name for m in ob.data.materials],
                bbox=[tuple(round(c, 3) for c in ob.bound_box[0]), tuple(round(c, 3) for c in ob.bound_box[6])])


def build_all(save_blend=True, do_export=True, bake=True):
    """Open the .blend, rebuild body + hair, bake the texture sets, save, export."""
    arm = open_blend()
    clear_old()
    body = build_body(arm)
    hair = _load("wave2_hair")
    hairs = hair.build_all(body, arm)
    if bake:
        tx = _load("wave2_body_textures")
        tx.bake_skin()
        tx.bake_hair()
        tx.manifest()
    # The 160k-triangle high-res is only the normal-map source; it is rebuilt
    # in a second by ``build_body``, so it is not kept in the .blend.
    if HIGHRES in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects[HIGHRES])
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    if save_blend:
        save()
    if do_export:
        export()
    return [report(o) for o in [body] + hairs]
