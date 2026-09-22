"""
Body parts and equipment shapes, split by slot.

Slot contract
-------------
Every slot that sits on a limb redraws that WHOLE limb, the way a real paperdoll
does: `chest` draws the torso *and* both full arms, `legs` draws both full legs.
Slots that sit further out (`gloves`, `boots`) then draw over the top. That is
what keeps a layer correct no matter which other slots are equipped.
"""
import math
from rig import (Canvas, View, DEPTH, CX, GROUND_Y, OUTLINE, TAG_BODY, TAG_ITEM,
                 lighten, darken, mix)

# ------------------------------------------------------------------ skeleton
HEAD_V      = 30.3
HEAD_R      = 4.9
SHOULDER_V  = 24.2
HIP_V       = 13.4
FOOT_V      = 1.2
S_HW        = 5.3          # shoulder lateral half-width
LEG_HW      = 2.4
TORSO_LAT   = 5.3
TORSO_SAG   = 4.4

EYE = (46, 38, 42)


def _shade_ellipse(cv, cx, cy, rx, ry, col, hi=True, outline=True):
    cv.ellipse(cx, cy, rx, ry, col, outline=outline)
    if hi and rx > 1.6 and ry > 1.6:
        cv.ellipse(cx - rx * 0.22, cy - ry * 0.30, rx * 0.58, ry * 0.48,
                   lighten(col, 0.26), outline=False)
    if hi and rx > 2.2 and ry > 2.2:
        cv.ellipse(cx + rx * 0.34, cy + ry * 0.36, rx * 0.42, ry * 0.38,
                   darken(col, 0.22), outline=False)


# ------------------------------------------------------------------ joints
def arm_points(vw, p, side):
    hand = p["handR"] if side > 0 else p["handL"]
    sh = vw.p(side * S_HW, p["lean"] * 0.6, SHOULDER_V + p["bob"])
    hp = vw.p(*hand)
    elbow = ((sh[0] + hp[0]) / 2 + side * 0.6 * vw.fy, (sh[1] + hp[1]) / 2 + 0.7)
    return sh, elbow, hp


def leg_points(vw, p, side):
    swing, lift = p["legR"] if side > 0 else p["legL"]
    hip = vw.p(side * LEG_HW, p["lean"], HIP_V + p["bob"])
    knee = vw.p(side * LEG_HW, p["lean"] + swing * 0.55,
                (HIP_V + FOOT_V) / 2 + lift * 0.5 + p["bob"] * 0.5)
    foot = vw.p(side * LEG_HW, p["lean"] + swing, FOOT_V + lift)
    return hip, knee, foot


def arm_z(vw, p, side):
    h = p["handR"] if side > 0 else p["handL"]
    return vw.z(h[0] * 0.8, h[1] * 0.8)


def leg_z(vw, p, side):
    swing, _ = p["legR"] if side > 0 else p["legL"]
    return vw.z(side * LEG_HW, swing)


# ================================================================== BASE BODY

def body_torso(cv, vw, p, b):
    hw = vw.width(TORSO_LAT, TORSO_SAG)
    midv = (SHOULDER_V + HIP_V) / 2 + p["bob"]
    cx, cy = vw.p(0, p["lean"], midv)
    _shade_ellipse(cv, cx, cy, hw, (SHOULDER_V - HIP_V) / 2 + 0.9, b["skin"])
    # simple undershirt / chest wrap so an unequipped character is decent
    ux, uy = vw.p(0, p["lean"], midv - 0.2)
    cv.ellipse(ux, uy, hw * 0.99, 4.6, b["cloth"], outline=False)
    cv.ellipse(ux - hw * 0.22, uy - 1.9, hw * 0.52, 1.5, lighten(b["cloth"], 0.18), outline=False)
    cv.ellipse(ux + hw * 0.34, uy + 2.0, hw * 0.44, 1.8, darken(b["cloth"], 0.20), outline=False)
    nx, ny = vw.p(0, p["lean"] + 0.4, SHOULDER_V + p["bob"] + 1.0)
    cv.ellipse(nx, ny, 2.1, 1.7, darken(b["skin"], 0.24), outline=False)


def body_arm(cv, vw, p, b, side):
    sh, el, hp = arm_points(vw, p, side)
    cv.line(sh, el, 2.6, b["skin"])
    cv.line(el, hp, 2.4, b["skin"])
    cv.ellipse(hp[0], hp[1], 1.5, 1.4, darken(b["skin"], 0.14))


def body_leg(cv, vw, p, b, side):
    hip, knee, foot = leg_points(vw, p, side)
    cv.line(hip, knee, 3.0, b["skin"])
    cv.line(knee, foot, 2.8, b["skin"])
    # shorts on the thigh
    mid = ((hip[0] + knee[0] * 1.25) / 2.25, (hip[1] + knee[1] * 1.25) / 2.25)
    cv.line(hip, mid, 3.1, darken(b["cloth"], 0.12), outline=False)
    cv.ellipse(foot[0], foot[1] + 0.2, 2.3, 1.5, darken(b["skin"], 0.18))


def body_head(cv, vw, p, b):
    hd = p["lean"] + p.get("headD", 0.0)
    hv = HEAD_V + p["bob"] + p.get("headV", 0.0)
    f = vw.facing_camera
    cx, cy = vw.p(0, hd, hv)
    skin, hair = b["skin"], b["hair"]

    cv.ellipse(cx, cy, HEAD_R, HEAD_R * 1.02, skin)
    cv.ellipse(cx - HEAD_R * 0.24, cy - HEAD_R * 0.34, HEAD_R * 0.50, HEAD_R * 0.40,
               lighten(skin, 0.18), outline=False)
    cv.ellipse(cx + HEAD_R * 0.36, cy + HEAD_R * 0.40, HEAD_R * 0.40, HEAD_R * 0.34,
               darken(skin, 0.18), outline=False)

    if f < -0.25:
        bx, by = vw.p(0, hd - 0.6, hv + 0.2)
        cv.ellipse(bx, by, HEAD_R * 0.94, HEAD_R * 0.94, hair, outline=False)
    hx, hy = vw.p(0, hd - 1.0, hv + HEAD_R * 0.46)
    cv.ellipse(hx, hy, HEAD_R * 0.97, HEAD_R * 0.64, hair, outline=False)
    cv.ellipse(hx - HEAD_R * 0.3, hy - HEAD_R * 0.20, HEAD_R * 0.48, HEAD_R * 0.26,
               lighten(hair, 0.24), outline=False)
    if f > 0.25:
        for side in (1, -1):
            sx, sy = vw.p(side * HEAD_R * 0.82, hd - 0.4, hv - 0.6)
            cv.ellipse(sx, sy, 1.1, 1.9, hair, outline=False)

    if f > 0.18:
        for side in (1, -1):
            ex, ey = vw.p(side * 2.0, hd + HEAD_R * 0.80, hv - 0.5)
            cv.dot(ex, ey, 0.62, EYE)
    elif f > -0.15:
        sgn = 1 if vw.fx >= 0 else -1
        ex, ey = vw.p(-sgn * 1.4, hd + HEAD_R * 0.66, hv - 0.4)
        cv.dot(ex, ey, 0.6, EYE)

    if b.get("beard") and f > -0.1:
        bx, by = vw.p(0, hd + HEAD_R * 0.60, hv - 3.4)
        cv.ellipse(bx, by, vw.width(2.2, 1.4) + 0.3, 1.9, b["beard"], outline=False)


# ================================================================== CHEST
def chest_torso(cv, vw, p, it):
    hw = vw.width(TORSO_LAT, TORSO_SAG)
    midv = (SHOULDER_V + HIP_V) / 2 + p["bob"]
    cx, cy = vw.p(0, p["lean"], midv)
    body = it["main"]
    _shade_ellipse(cv, cx, cy, hw, (SHOULDER_V - HIP_V) / 2 + 0.9, body)

    f = vw.facing_camera
    if f > -0.35 and it.get("cloth"):
        fx_, fy_ = vw.p(0, p["lean"] + TORSO_SAG * 0.80, midv + 1.0)
        cv.ellipse(fx_, fy_, hw * 0.36, 3.6, it["cloth"], outline=False)
        cv.ellipse(fx_, fy_ - 1.2, hw * 0.20, 1.6, lighten(it["cloth"], 0.22), outline=False)

    nx, ny = vw.p(0, p["lean"] + 0.4, SHOULDER_V + p["bob"] + 1.0)
    cv.ellipse(nx, ny, 2.1, 1.7, darken(body, 0.28), outline=False)

    bx, by = vw.p(0, p["lean"], HIP_V + p["bob"] + 0.4)
    cv.ellipse(bx, by, hw * 0.94, 1.5, it.get("belt") or darken(body, 0.4), outline=False)
    if it.get("trim"):
        cv.dot(bx, by, 0.9, it["trim"])

    if it.get("pauldrons"):
        for side in (1, -1):
            px_, py_ = vw.p(side * (S_HW + 0.2), p["lean"] * 0.5, SHOULDER_V + p["bob"] + 1.3)
            _shade_ellipse(cv, px_, py_, 2.2, 1.8, it.get("alt") or body)


def chest_arm(cv, vw, p, it, side):
    sh, el, hp = arm_points(vw, p, side)
    col = it.get("alt") or it["main"]
    cv.line(sh, el, 2.6, col)
    cv.line(el, hp, 2.4, col)
    cv.ellipse(hp[0], hp[1], 1.5, 1.4, darken(col, 0.26))


# ================================================================== LEGS
def legs_leg(cv, vw, p, it, side):
    hip, knee, foot = leg_points(vw, p, side)
    col = it.get("alt") or it["main"]
    cv.line(hip, knee, 3.0, col)
    cv.line(knee, foot, 2.8, col)
    cv.ellipse(foot[0], foot[1] + 0.2, 2.3, 1.5, darken(col, 0.3))


def legs_robe(cv, vw, p, it):
    """Skirt of a robe. Built from projected half-widths so it stays wide enough
    to hide the legs from every angle, including side views."""
    lean, bob, sway = p["lean"], p["bob"], p.get("sway", 0.0)
    col = it["main"]
    topw = vw.width(3.6, 2.9)
    botw = vw.width(5.4, 4.3)
    tx, ty = vw.p(0, lean, HIP_V + bob + 1.8)
    bx, by = vw.p(sway * 0.8, lean, FOOT_V + 0.5)
    cv.polygon([(tx - topw, ty), (tx + topw, ty), (bx + botw, by), (bx - botw, by)], col)
    cv.line((tx, ty), (bx, by), 1.0, darken(col, 0.28), outline=False)
    cv.ellipse(bx, by, botw, 1.5, darken(col, 0.16), outline=False)
    cv.ellipse(tx - topw * 0.4, ty + 3.0, topw * 0.5, 2.6, lighten(col, 0.14), outline=False)
    if it.get("trim"):
        cv.ellipse(bx, by + 0.7, botw * 0.92, 0.7, it["trim"], outline=False)


# ================================================================== GLOVES / BOOTS
def gloves_hand(cv, vw, p, it, side):
    sh, el, hp = arm_points(vw, p, side)
    col = it["main"]
    cv.line(el, hp, 2.5, col)
    cv.ellipse(hp[0], hp[1], 1.7, 1.6, col)
    if it.get("trim"):
        cv.dot(el[0], el[1], 0.7, it["trim"])


def boots_foot(cv, vw, p, it, side):
    hip, knee, foot = leg_points(vw, p, side)
    col = it["main"]
    mid = ((knee[0] + foot[0]) / 2, (knee[1] + foot[1]) / 2)
    cv.line(mid, foot, 2.9, col)
    cv.ellipse(foot[0], foot[1] + 0.2, 2.5, 1.6, col)
    if it.get("trim"):
        cv.dot(mid[0], mid[1], 0.7, it["trim"])


# ================================================================== HELM
def _hole(cv, fn):
    """Draw tagged as body, so the pixels drop out of the item layer and the
    body underneath shows through -- keeps faces the right skin tone."""
    prev = cv.tag
    cv.tag = TAG_BODY
    fn()
    cv.tag = prev


def helm_draw(cv, vw, p, it, body):
    hd = p["lean"] + p.get("headD", 0.0)
    hv = HEAD_V + p["bob"] + p.get("headV", 0.0)
    f = vw.facing_camera
    cx, cy = vw.p(0, hd, hv)
    style = it["style"]
    base = it["main"]
    skin = body["skin"]

    if style in ("open", "full", "horned"):
        cv.ellipse(cx, cy, HEAD_R, HEAD_R * 1.02, base)
        cv.ellipse(cx - HEAD_R * 0.24, cy - HEAD_R * 0.34, HEAD_R * 0.50, HEAD_R * 0.40,
                   lighten(base, 0.20), outline=False)
        cv.ellipse(cx + HEAD_R * 0.36, cy + HEAD_R * 0.40, HEAD_R * 0.40, HEAD_R * 0.34,
                   darken(base, 0.20), outline=False)

    if style == "open":
        if f > -0.3:
            fxo, fyo = vw.p(0, hd + HEAD_R * 0.80, hv - 1.5)
            w_ = vw.width(3.0, 1.7) + 0.6
            cv.ellipse(fxo, fyo, w_ + 0.4, 3.7, darken(base, 0.34), outline=False)
            _hole(cv, lambda: cv.ellipse(fxo, fyo, w_, 3.4, skin, outline=False))

    elif style in ("full", "horned"):
        if f < -0.05:
            rx_, ry_ = vw.p(0, hd - HEAD_R * 0.5, hv - 2.4)
            cv.ellipse(rx_, ry_, HEAD_R * 0.62, 0.9, darken(base, 0.42), outline=False)
        if f > -0.3:
            vx, vy = vw.p(0, hd + HEAD_R * 0.72, hv + (0.4 if style == "full" else 0.2))
            cv.ellipse(vx, vy, vw.width(3.4, 1.2) + 0.6, 1.5,
                       darken(base, 0.72) if style == "full" else (18, 16, 22), outline=False)
            if f > 0.18:
                for side in (1, -1):
                    ex, ey = vw.p(side * 1.8, hd + HEAD_R * 0.78, hv + 0.3)
                    cv.dot(ex, ey, 0.5, it.get("glow") or (226, 96, 60))
        if style == "full":
            c0 = vw.p(0, hd - 1.6, hv + HEAD_R * 0.80)
            c1 = vw.p(0, hd + 1.8, hv + HEAD_R * 0.70)
            cv.line(c0, c1, 0.9, it.get("trim") or lighten(base, 0.4), outline=False)
        else:
            for side in (1, -1):
                h0 = vw.p(side * (HEAD_R * 0.80), hd - 0.2, hv + 1.4)
                hm = vw.p(side * (HEAD_R * 1.34), hd - 0.5, hv + 2.6)
                h1 = vw.p(side * (HEAD_R * 1.42), hd - 0.6, hv + 4.2)
                cv.line(h0, hm, 1.7, it.get("horn") or (226, 218, 196))
                cv.line(hm, h1, 1.2, it.get("horn") or (226, 218, 196))

    elif style == "hood":
        hx, hy = vw.p(0, hd - 1.2, hv + 0.8)
        cv.ellipse(hx, hy, HEAD_R + 0.7, HEAD_R + 0.4, base)
        cv.ellipse(hx - 1.1, hy - 1.4, HEAD_R * 0.6, HEAD_R * 0.42, lighten(base, 0.2), outline=False)
        if it.get("trim"):
            tx2, ty2 = vw.p(0, hd + HEAD_R * 0.30, hv - 3.0)
            cv.ellipse(tx2, ty2, HEAD_R * 0.80, 0.7, it["trim"], outline=False)
        if f > -0.2:
            ox, oy = vw.p(0, hd + HEAD_R * 0.74, hv - 1.2)
            w_ = vw.width(2.6, 1.4) + 0.5
            cv.ellipse(ox, oy, w_ + 0.6, 3.1, darken(base, 0.66), outline=False)
            _hole(cv, lambda: cv.ellipse(ox, oy + 0.2, w_, 2.5, skin, outline=False))

    elif style == "hat":
        tipL = -2.4 if vw.fx >= 0 else 2.4
        tip = vw.p(tipL, hd - 3.0, hv + HEAD_R + 4.6)
        b0 = vw.p(-HEAD_R * 0.92, hd - 0.4, hv + HEAD_R * 0.42)
        b1 = vw.p(HEAD_R * 0.92, hd - 0.4, hv + HEAD_R * 0.42)
        bm = vw.p(0, hd + HEAD_R * 0.6, hv + HEAD_R * 0.30)
        cv.polygon([b0, bm, b1, tip], base)
        sh0 = vw.p(HEAD_R * 0.30, hd + HEAD_R * 0.2, hv + HEAD_R * 0.44)
        cv.line(sh0, tip, 0.9, darken(base, 0.26), outline=False)
        bx, by = vw.p(0, hd - 0.3, hv + HEAD_R * 0.42)
        br = HEAD_R + 1.7
        cv.ellipse(bx, by, br, 1.3 + 0.8 * abs(vw.fy), base)
        if it.get("trim"):
            bx2, by2 = vw.p(0, hd - 0.3, hv + HEAD_R * 0.42 + 1.3)
            cv.ellipse(bx2, by2, br * 0.60, 0.75 + 0.45 * abs(vw.fy), it["trim"], outline=False)

    elif style == "mitre":
        for i, (w, vv) in enumerate(((5.4, 1.8), (4.4, 3.6), (2.6, 5.0), (1.0, 6.1))):
            mx, my = vw.p(0, hd - 0.3, hv + HEAD_R * 0.30 + vv)
            cv.ellipse(mx, my, w * (0.62 + 0.38 * abs(vw.fy)) + 1.0, 1.9,
                       base if i % 2 == 0 else lighten(base, 0.16))
        if it.get("trim"):
            tx, ty = vw.p(0, hd + 2.0, hv + HEAD_R * 0.30 + 3.2)
            cv.dot(tx, ty, 1.0, it["trim"])

    elif style == "circlet" and it.get("trim"):
        bx, by = vw.p(0, hd - 0.2, hv + HEAD_R * 0.42)
        cv.ellipse(bx, by, HEAD_R * 0.94, HEAD_R * 0.40, it["trim"], outline=False)


# ================================================================== BACK
def back_draw(cv, vw, p, it):
    style = it["style"]
    lean, bob, sway = p["lean"], p["bob"], p.get("sway", 0.0)
    col = it["main"]
    if style == "quiver":
        a = vw.p(-2.6, lean - 3.4, HIP_V + bob + 1.0)
        b = vw.p(-3.4, lean - 3.8, SHOULDER_V + bob + 2.4)
        cv.line(a, b, 2.6, col)
        for off in (-1.1, 0.0, 1.1):
            t0 = vw.p(-3.4 + off * 0.5, lean - 3.8, SHOULDER_V + bob + 3.4)
            t1 = vw.p(-3.6 + off * 0.8, lean - 4.0, SHOULDER_V + bob + 5.6)
            cv.line(t0, t1, 0.7, it.get("trim") or (206, 196, 170), outline=False)
        return
    length = 7.0 if style == "cape" else 4.0
    a = vw.p(-3.9, lean - 2.4, SHOULDER_V + bob + 1.2)
    b = vw.p(3.9, lean - 2.4, SHOULDER_V + bob + 1.2)
    c = vw.p(4.9 + sway * 1.3, lean - 3.8, FOOT_V + length)
    d = vw.p(-4.9 + sway * 1.3, lean - 3.8, FOOT_V + length)
    cv.polygon([a, b, c, d], col)
    m0 = vw.p(0, lean - 2.6, SHOULDER_V + bob)
    m1 = vw.p(sway * 1.3, lean - 3.8, FOOT_V + length + 0.4)
    cv.line(m0, m1, 1.0, darken(col, 0.3), outline=False)
    if it.get("trim"):
        cv.line(a, b, 0.8, it["trim"], outline=False)


# ================================================================== WEAPONS
def _v3(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def mainhand_draw(cv, vw, p, it):
    kind = it["style"]
    hand = p["handR"]
    d = p.get("wep_dir", (0.15, 0.25, 1.0))
    n = math.sqrt(sum(c * c for c in d)) or 1.0
    d = tuple(c / n for c in d)
    metal = it.get("metal") or (206, 212, 222)
    wood = it.get("wood") or (122, 84, 52)
    trim = it.get("trim") or (214, 176, 86)

    if kind in ("sword", "greatsword"):
        blen = 12.5 if kind == "greatsword" else 10.0
        bw = 2.2 if kind == "greatsword" else 1.7
        grip0 = tuple(hand[i] - d[i] * 1.6 for i in range(3))
        guard = tuple(hand[i] + d[i] * 1.4 for i in range(3))
        tip = tuple(hand[i] + d[i] * blen for i in range(3))
        cv.line(vw.p(*grip0), vw.p(*guard), 1.5, wood)
        perp = (d[2], 0.0, -d[0])
        pn = math.hypot(perp[0], perp[2]) or 1.0
        perp = (perp[0] / pn, 0.0, perp[2] / pn)
        g0 = tuple(guard[i] - perp[i] * 2.6 for i in range(3))
        g1 = tuple(guard[i] + perp[i] * 2.6 for i in range(3))
        cv.line(vw.p(*g0), vw.p(*g1), 1.4, trim)
        cv.line(vw.p(*guard), vw.p(*tip), bw, metal)
        cv.line(vw.p(*_v3(guard, tip, 0.12)), vw.p(*_v3(guard, tip, 0.9)), 0.7,
                lighten(metal, 0.45), outline=False)
        cv.dot(*vw.p(*grip0), 0.9, trim)

    elif kind == "mace":
        head = tuple(hand[i] + d[i] * 8.4 for i in range(3))
        cv.line(vw.p(*tuple(hand[i] - d[i] * 2.2 for i in range(3))), vw.p(*head), 1.6, wood)
        hx, hy = vw.p(*head)
        _shade_ellipse(cv, hx, hy, 2.9, 2.8, metal)
        for a in (0.0, 2.09, 4.19):
            cv.dot(hx + math.cos(a) * 2.4, hy + math.sin(a) * 2.2, 0.7, darken(metal, 0.34))
        if it.get("trim"):
            cv.dot(hx, hy - 0.6, 0.8, trim)

    elif kind == "staff":
        top = tuple(hand[i] + d[i] * 9.5 for i in range(3))
        bot = tuple(hand[i] - d[i] * 7.0 for i in range(3))
        cv.line(vw.p(*bot), vw.p(*top), 1.5, wood)
        tx, ty = vw.p(*top)
        g = it.get("glow") or (120, 190, 240)
        cv.ellipse(tx, ty, 2.4, 2.4, g)
        cv.ellipse(tx - 0.5, ty - 0.6, 1.2, 1.2, lighten(g, 0.65), outline=False)
        halo = p.get("glow_t", 0.0)
        if halo > 0.05:
            cv.ellipse(tx, ty, 2.4 + halo * 2.2, 2.4 + halo * 2.2, lighten(g, 0.55), outline=False)
            cv.ellipse(tx, ty, 1.6, 1.6, lighten(g, 0.9), outline=False)

    elif kind == "bow":
        hl = p["handL"]
        pull = p.get("bow_pull", 0.0)
        bl, bd = -0.90, 0.56
        pts = []
        for i in range(9):
            t = -1 + i * 0.25
            b = 4.3 * (1 - t * t)
            pts.append(vw.p(hl[0] + bl * b, hl[1] + bd * b, hl[2] + t * 6.6))
        for i in range(len(pts) - 1):
            cv.line(pts[i], pts[i + 1], 1.3, wood)
        tip0 = vw.p(hl[0], hl[1], hl[2] - 6.6)
        tip1 = vw.p(hl[0], hl[1], hl[2] + 6.6)
        sx = hl[0] - bl * pull * 4.6
        sd = hl[1] - bd * pull * 4.6
        midstr = vw.p(sx, sd, hl[2])
        cv.line(tip0, midstr, 0.55, (236, 230, 212), outline=False)
        cv.line(midstr, tip1, 0.55, (236, 230, 212), outline=False)
        if pull > 0.15:
            a1 = vw.p(hl[0] + bl * 4.0, hl[1] + bd * 4.0 + 3.0, hl[2])
            cv.line(midstr, a1, 0.7, (210, 204, 190), outline=False)
            cv.dot(*a1, 0.8, metal)


def offhand_draw(cv, vw, p, it):
    hand = p["handL"]
    lat, sag, hh = {"buckler": (2.9, 0.9, 2.9),
                    "kite": (3.4, 1.0, 4.6),
                    "tower": (4.0, 1.1, 5.6)}[it["style"]]
    cx, cy = vw.p(hand[0], hand[1] + 1.6, hand[2] + 0.6)
    col = it["main"]
    _shade_ellipse(cv, cx, cy, vw.width(lat, sag) + 0.6, hh, col)
    if it.get("trim"):
        cv.ellipse(cx, cy, max(0.8, (vw.width(lat, sag) + 0.6) * 0.26), hh * 0.26,
                   it["trim"], outline=False)


# ================================================================== FX
def draw_slash(cv, vw, p, col=(250, 246, 230)):
    t = p.get("slash_t", 0.0)
    if t <= 0.05:
        return
    c = lighten(col, 0.72)
    for i in range(11):
        a = -1.05 + i * 0.21
        x, y = vw.p(math.sin(a) * 10.5, math.cos(a) * 10.5, 17.5 + math.sin(a) * 3.0)
        cv.dot(x, y, 0.55 * t, c)


def draw_cast_fx(cv, vw, p, glow):
    t = p.get("cast_t", 0.0)
    if t <= 0.02:
        return
    g = glow or (150, 210, 250)
    cx, cy = vw.p(0, p["lean"] + 8.0, 22.0 + p["bob"])
    r = 0.9 + 1.9 * t
    cv.ellipse(cx, cy, r, r, g, outline=False)
    cv.ellipse(cx - r * 0.2, cy - r * 0.25, r * 0.5, r * 0.5, lighten(g, 0.75), outline=False)
    for i in range(3):
        a = t * 6.0 + i * 2.09
        cv.dot(cx + math.cos(a) * (r + 2.2), cy + math.sin(a) * (r + 2.2) * 0.65,
               0.55, lighten(g, 0.7))
