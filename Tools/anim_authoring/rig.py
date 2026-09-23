"""Character-space helpers for the MetaHuman base (mesh space, unscaled cm):
forward = +Y, right = -X, left = +X, up = +Z. The game draws the body at SCALE."""
import math
from qm import *
SCALE = 122.0 / 180.3
F = (0.0, 1.0, 0.0); R = (-1.0, 0.0, 0.0); Lf = (1.0, 0.0, 0.0); U = (0.0, 0.0, 1.0)

def rotator_to_quat(pitch, yaw, roll):
    d = math.pi / 180.0
    sp, cp = math.sin(pitch*d/2), math.cos(pitch*d/2)
    sy, cy = math.sin(yaw*d/2), math.cos(yaw*d/2)
    sr, cr = math.sin(roll*d/2), math.cos(roll*d/2)
    return (cr*sp*sy - sr*cp*cy, -cr*sp*cy - sr*cp*sy, cr*cp*sy - sr*sp*cy, cr*cp*cy + sr*sp*sy)

def quat_to_rotator(q):
    x, y, z, w = q
    sing = z*x - w*y
    yaw_y = 2*(w*z + x*y); yaw_x = 1 - 2*(y*y + z*z)
    r2d = 180.0/math.pi
    if sing < -0.4999995:
        pitch = -90.0; yaw = math.atan2(yaw_y, yaw_x)*r2d; roll = -yaw - 2*math.atan2(x, w)*r2d
    elif sing > 0.4999995:
        pitch = 90.0; yaw = math.atan2(yaw_y, yaw_x)*r2d; roll = yaw - 2*math.atan2(x, w)*r2d
    else:
        pitch = math.asin(2*sing)*r2d; yaw = math.atan2(yaw_y, yaw_x)*r2d
        roll = math.atan2(-2*(w*x + y*z), 1 - 2*(x*x + y*y))*r2d
    return pitch, yaw, roll

FINGERS = ["index", "middle", "ring", "pinky"]

def palm_frame(p, side):
    s = "_" + side
    h = p.wpos("hand" + s)
    along = v_norm(v_sub(p.wpos("middle_01" + s), h))
    across = v_norm(v_sub(p.wpos("index_01" + s), p.wpos("pinky_01" + s)))
    n = v_norm(v_cross(along, across))
    # palm side is where the (slightly curled) finger tips lean
    tipdir = v_sub(p.wpos("middle_03" + s), p.wpos("middle_01" + s))
    if v_dot(tipdir, n) < 0: n = v_mul(n, -1)
    return h, along, across, n

def curl_finger(p, side, finger, angles, palmN):
    s = "_" + side
    bones = ["%s_0%d%s" % (finger, i, s) for i in (1, 2, 3)]
    d0 = v_sub(p.wpos("%s_02%s" % (finger, s)), p.wpos(bones[0]))
    axis = v_norm(v_cross(d0, palmN))
    for b, a in zip(bones, angles):
        p.rotate_world(b, q_axis_angle(axis, math.radians(a)))

def grip(p, side, strength=1.0, thumb=True):
    """Close a hand around a ~3 cm handle. strength 0..1."""
    h, along, across, n = palm_frame(p, side)
    k = strength
    curl_finger(p, side, "index",  [55*k, 75*k, 45*k], n)
    curl_finger(p, side, "middle", [62*k, 80*k, 45*k], n)
    curl_finger(p, side, "ring",   [66*k, 80*k, 45*k], n)
    curl_finger(p, side, "pinky",  [70*k, 78*k, 45*k], n)
    if thumb:
        s = "_" + side
        # thumb swings across toward the index knuckle, then wraps
        t1 = "thumb_01" + s
        d = v_sub(p.wpos("thumb_02" + s), p.wpos(t1))
        tgt = v_add(v_mul(v_norm(d), 0.6), v_mul(across, 0.25 * (1 if side == "r" else 1)))
        tgt = v_add(tgt, v_mul(n, 0.45))
        dq = q_between(d, tgt)
        p.rotate_world(t1, q_slerp((0, 0, 0, 1), dq, k))
        d2 = v_sub(p.wpos("thumb_03" + s), p.wpos("thumb_02" + s))
        ax = v_norm(v_cross(d2, n))
        p.rotate_world("thumb_02" + s, q_axis_angle(ax, math.radians(28*k)))
        p.rotate_world("thumb_03" + s, q_axis_angle(ax, math.radians(30*k)))

def pinch(p, side):
    """Index and middle hooked on a bowstring, thumb resting: a draw hand."""
    h, along, across, n = palm_frame(p, side)
    curl_finger(p, side, "index",  [20, 55, 35], n)
    curl_finger(p, side, "middle", [22, 55, 35], n)
    curl_finger(p, side, "ring",   [65, 85, 45], n)
    curl_finger(p, side, "pinky",  [70, 85, 45], n)

def local_frame_in_world(p, bone, axes_local):
    return [q_rot(p.wrot(bone), a) for a in axes_local]

def hand_axes_local(p, side):
    """The palm frame (along, palmN) expressed in the hand bone's local space."""
    h, along, across, n = palm_frame(p, side)
    qi = q_conj(p.wrot("hand_" + side))
    return q_rot(qi, along), q_rot(qi, n)

def set_hand_world(p, side, along_w, palmN_w, along_l, palmN_l):
    """Rotate the hand so its local palm frame (along_l, palmN_l) lands on (along_w, palmN_w)."""
    Fl = axes_from_xy(along_l, palmN_l); Fw = axes_from_xy(along_w, palmN_w)
    qw = q_mul(q_from_axes(*Fw), q_conj(q_from_axes(*Fl)))
    p.set_world_rot("hand_" + side, qw)
