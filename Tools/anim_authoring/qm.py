"""Tiny quaternion/vector math (UE conventions: Q = (x, y, z, w), world = parent * local)."""
import math

def v_add(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def v_sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def v_mul(a, s): return (a[0]*s, a[1]*s, a[2]*s)
def v_dot(a, b): return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
def v_cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def v_len(a): return math.sqrt(v_dot(a, a))
def v_norm(a):
    l = v_len(a)
    return (a[0]/l, a[1]/l, a[2]/l) if l > 1e-9 else (0.0, 0.0, 0.0)
def v_lerp(a, b, t): return (a[0]+(b[0]-a[0])*t, a[1]+(b[1]-a[1])*t, a[2]+(b[2]-a[2])*t)

def q_mul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx + ax*bw + ay*bz - az*by,
            aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw,
            aw*bw - ax*bx - ay*by - az*bz)
def q_conj(q): return (-q[0], -q[1], -q[2], q[3])
def q_norm(q):
    l = math.sqrt(sum(c*c for c in q)); return tuple(c/l for c in q)
def q_rot(q, v):
    p = q_mul(q_mul(q, (v[0], v[1], v[2], 0.0)), q_conj(q)); return (p[0], p[1], p[2])
def q_axis_angle(axis, ang):
    ax = v_norm(axis); s = math.sin(ang/2)
    return (ax[0]*s, ax[1]*s, ax[2]*s, math.cos(ang/2))
def q_between(a, b):
    """Minimal rotation taking direction a to direction b."""
    a = v_norm(a); b = v_norm(b); d = v_dot(a, b)
    if d > 0.999999: return (0.0, 0.0, 0.0, 1.0)
    if d < -0.999999:
        ax = v_cross((1, 0, 0), a)
        if v_len(ax) < 1e-6: ax = v_cross((0, 1, 0), a)
        return q_axis_angle(ax, math.pi)
    c = v_cross(a, b)
    return q_norm((c[0], c[1], c[2], 1.0 + d))
def q_slerp(a, b, t):
    d = sum(x*y for x, y in zip(a, b))
    if d < 0: b = tuple(-c for c in b); d = -d
    if d > 0.9995: return q_norm(tuple(x + (y-x)*t for x, y in zip(a, b)))
    th = math.acos(d); s = math.sin(th)
    wa = math.sin((1-t)*th)/s; wb = math.sin(t*th)/s
    return tuple(wa*x + wb*y for x, y in zip(a, b))
def q_from_axes(x, y, z):
    """Quaternion whose local X/Y/Z map to the given orthonormal world axes."""
    m00, m01, m02 = x[0], y[0], z[0]
    m10, m11, m12 = x[1], y[1], z[1]
    m20, m21, m22 = x[2], y[2], z[2]
    tr = m00 + m11 + m22
    if tr > 0:
        s = math.sqrt(tr + 1.0)*2; w = 0.25*s
        qx = (m21 - m12)/s; qy = (m02 - m20)/s; qz = (m10 - m01)/s
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22)*2; w = (m21 - m12)/s
        qx = 0.25*s; qy = (m01 + m10)/s; qz = (m02 + m20)/s
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22)*2; w = (m02 - m20)/s
        qx = (m01 + m10)/s; qy = 0.25*s; qz = (m12 + m21)/s
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11)*2; w = (m10 - m01)/s
        qx = (m02 + m20)/s; qy = (m12 + m21)/s; qz = 0.25*s
    return q_norm((qx, qy, qz, w))
def axes_from_yz(y, z):
    """Orthonormal frame with Y exact and Z as close to z as possible (UE MakeRotFromYZ)."""
    y = v_norm(y); x = v_norm(v_cross(y, z)); z2 = v_cross(x, y)
    return x, y, z2
def axes_from_xy(x, y):
    x = v_norm(x); z = v_norm(v_cross(x, y)); y2 = v_cross(z, x)
    return x, y2, z
def ease(t): return t*t*(3 - 2*t)
