"""B-15 Wave 2, body v2: a realistic adult male base body (A-001 redo).

Kevin's review of Wave 2: "not realistic enough, short and cartoony". The
reference is NWN 1 gameplay (``Docs/reference/``): adult proportions, about
7.25 heads tall, crotch at half height, long legs, shoulders 2-2.3 head
widths with a real neck-to-shoulder slope, arms to mid-thigh, a small head
with a face that reads at the game camera, subtle musculature.

The game height stays 122 cm (the world is built at 0.68x real scale, so
122 cm *is* a 1.8 m human next to the doors): every real-human landmark below
is a 180 cm male times 0.678. The pivot stays at the feet, the character
faces -Y, and the capsule and camera are untouched.

What changes against ``wave2_body.py`` (Stage A, kept for the pipeline
functions and as the comparison body in the review renders):

* the armature keeps every bone NAME, the hierarchy, connections, rolls and
  the IK set-up, but every bone's rest position moves (``BONES`` /
  ``retarget_armature``): pelvis 0.60-0.70, spine to 0.965, neck to 1.055,
  head 1.055-1.215 (16 cm), shoulders at x 0.135 / z 0.98, elbows 0.765,
  wrists 0.57, hips at x 0.06 / z 0.62, knees 0.34, ankles 0.055. Sockets
  move with their parents: ``socket_weapon_r`` in the new palm,
  ``socket_offhand_l`` on the forearm (the shield sits on the forearm),
  ``socket_back`` on the shoulder blades, ``socket_head_top`` on the crown.
* the shells are placed from the new table with real girths (thigh 0.068,
  upper arm 0.042, wrist 0.026, neck 0.044); the hands have four separated
  fingers and a thumb, the feet a heel, instep, ball and toes.
* the union is voxelised at 2.5 mm (the head is 16 cm, the nose 2.5 cm:
  5 mm would lose the face), sculpt fields at the new landmarks add the
  brow, eye sockets and lids, nose bridge and wings, cheekbones, lips, chin,
  jaw line, ears, collarbones, pecs, abs, spine groove, shoulder blades,
  kneecaps, elbows, ankle bones; the retopo targets 5,800 quads (~11.5k
  triangles, budget 12k).
* the skin texture is baked by ``wave2_body_textures`` with the face
  landmarks and feature sizes of this head (``FACE``); the map stays a
  neutral tint multiplier so the runtime ``BaseColor`` tint still decides
  the tone, with a little more realistic mottling and warm zones.
* hair is ``wave2_hair2.py`` (layered cards); animations are re-run by
  ``wave2_anims.py``, which now reads the leg geometry from the armature.

Run in Blender: exec, then ``build_all()``. It archives the Stage A file as
``valhalla_body_wave2a.blend`` (once), retargets the armature, rebuilds body
+ hair, bakes the texture sets, saves, exports the body glb with the 15
actions rebuilt on the new rig.
"""

import importlib.util
import math
import os
import shutil

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


w2 = _load("wave2_body")
kit = w2.kit

BLEND = w2.BLEND
ARCHIVE_BLEND = os.path.join(os.path.dirname(BLEND), "valhalla_body_wave2a.blend")
OUT_GLB = w2.OUT_GLB
REVIEW = w2.REVIEW
ARMATURE, BODY, HIGHRES = w2.ARMATURE, w2.BODY, w2.HIGHRES

# ── Landmarks (metres; a 180 cm male x 0.678) ────────────────────────────────
HEAD_TOP = 1.215
HEAD_H = 0.165                     # chin at 1.05
SHOULDER = (0.128, 0.0, 0.98)      # glenohumeral joint (real 19-20 cm x 0.678)
ELBOW = (0.148, 0.012, 0.765)      # the arm hangs 5 degrees out (a slight A-pose):
WRIST = (0.168, 0.0, 0.57)         # the hand just clears the thigh, which the retopo
HAND_TIP = (0.175, 0.0, 0.45)      # cut at the wrist and the armpit skinning both need
HIP = (0.06, 0.0, 0.62)
KNEE = (0.06, -0.012, 0.34)
ANKLE = (0.06, 0.0, 0.055)
TOE = (0.06, -0.105, 0.0)
HEAD_C = (0.0, 0.012, 1.153)       # cranium ellipsoid
HEAD_R = (0.0545, 0.066, 0.062)


def _m(p):
    return (-p[0], p[1], p[2])


#: Rest positions for every bone (head, tail). Names, parents, connections
#: and rolls come from the existing armature; only these move.
BONES = {
    "root": ((0.0, 0.0, 0.0), (0.0, -0.2, 0.0)),
    "pelvis": ((0.0, 0.0, 0.60), (0.0, 0.0, 0.70)),
    "spine_01": ((0.0, 0.0, 0.70), (0.0, 0.0, 0.82)),
    "spine_02": ((0.0, 0.0, 0.82), (0.0, 0.0, 0.965)),
    "neck": ((0.0, 0.0, 0.965), (0.0, 0.0, 1.055)),
    "head": ((0.0, 0.0, 1.055), (0.0, 0.0, HEAD_TOP)),
    "socket_head_top": ((0.0, 0.0, HEAD_TOP), (0.0, 0.0, HEAD_TOP + 0.10)),
    "clavicle_l": ((0.02, 0.0, 0.972), (0.118, 0.0, 0.98)),
    "upperarm_l": (SHOULDER, ELBOW),
    "lowerarm_l": (ELBOW, WRIST),
    "hand_l": (WRIST, HAND_TIP),
    "socket_offhand_l": ((0.167, -0.04, 0.605), (0.167, -0.04, 0.705)),
    "socket_weapon_r": ((-0.17, -0.03, 0.515), (-0.17, -0.13, 0.515)),
    "socket_back": ((0.0, 0.11, 0.85), (0.0, 0.11, 0.95)),
    "thigh_l": (HIP, KNEE),
    "calf_l": (KNEE, ANKLE),
    "foot_l": (ANKLE, TOE),
    "ik_foot_l": (ANKLE, TOE),
    "ik_pole_knee_l": ((0.06, -0.40, 0.34), (0.06, -0.40, 0.42)),
    "ik_hand_l": (WRIST, HAND_TIP),
    "ik_pole_elbow_l": ((0.148, 0.40, 0.765), (0.148, 0.40, 0.85)),
}
for _n in list(BONES):
    if _n.endswith("_l") and _n not in ("socket_offhand_l",):
        BONES[_n[:-2] + "_r"] = (_m(BONES[_n][0]), _m(BONES[_n][1]))
BONES["clavicle_r"] = (_m(BONES["clavicle_l"][0]), _m(BONES["clavicle_l"][1]))


def retarget_armature(arm, table=BONES):
    """Move every bone to ``table`` keeping its roll convention (the bone's
    local X axis direction is preserved, e.g. world X for the limbs)."""
    w2._activate(arm)
    with bpy.context.temp_override(**w2._ctx()):
        bpy.ops.object.mode_set(mode="EDIT")
        eb = arm.data.edit_bones
        # parents first so connected children follow
        order = []
        def visit(b):
            order.append(b.name)
            for c in b.children:
                visit(c)
        for b in eb:
            if b.parent is None:
                visit(b)
        for name in order:
            if name not in table:
                raise KeyError("no rest position for bone " + name)
        # the axes must be read before anything moves: a connected child's
        # head follows its parent's tail and would change its direction
        x_old = {name: eb[name].x_axis.copy() for name in order}
        for name in order:
            head, tail = table[name]
            eb[name].head = mathutils.Vector(head)
            eb[name].tail = mathutils.Vector(tail)
        for name in order:
            b = eb[name]
            b.align_roll(x_old[name].cross(b.y_axis))       # local Z = X x Y keeps X where it was
            if b.x_axis.dot(x_old[name]) < 0:
                b.roll += math.pi
            if b.x_axis.dot(x_old[name]) < 0.99:            # a slightly tilted bone tilts X with it
                raise RuntimeError("roll of %s could not be kept (%s vs %s)" % (name, tuple(b.x_axis), tuple(x_old[name])))
        bpy.ops.object.mode_set(mode="OBJECT")
    return arm


# ── Shells ───────────────────────────────────────────────────────────────────

def shells():
    sb = w2.ShellBuilder()
    sx, hx = SHOULDER[0], HIP[0]

    # Torso: crotch -> hips -> waist -> ribcage -> chest -> shoulder line -> neck root.
    sb.loft([
        (0.575, 0, 0.006, 0.098, 0.076, 2.2),
        (0.615, 0, 0.010, 0.116, 0.088, 2.2),
        (0.665, 0, 0.010, 0.118, 0.092, 2.2),
        (0.72, 0, 0.000, 0.100, 0.080, 2.1),
        (0.77, 0, -0.004, 0.106, 0.084, 2.2),
        (0.83, 0, -0.008, 0.120, 0.092, 2.2),
        (0.88, 0, -0.012, 0.131, 0.098, 2.3),
        (0.93, 0, -0.008, 0.122, 0.090, 2.3),
        (0.962, 0, 0.000, 0.106, 0.074, 2.2),
        (0.99, 0, 0.004, 0.072, 0.058, 2.1),
        (1.01, 0, 0.006, 0.050, 0.046, 2.0),
    ], segs=56)
    for s in (1, -1):
        sb.ellipsoid((s * 0.046, 0.040, 0.645), (0.062, 0.054, 0.060))         # buttocks (near flush; the crease is sculpted)
        sb.ellipsoid((s * 0.066, 0.014, 0.972), (0.066, 0.040, 0.026))         # trapezius slope, out to the acromion
    # Neck (real 38 cm circumference -> r 0.041)
    sb.capsule((0, 0.012, 0.955), (0, 0.016, 1.075), 0.041, 0.038, segs=24, along=4, cap_rings=2)

    # Head: cranium, occiput, face mass (recessed around the eyes), brow, cheekbones,
    # eyeballs, jaw, chin, maxilla, nose bridge + tip + wings, ears.
    sb.ellipsoid(HEAD_C, HEAD_R, segs=56, rings=32)
    sb.ellipsoid((0, 0.022, 1.138), (0.05, 0.058, 0.062), segs=48, rings=28)       # occiput
    sb.ellipsoid((0, -0.012, 1.105), (0.048, 0.044, 0.058), segs=48, rings=28)     # face mass, front at -0.056
    sb.ellipsoid((0, -0.006, 1.072), (0.043, 0.047, 0.034), segs=48, rings=24)     # jaw
    sb.ellipsoid((0, -0.037, 1.057), (0.024, 0.022, 0.018), segs=32, rings=16)     # chin, front at -0.059
    sb.capsule((0, -0.056, 1.141), (0, -0.066, 1.113), 0.0048, 0.0085, segs=16, along=4, cap_rings=2)  # nose bridge
    sb.ellipsoid((0, -0.066, 1.108), (0.010, 0.010, 0.009), segs=24, rings=12)     # nose tip
    for s in (1, -1):
        sb.ellipsoid((s * 0.011, -0.061, 1.104), (0.0065, 0.008, 0.0055), segs=16, rings=10)   # nostril wing
        sb.ellipsoid((s * 0.025, -0.043, 1.135), (0.0095, 0.0095, 0.0095), segs=24, rings=14)  # eyeball, 3.5 mm under the face plane
        sb.ellipsoid((s * 0.055, 0.012, 1.112), (0.007, 0.014, 0.021), segs=20, rings=12)      # ear

    for s in (1, -1):
        # deltoid: a teardrop over the joint, flowing down the arm
        sb.ellipsoid((s * (sx + 0.004), -0.004, 0.95), (0.043, 0.045, 0.040), segs=32, rings=18)
        sb.capsule((s * sx, 0.0, SHOULDER[2]), (s * ELBOW[0], ELBOW[1], ELBOW[2]), 0.044, 0.036, segs=22, along=8,
                   bulge=lambda t: 1.0 + 0.09 * math.sin(math.pi * min(1.0, t * 1.3)) * (1 - t))
        sb.capsule((s * ELBOW[0], ELBOW[1], ELBOW[2]), (s * WRIST[0], WRIST[1], WRIST[2]), 0.038, 0.026, segs=22, along=8,
                   bulge=lambda t: 1.0 + 0.14 * math.sin(math.pi * min(1.0, t * 1.4 + 0.1)) * (1 - t))
        # Hand: palm, four fingers, thumb (real hand 19 cm -> 0.13 m).
        hz = WRIST[2]
        sb.ellipsoid((s * WRIST[0], -0.002, hz - 0.042), (0.031, 0.014, 0.047), segs=24, rings=14)
        for i, fx in enumerate((-0.0255, -0.0085, 0.0085, 0.0255)):
            ln = (0.040, 0.049, 0.046, 0.037)[i]
            top = hz - 0.078
            sb.capsule((s * WRIST[0] + fx, -0.004, top), (s * WRIST[0] + fx, -0.010, top - ln), 0.0068, 0.0058,
                       segs=12, along=3, cap_rings=2)
        # thumb, angled forward so it clears the thigh (the union would weld them)
        sb.capsule((s * (WRIST[0] - 0.026), -0.018, hz - 0.045), (s * (WRIST[0] - 0.042), -0.038, hz - 0.080), 0.0078,
                   0.0064, segs=10, along=3, cap_rings=2)

        # Legs: thigh, calf (belly high and behind), foot (heel, instep, ball, toes).
        sb.capsule((s * hx, 0.004, 0.632), (s * KNEE[0], KNEE[1], KNEE[2]), 0.068, 0.046, segs=24, along=8,
                   bulge=lambda t: 1.0 + 0.05 * math.sin(math.pi * t))
        sb.capsule((s * KNEE[0], KNEE[1], KNEE[2]), (s * ANKLE[0], ANKLE[1], ANKLE[2]), 0.046, 0.028, segs=22, along=8,
                   bulge=lambda t: 1.0 + 0.20 * math.sin(math.pi * min(1.0, t * 1.6 + 0.05)) * (1 - t))
        sb.capsule((s * hx, 0.045, 0.030), (s * hx, -0.118, 0.022), 0.034, 0.030, segs=18, along=6, ratio=0.78)
        sb.ellipsoid((s * hx, -0.018, 0.045), (0.036, 0.055, 0.038), segs=16, rings=10)     # instep
        sb.ellipsoid((s * hx, -0.10, 0.02), (0.042, 0.03, 0.019), segs=16, rings=10)        # ball of the foot
        for i, tx in enumerate((-0.028, -0.012, 0.002, 0.015, 0.027)):
            ln = (0.030, 0.028, 0.025, 0.022, 0.018)[i]
            r = (0.0105, 0.0075, 0.007, 0.0065, 0.006)[i]
            x = s * hx + (tx if s > 0 else -tx)
            sb.capsule((x, -0.115, 0.012 + r * 0.3), (x, -0.115 - ln, 0.012 + r * 0.3), r, r * 0.9, segs=10, along=2, cap_rings=2)
    return sb.finish("_BodyShells")


# ── Sculpt fields (centre, radii, amount) at the new landmarks ───────────────

def _f(c, r, amt, mirror=True, **kw):
    d = dict(c=c, r=r, amt=amt, mirror=mirror)
    d.update(kw)
    return d


EYE = (0.026, -0.053, 1.134)        # on the skin (the eyeball sits 3 mm under the lids)
BROW = (0.027, -0.058, 1.153)
MOUTH = (0.0, -0.064, 1.081)
NOSTRIL = (0.0085, -0.074, 1.101)

FEATURES = [
    # face
    _f((0.025, -0.05, 1.135), (0.020, 0.016, 0.014), -0.0030),       # eye socket
    _f((0.025, -0.052, 1.135), (0.0115, 0.010, 0.0095), 0.0020),      # eyeball, still just under the lids
    _f((0.0, -0.058, 1.147), (0.008, 0.008, 0.006), -0.0020, mirror=False),  # nasion
    _f((0.025, -0.052, 1.143), (0.014, 0.010, 0.004), 0.0015),        # upper lid
    _f((0.0, -0.055, 1.152), (0.046, 0.016, 0.009), 0.0035, mirror=False),   # brow ridge
    _f((0.038, -0.045, 1.117), (0.016, 0.016, 0.014), 0.0030),        # cheekbone
    _f((0.0, -0.058, 1.087), (0.026, 0.014, 0.014), 0.0025, mirror=False),   # maxilla
    _f((0.048, -0.024, 1.15), (0.014, 0.02, 0.02), -0.002),           # temple hollow
    _f((0.040, -0.052, 1.095), (0.016, 0.016, 0.016), -0.0022),       # cheek hollow
    _f((0.054, -0.028, 1.160), (0.018, 0.024, 0.024), -0.0025),       # temple
    _f((0.048, -0.02, 1.062), (0.014, 0.03, 0.012), 0.0025),          # jaw line / angle
    _f(MOUTH, (0.021, 0.010, 0.0035), -0.0025, mirror=False),         # mouth line
    _f((0.0, -0.067, 1.0755), (0.017, 0.010, 0.0055), 0.0025, mirror=False),  # lower lip
    _f((0.0, -0.069, 1.0875), (0.019, 0.010, 0.0045), 0.0018, mirror=False),  # upper lip
    _f((0.0, -0.072, 1.094), (0.005, 0.008, 0.006), -0.0018, mirror=False),   # philtrum
    _f((0.0, -0.078, 1.098), (0.011, 0.008, 0.004), -0.0015, mirror=False),   # under the nose
    _f((0.011, -0.074, 1.104), (0.006, 0.008, 0.006), 0.0018),        # nostril wing
    _f((0.0, -0.062, 1.068), (0.020, 0.010, 0.006), -0.0015, mirror=False),   # chin crease
    _f((0.0, 0.072, 1.06), (0.035, 0.02, 0.02), -0.003, mirror=False),        # nape hollow
    # torso
    _f((0.045, -0.055, 0.975), (0.038, 0.014, 0.008), 0.0025),        # collarbone
    _f((0.0, -0.09, 0.905), (0.007, 0.02, 0.05), -0.0028, mirror=False),      # sternum
    _f((0.048, -0.086, 0.893), (0.048, 0.03, 0.04), 0.0045),          # pec
    _f((0.0, -0.088, 0.855), (0.055, 0.02, 0.006), -0.0025, mirror=False),    # pec underline
    _f((0.0, -0.092, 0.80), (0.006, 0.02, 0.09), -0.0025, mirror=False),      # linea alba
    _f((0.0, -0.09, 0.815), (0.046, 0.02, 0.065), 0.0025, mirror=False),     # abdominal wall
    _f((0.0, -0.092, 0.812), (0.05, 0.02, 0.0035), -0.0015, mirror=False),    # abs separation
    _f((0.0, 0.09, 0.64), (0.012, 0.03, 0.06), -0.008, mirror=False),         # gluteal crease
    _f((0.07, -0.06, 0.80), (0.02, 0.03, 0.06), -0.002),              # oblique groove
    _f((0.0, 0.085, 0.82), (0.014, 0.03, 0.15), -0.0045, mirror=False),       # spine groove
    _f((0.05, 0.075, 0.885), (0.038, 0.03, 0.05), 0.004),             # shoulder blade
    _f((0.0, -0.083, 0.745), (0.045, 0.02, 0.03), 0.002, mirror=False),       # lower belly
    # limbs
    _f((ELBOW[0], ELBOW[1] + 0.038, ELBOW[2]), (0.024, 0.02, 0.026), 0.005),          # elbow point
    _f((ELBOW[0], ELBOW[1] - 0.03, ELBOW[2] + 0.005), (0.02, 0.015, 0.02), -0.0025),   # elbow crease
    _f((WRIST[0], -0.012, WRIST[2] - 0.075), (0.032, 0.012, 0.008), 0.002),           # knuckle ridge
    _f((KNEE[0], KNEE[1] - 0.045, KNEE[2] + 0.005), (0.024, 0.02, 0.028), 0.0055),    # kneecap
    _f((KNEE[0], KNEE[1] + 0.036, KNEE[2] - 0.01), (0.024, 0.016, 0.026), -0.0035),   # back of knee
    _f((HIP[0] + 0.034, 0.0, 0.07), (0.013, 0.016, 0.014), 0.0035),                   # outer ankle
    _f((HIP[0] - 0.034, 0.0, 0.075), (0.013, 0.016, 0.014), 0.0035),                  # inner ankle
    _f((HIP[0], 0.048, 0.10), (0.016, 0.016, 0.04), -0.0025),                         # achilles
]


# ── Skin weights for the new proportions ─────────────────────────────────────

BONE_INFLUENCE = {
    "pelvis":     dict(R=0.20, head=0.05, tail=0.04),
    "spine_01":   dict(R=0.20, head=0.04, tail=0.04),
    "spine_02":   dict(R=0.22, head=0.04, tail=0.045),
    "neck":       dict(R=0.075, head=0.02, tail=0.03),
    "head":       dict(R=0.15, head=0.03, tail=0.05),
    "clavicle_l": dict(R=0.05, head=0.02, tail=0.04),
    "clavicle_r": dict(R=0.05, head=0.02, tail=0.04),
    "upperarm_l": dict(R=0.07, head=0.065, tail=0.03, gain=2.0, xmin=0.105),
    "upperarm_r": dict(R=0.07, head=0.065, tail=0.03, gain=2.0, xmin=0.105),
    "lowerarm_l": dict(R=0.06, head=0.03, tail=0.025),
    "lowerarm_r": dict(R=0.06, head=0.03, tail=0.025),
    "hand_l":     dict(R=0.075, head=0.025, tail=0.05),
    "hand_r":     dict(R=0.075, head=0.025, tail=0.05),
    "thigh_l":    dict(R=0.10, head=0.05, tail=0.03, gain=1.5),
    "thigh_r":    dict(R=0.10, head=0.05, tail=0.03, gain=1.5),
    "calf_l":     dict(R=0.08, head=0.03, tail=0.03),
    "calf_r":     dict(R=0.08, head=0.03, tail=0.03),
    "foot_l":     dict(R=0.12, head=0.10, tail=0.08),
    "foot_r":     dict(R=0.12, head=0.10, tail=0.08),
}

#: Face landmarks for the texture bake (``wave2_body_textures`` reads these).
FACE = dict(
    EYE=EYE, BROW=BROW, MOUTH=MOUTH, NOSTRIL=NOSTRIL, SCALE=0.42,
    WARM=[((0.046, -0.045, 1.112), (0.028, 0.024, 0.02), True), ((0.0, -0.084, 1.113), (0.014, 0.012, 0.012), False),
          ((0.057, 0.008, 1.118), (0.012, 0.018, 0.026), True), ((0.17, -0.012, 0.495), (0.036, 0.016, 0.014), True),
          ((0.148, 0.045, 0.765), (0.032, 0.026, 0.032), True), ((0.06, -0.05, 0.34), (0.036, 0.026, 0.036), True),
          ((0.173, -0.012, 0.458), (0.036, 0.016, 0.012), True)],
)


# ── Build ────────────────────────────────────────────────────────────────────

# ── Retopo in parts ──────────────────────────────────────────────────────────
#
# QuadriFlow spreads its quads by area, so at 5-6k quads for the whole body the
# head gets ~400 of them (no eyes, a spike for a nose) and a hand ~60 (the
# fingers collapse). The high-res is therefore cut at the neck, the wrists and
# the ankles, each piece is retopologised with its own budget, the temporary
# caps are removed again and the open rings are bridged, so the result is one
# watertight mesh with the detail where the eye goes.

PARTS = dict(body=3500, head=1200, hand_l=600, hand_r=600, foot_l=250, foot_r=250)   # QuadriFlow lands ~15 % under
WRIST_CUT, ANKLE_CUT, NECK_CUT = WRIST[2] + 0.008, ANKLE[2] + 0.024, 1.028
#: cut height and "which side of the mesh" for every part's ring
#: (cut height, which side of the mesh, the side the part keeps: +1 above / -1 below)
CUTS = {
    "head": (NECK_CUT, lambda c: True, 1),
    "hand_l": (WRIST_CUT, lambda c: c.x > 0.125, -1), "hand_r": (WRIST_CUT, lambda c: c.x < -0.125, -1),
    "foot_l": (ANKLE_CUT, lambda c: 0 < c.x < 0.11, -1), "foot_r": (ANKLE_CUT, lambda c: -0.11 < c.x < 0, -1),
}
OVERLAP = 0.015   # each high-res part runs this far past its cut; the retopo is trimmed back at the plane
SEEDS = {"head": (0.0, 0.0, 1.15), "hand_l": (WRIST[0], 0.0, 0.52), "hand_r": (-WRIST[0], 0.0, 0.52),
         "foot_l": (HIP[0], -0.05, 0.03), "foot_r": (-HIP[0], -0.05, 0.03)}


def _split_at(bm, select, z):
    """Cut the faces ``select`` picks with the plane at height ``z`` and split
    the mesh along the cut, so the piece below becomes its own component."""
    faces = [f for f in bm.faces if select(f.calc_center_median())]
    geom = faces + list({e for f in faces for e in f.edges}) + list({v for f in faces for v in f.verts})
    r = bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=(0, 0, z), plane_no=(0, 0, 1))
    cut = [e for e in r["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
    bmesh.ops.split_edges(bm, edges=cut)


def _component(bm, seed):
    """Faces connected to the vertex nearest ``seed``."""
    seed = mathutils.Vector(seed)
    v0 = min(bm.verts, key=lambda v: (v.co - seed).length_squared)
    seen = set()
    stack = [v0]
    while stack:
        v = stack.pop()
        if v.index in seen:
            continue
        seen.add(v.index)
        for e in v.link_edges:
            o = e.other_vert(v)
            if o.index not in seen:
                stack.append(o)
    return [f for f in bm.faces if all(v.index in seen for v in f.verts)]


def _fan_fill(bm):
    """Cap every boundary loop with a fan to its centroid (always manifold,
    which QuadriFlow insists on; an n-gon fill of a 100-vertex loop is not)."""
    boundary = [e for e in bm.edges if e.is_boundary]
    seen = set()
    for e0 in boundary:
        if e0.index in seen:
            continue
        loop = []
        e = e0
        v = e.verts[0]
        while True:
            seen.add(e.index)
            loop.append(v)
            v = e.other_vert(v)
            nxt = [x for x in v.link_edges if x.is_boundary and x.index not in seen]
            if not nxt:
                break
            e = nxt[0]
        c = bm.verts.new(sum((x.co for x in loop), mathutils.Vector()) / len(loop))
        for a, b in zip(loop, loop[1:] + loop[:1]):
            try:
                bm.faces.new((b, a, c))
            except ValueError:
                pass
    bm.edges.index_update()


def _part(hi, name):
    bm = bmesh.new()
    bm.from_mesh(hi.data)
    bm.verts.ensure_lookup_table()
    # every part runs OVERLAP past its cut, so the retopo can be trimmed at the
    # exact plane afterwards (QuadriFlow rounds a capped edge off)
    body = name == "body"
    for pname, (z, sel, keep) in CUTS.items():
        band = lambda c, z=z, sel=sel: sel(c) and abs(c.z - z) < 0.04 and (abs(c.x) > 0.133 if pname.startswith("hand") else
                                                                          abs(c.x) < 0.11 if pname.startswith("foot") else True)
        zc = z + keep * OVERLAP if body else z - keep * OVERLAP   # the part runs past the plane on its own side
        if body or pname == name:
            _split_at(bm, band, zc)
    bm.verts.index_update()
    if body:
        drop = set()
        for seed in SEEDS.values():
            drop.update(f.index for f in _component(bm, seed))
        bm.faces.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index in drop], context="FACES")
    else:
        keep = set(f.index for f in _component(bm, SEEDS[name]))
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index not in keep], context="FACES")
    # the bisect leaves slivers along the cut that make QuadriFlow give up
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=4e-4)
    _fan_fill(bm)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("_part_" + name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("_part_" + name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def _trim(part, cuts):
    """Cut the retopologised part back at the exact plane(s), dropping the
    overlap, so it ends in a clean planar ring."""
    bm = bmesh.new()
    bm.from_mesh(part.data)
    for z, sel, keep in cuts:
        faces = [f for f in bm.faces if sel(f.calc_center_median()) and abs(f.calc_center_median().z - z) < OVERLAP + 0.02]
        geom = faces + list({e for f in faces for e in f.edges}) + list({v for f in faces for v in f.verts})
        bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=(0, 0, z), plane_no=(0, 0, 1),
                               clear_outer=(keep < 0), clear_inner=(keep > 0))
    bm.to_mesh(part.data)
    bm.free()


def _retopo_part(part, hi, faces, symmetric, cuts):
    w2._activate(part)
    with bpy.context.temp_override(**w2._ctx()):
        bpy.ops.object.quadriflow_remesh(mode="FACES", target_faces=faces, use_mesh_symmetry=symmetric,
                                         use_preserve_sharp=False, use_preserve_boundary=False,
                                         smooth_normals=True, seed=7)
        if symmetric:
            bm = bmesh.new()
            bm.from_mesh(part.data)
            bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
            bm.to_mesh(part.data)
            bm.free()
        _trim(part, cuts)
        sw = part.modifiers.new("Wrap", "SHRINKWRAP")
        sw.target = hi
        sw.wrap_method = "NEAREST_SURFACEPOINT"
        bpy.ops.object.modifier_apply(modifier=sw.name)
    return part


def _bridge(bm, z, sel, tol=0.015):
    """Merge the two open rings at a cut into one (bridge_loops with merge:
    each pair of facing vertices meets halfway), so the joint is seamless."""
    edges = [e for e in bm.edges if e.is_boundary and all(abs(v.co.z - z) < tol for v in e.verts)
             and sel((e.verts[0].co + e.verts[1].co) / 2)]
    if edges:
        ring = set(edges)
        r = bmesh.ops.bridge_loops(bm, edges=edges)
        rungs = [e for e in r["edges"] if e not in ring]
        bmesh.ops.collapse(bm, edges=rungs, uvs=False)       # each rung's two ends meet halfway
        ring = list({v for e in bm.edges if all(abs(x.co.z - z) < tol for x in e.verts) and sel((e.verts[0].co + e.verts[1].co) / 2)
                     for v in e.verts})
        bmesh.ops.remove_doubles(bm, verts=ring, dist=0.002)  # slivers left along the joint would get no texture
        bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-3)
    return len(edges)


def retopo(hi, name=BODY, parts=PARTS):
    pieces = []
    for pname, faces in parts.items():
        part = _part(hi, pname)
        cuts = [(z, sel, -keep) for z, sel, keep in CUTS.values()] if pname == "body" else [CUTS[pname]]
        _retopo_part(part, hi, faces, symmetric=(pname in ("body", "head")), cuts=cuts)
        pieces.append(part)
    bm = bmesh.new()
    for part in pieces:
        bm.from_mesh(part.data)
    for z, sel, _keep in CUTS.values():
        _bridge(bm, z, sel)
    _fan_fill(bm)                     # QuadriFlow's symmetric weld leaves the odd pinhole
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    low = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(low)
    for part in pieces:
        bpy.data.objects.remove(part)
    w2.flatten_soles(low)
    for p in low.data.polygons:
        p.use_smooth = True
    return low


def build_body(arm, voxel=0.0025, keep_highres=True):
    for n in (BODY, HIGHRES, "_BodyShells"):
        if n in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[n])
    hi = w2.union(shells(), voxel=voxel, smooth_iters=10, smooth_factor=0.5)
    w2.sculpt(hi, FEATURES)
    low = retopo(hi, name=BODY)
    w2.unwrap(low)
    w2.material(low)
    w2.skin(low, arm, BONE_INFLUENCE)
    w2.race_keys(low, arm)
    if keep_highres:
        hi.hide_set(True)
        hi.hide_render = True
    else:
        bpy.data.objects.remove(hi)
    return low


def archive_stage_a():
    """Keep the Stage A body file next to the new one (comparison renders)."""
    if not os.path.exists(ARCHIVE_BLEND):
        shutil.copy2(BLEND, ARCHIVE_BLEND)
    return ARCHIVE_BLEND


def measurements(body, arm):
    """Numbers for the report: heights, widths and head units."""
    co = w2._coords(body)
    top = float(co[:, 2].max())
    chin = 1.05
    head_h = top - chin
    def width_at(z, tol=0.01, xmax=0.135):
        sel = co[(np.abs(co[:, 2] - z) < tol) & (np.abs(co[:, 0]) < xmax)]
        return float(sel[:, 0].max() - sel[:, 0].min()) if len(sel) else 0.0
    head_w = width_at(1.15, 0.02, 0.055)          # cranium, ears excluded
    shoulder_w = width_at(0.955, 0.02, 0.3)        # deltoid to deltoid
    out = dict(height=round(top, 3), head_height=round(head_h, 3), heads_tall=round(top / head_h, 2),
               head_width=round(head_w, 3), shoulder_width=round(shoulder_w, 3),
               shoulder_in_head_widths=round(shoulder_w / head_w, 2),
               hip_width=round(width_at(0.64), 3), waist_width=round(width_at(0.74), 3),
               crotch_z=0.575, crotch_fraction=round(0.575 / top, 3),
               leg_length=round(HIP[2] - ANKLE[2] + ANKLE[2], 3), arm_length=round(SHOULDER[2] - HAND_TIP[2], 3),
               fingertip_z=HAND_TIP[2])
    out["bones"] = {b.name: [tuple(round(c, 3) for c in b.head_local), tuple(round(c, 3) for c in b.tail_local)]
                    for b in arm.data.bones}
    return out


def build_all(save_blend=True, do_export=True, bake=True, anims=True):
    archive_stage_a()
    arm = w2.open_blend()
    w2.clear_old()
    retarget_armature(arm)
    body = build_body(arm)
    hair = _load("wave2_hair2")
    hairs = hair.build_all(body, arm)
    if bake:
        tx = _load("wave2_body_textures")
        tx.set_face(FACE)
        tx.bake_skin()
        tx.bake_hair()
        tx.manifest()
    if HIGHRES in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects[HIGHRES])
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    if anims:
        an = _load("wave2_anims")
        an.build(arm)
    if save_blend:
        w2.save()
    if do_export:
        w2.export()
        export_hair()
    return [w2.report(o) for o in [body] + hairs]


HAIR_GLB = os.path.join(os.path.dirname(OUT_GLB), "SK_Valhalla_Hair.glb")


def export_hair(path=HAIR_GLB):
    """The three hair meshes alone, no animations: Interchange re-imports the
    body glb in place only for the mesh that lives in the destination folder
    (the body), so the hair gets its own glb imported into the Hair folder."""
    return w2.export(names=("SK_Hair_Brown_Short", "SK_Hair_Blonde", "SK_Hood_Bald_Cap"), path=path, animations=False)
