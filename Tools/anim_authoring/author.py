"""Author the MetaHuman stance poses, weapon grip frames and hand-keyed attacks.
Pure Python; writes out/*.json that ue_write.py turns into AnimSequences."""
import json, math, os, sys
from qm import *
import skel
from rig import *
H = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(H, "out"); os.makedirs(OUT, exist_ok=True)
FPS = 30
base = skel.from_json(os.path.join(H, "pose_MH_MM_Idle.json")); base.fk()

def cp(p):
    q = p.copy(); q.fk(); return q

# ── hand frames (hand-local palm axes, measured once on the idle) ─────────
ALONG_R, PALMN_R = hand_axes_local(base, "r")
ALONG_L, PALMN_L = hand_axes_local(base, "l")

# ── stance poses ───────────────────────────────────────────────────────────
gripR = cp(base); grip(gripR, "r")
gripL = cp(base); grip(gripL, "l")

SH_ELBOW_POLE = (40.0, -25.0, 95.0)
def shield_arm(p):
    # forearm across the front, a little above the navel, fist forward-in
    p.two_bone_ik("upperarm_l", "lowerarm_l", "hand_l", (9.0, 27.0, 103.0), SH_ELBOW_POLE)
    fa = v_norm(v_sub(p.wpos("hand_l"), p.wpos("lowerarm_l")))
    set_hand_world(p, "l", fa, v_norm((-1.0, 0.15, -0.1)), ALONG_L, PALMN_L)
    grip(p, "l")
shieldP = cp(gripR); shield_arm(shieldP)

# ── grip frames (prop relative to its bone), same intents as gid_calib ─────
def rel_of(p, bone, loc, rot):
    qh = p.wrot(bone); ph = p.wpos(bone)
    return q_rot(q_conj(qh), v_sub(loc, ph)), q_mul(q_conj(qh), rot)

def sword_rot(blade, edge):
    return q_from_axes(*axes_from_xy(v_sub(edge, v_mul(v_norm(blade), v_dot(edge, v_norm(blade)))), v_mul(blade, -1)))

W2U = 1.0 / SCALE   # world cm -> unscaled cm
def palm_point(p, side, t=0.95, inward=-2.0):
    s = "_" + side
    h = p.wpos("hand" + s); m = p.wpos("middle_01" + s)
    rgt = R if side == "r" else v_mul(R, -1)
    return v_add(v_lerp(h, m, t), v_mul(R, inward * W2U if side == "r" else -inward * W2U))

grips = {}
# one-handed: blade 40 deg below forward, a little outward, flat facing out
a = math.radians(40)
blade = v_norm(v_add(v_add(v_mul(F, math.cos(a)), v_mul(U, -math.sin(a))), v_mul(R, 0.3)))
flat = v_norm(v_sub(R, v_mul(blade, v_dot(R, blade))))
X = v_norm(v_cross(flat, v_mul(blade, -1)))
rot = q_from_axes(*axes_from_xy(X, v_mul(blade, -1)))
grips["OneHand"] = ("hand_r",) + rel_of(gripR, "hand_r", palm_point(gripR, "r"), rot) + (1.0,)
# staff: shaft upright, tilted 8 deg forward, head up
t = math.radians(8)
shaft_up = v_norm(v_add(v_mul(U, math.cos(t)), v_mul(F, math.sin(t))))
rot = q_from_axes(*axes_from_yz(v_mul(shaft_up, -1), R))
grips["Staff"] = ("hand_r",) + rel_of(gripR, "hand_r", palm_point(gripR, "r"), rot) + (1.0,)
# bow: left fist, limbs upright, belly forward
rot = q_from_axes(*axes_from_yz(U, F))
grips["Bow"] = ("hand_l",) + rel_of(gripL, "hand_l", palm_point(gripL, "l"), rot) + (1.0,)
# shield: on the raised left forearm, face forward-out, top up
fa0, hl0 = shieldP.wpos("lowerarm_l"), shieldP.wpos("hand_l")
face = v_norm(v_add(v_mul(F, 0.8), v_mul(Lf, 0.6)))
topv = v_norm(v_sub(U, v_mul(face, v_dot(U, face))))
rot = q_from_axes(*axes_from_yz(v_mul(topv, -1), face))
loc = v_add(v_add(v_lerp(fa0, hl0, 0.45), v_mul(face, 4.0)), v_mul(U, -2.0))
grips["Shield"] = ("lowerarm_l",) + rel_of(shieldP, "lowerarm_l", loc, rot) + (0.75,)

def prop_world(p, g):
    bone, rl, rq, _ = grips[g]
    return v_add(p.wpos(bone), q_rot(p.wrot(bone), rl)), q_mul(p.wrot(bone), rq)

def hand_rot_for_prop(g, prop_q):
    return q_mul(prop_q, q_conj(grips[g][2]))

# ── keyframe engine ────────────────────────────────────────────────────────
def catmull(p0, p1, p2, p3, t):
    t2, t3 = t*t, t*t*t
    return tuple(0.5*((2*b) + (-a + c)*t + (2*a - 5*b + 4*c - d)*t2 + (-a + 3*b - 3*c + d)*t3)
                 for a, b, c, d in zip(p0, p1, p2, p3))

def sample(keys, name, tm, kind):
    ks = [(k["t"], k[name]) for k in keys if name in k]
    if not ks: return None
    if tm <= ks[0][0]: return ks[0][1]
    if tm >= ks[-1][0]: return ks[-1][1]
    for i in range(len(ks) - 1):
        (t0, v0), (t1, v1) = ks[i], ks[i+1]
        if t0 <= tm <= t1:
            u = ease((tm - t0) / (t1 - t0)) if kind != "lin" else (tm - t0)/(t1 - t0)
            if kind == "q": return q_slerp(v0, v1, u)
            if kind == "pos":
                vp = ks[i-1][1] if i > 0 else v0
                vn = ks[i+2][1] if i + 2 < len(ks) else v1
                return catmull(vp, v0, v1, vn, u)
            if isinstance(v0, tuple): return v_lerp(v0, v1, u)
            return v0 + (v1 - v0)*u

SPINE = ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05"]
def apply_spine(p, yaw, pitch, roll=0.0):
    for b in SPINE:
        p.rotate_world(b, q_axis_angle(U, math.radians(yaw)/len(SPINE)))
        p.rotate_world(b, q_axis_angle(R, math.radians(pitch)/len(SPINE)))
        if roll: p.rotate_world(b, q_axis_angle(F, math.radians(roll)/len(SPINE)))

def build(keys, length, start_pose, right_grip=None, left_grip=None, extra=None):
    frames = []
    n = int(round(length*FPS)) + 1
    for fi in range(n):
        tm = fi / FPS
        p = cp(start_pose)
        apply_spine(p, sample(keys, "yaw", tm, "f") or 0.0, sample(keys, "pitch", tm, "f") or 0.0, sample(keys, "roll", tm, "f") or 0.0)
        pel = sample(keys, "pelvis_z", tm, "f")
        if pel:
            t0, q0 = p.local[p.idx["pelvis"]]; p.set_local_pos("pelvis", v_add(t0, (0, 0, pel)))
        for side, g in (("r", right_grip), ("l", left_grip)):
            hp = sample(keys, side + "_hand", tm, "pos")
            if hp is None: continue
            pole = sample(keys, side + "_pole", tm, "lin") or ((-40, -20, 90) if side == "r" else (40, -20, 90))
            p.two_bone_ik("upperarm_" + side, "lowerarm_" + side, "hand_" + side, hp, pole)
            pq = sample(keys, side + "_prop", tm, "q")
            if pq is not None and g:
                p.set_world_rot("hand_" + side, hand_rot_for_prop(g, pq))
            hq = sample(keys, side + "_handq", tm, "q")
            if hq is not None:
                p.set_world_rot("hand_" + side, hq)
        if extra: extra(p, tm)
        frames.append(p)
    return frames

# idle references for the ends of every attack
idle_r_hand = gripR.wpos("hand_r"); idle_l_hand = gripR.wpos("hand_l")
idle_r_pole = v_add(gripR.wpos("lowerarm_r"), v_mul(v_norm(v_sub(gripR.wpos("lowerarm_r"), v_lerp(gripR.wpos("upperarm_r"), gripR.wpos("hand_r"), 0.5))), 30))
idle_l_pole = v_add(gripR.wpos("lowerarm_l"), v_mul(v_norm(v_sub(gripR.wpos("lowerarm_l"), v_lerp(gripR.wpos("upperarm_l"), gripR.wpos("hand_l"), 0.5))), 30))
idle_prop = {g: prop_world(gripR if grips[g][0] != "lowerarm_l" else shieldP, g)[1] for g in grips}
idle_r_handq = gripR.wrot("hand_r"); idle_l_handq = gripR.wrot("hand_l")

def swordq(blade, edge): return sword_rot(v_norm(blade), v_norm(edge))
def staffq(head, side): return q_from_axes(*axes_from_yz(v_mul(v_norm(head), -1), side))
def bowq(limb, belly): return q_from_axes(*axes_from_yz(v_norm(limb), belly))

anims = {}
# ── 1H slash (sword, and the fallback for anything one-handed) ─────────────
k = [
 dict(t=0.00, yaw=0, pitch=0, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"], l_hand=idle_l_hand, l_pole=idle_l_pole),
 dict(t=0.20, yaw=28, pitch=-3, r_hand=(-24, -8, 138), r_pole=(-60, -15, 120), r_prop=swordq((-0.35, -0.5, 0.8), (0.4, 0.85, 0.2)), l_hand=(20, 22, 98), l_pole=(50, 0, 100)),
 dict(t=0.32, yaw=-12, pitch=6, r_hand=(-4, 40, 112), r_pole=(-45, 10, 90), r_prop=swordq((0.55, 0.75, -0.2), (0.3, -0.1, -0.95)), l_hand=(20, 0, 94), l_pole=(50, -10, 100)),
 dict(t=0.44, yaw=-26, pitch=8, r_hand=(18, 26, 94), r_pole=(-30, 30, 80), r_prop=swordq((0.75, -0.1, -0.6), (0.2, -0.9, 0.3)), l_hand=(20, -6, 94), l_pole=(50, -20, 100)),
 dict(t=0.88, yaw=0, pitch=0, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"], l_hand=idle_l_hand, l_pole=idle_l_pole),
]
anims["MH_Attack_Sword"] = build(k, 0.88, gripR, right_grip="OneHand")
# ── mace / totem overhead chop ─────────────────────────────────────────────
k = [
 dict(t=0.00, yaw=0, pitch=0, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"], l_hand=idle_l_hand, l_pole=idle_l_pole),
 dict(t=0.24, yaw=12, pitch=-8, r_hand=(-12, -2, 160), r_pole=(-60, 0, 150), r_prop=swordq((0.05, -0.8, 0.6), (0, 0.6, 0.8)), l_hand=(18, 24, 110), l_pole=(50, 0, 100)),
 dict(t=0.36, yaw=-4, pitch=18, r_hand=(-6, 38, 106), r_pole=(-55, 0, 110), r_prop=swordq((0.0, 0.55, -0.83), (0, -0.83, -0.55)), l_hand=(20, 6, 96), l_pole=(50, -10, 100)),
 dict(t=0.46, yaw=-6, pitch=22, r_hand=(-6, 32, 92), r_pole=(-55, 0, 100), r_prop=swordq((0.0, 0.25, -0.97), (0, -0.97, -0.25)), l_hand=(20, 2, 94), l_pole=(50, -10, 100)),
 dict(t=0.92, yaw=0, pitch=0, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"], l_hand=idle_l_hand, l_pole=idle_l_pole),
]
anims["MH_Attack_Mace"] = build(k, 0.92, gripR, right_grip="OneHand")
# ── dagger stab ─────────────────────────────────────────────────────────────
k = [
 dict(t=0.00, yaw=0, pitch=0, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"]),
 dict(t=0.14, yaw=16, pitch=0, r_hand=(-20, -8, 100), r_pole=(-55, -30, 95), r_prop=swordq((0.0, 1.0, 0.05), (0, 0, 1))),
 dict(t=0.24, yaw=-12, pitch=7, r_hand=(-5, 44, 112), r_pole=(-50, 0, 90), r_prop=swordq((0.05, 1.0, -0.05), (0, 0, 1))),
 dict(t=0.34, yaw=-10, pitch=6, r_hand=(-5, 40, 111), r_pole=(-50, 0, 90), r_prop=swordq((0.05, 1.0, -0.1), (0, 0, 1))),
 dict(t=0.62, yaw=0, pitch=0, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"]),
]
anims["MH_Attack_Dagger"] = build(k, 0.62, gripR, right_grip="OneHand")

# ── staff two-hand thrust ───────────────────────────────────────────────────
STAFF_L_OFFSET = 30.0   # left hand this far up the shaft from the right fist (unscaled cm)
def staff_extra(p, tm, keys=None):
    pass
def build_staff(keys, length):
    frames = build(keys, length, gripR, right_grip="Staff")
    out = []
    for fi, p in enumerate(frames):
        tm = fi / FPS
        w = sample(keys, "l_on", tm, "f") or 0.0
        if w > 0.001:
            ppos, pq = prop_world(p, "Staff")
            head = v_mul(q_rot(pq, (0, 1, 0)), -1)          # shaft head direction
            target = v_add(ppos, v_mul(head, STAFF_L_OFFSET))
            cur = p.wpos("hand_l")
            tgt = v_lerp(cur, target, w)
            p.two_bone_ik("upperarm_l", "lowerarm_l", "hand_l", tgt, (45, 10, 100))
            # fist around the shaft, knuckles up-out
            side = v_norm(v_cross(head, U)) if abs(v_dot(head, U)) < 0.95 else Lf
            hq_cur = p.wrot("hand_l")
            set_hand_world(p, "l", v_norm(v_cross(head, (0, 0, -1)) if abs(v_dot(head, U)) < 0.95 else F), v_norm(v_mul(side, -1)), ALONG_L, PALMN_L)
            hq_new = p.wrot("hand_l")
            p.set_world_rot("hand_l", q_slerp(hq_cur, hq_new, w))
            grip(p, "l", strength=w)
        out.append(p)
    return out
k = [
 dict(t=0.00, yaw=0, pitch=0, l_on=0.0, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["Staff"]),
 dict(t=0.24, yaw=22, pitch=-2, l_on=1.0, r_hand=(-18, -8, 102), r_pole=(-55, -20, 95), r_prop=staffq((0.25, 0.85, 0.45), (-1, 0, 0))),
 dict(t=0.38, yaw=-8, pitch=8, l_on=1.0, r_hand=(-8, 30, 110), r_pole=(-50, 0, 95), r_prop=staffq((0.15, 0.95, 0.25), (-1, 0, 0))),
 dict(t=0.50, yaw=-6, pitch=6, l_on=1.0, r_hand=(-8, 27, 109), r_pole=(-50, 0, 95), r_prop=staffq((0.15, 0.95, 0.28), (-1, 0, 0))),
 dict(t=0.95, yaw=0, pitch=0, l_on=0.0, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["Staff"]),
]
anims["MH_Attack_Staff"] = build_staff(k, 0.95)

# ── bow: raise, draw to the cheek, release ──────────────────────────────────
aimq = bowq((0.15, 0.0, 1.0), F)
def bow_extra(p, tm):
    if 0.18 < tm < 1.0: pinch(p, "r")
k = [
 dict(t=0.00, yaw=0, pitch=0, l_hand=idle_l_hand, l_pole=idle_l_pole, l_prop=idle_prop["Bow"], r_hand=idle_r_hand, r_pole=idle_r_pole, r_handq=idle_r_handq),
 dict(t=0.26, yaw=35, pitch=0, l_hand=(6, 38, 128), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(0, 30, 128), r_pole=(-45, -10, 120), r_handq=idle_r_handq),
 dict(t=0.56, yaw=38, pitch=0, l_hand=(6, 40, 129), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(-7, 6, 150), r_pole=(-50, -30, 140), r_handq=idle_r_handq),
 dict(t=0.70, yaw=38, pitch=0, l_hand=(6, 40, 129), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(-7, 5, 150), r_pole=(-50, -30, 140), r_handq=idle_r_handq),
 dict(t=0.78, yaw=36, pitch=0, l_hand=(6, 40, 129), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(-16, -8, 150), r_pole=(-50, -40, 140), r_handq=idle_r_handq),
 dict(t=1.15, yaw=0, pitch=0, l_hand=idle_l_hand, l_pole=idle_l_pole, l_prop=idle_prop["Bow"], r_hand=idle_r_hand, r_pole=idle_r_pole, r_handq=idle_r_handq),
]
anims["MH_Attack_Bow"] = build(k, 1.15, gripL, left_grip="Bow", extra=bow_extra)

# ── bow skill with a cast time (Aimed Shot): draw and hold, then release ────
k = [
 dict(t=0.00, yaw=0, pitch=0, l_hand=idle_l_hand, l_pole=idle_l_pole, l_prop=idle_prop["Bow"], r_hand=idle_r_hand, r_pole=idle_r_pole, r_handq=idle_r_handq),
 dict(t=0.26, yaw=35, pitch=0, l_hand=(6, 38, 128), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(0, 30, 128), r_pole=(-45, -10, 120), r_handq=idle_r_handq),
 dict(t=0.56, yaw=38, pitch=0, l_hand=(6, 40, 129), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(-7, 6, 150), r_pole=(-50, -30, 140), r_handq=idle_r_handq),
]
# the last frame is the held full draw (a non-looping player clamps on it)
anims["MH_Bow_Draw"] = build(k, 0.56, gripL, left_grip="Bow", extra=bow_extra)
def bow_release_extra(p, tm):
    if tm < 0.06: pinch(p, "r")
k = [
 dict(t=0.00, yaw=38, pitch=0, l_hand=(6, 40, 129), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(-7, 6, 150), r_pole=(-50, -30, 140), r_handq=idle_r_handq),
 dict(t=0.08, yaw=36, pitch=0, l_hand=(6, 40, 129), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(-16, -8, 150), r_pole=(-50, -40, 140), r_handq=idle_r_handq),
 dict(t=0.20, yaw=34, pitch=0, l_hand=(6, 39, 128), l_pole=(50, 10, 110), l_prop=aimq, r_hand=(-17, -9, 148), r_pole=(-50, -40, 140), r_handq=idle_r_handq),
 dict(t=0.55, yaw=0, pitch=0, l_hand=idle_l_hand, l_pole=idle_l_pole, l_prop=idle_prop["Bow"], r_hand=idle_r_hand, r_pole=idle_r_pole, r_handq=idle_r_handq),
]
anims["MH_Bow_Release"] = build(k, 0.55, gripL, left_grip="Bow", extra=bow_release_extra)

# ── spell casting: gather and hold while the cast bar runs, then push ──────
def palmq(side, along, palmN):
    al, nl = (ALONG_R, PALMN_R) if side == "r" else (ALONG_L, PALMN_L)
    Fl = axes_from_xy(al, nl); Fw = axes_from_xy(v_norm(along), v_norm(palmN))
    return q_mul(q_from_axes(*Fw), q_conj(q_from_axes(*Fl)))
idle_l_handq0 = base.wrot("hand_l"); idle_r_handq0 = base.wrot("hand_r")
# hands cupped around a gathering light in front of the chest, palms facing
GATHER_R = dict(r_hand=(-7, 29, 112), r_pole=(-45, -5, 90), r_handq=palmq("r", (0.1, 0.55, 0.83), (1, 0.1, 0)))
GATHER_L = dict(l_hand=(7, 29, 112), l_pole=(45, -5, 90), l_handq=palmq("l", (-0.1, 0.55, 0.83), (-1, 0.1, 0)))
PULL_R = dict(r_hand=(-8, 21, 115), r_pole=(-45, -10, 92), r_handq=palmq("r", (0.1, 0.4, 0.9), (1, 0.2, 0)))
PULL_L = dict(l_hand=(8, 21, 115), l_pole=(45, -10, 92), l_handq=palmq("l", (-0.1, 0.4, 0.9), (-1, 0.2, 0)))
PUSH_R = dict(r_hand=(-9, 46, 118), r_pole=(-40, 0, 95), r_handq=palmq("r", (0.05, 0.2, 0.98), (0.1, 1, 0)))
PUSH_L = dict(l_hand=(9, 46, 118), l_pole=(40, 0, 95), l_handq=palmq("l", (-0.05, 0.2, 0.98), (-0.1, 1, 0)))
IDLE_R = dict(r_hand=base.wpos("hand_r"), r_pole=idle_r_pole, r_handq=idle_r_handq0)
IDLE_L = dict(l_hand=base.wpos("hand_l"), l_pole=idle_l_pole, l_handq=idle_l_handq0)
def cup_extra(p, tm, keys):
    c = sample(keys, "cup", tm, "f") or 0.0
    if c > 0.01:
        grip(p, "r", strength=0.28*c, thumb=False); grip(p, "l", strength=0.28*c, thumb=False)
def K(t, yaw=0, pitch=0, cup=0.0, *parts):
    d = dict(t=t, yaw=yaw, pitch=pitch, cup=cup)
    for p_ in parts: d.update(p_)
    return d
def jitter(d, dr, dl):
    e = dict(d)
    e["r_hand"] = v_add(d["r_hand"], dr); e["l_hand"] = v_add(d["l_hand"], dl); return e
# channel loop (2.0 s): the cupped hands drift in a slow opposing circle; first == last
loop = []
for i in range(5):
    t = i * 0.5; a = i * math.pi / 2
    dr = (1.5*math.cos(a), 1.0*math.sin(a), 2.0*math.sin(a)); dl = (-1.5*math.cos(a), -1.0*math.sin(a), -2.0*math.sin(a))
    loop.append(jitter(K(t, 0, 4, 1.0, GATHER_R, GATHER_L), dr, dl))
anims["MH_Cast_Channel"] = build(loop, 2.0, base, extra=lambda p, tm: cup_extra(p, tm, loop))
k = [K(0.00, 0, 4, 1.0, GATHER_R, GATHER_L), K(0.12, 0, 0, 1.0, PULL_R, PULL_L), K(0.22, 0, 7, 0.0, PUSH_R, PUSH_L),
     K(0.36, 0, 6, 0.0, PUSH_R, PUSH_L), K(0.80, 0, 0, 0.0, IDLE_R, IDLE_L)]
anims["MH_Cast_Release"] = build(k, 0.80, base, extra=lambda p, tm: cup_extra(p, tm, k))
ki = [K(0.00, 0, 0, 0.0, IDLE_R, IDLE_L), K(0.20, 0, 4, 1.0, GATHER_R, GATHER_L), K(0.30, 0, 0, 1.0, PULL_R, PULL_L),
      K(0.40, 0, 7, 0.0, PUSH_R, PUSH_L), K(0.52, 0, 6, 0.0, PUSH_R, PUSH_L), K(0.95, 0, 0, 0.0, IDLE_R, IDLE_L)]
anims["MH_Cast_Instant"] = build(ki, 0.95, base, extra=lambda p, tm: cup_extra(p, tm, ki))

# staff held: raise it upright before you, off hand open toward the target, then tip it forward
S_CH_R = dict(r_hand=(-11, 24, 117), r_pole=(-45, -5, 95), r_prop=staffq((0.0, 0.1, 1.0), (-1, 0, 0)))
S_CH_L = dict(l_hand=(11, 33, 120), l_pole=(45, -5, 95), l_handq=palmq("l", (-0.05, 0.25, 0.97), (-0.15, 1, 0)))
S_PU_R = dict(r_hand=(-11, 18, 120), r_pole=(-45, -10, 98), r_prop=staffq((0.0, -0.15, 1.0), (-1, 0, 0)))
S_RE_R = dict(r_hand=(-10, 36, 121), r_pole=(-45, 0, 98), r_prop=staffq((0.0, 0.75, 0.66), (-1, 0, 0)))
S_RE_L = dict(l_hand=(10, 46, 119), l_pole=(40, 0, 95), l_handq=palmq("l", (-0.05, 0.2, 0.98), (-0.1, 1, 0)))
S_ID_R = dict(r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["Staff"])
loop = []
for i in range(5):
    t = i * 0.5; a = i * math.pi / 2
    e = K(t, 0, 3, 0.0, S_CH_R, S_CH_L); e["l_hand"] = v_add(S_CH_L["l_hand"], (0.8*math.cos(a), 1.2*math.sin(a), 1.5*math.sin(a)))
    loop.append(e)
anims["MH_Cast_Channel_Staff"] = build(loop, 2.0, gripR, right_grip="Staff")
k = [K(0.00, 0, 3, 0.0, S_CH_R, S_CH_L), K(0.12, 0, 0, 0.0, S_PU_R, S_CH_L), K(0.24, 0, 8, 0.0, S_RE_R, S_RE_L),
     K(0.38, 0, 7, 0.0, S_RE_R, S_RE_L), K(0.85, 0, 0, 0.0, S_ID_R, IDLE_L)]
anims["MH_Cast_Release_Staff"] = build(k, 0.85, gripR, right_grip="Staff")
ki = [K(0.00, 0, 0, 0.0, S_ID_R, IDLE_L), K(0.22, 0, 3, 0.0, S_CH_R, S_CH_L), K(0.32, 0, 0, 0.0, S_PU_R, S_CH_L),
      K(0.44, 0, 8, 0.0, S_RE_R, S_RE_L), K(0.56, 0, 7, 0.0, S_RE_R, S_RE_L), K(1.00, 0, 0, 0.0, S_ID_R, IDLE_L)]
anims["MH_Cast_Instant_Staff"] = build(ki, 1.00, gripR, right_grip="Staff")

# ── report + save ───────────────────────────────────────────────────────────
rep = []
for g, (bone, rl, rq, sc) in grips.items():
    rep.append("%s bone=%s loc=(%.2f, %.2f, %.2f) rot=(%.2f, %.2f, %.2f) scale=%.2f" % ((g, bone) + tuple(rl) + quat_to_rotator(rq) + (sc,)))
json.dump({g: {"bone": b, "loc": rl, "rot_pyr": quat_to_rotator(rq), "scale": sc} for g, (b, rl, rq, sc) in grips.items()}, open(os.path.join(OUT, "grips.json"), "w"), indent=1)
def dump(name, frames):
    json.dump({"fps": FPS, "names": frames[0].names, "frames": [f.local for f in frames]}, open(os.path.join(OUT, name + ".json"), "w"))
dump("MHP_GripR", [gripR]); dump("MHP_GripL", [gripL]); dump("MHP_ShieldArm", [shieldP])
for n, fr in anims.items():
    dump(n, fr)
    # reach check: hand where asked?
    rep.append("%s frames=%d" % (n, len(fr)))
# roundtrip check of rotator conversion
q = rotator_to_quat(*quat_to_rotator(grips["OneHand"][2])); rep.append("rot roundtrip dot=%.6f" % abs(sum(a*b for a, b in zip(q, grips["OneHand"][2]))))
# diagnostics
ph, pq = prop_world(shieldP, "Shield")
rep.append("shield center=%s face=%s  hand_l=%s elbow=%s" % (tuple(round(c, 1) for c in ph), tuple(round(c, 2) for c in q_rot(pq, (0, 0, 1))), tuple(round(c, 1) for c in shieldP.wpos("hand_l")), tuple(round(c, 1) for c in shieldP.wpos("lowerarm_l"))))
f = anims["MH_Attack_Sword"][int(0.32*FPS)]; ph, pq = prop_world(f, "OneHand")
rep.append("sword strike: hand=%s blade=%s" % (tuple(round(c, 1) for c in f.wpos("hand_r")), tuple(round(c, 2) for c in v_mul(q_rot(pq, (0, 1, 0)), -1))))
print("\n".join(rep))
open(os.path.join(OUT, "report.txt"), "w").write("\n".join(rep))
