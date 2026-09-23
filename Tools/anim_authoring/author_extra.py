"""A-030 on the MetaHuman: sit, emotes (wave / cheer / bow), two-handed
attack, block and dodge, hand-keyed on the same engine as author.py (which
this imports and re-runs; its outputs are deterministic).

Adds what author.build() has no keys for: a pelvis offset/tilt (``pel``,
``pel_pitch``) and planted-foot leg IK (``r_foot`` / ``l_foot`` targets,
``r_knee`` / ``l_knee`` poles; default = the idle's own feet, so a lowered or
shifted pelvis bends the knees instead of sinking the feet).

    python author_extra.py      -> out/MH_Sit.json ... and out/extra_names.txt
"""
import json, math, os
from author import *          # noqa: F401,F403  (engine, base poses, grips, K, palmq, dump ...)

EXTRA = {}
FOOT = {s: base.wpos("foot_" + s) for s in "rl"}
FOOTQ = {s: base.wrot("foot_" + s) for s in "rl"}
BALLQ = {s: base.wrot("ball_" + s) for s in "rl"}
PEL0 = base.wpos("pelvis")


def build_body(keys, length, start_pose, right_grip=None, left_grip=None, extra=None, legs=True):
    frames = []
    n = int(round(length * FPS)) + 1
    for fi in range(n):
        tm = fi / FPS
        p = cp(start_pose)
        pel = sample(keys, "pel", tm, "pos")
        if pel:
            t0, q0 = p.local[p.idx["pelvis"]]
            p.set_local_pos("pelvis", v_add(t0, pel))
        pp = sample(keys, "pel_pitch", tm, "f")
        if pp:
            p.rotate_world("pelvis", q_axis_angle(R, math.radians(pp)))
        pr = sample(keys, "pel_roll", tm, "f")
        if pr:
            p.rotate_world("pelvis", q_axis_angle(F, math.radians(pr)))
        if legs:
            for s in "rl":
                tgt = sample(keys, s + "_foot", tm, "pos") or FOOT[s]
                knee = sample(keys, s + "_knee", tm, "lin") or v_add(p.wpos("calf_" + s), (0, 40, 0))
                p.two_bone_ik("thigh_" + s, "calf_" + s, "foot_" + s, tgt, knee)
                fq = sample(keys, s + "_footq", tm, "q")
                p.set_world_rot("foot_" + s, fq or FOOTQ[s])
        apply_spine(p, sample(keys, "yaw", tm, "f") or 0.0, sample(keys, "pitch", tm, "f") or 0.0,
                    sample(keys, "roll", tm, "f") or 0.0)
        hp = sample(keys, "head_pitch", tm, "f")
        if hp:
            p.rotate_world("head", q_axis_angle(R, math.radians(hp)))
        for side, g in (("r", right_grip), ("l", left_grip)):
            hpos = sample(keys, side + "_hand", tm, "pos")
            if hpos is None:
                continue
            pole = sample(keys, side + "_pole", tm, "lin") or ((-40, -20, 90) if side == "r" else (40, -20, 90))
            p.two_bone_ik("upperarm_" + side, "lowerarm_" + side, "hand_" + side, hpos, pole)
            pq = sample(keys, side + "_prop", tm, "q")
            if pq is not None and g:
                p.set_world_rot("hand_" + side, hand_rot_for_prop(g, pq))
            hq = sample(keys, side + "_handq", tm, "q")
            if hq is not None:
                p.set_world_rot("hand_" + side, hq)
        if extra:
            extra(p, tm)
        frames.append(p)
    return frames


REST = dict(pel=(0.0, 0.0, 0.0), yaw=0.0, pitch=0.0, roll=0.0, pel_pitch=0.0, pel_roll=0.0, head_pitch=0.0,
            r_foot=FOOT["r"], l_foot=FOOT["l"])


def D(t, **kw):
    kw["t"] = t
    return kw


def R0(t, **kw):
    """A key at rest (every body channel back to the idle), plus arms/props."""
    d = dict(REST)
    d.update(kw)
    d["t"] = t
    return d


IR = dict(r_hand=idle_r_hand, r_pole=idle_r_pole)
IL = dict(l_hand=idle_l_hand, l_pole=idle_l_pole)
IRq = dict(IR, r_handq=idle_r_handq0)
ILq = dict(IL, l_handq=idle_l_handq0)


def M(*parts, **kw):
    d = {}
    for p_ in parts:
        d.update(p_)
    d.update(kw)
    return d

# ── sit on the ground (EQ /sit): down in 0.8 s, then hold the last frame ───
SIT_PEL = (0.0, -6.0, 18.0 - PEL0[2])           # pelvis 18 cm off the ground, a little back
SIT_FOOT_R = (-14.0, 34.0, 8.0)
SIT_FOOT_L = (15.0, 38.0, 8.0)
SIT_KNEE_R = (-22.0, 40.0, 90.0)
SIT_KNEE_L = (22.0, 40.0, 90.0)
def sit_extra(p, tm):
    w = min(1.0, max(0.0, (tm - 0.35) / 0.45))
    if w <= 0.0:
        return
    w = ease(w)
    for s, sg in (("r", -1), ("l", 1)):
        knee = p.wpos("calf_" + s)
        tgt = v_add(knee, (sg * 2.0, 6.0, 4.0))
        cur = p.wpos("hand_" + s)
        p.two_bone_ik("upperarm_" + s, "lowerarm_" + s, "hand_" + s, v_lerp(cur, tgt, w),
                      v_add(p.wpos("lowerarm_" + s), (sg * 30.0, -10.0, 0.0)))
        grip(p, s, strength=0.25 * w, thumb=False)
k = [R0(0.00, **IRq, **ILq),
     D(0.35, pel=(0, -2, -30), pitch=12, r_foot=v_lerp(FOOT["r"], SIT_FOOT_R, 0.4), l_foot=v_lerp(FOOT["l"], SIT_FOOT_L, 0.4),
       r_knee=SIT_KNEE_R, l_knee=SIT_KNEE_L, **IRq, **ILq),
     D(0.80, pel=SIT_PEL, pitch=14, head_pitch=-6, r_foot=SIT_FOOT_R, l_foot=SIT_FOOT_L, r_knee=SIT_KNEE_R, l_knee=SIT_KNEE_L,
       **IRq, **ILq)]
EXTRA["MH_Sit"] = build_body(k, 0.80, base, extra=sit_extra)

# ── wave: right hand up by the head, three side-to-side waves ──────────────
WAVE_UP = (-42.0, 14.0, 156.0)
wave_q_a = palmq("r", (-0.25, 0.1, 0.96), (0.05, 1, 0))
wave_q_b = palmq("r", (0.25, 0.1, 0.96), (-0.05, 1, 0))
k = [R0(0.00, **IRq, **ILq)]
k.append(D(0.35, yaw=-6, r_hand=WAVE_UP, r_pole=(-70, -20, 105), r_handq=wave_q_a, **ILq))
for i, t in enumerate((0.60, 0.85, 1.10, 1.35, 1.60)):
    k.append(D(t, yaw=-6, r_hand=v_add(WAVE_UP, ((5.0 if i % 2 == 0 else -5.0), 0, 0)), r_pole=(-70, -20, 105),
               r_handq=wave_q_b if i % 2 == 0 else wave_q_a, **ILq))
k.append(R0(2.10, **IRq, **ILq))
EXTRA["MH_Emote_Wave"] = build_body(k, 2.10, base, legs=False)

# ── cheer: both fists thrown up, a little bounce, twice ─────────────────────
UP_R, UP_L = (-22.0, 8.0, 170.0), (22.0, 8.0, 170.0)
def cheer_extra(p, tm):
    if 0.2 < tm < 1.5:
        grip(p, "r"); grip(p, "l")
k = [R0(0.00, **IRq, **ILq),
     D(0.25, pel=(0, 0, -6), pitch=-4, r_hand=UP_R, r_pole=(-60, -10, 140), l_hand=UP_L, l_pole=(60, -10, 140)),
     D(0.45, pel=(0, 0, 2), pitch=-6, r_hand=v_add(UP_R, (0, 0, 6)), r_pole=(-60, -10, 150), l_hand=v_add(UP_L, (0, 0, 6)), l_pole=(60, -10, 150)),
     D(0.70, pel=(0, 0, -5), pitch=-3, r_hand=v_add(UP_R, (-2, 0, -8)), r_pole=(-60, -10, 140), l_hand=v_add(UP_L, (2, 0, -8)), l_pole=(60, -10, 140)),
     D(0.95, pel=(0, 0, 2), pitch=-6, r_hand=v_add(UP_R, (0, 0, 6)), r_pole=(-60, -10, 150), l_hand=v_add(UP_L, (0, 0, 6)), l_pole=(60, -10, 150)),
     D(1.20, pel=(0, 0, -3), pitch=-3, r_hand=UP_R, r_pole=(-60, -10, 140), l_hand=UP_L, l_pole=(60, -10, 140)),
     R0(1.70, **IRq, **ILq)]
EXTRA["MH_Emote_Cheer"] = build_body(k, 1.70, base, extra=cheer_extra)

# ── bow: right hand to the heart, bend from the waist, hold, rise ───────────
HEART = (-6.0, 22.0, 116.0)
heart_q = palmq("r", (0.9, 0.1, 0.4), (0, -1, 0))
k = [R0(0.00, **IRq, **ILq),
     D(0.40, pitch=4, r_hand=HEART, r_pole=(-45, -10, 100), r_handq=heart_q, **ILq),
     D(0.85, pitch=34, pel=(0, -4, -2), head_pitch=10, r_hand="rel", r_pole=(-45, -10, 90), r_handq=heart_q, **ILq),
     D(1.25, pitch=34, pel=(0, -4, -2), head_pitch=10, r_hand="rel", r_pole=(-45, -10, 90), r_handq=heart_q, **ILq),
     D(1.65, pitch=2, r_hand=HEART, r_pole=(-45, -10, 100), r_handq=heart_q, **ILq),
     R0(2.00, **IRq, **ILq)]
# while bent, the hand stays on the chest: key it in chest space
def bow_hand_fix(keys):
    for d in keys:
        if d.get("r_hand") == "rel":
            d.pop("r_hand")
    return keys
k = bow_hand_fix(k)
def bow_extra_hand(p, tm):
    if 0.40 < tm < 1.65:
        w = min(1.0, (tm - 0.40) / 0.1, (1.65 - tm) / 0.1)
        s5 = p.wpos("spine_05")
        tgt = v_add(s5, q_rot(p.wrot("spine_05"), q_rot(q_conj(base.wrot("spine_05")), v_sub(HEART, base.wpos("spine_05")))))
        cur = p.wpos("hand_r")
        p.two_bone_ik("upperarm_r", "lowerarm_r", "hand_r", v_lerp(cur, tgt, w), v_add(p.wpos("lowerarm_r"), (-30, -5, 0)))
        p.set_world_rot("hand_r", q_mul(p.wrot("spine_05"), q_mul(q_conj(base.wrot("spine_05")), heart_q)))
EXTRA["MH_Emote_Bow"] = build_body(k, 2.00, base, extra=bow_extra_hand)

# ── two-handed overhead chop (a greatsword: right hand at the guard, left below) ─
TWO_H_OFFSET = 10.0
def two_hand_extra(p, tm):
    ppos, pq = prop_world(p, "OneHand")
    pommel = q_rot(pq, (0, 1, 0))                 # prop +Y points at the pommel (-blade)
    tgt = v_add(ppos, v_mul(pommel, TWO_H_OFFSET))
    p.two_bone_ik("upperarm_l", "lowerarm_l", "hand_l", tgt, v_add(p.wpos("lowerarm_l"), (30, -10, 0)))
    blade = v_mul(pommel, -1)
    side = v_norm(v_cross(blade, U)) if abs(v_dot(blade, U)) < 0.95 else Lf
    set_hand_world(p, "l", v_norm(v_cross(blade, v_mul(side, -1))), v_mul(side, -1), ALONG_L, PALMN_L)
    grip(p, "l")
k = [
 R0(0.00, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"]),
 D(0.18, yaw=10, pitch=-2, r_hand=(-12, 18, 110), r_pole=(-55, -10, 100), r_prop=swordq((0.0, 0.6, 0.8), (1, 0, 0))),
 D(0.42, yaw=8, pitch=-10, pel=(0, -2, -2), r_hand=(-8, -4, 162), r_pole=(-60, 0, 150), r_prop=swordq((0.05, -0.85, 0.5), (1, 0, 0))),
 D(0.56, yaw=-4, pitch=20, pel=(0, 4, -10), r_hand=(-6, 40, 110), r_pole=(-55, 10, 110), r_prop=swordq((0.0, 0.8, -0.6), (1, 0, 0))),
 D(0.66, yaw=-6, pitch=26, pel=(0, 5, -12), r_hand=(-6, 34, 90), r_pole=(-55, 10, 100), r_prop=swordq((0.0, 0.35, -0.94), (1, 0, 0))),
 R0(1.20, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"]),
]
def two_h_blend(p, tm):
    w = min(1.0, max(0.0, tm / 0.18), max(0.0, (1.20 - tm) / 0.3))
    if w > 0.05:
        cur_l = p.wpos("hand_l"); curq = p.wrot("hand_l")
        two_hand_extra(p, tm)
        if w < 1.0:
            tgt = p.wpos("hand_l"); tq = p.wrot("hand_l")
            p2 = p
            p2.two_bone_ik("upperarm_l", "lowerarm_l", "hand_l", v_lerp(cur_l, tgt, w), v_add(p.wpos("lowerarm_l"), (30, -10, 0)))
            p2.set_world_rot("hand_l", q_slerp(curq, tq, w))
EXTRA["MH_Attack_2H"] = build_body(k, 1.20, gripR, right_grip="OneHand", extra=two_h_blend)

# ── block: guard up (shield arm or weapon across), brace, lower ─────────────
GUARD_L = v_add(shieldP.wpos("hand_l"), (-2.0, 10.0, 22.0))    # the carry pose, raised to the face
GUARD_R = (-14.0, 26.0, 120.0)
k = [R0(0.00, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"], l_hand=shieldP.wpos("hand_l"), l_pole=SH_ELBOW_POLE),
     D(0.12, pel=(0, -3, -4), pitch=-4, r_hand=GUARD_R, r_pole=(-55, -10, 110), r_prop=swordq((0.9, 0.3, 0.3), (0, 1, 0)),
       l_hand=GUARD_L, l_pole=v_add(SH_ELBOW_POLE, (0, 5, 22))),
     D(0.40, pel=(0, -3, -4), pitch=-5, r_hand=GUARD_R, r_pole=(-55, -10, 110), r_prop=swordq((0.9, 0.3, 0.3), (0, 1, 0)),
       l_hand=v_add(GUARD_L, (0, -2, 0)), l_pole=v_add(SH_ELBOW_POLE, (0, 5, 22))),
     R0(0.70, r_hand=idle_r_hand, r_pole=idle_r_pole, r_prop=idle_prop["OneHand"], l_hand=shieldP.wpos("hand_l"), l_pole=SH_ELBOW_POLE)]
SHIELD_FACE = v_norm((0.55, 0.83, 0.08))       # the carry pose's face, tipped a touch up


def twist_shield(p, face):
    """Roll the forearm about itself so the shield (hung on lowerarm_l) faces ``face``:
    arm IK aims the bones but leaves their twist to chance."""
    axis = v_norm(v_sub(p.wpos("hand_l"), p.wpos("lowerarm_l")))
    cur = q_rot(q_mul(p.wrot("lowerarm_l"), grips["Shield"][2]), (0, 0, 1))
    def proj(v):
        return v_norm(v_sub(v, v_mul(axis, v_dot(v, axis))))
    a, b = proj(cur), proj(face)
    ang = math.atan2(v_dot(v_cross(a, b), axis), v_dot(a, b))
    p.rotate_world("lowerarm_l", q_axis_angle(axis, ang))


def block_extra(p, tm):
    twist_shield(p, SHIELD_FACE)
    fa = v_norm(v_sub(p.wpos("hand_l"), p.wpos("lowerarm_l")))
    set_hand_world(p, "l", fa, v_norm((-1.0, 0.15, -0.1)), ALONG_L, PALMN_L)
    grip(p, "l")
EXTRA["MH_Block"] = build_body(k, 0.70, shieldP, right_grip="OneHand", extra=block_extra)

# ── dodge: a quick sidestep to the left and back (in place: no root motion) ─
k = [R0(0.00, **IRq, **ILq),
     D(0.16, pel=(16, 0, -12), roll=-10, pel_roll=-6, yaw=4,
       l_foot=v_add(FOOT["l"], (14, 0, 0)), r_foot=v_add(FOOT["r"], (6, 0, 3)),
       r_hand=v_add(idle_r_hand, (10, 6, 12)), r_pole=(-40, -10, 100), l_hand=v_add(idle_l_hand, (14, 0, 16)), l_pole=(60, -10, 100)),
     D(0.34, pel=(18, 0, -10), roll=-8, pel_roll=-5, yaw=3,
       l_foot=v_add(FOOT["l"], (14, 0, 0)), r_foot=v_add(FOOT["r"], (8, 0, 0)),
       r_hand=v_add(idle_r_hand, (10, 6, 10)), r_pole=(-40, -10, 100), l_hand=v_add(idle_l_hand, (14, 0, 14)), l_pole=(60, -10, 100)),
     R0(0.70, **IRq, **ILq)]
EXTRA["MH_Dodge"] = build_body(k, 0.70, base)

for n, fr in EXTRA.items():
    dump(n, fr)
open(os.path.join(OUT, "extra_names.txt"), "w").write("\n".join(EXTRA))
open(os.path.join(H, "mh_write_only.txt"), "w").write("\n".join(EXTRA))
print("extra:", ", ".join("%s(%d)" % (n, len(f)) for n, f in EXTRA.items()))
for n, fr in EXTRA.items():
    last = fr[-1]
    print(n, "pelvis", tuple(round(c, 1) for c in last.wpos("pelvis")), "foot_r", tuple(round(c, 1) for c in last.wpos("foot_r")),
          "hand_r", tuple(round(c, 1) for c in last.wpos("hand_r")))
