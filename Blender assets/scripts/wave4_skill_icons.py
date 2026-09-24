"""B-15 Wave 4 (A-060): skill icons, painted procedurally.

Each skill gets an emblem built from signed-distance shapes (blades, arrows,
shields, flames, bolts, snowflakes...) and shaded as raised metal (weapon and
martial skills) or as glowing light (spells), on a background washed in the
skill's own ``iconColor`` from skills.json. Full-bleed squares: the HUD slot
supplies the frame. 128 px PNGs in ``Import/UI/Icons/Skills/<skill id>.png``.

    python wave4_skill_icons.py [skill_id ...]
"""

import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(REPO, "Import", "UI", "Icons", "Skills")
N = 256                       # render size; saved at 128
PX = 2.0 / N

yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
ZOOM = 1.18                   # emblems fill more of the tile (they are seen at 36-48 px)
X = ((xx + 0.5) / N * 2 - 1) / ZOOM
Y = (1 - (yy + 0.5) / N * 2) / ZOOM
PX = PX / ZOOM


# ── SDF primitives (coordinates -1..1, y up) ─────────────────────────────────

def circle(c, r):
    return np.hypot(X - c[0], Y - c[1]) - r


def ring(c, r, w):
    return np.abs(circle(c, r)) - w


def seg(a, b, r):
    ax, ay = a
    bx, by = b
    px, py = X - ax, Y - ay
    vx, vy = bx - ax, by - ay
    t = np.clip((px * vx + py * vy) / (vx * vx + vy * vy + 1e-9), 0, 1)
    return np.hypot(px - t * vx, py - t * vy) - r


def poly(pts):
    """Exact SDF of a closed polygon (IQ)."""
    pts = [np.array(p, np.float32) for p in pts]
    d = (X - pts[0][0]) ** 2 + (Y - pts[0][1]) ** 2
    s = np.ones_like(X)
    n = len(pts)
    j = n - 1
    for i in range(n):
        e = pts[j] - pts[i]
        wx, wy = X - pts[i][0], Y - pts[i][1]
        t = np.clip((wx * e[0] + wy * e[1]) / max(float(e @ e), 1e-12), 0, 1)
        bx, by = wx - e[0] * t, wy - e[1] * t
        d = np.minimum(d, bx * bx + by * by)
        c1 = Y >= pts[i][1]
        c2 = Y < pts[j][1]
        c3 = e[0] * wy > e[1] * wx
        flip = (c1 & c2 & c3) | (~c1 & ~c2 & ~c3)
        s = np.where(flip, -s, s)
        j = i
    return s * np.sqrt(d)


def box(c, hw, hh, ang=0.0):
    ca, sa = math.cos(ang), math.sin(ang)
    pts = [(-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh)]
    return poly([(c[0] + x * ca - y * sa, c[1] + x * sa + y * ca) for x, y in pts])


def star(c, n, r1, r2, rot=0.0):
    pts = []
    for k in range(2 * n):
        r = r1 if k % 2 == 0 else r2
        a = rot + math.pi / 2 + k * math.pi / n
        pts.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a)))
    return poly(pts)


def U(*ds):
    out = ds[0]
    for d in ds[1:]:
        out = np.minimum(out, d)
    return out


def sub(a, b):
    return np.maximum(a, -b)


def rot_pts(pts, ang, c=(0, 0)):
    ca, sa = math.cos(ang), math.sin(ang)
    return [(c[0] + (x - c[0]) * ca - (y - c[1]) * sa, c[1] + (x - c[0]) * sa + (y - c[1]) * ca) for x, y in pts]


# ── Motifs ───────────────────────────────────────────────────────────────────

def sword(ang=-math.pi / 4, s=1.0, c=(0, 0)):
    pts = [(0, 0.78), (0.075, 0.62), (0.07, -0.12), (-0.07, -0.12), (-0.075, 0.62)]
    blade = poly(rot_pts([(c[0] + x * s, c[1] + y * s) for x, y in pts], ang, c))
    guard = poly(rot_pts([(c[0] + x * s, c[1] + y * s) for x, y in [(-0.28, -0.12), (0.28, -0.12), (0.28, -0.2), (-0.28, -0.2)]], ang, c))
    grip = poly(rot_pts([(c[0] + x * s, c[1] + y * s) for x, y in [(-0.04, -0.2), (0.04, -0.2), (0.04, -0.5), (-0.04, -0.5)]], ang, c))
    p = rot_pts([(c[0], c[1] - 0.56 * s)], ang, c)[0]
    return U(blade, guard, grip, circle(p, 0.07 * s))


def dagger(ang=-math.pi * 0.75, s=0.9, c=(0, 0)):
    pts = [(0, 0.55), (0.07, 0.35), (0.06, -0.05), (-0.06, -0.05), (-0.07, 0.35)]
    blade = poly(rot_pts([(c[0] + x * s, c[1] + y * s) for x, y in pts], ang, c))
    guard = poly(rot_pts([(c[0] + x * s, c[1] + y * s) for x, y in [(-0.2, -0.05), (0.2, -0.05), (0.2, -0.12), (-0.2, -0.12)]], ang, c))
    grip = poly(rot_pts([(c[0] + x * s, c[1] + y * s) for x, y in [(-0.035, -0.12), (0.035, -0.12), (0.035, -0.38), (-0.035, -0.38)]], ang, c))
    return U(blade, guard, grip)


def arrow(a, b, head=0.16, w=0.025, fletch=True):
    a, b = np.array(a, np.float32), np.array(b, np.float32)
    d = b - a
    L = float(np.hypot(*d))
    u = d / L
    n = np.array([-u[1], u[0]])
    tip = b
    hb = b - u * head
    head_d = poly([tuple(tip), tuple(hb + n * head * 0.55), tuple(hb - n * head * 0.55)])
    shaft = seg(tuple(a), tuple(hb), w)
    out = U(head_d, shaft)
    if fletch:
        f0 = a + u * 0.02
        f1 = a + u * 0.2
        for sgn in (1, -1):
            out = U(out, poly([tuple(f1), tuple(f1 - u * 0.04 + n * sgn * 0.09), tuple(f0 + n * sgn * 0.1), tuple(f0)]))
    return out


def shield(c=(0, 0), s=1.0):
    pts = [(-0.5, 0.55), (0.5, 0.55), (0.5, 0.1), (0.35, -0.3), (0, -0.62), (-0.35, -0.3), (-0.5, 0.1)]
    return poly([(c[0] + x * s, c[1] + y * s) for x, y in pts])


def cross(c=(0, 0), s=1.0, w=0.14):
    return U(box(c, w * s, 0.5 * s), box(c, 0.5 * s, w * s))


def _tongue(base, h, w, lean):
    """One flame tongue: a curved, pointed teardrop rising from ``base``."""
    pts = []
    for k in range(24):
        t = k / 23
        # Right edge going up, then left edge coming down.
        x = w * math.sin(math.pi * t) ** 0.8 * (1 - t) ** 0.4
        pts.append((base[0] + x + lean * t * t * h, base[1] + t * h))
    for k in range(22, 0, -1):      # skip the shared tip and base points
        t = k / 23
        x = w * math.sin(math.pi * t) ** 0.8 * (1 - t) ** 0.4
        pts.append((base[0] - x + lean * t * t * h, base[1] + t * h))
    return poly(pts)


def flame(c=(0, -0.1), s=1.0):
    """A flame: a round belly and three tongues, the middle one tallest."""
    x, y = c
    return U(circle((x, y - 0.18 * s), 0.3 * s),
             _tongue((x, y - 0.3 * s), 1.05 * s, 0.3 * s, 0.05),
             _tongue((x - 0.16 * s, y - 0.25 * s), 0.7 * s, 0.2 * s, -0.25),
             _tongue((x + 0.17 * s, y - 0.25 * s), 0.62 * s, 0.18 * s, 0.3))


def bolt(s=1.0, c=(0, 0)):
    pts = [(0.18, 0.8), (-0.32, 0.02), (-0.02, 0.02), (-0.2, -0.8), (0.34, 0.08), (0.04, 0.08)]
    return poly([(c[0] + x * s, c[1] + y * s) for x, y in pts])


def snowflake(r=0.72):
    d = circle((0, 0), 0.12)
    for k in range(6):
        a = k * math.pi / 3
        e = (r * math.cos(a), r * math.sin(a))
        d = U(d, seg((0, 0), e, 0.045))
        for t in (0.45, 0.72):
            m = (t * r * math.cos(a), t * r * math.sin(a))
            for sgn in (1, -1):
                b = a + sgn * 0.7
                d = U(d, seg(m, (m[0] + 0.2 * math.cos(b), m[1] + 0.2 * math.sin(b)), 0.035))
    return d


def sun(c=(0, 0), r=0.3, rays=12, L=0.3):
    d = circle(c, r)
    for k in range(rays):
        a = k * 2 * math.pi / rays
        p0 = (c[0] + (r + 0.06) * math.cos(a), c[1] + (r + 0.06) * math.sin(a))
        p1 = (c[0] + (r + L * (1 if k % 2 == 0 else 0.6)) * math.cos(a), c[1] + (r + L * (1 if k % 2 == 0 else 0.6)) * math.sin(a))
        d = U(d, seg(p0, p1, 0.035))
    return d


def reticle(r=0.55):
    d = ring((0, 0), r, 0.04)
    for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
        d = U(d, seg(((r - 0.2) * math.cos(a), (r - 0.2) * math.sin(a)), ((r + 0.2) * math.cos(a), (r + 0.2) * math.sin(a)), 0.035))
    return U(d, circle((0, 0), 0.07))


def crescent(c=(0, 0), r=0.55, off=(0.25, 0.12)):
    return sub(circle(c, r), circle((c[0] + off[0], c[1] + off[1]), r * 0.85))


def drop(c=(0, 0), s=1.0):
    return U(circle((c[0], c[1] - 0.12 * s), 0.3 * s),
             poly([(c[0], c[1] + 0.55 * s), (c[0] + 0.26 * s, c[1] - 0.02 * s), (c[0] - 0.26 * s, c[1] - 0.02 * s)]))


def spiral(turns=2.5, w=0.05):
    d = np.full_like(X, 9.0)
    prev = None
    for k in range(120):
        t = k / 119
        a = t * turns * 2 * math.pi
        r = 0.08 + 0.55 * t
        p = (r * math.cos(a), r * math.sin(a))
        if prev:
            d = np.minimum(d, seg(prev, p, w))
        prev = p
    return d


def arcs(c=(0, 0), n=3, r0=0.25, gap=0.18, w=0.04, span=1.1, ang=0.0):
    d = np.full_like(X, 9.0)
    for i in range(n):
        r = r0 + i * gap
        prev = None
        for k in range(24):
            a = ang - span / 2 + span * k / 23
            p = (c[0] + r * math.cos(a), c[1] + r * math.sin(a))
            if prev:
                d = np.minimum(d, seg(prev, p, w))
            prev = p
    return d


def rotate_field(fn, ang):
    global X, Y
    ox, oy = X, Y
    ca, sa = math.cos(-ang), math.sin(-ang)
    X, Y = ox * ca - oy * sa, ox * sa + oy * ca
    try:
        return fn()
    finally:
        X, Y = ox, oy


def leaf(ang=0.6):
    def f():
        body = U(poly([(0, 0.7)] + [(0.34 * math.sin(math.pi * t) * (1 - 0.3 * t), 0.7 - 1.25 * t) for t in np.linspace(0.05, 1, 12)]
                      + [(-0.34 * math.sin(math.pi * t) * (1 - 0.3 * t), 0.7 - 1.25 * t) for t in np.linspace(1, 0.05, 12)]))
        vein = seg((0, 0.6), (0, -0.75), 0.025)
        return sub(U(body, seg((0, -0.5), (0, -0.8), 0.035)), sub(vein, circle((0, 0.9), 0.0)))
    return rotate_field(f, ang)


def ankh():
    return U(ring((0, 0.38), 0.2, 0.07), box((0, -0.3), 0.07, 0.42), box((0, 0.05), 0.38, 0.07))


def orb(c, r):
    return circle(c, r)


def trail(a, b, r0, r1):
    # A tapering streak from a (thin) to b (thick).
    d = np.full_like(X, 9.0)
    for k in range(12):
        t0, t1 = k / 12, (k + 1) / 12
        p0 = (a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0)
        p1 = (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1)
        d = np.minimum(d, seg(p0, p1, r0 + (r1 - r0) * t1))
    return d


def jaws():
    upper = sub(circle((0, -0.05), 0.62), circle((0, -0.05), 0.5))
    upper = np.maximum(upper, -(Y + 0.05))
    teeth = U(*[poly([(x - 0.07, -0.05), (x + 0.07, -0.05), (x, -0.25)]) for x in np.linspace(-0.42, 0.42, 6)])
    base = box((0, -0.58), 0.62, 0.06)
    chain = seg((0, -0.58), (0, -0.05), 0.03)
    return U(upper, teeth, base, chain)


def rock(c=(0.15, 0.15), r=0.33):
    pts = [(c[0] + r * (1 + 0.18 * math.sin(k * 2.7)) * math.cos(k * 2 * math.pi / 9),
            c[1] + r * (1 + 0.18 * math.sin(k * 2.7)) * math.sin(k * 2 * math.pi / 9)) for k in range(9)]
    return poly(pts)


def mountains():
    return U(poly([(-0.55, -0.3), (-0.15, 0.3), (0.1, -0.05), (0.25, 0.15), (0.55, -0.3)]))


def feather(ang=-0.7):
    def f():
        body = poly([(0, 0.75), (0.2, 0.4), (0.18, -0.3), (0, -0.45), (-0.16, -0.25), (-0.2, 0.4)])
        return U(sub(body, U(*[seg((0, 0.5 - i * 0.18), (0.25, 0.62 - i * 0.18), 0.012) for i in range(5)])),
                 seg((0, -0.45), (0, -0.8), 0.025))
    return rotate_field(f, ang)


def fist():
    return U(box((0, 0.05), 0.32, 0.3), *[circle((x, 0.33), 0.09) for x in (-0.22, -0.07, 0.08, 0.23)],
             box((0, -0.45), 0.2, 0.2), circle((-0.34, 0.0), 0.11))


def portal():
    return U(ring((0, 0), 0.5, 0.07), star((0, 0), 4, 0.3, 0.08))


# ── Skill table ─────────────────────────────────────────────────────────────
# (emblem function, style) — "metal" for martial, "glow" for spells.
Q = math.pi / 4

EMBLEMS = {
    "melee_attack": (lambda: sword(-math.pi / 4, 1.25, (0.0, 0.05)), "metal"),
    "ranged_attack": (lambda: arrow((-0.65, -0.65), (0.68, 0.68), 0.26, 0.04), "metal"),
    "warrior_shield_bash": (lambda: U(shield((-0.1, -0.05), 0.85), star((0.45, 0.42), 8, 0.3, 0.1)), "metal"),
    "warrior_taunt": (lambda: U(reticle(0.5), *[arrow((0.95 * math.cos(a), 0.95 * math.sin(a)), (0.62 * math.cos(a), 0.62 * math.sin(a)), 0.12, 0.02, False) for a in (Q, 3 * Q, 5 * Q, 7 * Q)]), "metal"),
    "warrior_cleave": (lambda: U(sword(-math.pi * 0.6, 0.9, (-0.1, 0.1)), arcs((-0.1, -0.1), 2, 0.55, 0.14, 0.05, 1.5, -1.3)), "metal"),
    "warrior_battle_shout": (lambda: U(arcs((-0.45, 0), 3, 0.28, 0.2, 0.05, 1.4, 0.0), poly([(-0.72, 0.2), (-0.4, 0.1), (-0.4, -0.1), (-0.72, -0.2)])), "metal"),
    "warrior_charge": (lambda: U(*[poly([(x, 0.4), (x + 0.3, 0.0), (x, -0.4), (x - 0.12, -0.4), (x + 0.18, 0.0), (x - 0.12, 0.4)]) for x in (-0.55, -0.15, 0.25)]), "metal"),
    "warrior_shield_wall": (lambda: U(shield((-0.28, 0.05), 0.72), shield((0.28, -0.05), 0.72)), "metal"),
    "cleric_minor_heal": (lambda: cross((0, 0), 0.9, 0.13), "glow"),
    "cleric_smite": (lambda: U(sun((0, 0.35), 0.18, 10, 0.2), poly([(-0.12, 0.1), (0.12, 0.1), (0.05, -0.8), (-0.05, -0.8)])), "glow"),
    "cleric_shield_of_faith": (lambda: sub(shield((0, 0), 1.0), cross((0, 0.02), 0.62, 0.11)), "metal"),
    "cleric_cure_ailment": (lambda: sub(drop((0, 0), 1.2), cross((0, -0.1), 0.42, 0.1)), "glow"),
    "cleric_holy_light": (lambda: sun((0, 0), 0.32, 16, 0.36), "glow"),
    "cleric_divine_hammer": (lambda: rotate_field(lambda: U(box((0, 0.35), 0.34, 0.17), box((0, -0.18), 0.05, 0.45)), -Q), "metal"),
    "cleric_resurrection": (lambda: ankh(), "glow"),
    "ranger_aimed_shot": (lambda: U(reticle(0.55), arrow((-0.7, -0.7), (0.05, 0.05), 0.18, 0.025, True)), "metal"),
    "ranger_serpent_arrow": (lambda: U(arrow((-0.65, -0.65), (0.65, 0.65), 0.18, 0.02, True),
                                     *[seg((-0.5 + 0.1 * k + 0.08 * math.sin(k * 1.4), -0.5 + 0.1 * k - 0.08 * math.sin(k * 1.4)),
                                           (-0.4 + 0.1 * k + 0.08 * math.sin((k + 1) * 1.4), -0.4 + 0.1 * k - 0.08 * math.sin((k + 1) * 1.4)), 0.035) for k in range(10)]), "metal"),
    "ranger_trap": (lambda: jaws(), "metal"),
    "ranger_multi_shot": (lambda: U(*[arrow((-0.55, -0.62), (-0.55 + 1.1 * math.cos(a), -0.62 + 1.1 * math.sin(a)), 0.15, 0.02, False) for a in (0.9, 1.15, 1.4)]), "metal"),
    "ranger_camouflage": (lambda: leaf(0.6), "metal"),
    "ranger_snipe": (lambda: U(reticle(0.62), ring((0, 0), 0.3, 0.025)), "metal"),
    "rogue_backstab": (lambda: dagger(-math.pi * 0.85, 1.6, (0.05, 0.2)), "metal"),
    "rogue_poison_blade": (lambda: U(dagger(-math.pi * 0.75, 1.35, (-0.12, 0.18)), drop((0.45, -0.42), 0.6)), "metal"),
    "rogue_stealth": (lambda: crescent((0, 0), 0.6, (0.28, 0.14)), "glow"),
    "rogue_kidney_shot": (lambda: U(dagger(-math.pi * 0.75, 1.3, (-0.18, 0.0)), star((0.48, 0.48), 5, 0.26, 0.11), star((0.1, 0.66), 5, 0.15, 0.065)), "metal"),
    "rogue_evasion": (lambda: U(feather(-0.6), arcs((0.1, 0), 2, 0.55, 0.16, 0.04, 1.2, math.pi)), "metal"),
    "rogue_shadow_dance": (lambda: U(crescent((-0.12, 0.05), 0.5, (0.22, 0.12)), crescent((0.2, -0.1), 0.42, (-0.2, -0.1))), "glow"),
    "shaman_lightning_bolt": (lambda: bolt(1.0), "glow"),
    "shaman_earth_shield": (lambda: U(sub(shield((0, 0), 1.0), shield((0, 0), 0.8)), rotate_field(mountains, 0.0)), "metal"),
    "shaman_hex": (lambda: spiral(2.6, 0.045), "glow"),
    "shaman_ancestral_spirit": (lambda: flame((0, -0.2), 1.0), "glow"),
    "shaman_flame_shock": (lambda: U(flame((-0.15, -0.3), 0.85), bolt(0.5, (0.42, 0.32))), "glow"),
    "shaman_bloodlust": (lambda: U(drop((0, -0.05), 1.15), arcs((0, -0.05), 1, 0.62, 0, 0.04, 2.2, math.pi / 2)), "glow"),
    "wizard_fireball": (lambda: U(circle((0.2, 0.2), 0.36), trail((-0.72, -0.72), (0.05, 0.05), 0.02, 0.24)), "glow"),
    "wizard_frost_nova": (lambda: snowflake(0.72), "glow"),
    "wizard_blink": (lambda: U(star((0.2, 0.2), 4, 0.45, 0.1, 0.0), *[seg((-0.75 + 0.1 * i, -0.3 - 0.12 * i), (-0.35 + 0.1 * i, 0.1 - 0.12 * i), 0.025) for i in range(3)]), "glow"),
    "wizard_arcane_missiles": (lambda: U(*[U(circle((0.35 - 0.3 * i, 0.4 - 0.45 * i + 0.1), 0.12), trail((-0.55 - 0.3 * i + 0.3, -0.3 - 0.45 * i + 0.5), (0.35 - 0.3 * i, 0.4 - 0.45 * i + 0.1), 0.01, 0.07)) for i in range(3)]), "glow"),
    "wizard_mana_shield": (lambda: U(ring((0, 0), 0.6, 0.06), poly([(0, 0.38), (0.26, 0), (0, -0.38), (-0.26, 0)])), "glow"),
    "wizard_meteor": (lambda: U(rock((0.22, 0.22), 0.3), trail((-0.75, -0.75), (0.1, 0.1), 0.03, 0.22)), "glow"),
    "wizard_magic_missile": (lambda: U(star((0.3, 0.3), 5, 0.3, 0.12), trail((-0.7, -0.7), (0.2, 0.2), 0.01, 0.1)), "glow"),
    "skill_1771786697290": (lambda: portal(), "glow"),
}


# ── Painting ─────────────────────────────────────────────────────────────────

def _blur(a, r):
    im = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.GaussianBlur(r)), np.float32) / 255


def hexcol(v):
    return np.array([(v >> 16) & 255, (v >> 8) & 255, v & 255], np.float32) / 255


def paint(skill, d, style):
    col = hexcol(int(skill.get("iconColor", 0x888888)))
    # Background: the skill colour, deep and dark, a lighter centre, a vignette
    # and a little mottling so it reads as enamel rather than flat fill.
    r = np.hypot(X, Y)
    noise = _blur(np.random.RandomState(hash(skill["id"]) & 0xffff).rand(N, N).astype(np.float32), 6)
    bg = col * 0.48 + 0.07
    img = np.zeros((N, N, 3), np.float32) + bg
    img *= (1.3 - 0.6 * np.clip(r * ZOOM / 1.3, 0, 1) ** 1.5)[..., None]
    img *= (0.9 + 0.2 * noise)[..., None]

    inside = np.clip(0.5 - d / PX, 0, 1)
    # Drop shadow down-right.
    sh = _blur(np.roll(np.roll(inside, 5, axis=0), 4, axis=1), 5)
    img *= (1 - 0.6 * sh)[..., None]

    # Bevel: height from the distance inside the shape.
    bevel = 0.07
    h = np.clip(-d / bevel, 0, 1)
    h = h * h * (3 - 2 * h)
    gy, gx = np.gradient(h)
    nx, ny, nz = -gx * 18, gy * 18, np.ones_like(h)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / ln, ny / ln, nz / ln
    L = np.array([-0.55, 0.6, 0.58])
    L /= np.linalg.norm(L)
    lam = np.clip(nx * L[0] + ny * L[1] + nz * L[2], 0, 1)
    hv = np.array([L[0], L[1], L[2] + 1.0])
    hv /= np.linalg.norm(hv)
    spec = np.clip(nx * hv[0] + ny * hv[1] + nz * hv[2], 0, 1) ** 28

    if style == "metal":
        # Polished steel with a warm cast, a touch of the skill colour.
        base = np.array([0.78, 0.76, 0.72]) * 0.85 + col * 0.15
        face = base * (0.28 + 0.85 * lam)[..., None] + spec[..., None] * 0.9
        edge = np.clip(1 - h * 3, 0, 1)
        face = face * (1 - 0.25 * edge)[..., None]
        glow = np.zeros_like(img)
    else:
        bright = np.clip(col * 1.1 + 0.25, 0, 1)
        core = bright * (0.6 + 0.5 * lam)[..., None] + (h ** 2)[..., None] * (1 - bright) * 0.5 + spec[..., None] * 0.4
        face = core
        g = np.exp(-np.maximum(d, 0) / 0.09) * (d > 0)
        glow = bright * (0.85 * g)[..., None]
    img = img + glow
    img = img * (1 - inside[..., None]) + face * inside[..., None]
    # A dark outline so the emblem separates from the wash.
    ol = np.clip(0.5 - np.abs(d - 0.012) / (PX * 1.5), 0, 1) * (style == "metal")
    img = img * (1 - 0.7 * ol[..., None])
    im = Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").resize((128, 128), Image.LANCZOS)
    return im


def main(ids=None):
    data = json.load(open(os.path.join(REPO, "shared", "data", "skills.json")))
    skills = data.get("skills", data)
    skills = list(skills.values()) if isinstance(skills, dict) else skills
    os.makedirs(OUT, exist_ok=True)
    done, missing = [], []
    for s in skills:
        if ids and s["id"] not in ids:
            continue
        e = EMBLEMS.get(s["id"])
        if e is None:
            missing.append(s["id"])
            continue
        fn, style = e
        paint(s, fn(), style).save(os.path.join(OUT, s["id"] + ".png"))
        done.append(s["id"])
    print("painted", len(done), "no emblem:", missing)


if __name__ == "__main__":
    main(sys.argv[1:] or None)
