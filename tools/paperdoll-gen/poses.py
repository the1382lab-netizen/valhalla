"""
Animation cycles -> per-frame pose dictionaries.

These are WEAPON-AGNOSTIC. Every layer sheet (body and every item) contains the
same five cycles in the same columns, so any item can be worn with any other.
The game picks which cycle to play from the equipped weapon:

    sword / mace / axe   -> 'attack'
    bow / crossbow       -> 'shoot'
    staff / spell        -> 'cast'
"""
import math
from parts import S_HW, HIP_V, FOOT_V

REST_HAND_L = S_HW + 0.7
REST_HAND_V = 15.6
REST_HAND_D = 1.0

ANIMS = [("idle", 2), ("walk", 6), ("attack", 4), ("shoot", 4), ("cast", 4)]
FRAMES_PER_DIR = sum(n for _, n in ANIMS)          # 20


def base_pose():
    return dict(
        bob=0.0, lean=0.0, sway=0.0,
        legR=(0.0, 0.0), legL=(0.0, 0.0),
        handR=(REST_HAND_L, REST_HAND_D, REST_HAND_V),
        handL=(-REST_HAND_L, REST_HAND_D, REST_HAND_V),
        headD=0.0, headV=0.0,
        wep_dir=(0.16, 0.34, 0.93),
        bow_pull=0.0, glow_t=0.0, cast_t=0.0, slash_t=0.0,
    )


# ------------------------------------------------------------------ idle
def pose_idle(i):
    p = base_pose()
    breathe = (0.0, -0.45)[i]
    p["bob"] = breathe
    p["handR"] = (REST_HAND_L, REST_HAND_D, REST_HAND_V + breathe * 0.6)
    p["handL"] = (-REST_HAND_L - 0.9, REST_HAND_D + 2.0, REST_HAND_V + 2.6 + breathe * 0.6)
    p["headV"] = breathe * 0.4
    p["glow_t"] = (0.10, 0.22)[i]
    p["wep_dir"] = (0.08, 0.16, 0.98)
    return p


# ------------------------------------------------------------------ walk
def pose_walk(i, n=6):
    p = base_pose()
    ph = 2 * math.pi * i / n
    sw = math.sin(ph)
    p["legR"] = (sw * 3.5, max(0.0, math.cos(ph)) * 1.3)
    p["legL"] = (-sw * 3.5, max(0.0, -math.cos(ph)) * 1.3)
    p["bob"] = 0.55 * abs(sw) - 0.28
    p["lean"] = 0.35
    p["sway"] = sw * 0.9
    p["handR"] = (REST_HAND_L, REST_HAND_D - sw * 2.3, REST_HAND_V + abs(sw) * 0.5)
    p["handL"] = (-REST_HAND_L - 0.9, REST_HAND_D + 2.0 + sw * 1.4, REST_HAND_V + 2.6)
    p["headV"] = -p["bob"] * 0.25
    p["glow_t"] = 0.14 + 0.10 * abs(sw)
    p["wep_dir"] = (0.08, 0.16, 0.98)
    return p


# ------------------------------------------------------------------ attack (melee)
_SWING = [
    # (wep_dir, handR, lean, slash)
    ((0.80, -0.55, 0.72), (S_HW + 1.6, -2.6, 18.6), -0.9, 0.0),
    ((0.34, 0.62, 0.72), (S_HW + 0.4, 1.6, 19.0), 0.4, 0.0),
    ((-0.42, 1.00, -0.22), (S_HW - 2.4, 5.0, 14.6), 1.7, 1.0),
    ((-0.12, 0.80, 0.36), (S_HW - 0.4, 3.0, 15.4), 0.8, 0.35),
]


def pose_attack(i):
    p = base_pose()
    d, hr, lean, slash = _SWING[i]
    p["wep_dir"], p["handR"], p["lean"], p["slash_t"] = d, hr, lean, slash
    p["handL"] = (-REST_HAND_L - 0.4, REST_HAND_D + (1.6 if i >= 2 else -0.6), REST_HAND_V + 1.0)
    p["legR"] = (2.2 if i >= 2 else -1.0, 0.0)
    p["legL"] = (-2.0 if i >= 2 else 1.0, 0.0)
    p["bob"] = (-0.3, 0.1, -0.5, -0.2)[i]
    p["headD"] = lean * 0.3
    p["glow_t"] = 0.16
    return p


# ------------------------------------------------------------------ shoot (bow)
_BOW = [
    # (handL, handR, pull, lean)
    ((-4.6, 4.6, 19.0), (1.6, 0.6, 19.0), 0.00, 0.0),
    ((-4.4, 5.4, 19.2), (2.2, -2.0, 19.4), 0.55, -0.5),
    ((-4.4, 5.6, 19.2), (2.6, -3.4, 19.5), 0.95, -0.8),
    ((-4.6, 5.0, 19.0), (1.4, 1.8, 19.0), 0.00, 0.9),
]


def pose_shoot(i):
    p = base_pose()
    hl, hr, pull, lean = _BOW[i]
    p["handL"], p["handR"] = hl, hr
    p["bow_pull"] = pull
    p["lean"] = lean
    p["legR"] = (1.6, 0.0)
    p["legL"] = (-1.4, 0.0)
    p["bob"] = -0.2
    p["wep_dir"] = (0.20, 0.62, 0.76)
    p["glow_t"] = 0.16
    return p


# ------------------------------------------------------------------ cast / channel
def pose_cast(i):
    p = base_pose()
    t = (0.28, 0.58, 0.88, 1.0)[i]
    lift = 1.4 + t * 3.6
    p["cast_t"] = t
    p["glow_t"] = t
    p["bob"] = -0.2 + t * 0.5
    p["lean"] = 0.5
    p["headV"] = 0.3 * t
    p["handR"] = (REST_HAND_L - 1.0 * t, REST_HAND_D + 2.2 * t, REST_HAND_V + lift)
    p["handL"] = (-REST_HAND_L + 1.6 * t, REST_HAND_D + 3.4 * t, REST_HAND_V + lift)
    p["legR"] = (0.8, 0.0)
    p["legL"] = (-0.8, 0.0)
    p["wep_dir"] = (0.08, 0.20, 0.97)
    return p


POSE_FN = {"idle": pose_idle, "walk": pose_walk, "attack": pose_attack,
           "shoot": pose_shoot, "cast": pose_cast}


def all_frames():
    """Ordered list of (anim, index, pose) for one direction row."""
    out = []
    for name, n in ANIMS:
        for i in range(n):
            out.append((name, i, POSE_FN[name](i)))
    return out
