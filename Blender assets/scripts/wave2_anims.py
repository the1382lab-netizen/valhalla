"""B-15 Wave 2, Stage C: the character animations (A-029, A-030) as scripted
keyframes on ``ARM_Valhalla`` in ``valhalla_body.blend``.

How the game plays them (Source/ValhallaGame/ValhallaAnimComponent.cpp,
ValhallaAnimInstance.cpp — a native anim instance, no AnimBP, no montages,
no notifies, play rate 1):

- ``A_Idle`` and ``A_Walk`` are looping sequence players cross-faded over
  0.15 s from the actor's 2D speed (walk above 10 cm/s).
- ``A_Attack`` / ``A_Shoot`` / ``A_Cast`` / ``A_Hit`` are one-shots layered on
  top with a 0.06 s blend in and a 0.12 s blend out that starts
  ``PlayLength - 0.12 s`` after the start. The swing is played *when the hit
  outcome arrives* (DispatchCombatEvent), so no frame inside the clip is a
  gameplay hit frame; only the clip length matters (how long the action layer
  stays up) and the weapon's attack speed is at least 1600 ms, longer than
  any of them. ``A_Cast`` is looped by the action layer for the whole cast
  bar, and played once for instant skills. ``A_Death`` plays once and holds
  its last frame as the corpse.
- The walk speed is the class ``baseSpeed`` (classes.json: 110, 122, 122,
  134, 147; NPC templates 60–80) with play rate 1, so one cycle was matched
  to 122 cm/s: 20 frames at 30 fps = 0.667 s, 81.3 cm per cycle, the foot
  planted for half the cycle and moving back at exactly that speed. Other
  classes slide by their speed ratio (110: 10 %, 147: 20 %); NPCs at 60–80
  slide the other way (a slower NPC walk is a backlog item).

Lengths and loops are kept from the first pass (30 fps): Idle 60 f loop,
Walk 24 -> 20 f loop (foot-slide match, above), Attack 20 f, Shoot 20 f,
Cast 24 f loop-able, Hit 12 f, Death 36 f hold.

New (A-030, not wired into gameplay): A_Run 16 f loop, A_Sit 30 f hold,
A_Emote_Wave 40 f, A_Emote_Cheer 40 f, A_Emote_Bow 45 f, A_Attack2H 26 f,
A_Block 20 f, A_Dodge 18 f.

How they are made: every clip is a list of poses (radians on the bones'
local axes, metres for the pelvis) at chosen frames, Bezier-eased between
them; the cyclic clips (idle, walk, run, cast) are functions of the cycle
phase keyed on every frame, so the loop point is seamless. Legs stand on the
ground through a small analytic two-bone IK (``leg``): the ankle target is
given in armature space and thigh / calf / foot pitch are solved in the
sagittal plane, so feet never go through the floor and the walk's planted
foot moves at the matched speed. The rig's own IK constraints stay at
influence 0, as in the first pass; everything is plain FK keys.

Bone conventions (all deform bones have local X = world X): for a limb
hanging down, +rotation.x swings it backward (+Y); for the spine, +x leans
forward. upperarm +z brings the left hand inward and the right hand outward.
Pelvis location is (x, up, -forward) in metres.

Run in Blender: exec, then ``build_all()`` (opens the body .blend, rebuilds
the 15 actions, saves, exports the body glb with them).
"""

import importlib.util
import math
import os

import bpy
import mathutils

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


w2 = _load("wave2_body")

FPS = 30
ARMATURE = w2.ARMATURE

# Leg geometry (metres) from the armature: thigh 0.1704, calf 0.1455, and the
# rest tilts of both (the knee is bent 8.7 degrees at rest).
L1, L2 = 0.1704, 0.1455
HIP_Z, HIP_X = 0.385, 0.085
ANKLE_Z = 0.07
P1_REST = math.atan2(-0.07, 0.998)          # thigh pitch at rest (slightly forward)
KNEE_REST = 0.152                            # calf pitch relative to the thigh at rest

DEFORM = ("root", "pelvis", "spine_01", "spine_02", "neck", "head", "clavicle_l", "upperarm_l", "lowerarm_l",
          "hand_l", "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r", "thigh_l", "calf_l", "foot_l",
          "thigh_r", "calf_r", "foot_r")


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


class Clip:
    """Poses at frames -> one action. ``rot`` values are radians on the bone's
    local XYZ; ``loc`` metres. Bones not mentioned at a frame keep whatever
    their other keys interpolate to (rest if never keyed)."""

    def __init__(self, name, frames, loop=False, dense=False):
        self.name, self.frames, self.loop, self.dense = name, frames, loop, dense
        self.keys = {}          # frame -> bone -> {"rot": (x,y,z), "loc": (x,y,z)}

    def rot(self, frame, bone, x=0.0, y=0.0, z=0.0):
        self.keys.setdefault(frame, {}).setdefault(bone, {})["rot"] = (x, y, z)
        return self

    def loc(self, frame, bone, x=0.0, y=0.0, z=0.0):
        self.keys.setdefault(frame, {}).setdefault(bone, {})["loc"] = (x, y, z)
        return self

    def pose(self, frame, **bones):
        """``pose(f, upperarm_r=(-2.7, 0, 0.3), pelvis_loc=(0, -0.02, 0))``."""
        for name, val in bones.items():
            if name.endswith("_loc"):
                self.loc(frame, name[:-4], *val)
            else:
                self.rot(frame, name, *val)
        return self

    def rest(self, frame, bones=DEFORM):
        for b in bones:
            self.rot(frame, b)
            if b == "pelvis":
                self.loc(frame, b)
        return self

    def leg(self, frame, side, ankle_y, ankle_z=ANKLE_Z, hip_dz=0.0, hip_dy=0.0, toe=0.0, lateral=0.0):
        """Plant the ankle at (ankle_y, ankle_z) in armature space, the hip
        moved by (hip_dy, hip_dz): two-bone IK in the sagittal plane. ``toe``
        pitches the foot (+ = toe down); ``lateral`` is the pelvis x shift
        the leg has to absorb so the foot stays put."""
        pre = "l" if side > 0 else "r"
        dy, dz = ankle_y - hip_dy, ankle_z - (HIP_Z + hip_dz)
        d = max(0.02, min(math.hypot(dy, dz), L1 + L2 - 0.003))
        p_d = math.atan2(dy, -dz)
        alpha = math.acos(max(-1.0, min(1.0, (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d))))
        knee = math.pi - math.acos(max(-1.0, min(1.0, (L1 * L1 + L2 * L2 - d * d) / (2 * L1 * L2))))
        thigh = (p_d - alpha) - P1_REST
        calf = knee - KNEE_REST
        zrot = lateral / (L1 + L2)
        self.rot(frame, "thigh_" + pre, thigh, 0.0, zrot)
        self.rot(frame, "calf_" + pre, calf, 0.0, 0.0)
        self.rot(frame, "foot_" + pre, -(thigh + calf) + toe, 0.0, -zrot)
        return self

    def stand(self, frame, hip_dz=0.0, hip_dy=0.0, lateral=0.0, spread=0.0):
        """Both feet planted where they rest, hips moved by (dy, dz)."""
        self.leg(frame, 1, spread, hip_dz=hip_dz, hip_dy=hip_dy, lateral=lateral)
        self.leg(frame, -1, -spread, hip_dz=hip_dz, hip_dy=hip_dy, lateral=lateral)
        return self.loc(frame, "pelvis", lateral, hip_dz, -hip_dy)

    def build(self, arm):
        old = bpy.data.actions.get(self.name)
        if old is not None:
            bpy.data.actions.remove(old)
        act = bpy.data.actions.new(self.name)
        arm.animation_data_create()
        arm.animation_data.action = act
        try:
            arm.animation_data.action_slot = act.slots.new("OBJECT", ARMATURE) if len(act.slots) == 0 else act.slots[0]
        except Exception:
            pass
        for pb in arm.pose.bones:
            pb.matrix_basis = mathutils.Matrix.Identity(4)
        for frame in sorted(self.keys):
            for bone, tr in self.keys[frame].items():
                pb = arm.pose.bones[bone]
                if "rot" in tr:
                    pb.rotation_euler = tr["rot"]
                    pb.keyframe_insert("rotation_euler", frame=frame)
                if "loc" in tr:
                    pb.location = tr["loc"]
                    pb.keyframe_insert("location", frame=frame)
        interp = "LINEAR" if self.dense else "BEZIER"
        for layer in act.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:
                    for fc in cb.fcurves:
                        for k in fc.keyframe_points:
                            k.interpolation = interp
                            k.handle_left_type = k.handle_right_type = "AUTO_CLAMPED"
        act.use_frame_range = True
        act.frame_start, act.frame_end = 0, self.frames
        act.use_cyclic = self.loop
        act["valhalla_loop"] = self.loop
        for pb in arm.pose.bones:
            pb.matrix_basis = mathutils.Matrix.Identity(4)
        arm.animation_data.action = None
        return act


# ── Cyclic clips (a function of phase, keyed every frame) ────────────────────

def walk_cycle(clip, n, reach, stance=0.5, drop=(0.05, 0.012), lift=0.075, arm=0.45, lean=0.06, hip_yaw=0.12,
               shoulder_yaw=0.18, sway=0.012, elbow=0.4, run=False):
    """Contact/passing cycle. The planted foot moves from ``reach`` in front
    to ``reach`` behind during ``stance`` of the cycle, i.e. at
    2 * reach / (stance * cycle seconds); the swing foot arcs forward over
    ``lift``. The hips drop ``drop[0]`` at contact (the leg is straight at
    rest, so it cannot reach forward without the knee bending) and
    ``drop[1]`` at mid-stance."""
    T = n / FPS
    speed = 2.0 * reach / (stance * T)
    dc, dm = drop
    for f in range(n + 1):
        t = (f % n) / n
        hip_dz = -(dc + dm) / 2.0 - (dc - dm) / 2.0 * math.cos(4 * math.pi * t)
        for side, phase in ((1, 0.0), (-1, 0.5)):
            u = (t - phase) % 1.0
            if u < stance:                   # planted, sliding back under the body
                y = -reach + speed * u * T
                toe = 0.35 * smooth((u - stance * 0.7) / (stance * 0.3)) if run else 0.25 * smooth((u - stance * 0.75) / (stance * 0.25))
                z = ANKLE_Z + 0.09 * math.sin(toe)          # toe-off pivots on the toe, so the ankle rises
            else:                            # swing
                s = (u - stance) / (1.0 - stance)
                y = reach - 2 * reach * smooth(s)
                toe = 0.3 * (1 - s) - 0.15 * s
                z = ANKLE_Z + lift * math.sin(math.pi * s) + 0.09 * math.sin(max(0.0, toe))
            lateral = sway * math.sin(2 * math.pi * t)
            clip.leg(f, side, y, z, hip_dz=hip_dz, toe=toe, lateral=lateral)
        lateral = sway * math.sin(2 * math.pi * t)
        c = math.cos(2 * math.pi * t)
        clip.loc(f, "pelvis", lateral, hip_dz, 0.0)
        clip.rot(f, "pelvis", 0.0, -hip_yaw * c, 0.04 * math.sin(2 * math.pi * t))
        clip.rot(f, "spine_01", lean, 0.0, 0.0)
        clip.rot(f, "spine_02", lean * 0.5 + 0.02 * math.cos(4 * math.pi * t), shoulder_yaw * c, -0.03 * math.sin(2 * math.pi * t))
        clip.rot(f, "neck", -lean * 0.4, 0.0, 0.0)
        clip.rot(f, "head", -lean * 0.6 - 0.02 * math.cos(4 * math.pi * t), -0.3 * shoulder_yaw * c, 0.0)
        # arms swing opposite to their leg; the forearm folds as the arm comes forward
        clip.rot(f, "upperarm_l", arm * c, 0.0, 0.12)
        clip.rot(f, "upperarm_r", -arm * c, 0.0, -0.12)
        clip.rot(f, "lowerarm_l", elbow + 0.3 * (1 - c) / 2, 0.0, 0.0)
        clip.rot(f, "lowerarm_r", elbow + 0.3 * (1 + c) / 2, 0.0, 0.0)
        clip.rot(f, "hand_l", 0.1, 0.0, 0.0)
        clip.rot(f, "hand_r", 0.1, 0.0, 0.0)
        clip.rot(f, "clavicle_l", 0.0, 0.0, 0.03 * c)
        clip.rot(f, "clavicle_r", 0.0, 0.0, 0.03 * c)
        clip.rot(f, "root")
    return clip


def a_walk():
    # 2 * 0.17 / (0.45 * 0.667 s) = 1.13 m/s: between the 110 and 122 cm/s classes.
    return walk_cycle(Clip("A_Walk", 20, loop=True, dense=True), 20, reach=0.17, stance=0.45, drop=(0.05, 0.012))


def a_run():
    # 2 * 0.20 / (0.35 * 0.533 s) = 2.14 m/s, 30 % of the cycle in the air.
    c = Clip("A_Run", 16, loop=True, dense=True)
    return walk_cycle(c, 16, reach=0.20, stance=0.35, drop=(0.075, 0.03), lift=0.12, arm=0.8, lean=0.22, hip_yaw=0.16,
                      shoulder_yaw=0.26, sway=0.008, elbow=1.3, run=True)


def a_idle():
    n = 60
    c = Clip("A_Idle", n, loop=True, dense=True)
    for f in range(n + 1):
        t = (f % n) / n
        breath = math.sin(2 * math.pi * t)
        shift = 0.012 * math.sin(2 * math.pi * t)                       # weight to the left, then right
        turn = 0.14 * (smooth((t - 0.30) / 0.12) - smooth((t - 0.62) / 0.12))   # look left, then back
        c.stand(f, hip_dz=-0.003 * (1 - math.cos(2 * math.pi * t)) / 2, lateral=shift)
        c.rot(f, "pelvis", 0.0, 0.02 * breath, 0.05 * shift / 0.012)
        c.rot(f, "spine_01", 0.01 * breath, 0.0, -0.02 * shift / 0.012)
        c.rot(f, "spine_02", 0.025 * breath, -0.02 * breath, -0.02 * shift / 0.012)
        c.rot(f, "neck", -0.01 * breath, 0.0, 0.0)
        c.rot(f, "head", -0.02 * breath, turn, 0.04 * shift / 0.012)
        c.rot(f, "clavicle_l", 0.0, 0.0, 0.03 * breath)
        c.rot(f, "clavicle_r", 0.0, 0.0, 0.03 * breath)
        c.rot(f, "upperarm_l", 0.04 * breath, 0.0, 0.10 + 0.02 * breath)
        c.rot(f, "upperarm_r", 0.04 * breath, 0.0, -0.10 - 0.02 * breath)
        c.rot(f, "lowerarm_l", 0.25 + 0.02 * breath, 0.0, 0.0)
        c.rot(f, "lowerarm_r", 0.25 + 0.02 * breath, 0.0, 0.0)
        c.rot(f, "hand_l", 0.05, 0.0, 0.0)
        c.rot(f, "hand_r", 0.05, 0.0, 0.0)
        c.rot(f, "root")
    return c


def a_cast():
    """Two-hand gesture: hands rise together, spread and pulse, come down.
    Starts and ends at rest so it loops for a long cast and also plays once."""
    n = 24
    c = Clip("A_Cast", n, loop=True, dense=True)
    for f in range(n + 1):
        t = f / n
        up = smooth(t / 0.22) * (1 - smooth((t - 0.78) / 0.22))     # arms up between 22 % and 78 %
        pulse = 0.5 * (1 - math.cos(2 * math.pi * (t - 0.22) / 0.56)) if 0.22 < t < 0.78 else 0.0
        c.stand(f, hip_dz=-0.01 * up)
        c.rot(f, "upperarm_l", -1.55 * up + 0.15 * pulse, 0.0, 0.10 - 0.45 * up * (0.4 + 0.6 * pulse))
        c.rot(f, "upperarm_r", -1.55 * up + 0.15 * pulse, 0.0, -0.10 + 0.45 * up * (0.4 + 0.6 * pulse))
        c.rot(f, "lowerarm_l", 0.25 + 0.7 * up - 0.3 * pulse * up, 0.0, 0.0)
        c.rot(f, "lowerarm_r", 0.25 + 0.7 * up - 0.3 * pulse * up, 0.0, 0.0)
        c.rot(f, "hand_l", -0.4 * up, 0.0, 0.0)
        c.rot(f, "hand_r", -0.4 * up, 0.0, 0.0)
        c.rot(f, "spine_01", -0.04 * up, 0.0, 0.0)
        c.rot(f, "spine_02", -0.08 * up + 0.04 * pulse, 0.0, 0.0)
        c.rot(f, "neck", -0.06 * up, 0.0, 0.0)
        c.rot(f, "head", -0.14 * up, 0.0, 0.0)
        c.rot(f, "clavicle_l", 0.0, 0.0, 0.06 * up)
        c.rot(f, "clavicle_r", 0.0, 0.0, 0.06 * up)
        c.rot(f, "root")
    return c


# ── One-shots (sparse poses, Bezier) ─────────────────────────────────────────

def _rest_arms(c, f):
    c.pose(f, upperarm_l=(0, 0, 0.10), upperarm_r=(0, 0, -0.10), lowerarm_l=(0.25, 0, 0), lowerarm_r=(0.25, 0, 0),
           hand_l=(0.05, 0, 0), hand_r=(0.05, 0, 0), clavicle_l=(0, 0, 0), clavicle_r=(0, 0, 0))


def _rest_all(c, f):
    c.rest(f)
    c.stand(f)
    _rest_arms(c, f)


def a_attack():
    """Overhand one-hand strike: wind up over the shoulder, cut down and
    across, follow through, recover. The weapon rides socket_weapon_r."""
    c = Clip("A_Attack", 20)
    _rest_all(c, 0)
    # anticipation
    c.stand(4, hip_dz=-0.01, hip_dy=0.02)
    c.pose(4, upperarm_r=(-2.7, 0.0, 0.35), lowerarm_r=(1.5, 0.0, 0.0), hand_r=(0.3, 0.0, 0.0),
           upperarm_l=(-0.6, 0.0, 0.25), lowerarm_l=(0.6, 0.0, 0.0), clavicle_r=(0.0, 0.0, -0.15),
           spine_01=(-0.05, 0.30, 0.0), spine_02=(-0.22, 0.15, 0.0), neck=(-0.05, 0.0, 0.0), head=(-0.12, -0.15, 0.0))
    c.pose(6, upperarm_r=(-2.9, 0.0, 0.30), lowerarm_r=(1.6, 0.0, 0.0))
    # the cut
    c.stand(9, hip_dz=-0.035, hip_dy=-0.03)
    c.pose(9, upperarm_r=(-1.0, 0.0, 0.05), lowerarm_r=(0.15, 0.0, 0.0), hand_r=(-0.25, 0.0, 0.0),
           upperarm_l=(0.5, 0.0, 0.2), lowerarm_l=(0.5, 0.0, 0.0), clavicle_r=(0.0, 0.0, 0.1),
           spine_01=(0.15, -0.35, 0.0), spine_02=(0.35, -0.2, 0.0), neck=(0.05, 0.0, 0.0), head=(0.15, 0.1, 0.0))
    # follow-through
    c.stand(12, hip_dz=-0.03, hip_dy=-0.03)
    c.pose(12, upperarm_r=(-0.35, 0.0, 0.1), lowerarm_r=(0.35, 0.0, 0.0), hand_r=(-0.1, 0.0, 0.0),
           spine_01=(0.12, -0.4, 0.0), spine_02=(0.3, -0.2, 0.0), head=(0.1, 0.15, 0.0))
    _rest_all(c, 20)
    return c


def a_attack2h():
    """Two-hand overhead chop: both hands together over the head, down through
    the target, a heavier recovery."""
    c = Clip("A_Attack2H", 26)
    _rest_all(c, 0)
    c.stand(6, hip_dz=-0.02, hip_dy=0.03)
    c.pose(6, upperarm_l=(-2.9, 0.0, 0.35), upperarm_r=(-2.9, 0.0, -0.35), lowerarm_l=(0.9, 0.0, 0.0), lowerarm_r=(0.9, 0.0, 0.0),
           hand_l=(0.2, 0.0, 0.0), hand_r=(0.2, 0.0, 0.0), spine_01=(-0.12, 0.0, 0.0), spine_02=(-0.25, 0.0, 0.0),
           neck=(-0.08, 0.0, 0.0), head=(-0.12, 0.0, 0.0), clavicle_l=(0, 0, 0.1), clavicle_r=(0, 0, 0.1))
    c.stand(12, hip_dz=-0.05, hip_dy=-0.04)
    c.pose(12, upperarm_l=(-0.9, 0.0, 0.3), upperarm_r=(-0.9, 0.0, -0.3), lowerarm_l=(0.2, 0.0, 0.0), lowerarm_r=(0.2, 0.0, 0.0),
           hand_l=(-0.3, 0.0, 0.0), hand_r=(-0.3, 0.0, 0.0), spine_01=(0.2, 0.0, 0.0), spine_02=(0.45, 0.0, 0.0),
           neck=(0.05, 0.0, 0.0), head=(0.2, 0.0, 0.0))
    c.stand(16, hip_dz=-0.045, hip_dy=-0.04)
    c.pose(16, upperarm_l=(-0.5, 0.0, 0.3), upperarm_r=(-0.5, 0.0, -0.3), lowerarm_l=(0.35, 0.0, 0.0), lowerarm_r=(0.35, 0.0, 0.0),
           spine_02=(0.4, 0.0, 0.0))
    _rest_all(c, 26)
    return c


def a_shoot():
    """Bow in the right hand (socket_weapon_r, pitched 90 by the game): the
    right arm pushes the bow out, the left hand draws to the cheek, holds,
    releases with a flick."""
    c = Clip("A_Shoot", 20)
    _rest_all(c, 0)
    c.stand(4, hip_dz=-0.01)
    c.pose(4, upperarm_r=(-1.5, 0.0, -0.1), lowerarm_r=(0.15, 0.0, 0.0), hand_r=(-0.1, 0.0, 0.0),
           upperarm_l=(-1.45, 0.0, -0.15), lowerarm_l=(0.5, 0.0, 0.0),
           spine_01=(0.0, -0.15, 0.0), spine_02=(-0.05, -0.1, 0.0), head=(0.0, 0.1, 0.0), clavicle_r=(0, 0, -0.1))
    c.stand(9, hip_dz=-0.015)
    c.pose(9, upperarm_r=(-1.55, 0.0, -0.1), lowerarm_r=(0.1, 0.0, 0.0),
           upperarm_l=(-1.45, 0.0, -0.45), lowerarm_l=(2.25, 0.0, 0.0), hand_l=(0.2, 0.0, 0.0),
           spine_01=(0.0, -0.25, 0.0), spine_02=(-0.05, -0.15, 0.0), head=(0.05, 0.12, 0.0), clavicle_l=(0, 0, 0.12))
    c.pose(12, upperarm_l=(-1.45, 0.0, -0.45), lowerarm_l=(2.3, 0.0, 0.0))
    # release
    c.stand(14, hip_dz=-0.01)
    c.pose(14, upperarm_l=(-1.3, 0.0, -0.6), lowerarm_l=(1.7, 0.0, 0.0), hand_l=(0.5, 0.0, 0.0),
           upperarm_r=(-1.45, 0.0, -0.1), spine_02=(-0.02, -0.12, 0.0))
    _rest_all(c, 20)
    return c


def a_hit():
    c = Clip("A_Hit", 12)
    _rest_all(c, 0)
    c.stand(3, hip_dz=-0.02, hip_dy=0.03)
    c.pose(3, spine_01=(-0.12, 0.1, 0.0), spine_02=(-0.3, 0.0, 0.0), neck=(-0.1, 0.0, 0.0), head=(-0.3, 0.1, 0.0),
           upperarm_l=(-0.7, 0.0, 0.1), upperarm_r=(-0.5, 0.0, -0.3), lowerarm_l=(0.9, 0.0, 0.0), lowerarm_r=(0.8, 0.0, 0.0),
           clavicle_l=(0, 0, 0.1), clavicle_r=(0, 0, 0.1))
    c.stand(6, hip_dz=-0.015, hip_dy=0.02)
    c.pose(6, spine_01=(-0.06, 0.05, 0.0), spine_02=(-0.15, 0.0, 0.0), head=(-0.15, 0.05, 0.0),
           upperarm_l=(-0.4, 0.0, 0.1), upperarm_r=(-0.3, 0.0, -0.2), lowerarm_l=(0.6, 0.0, 0.0), lowerarm_r=(0.5, 0.0, 0.0))
    _rest_all(c, 12)
    return c


def a_death():
    """Stagger, fall onto the back, settle, hold (the game clamps on the
    last frame). The pelvis is dropped so the back and head lie on z = 0."""
    c = Clip("A_Death", 36)
    _rest_all(c, 0)
    c.stand(6, hip_dz=-0.02, hip_dy=0.04)
    c.pose(6, spine_01=(-0.1, 0.0, 0.0), spine_02=(-0.3, 0.0, 0.0), neck=(-0.1, 0.0, 0.0), head=(-0.3, 0.0, 0.0),
           upperarm_l=(-0.9, 0.0, -0.2), upperarm_r=(-0.9, 0.0, 0.2), lowerarm_l=(0.8, 0.0, 0.0), lowerarm_r=(0.8, 0.0, 0.0))
    # mid-fall: the whole body tips back around the hips, knees give
    c.pose(14, pelvis=(-0.9, 0.0, 0.0), pelvis_loc=(0.0, -0.12, -0.06),
           thigh_l=(-0.4, 0.0, 0.08), thigh_r=(-0.4, 0.0, -0.08), calf_l=(0.5, 0.0, 0.0), calf_r=(0.5, 0.0, 0.0),
           foot_l=(0.2, 0.0, 0.0), foot_r=(0.2, 0.0, 0.0),
           spine_01=(-0.15, 0.0, 0.0), spine_02=(-0.2, 0.0, 0.0), neck=(-0.1, 0.0, 0.0), head=(-0.2, 0.0, 0.0),
           upperarm_l=(-1.2, 0.0, -0.8), upperarm_r=(-1.2, 0.0, 0.8), lowerarm_l=(0.6, 0.0, 0.0), lowerarm_r=(0.6, 0.0, 0.0))
    # on the ground
    c.pose(22, pelvis=(-1.5, 0.0, 0.0), pelvis_loc=(0.0, -0.19, -0.10),
           thigh_l=(-0.2, 0.0, 0.10), thigh_r=(-0.2, 0.0, -0.10), calf_l=(0.2, 0.0, 0.0), calf_r=(0.2, 0.0, 0.0),
           foot_l=(-0.1, 0.0, 0.0), foot_r=(-0.1, 0.0, 0.0),
           spine_01=(0.0, 0.0, 0.0), spine_02=(0.05, 0.0, 0.0), neck=(0.02, 0.0, 0.0), head=(0.12, 0.2, 0.0),
           upperarm_l=(-0.35, 0.0, -1.2), upperarm_r=(-0.35, 0.0, 1.2), lowerarm_l=(0.35, 0.0, 0.0), lowerarm_r=(0.35, 0.0, 0.0),
           hand_l=(-0.2, 0.0, 0.0), hand_r=(-0.2, 0.0, 0.0), clavicle_l=(0, 0, 0), clavicle_r=(0, 0, 0))
    # settle
    c.pose(28, pelvis=(-1.55, 0.0, 0.0), pelvis_loc=(0.0, -0.19, -0.10),
           thigh_l=(-0.12, 0.0, 0.10), thigh_r=(-0.12, 0.0, -0.10), calf_l=(0.15, 0.0, 0.0), calf_r=(0.15, 0.0, 0.0),
           head=(0.1, 0.25, 0.0), upperarm_l=(-0.3, 0.0, -1.25), upperarm_r=(-0.3, 0.0, 1.25))
    c.pose(36, pelvis=(-1.55, 0.0, 0.0), pelvis_loc=(0.0, -0.19, -0.10), head=(0.1, 0.25, 0.0),
           upperarm_l=(-0.3, 0.0, -1.25), upperarm_r=(-0.3, 0.0, 1.25))
    return c


def a_sit():
    """Sit down on the ground: crouch, a small hop to get the feet out, then a
    campfire sit (knees up, feet flat, forearms on the knees); holds."""
    c = Clip("A_Sit", 30)
    _rest_all(c, 0)
    c.stand(8, hip_dz=-0.04, hip_dy=0.01)
    c.pose(8, spine_01=(0.2, 0.0, 0.0), spine_02=(0.15, 0.0, 0.0), upperarm_l=(-0.5, 0.0, 0.1), upperarm_r=(-0.5, 0.0, -0.1),
           lowerarm_l=(0.5, 0.0, 0.0), lowerarm_r=(0.5, 0.0, 0.0))
    # on the way down the feet go out first (a small hop), or they would pass
    # through the floor between the crouch and the seat
    for s in (1, -1):
        c.leg(14, s, -0.15, ankle_z=0.10, hip_dz=-0.17, lateral=0.0)
        c.leg(20, s, -0.22, ankle_z=0.06, hip_dz=-0.315)
        c.leg(30, s, -0.22, ankle_z=0.06, hip_dz=-0.315)
    c.loc(14, "pelvis", 0.0, -0.17, 0.0)
    c.pose(14, pelvis=(0.05, 0.0, 0.0), spine_01=(0.2, 0.0, 0.0), spine_02=(0.1, 0.0, 0.0))
    for f in (20, 30):
        c.loc(f, "pelvis", 0.0, -0.315, 0.0)
        c.pose(f, pelvis=(-0.08, 0.0, 0.0),
               spine_01=(0.22, 0.0, 0.0), spine_02=(0.12, 0.0, 0.0), neck=(-0.05, 0.0, 0.0), head=(-0.08, 0.0, 0.0),
               upperarm_l=(-1.1, 0.0, 0.2), upperarm_r=(-1.1, 0.0, -0.2), lowerarm_l=(0.6, 0.0, 0.0), lowerarm_r=(0.6, 0.0, 0.0),
               hand_l=(-0.4, 0.0, 0.0), hand_r=(-0.4, 0.0, 0.0), clavicle_l=(0, 0, 0), clavicle_r=(0, 0, 0))
    return c


def a_emote_wave():
    c = Clip("A_Emote_Wave", 40)
    _rest_all(c, 0)
    c.stand(6)
    c.pose(6, upperarm_r=(-2.75, 0.0, 0.25), lowerarm_r=(0.55, 0.0, 0.0), hand_r=(-0.3, 0.0, 0.0), clavicle_r=(0, 0, -0.2),
           spine_02=(-0.05, 0.1, 0.08), head=(-0.05, 0.1, 0.12))
    for i, f in enumerate(range(9, 31, 4)):
        s = 1 if i % 2 == 0 else -1
        c.pose(f, hand_r=(-0.3, 0.0, 0.55 * s), lowerarm_r=(0.55, 0.0, 0.3 * s), upperarm_r=(-2.75, 0.0, 0.25 + 0.1 * s))
    c.pose(34, upperarm_r=(-2.6, 0.0, 0.2), lowerarm_r=(0.5, 0.0, 0.0), hand_r=(-0.1, 0.0, 0.0))
    _rest_all(c, 40)
    return c


def a_emote_cheer():
    c = Clip("A_Emote_Cheer", 40)
    _rest_all(c, 0)
    c.stand(6, hip_dz=-0.03)
    c.pose(6, upperarm_l=(-2.8, 0.0, -0.3), upperarm_r=(-2.8, 0.0, 0.3), lowerarm_l=(0.45, 0.0, 0.0), lowerarm_r=(0.45, 0.0, 0.0),
           hand_l=(-0.2, 0.0, 0.0), hand_r=(-0.2, 0.0, 0.0), spine_02=(-0.12, 0.0, 0.0), neck=(-0.08, 0.0, 0.0), head=(-0.2, 0.0, 0.0),
           clavicle_l=(0, 0, 0.15), clavicle_r=(0, 0, 0.15))
    # a hop: legs straight in the air, land soft
    c.pose(10, pelvis_loc=(0.0, 0.07, 0.0), thigh_l=(-0.15, 0.0, 0.0), thigh_r=(-0.15, 0.0, 0.0), calf_l=(0.25, 0.0, 0.0),
           calf_r=(0.25, 0.0, 0.0), foot_l=(0.35, 0.0, 0.0), foot_r=(0.35, 0.0, 0.0), upperarm_l=(-2.95, 0.0, -0.35), upperarm_r=(-2.95, 0.0, 0.35))
    c.stand(14, hip_dz=-0.05)
    c.pose(14, upperarm_l=(-2.6, 0.0, -0.3), upperarm_r=(-2.6, 0.0, 0.3), spine_02=(0.0, 0.0, 0.0))
    c.stand(20, hip_dz=-0.01)
    c.pose(20, upperarm_l=(-2.9, 0.0, -0.4), upperarm_r=(-2.9, 0.0, 0.4), lowerarm_l=(0.3, 0.0, 0.0), lowerarm_r=(0.3, 0.0, 0.0),
           spine_02=(-0.15, 0.0, 0.0), head=(-0.25, 0.0, 0.0))
    c.pose(26, upperarm_l=(-2.7, 0.0, -0.3), upperarm_r=(-2.7, 0.0, 0.3))
    c.stand(34)
    c.pose(34, upperarm_l=(-0.8, 0.0, 0.1), upperarm_r=(-0.8, 0.0, -0.1), lowerarm_l=(0.5, 0.0, 0.0), lowerarm_r=(0.5, 0.0, 0.0),
           spine_02=(0.0, 0.0, 0.0), head=(0.0, 0.0, 0.0))
    _rest_all(c, 40)
    return c


def a_emote_bow():
    c = Clip("A_Emote_Bow", 45)
    _rest_all(c, 0)
    c.stand(12, hip_dz=-0.02, hip_dy=0.02)
    c.pose(12, spine_01=(0.55, 0.0, 0.0), spine_02=(0.35, 0.0, 0.0), neck=(0.1, 0.0, 0.0), head=(0.25, 0.0, 0.0),
           upperarm_r=(-1.1, 0.0, -0.9), lowerarm_r=(1.7, 0.0, 0.0), hand_r=(0.2, 0.0, 0.0),
           upperarm_l=(0.5, 0.0, -0.35), lowerarm_l=(0.3, 0.0, 0.0), clavicle_r=(0, 0, -0.1))
    c.stand(28, hip_dz=-0.02, hip_dy=0.02)
    c.pose(28, spine_01=(0.58, 0.0, 0.0), spine_02=(0.36, 0.0, 0.0), head=(0.25, 0.0, 0.0),
           upperarm_r=(-1.1, 0.0, -0.9), lowerarm_r=(1.7, 0.0, 0.0), upperarm_l=(0.5, 0.0, -0.35))
    c.stand(40)
    c.pose(40, spine_01=(0.05, 0.0, 0.0), spine_02=(0.03, 0.0, 0.0), head=(0.02, 0.0, 0.0),
           upperarm_r=(-0.3, 0.0, -0.2), lowerarm_r=(0.5, 0.0, 0.0), upperarm_l=(0.1, 0.0, 0.05))
    _rest_all(c, 45)
    return c


def a_block():
    """Shield up (socket_offhand_l is on the left hand): left arm across and
    up, weight low, held, then down."""
    c = Clip("A_Block", 20)
    _rest_all(c, 0)
    c.stand(4, hip_dz=-0.04, hip_dy=0.01)
    c.pose(4, upperarm_l=(-1.25, 0.0, 0.7), lowerarm_l=(1.3, 0.0, 0.0), hand_l=(0.2, 0.0, 0.0), clavicle_l=(0, 0, 0.15),
           upperarm_r=(0.45, 0.0, -0.3), lowerarm_r=(0.9, 0.0, 0.0),
           spine_01=(0.1, 0.25, 0.0), spine_02=(0.15, 0.1, 0.0), neck=(0.02, 0.0, 0.0), head=(0.12, -0.15, 0.0))
    c.stand(12, hip_dz=-0.045, hip_dy=0.01)
    c.pose(12, upperarm_l=(-1.3, 0.0, 0.75), lowerarm_l=(1.35, 0.0, 0.0), spine_01=(0.1, 0.28, 0.0))
    _rest_all(c, 20)
    return c


def a_dodge():
    """A quick duck and lean to the character's right (-X), then back."""
    c = Clip("A_Dodge", 18)
    _rest_all(c, 0)
    c.stand(4, hip_dz=-0.05, lateral=-0.05)
    c.pose(4, pelvis=(0.05, 0.0, 0.12), spine_01=(0.25, 0.0, 0.15), spine_02=(0.2, 0.0, 0.1), neck=(0.05, 0.0, 0.0),
           head=(0.15, 0.0, 0.1), upperarm_l=(-0.4, 0.0, 0.4), upperarm_r=(-0.3, 0.0, 0.3), lowerarm_l=(0.9, 0.0, 0.0),
           lowerarm_r=(0.9, 0.0, 0.0))
    c.stand(9, hip_dz=-0.05, lateral=-0.02)
    c.pose(9, pelvis=(0.03, 0.0, 0.05), spine_01=(0.15, 0.0, -0.05), spine_02=(0.1, 0.0, -0.05), head=(0.1, 0.0, -0.05))
    _rest_all(c, 18)
    return c


CLIPS = [a_idle, a_walk, a_attack, a_shoot, a_cast, a_hit, a_death,
         a_run, a_sit, a_emote_wave, a_emote_cheer, a_emote_bow, a_attack2h, a_block, a_dodge]
NEW = ("A_Run", "A_Sit", "A_Emote_Wave", "A_Emote_Cheer", "A_Emote_Bow", "A_Attack2H", "A_Block", "A_Dodge")


def build(arm=None):
    arm = arm or bpy.data.objects[ARMATURE]
    bpy.context.scene.render.fps = FPS
    out = []
    for make in CLIPS:
        clip = make()
        out.append(clip.build(arm))
    for a in out:
        a.use_fake_user = True        # keep the ones not assigned to anything
    return out


def build_all(save_blend=True, do_export=True):
    """Open the body .blend, rebuild the 15 actions, save, export the body glb
    (wave2_body.export puts every action in it)."""
    arm = w2.open_blend()
    acts = build(arm)
    if save_blend:
        w2.save()
    if do_export:
        w2.export()
    return [(a.name, int(a.frame_end), bool(a.use_cyclic)) for a in acts]
