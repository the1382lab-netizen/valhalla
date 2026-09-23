"""B-15 Wave 2 (MetaHuman rework): the 16 skinned equipment pieces
(A-005..A-020) re-fitted to the MetaHuman body.

Same designs, names, materials and primitives as ``wave2_armour.py`` (Fit,
Piece, tube, band, hang are imported from it); what changes is where things
are. Heights go through ``wave2mh_base.Z`` (Fable landmark -> MetaHuman
landmark), offsets and thicknesses through ``O`` (x 180.3/122), limb axes come
from the MetaHuman's own bones, and head details scale with the head
(``HS``, chin-to-crown ratio). Weights are the MetaHuman's, transferred from
the nearest skin (``transfer_weights``), smoothed on hanging cloth and rigid
on the head pieces.

Run in Blender: exec ``wave2mh_base.py`` and ``prep()``, then exec this file
and ``build_all()``. Output: ``Import/Characters/MetaHuman/Equipment/SK_<name>.fbx``
and ``Blender assets/Characters/valhalla_mh_equipment.blend``.
"""

import importlib.util
import math
import os
import sys

import bpy
import mathutils

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"


def _load(name):
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    sys.modules[name] = mod
    return mod


mb = _load("wave2mh_base")
wa = _load("wave2_armour")

Fit, Piece, tube, band, hang = wa.Fit, wa.Piece, wa.tube, wa.band, wa.hang
LEATHER, LEATHER_D, CHAIN, PLATE, STEEL, GOLD, IRON_D = (wa.LEATHER, wa.LEATHER_D, wa.CHAIN, wa.PLATE, wa.STEEL,
                                                        wa.GOLD, wa.IRON_D)
BLUE, CREAM, BROWN, GREEN, ROPE = wa.BLUE, wa.CREAM, wa.BROWN, wa.GREEN, wa.ROPE
Z, O, C, H, B, lerp = mb.Z, mb.O, mb.C, mb.H, mb.B, mb.lerp
V = mathutils.Vector
PI = math.pi
HS = (1.802 - 1.580) / (1.200 - 0.865)          # head size, Fable -> MetaHuman
TORSO_R = 0.205                                  # torso rays stop short of the A-pose arms
UP = V((0, 0, 1))


def _outer(s):
    return (-PI, 0.0) if s > 0 else (0.0, PI)


def front_y(center, off):
    """y of a point ``off`` in front of the skin, straight ahead of ``center``."""
    c = V(center)
    return c.y - fit_now.radius(c, V((0, -1, 0))) - off


def spread(old_top, old_bottom, s_old):
    """A Fable ``hang`` spread with the same total flare over the MetaHuman drop."""
    return s_old * (old_top - old_bottom) / (Z(old_top) - Z(old_bottom))


def mhang(p, top, old_top, old_bottom, m, s_old, **kw):
    return hang(p, top, Z(old_bottom), m, spread=spread(old_top, old_bottom, s_old), **kw)


def hem_ring(top, old_top, old_z, s_old, extra=0.03):
    return wa.hang_ring(top, Z(old_z), spread(old_top, old_z, s_old), extra)


def torso(p, fit, z0, z1, off, m, n, segs=28):
    """Body of a shirt from Fable height z0 up to z1 (z1 >= 0.70 runs to the neck)."""
    return tube(p, fit, C(Z(z0)), C(Z(z1)), O(off), m, n=n, segs=segs, max_r=TORSO_R)


def ring_band(p, fit, z, off, h, m, segs=28, max_r=TORSO_R, **kw):
    return band(p, fit, C(Z(z)), UP, O(off), O(h), m, segs=segs, max_r=max_r, **kw)


# ── Chest ────────────────────────────────────────────────────────────────────

def chest_travelers_jerkin(fit):
    p = Piece("SK_chest_travelers_jerkin")
    torso(p, fit, 0.37, 0.70, 0.010, LEATHER, n=12)
    for s in (1, -1):
        a, b = mb.arm_axis(s, -0.18, 0.32)
        a0, a1 = _outer(s)
        tube(p, fit, a, b, O(0.012), LEATHER, n=5, segs=10, max_r=0.14, a0=a0, a1=a1, closed=False, rim=O(0.005))
    ring_band(p, fit, 0.692, 0.012, 0.035, LEATHER_D, segs=20, max_r=0.13)                     # collar
    ring_band(p, fit, 0.455, 0.018, 0.036, LEATHER_D)                                         # belt
    z = Z(0.455)
    fy = front_y(C(z), O(0.018))
    p.box((-O(0.022), fy - O(0.010), z - O(0.018)), (O(0.022), fy + O(0.006), z + O(0.018)), STEEL)
    for sx in (-0.42, 0.42):
        tube(p, fit, C(Z(0.475)), C(Z(0.645)), O(0.016), LEATHER_D, n=4, segs=2, max_r=TORSO_R,
             a0=sx - 0.12, a1=sx + 0.12, closed=False, rim=O(0.004))
        for zz in (0.50, 0.56, 0.62):
            c = C(Z(zz))
            d = V((math.sin(sx), -math.cos(sx), 0))
            r = fit.radius(c, d)
            p.stud(c + d * (r + O(0.018)), O(0.008), STEEL)
    return p.finish()


def chest_priests_chain(fit):
    p = Piece("SK_chest_priests_chain")
    torso(p, fit, 0.35, 0.70, 0.012, CHAIN, n=12)
    for s in (1, -1):
        a, b = mb.arm_axis(s, -0.15, 0.55)
        tube(p, fit, a, b, O(0.009), CHAIN, n=6, segs=16, max_r=0.13, flare=lambda t: O(0.006) * t)
    ring_band(p, fit, 0.70, 0.014, 0.045, CHAIN, segs=20, max_r=0.13)                         # coif collar
    # tabard: front and back panels over the mail, hem trim below
    for (a0, a1) in ((-0.62, 0.62), (PI - 0.62, PI + 0.62)):
        rings = mb.drape(p, fit, Z(0.64), Z(0.245), O(0.024), CREAM, flare=1.12, n=11, segs=8, a0=a0, a1=a1,
                         closed=False, far=0.3, mono_below=Z(0.44))
        mb.hem(p, rings, LEATHER_D, 0.035, closed=False)
    ring_band(p, fit, 0.455, 0.034, 0.034, LEATHER_D)                                         # belt over the tabard
    z = Z(0.58)
    p.stud((0, front_y(C(z), O(0.03)), z), O(0.02), LEATHER_D)                              # holy symbol boss
    return p.finish()


def chest_apprentice_robe(fit):
    p = Piece("SK_chest_apprentice_robe")
    torso(p, fit, 0.40, 0.70, 0.012, BLUE, n=11)
    for s in (1, -1):
        a, b = mb.arm_axis(s, -0.15, 1.10)
        tube(p, fit, a, b, O(0.009), BLUE, n=7, segs=16, max_r=0.13)
        a, b = mb.forearm_axis(s, -0.12, 0.97)
        tube(p, fit, a, b, O(0.012), BLUE, n=6, segs=16, max_r=0.10, flare=lambda t: O(0.028) * t ** 3)
    rings = mb.drape(p, fit, Z(0.42), Z(0.215), O(0.020), BLUE, flare=1.18, n=9, far=0.35)  # skirt to the knee
    mb.hem(p, rings, CREAM, 0.045)                                                          # hem trim
    ring_band(p, fit, 0.45, 0.026, 0.020, ROPE)                                             # rope belt
    ring_band(p, fit, 0.70, 0.016, 0.05, CREAM, segs=20, max_r=0.13)                         # cowl collar
    return p.finish()


def _hips(p, m, offset, z_top):
    """Seat and hips as a shell of the body itself (no ring can cover the
    crotch: every ray from the torso axis there is between the legs)."""
    return mb.skin_shell(p, m, None, offset, ratio=0.45,
                         region=lambda q: 0.70 <= q.z <= z_top and abs(q.x) < 0.24)


def legs_travelers(fit):
    p = Piece("SK_legs_travelers")
    _hips(p, BROWN, O(0.010) * 0.5, Z(0.47))
    for s in (1, -1):
        a, b = mb.thigh_axis(s, 0.30, 1.12)
        tube(p, fit, a, b, O(0.008), BROWN, n=5, segs=18, max_r=0.16)
        a, b = mb.calf_axis(s, -0.12, 0.92)
        tube(p, fit, a, b, O(0.0105), BROWN, n=6, segs=18, max_r=0.12)
        band(p, fit, B("calf_" + mb.side(s)), UP, O(0.016), O(0.07), LEATHER, segs=10, max_r=0.12,
             a0=-1.1, a1=1.1, closed=False)                                                # knee patch
    ring_band(p, fit, 0.455, 0.016, 0.03, LEATHER_D)
    z = Z(0.455)
    p.stud((0, front_y(C(z), O(0.016)), z), O(0.014), LEATHER_D)
    return p.finish()


def legs_iron_plate(fit):
    p = Piece("SK_legs_iron_plate")
    _hips(p, LEATHER_D, O(0.008) * 0.5, Z(0.43))                                           # breeches under the plate
    ring_band(p, fit, 0.445, 0.014, 0.06, PLATE)                                            # fauld
    ring_band(p, fit, 0.39, 0.019, 0.05, PLATE)                                             # second lame
    for s in (1, -1):
        knee = B("calf_" + mb.side(s))
        a, b = mb.thigh_axis(s, -0.12, 0.98)
        tube(p, fit, a, b, O(0.014), PLATE, n=6, segs=18, max_r=0.16)                     # cuisse
        band(p, fit, knee, UP, O(0.028), O(0.08), PLATE, segs=12, max_r=0.12, a0=-1.3, a1=1.3, closed=False)
        band(p, fit, knee, UP, O(0.006), O(0.03), LEATHER_D, segs=18, max_r=0.12)         # strap
        a, b = mb.calf_axis(s, 0.06, 0.92)
        tube(p, fit, a, b, O(0.010), PLATE, n=5, segs=18, max_r=0.12)                     # greave
    return p.finish()


def legs_apprentice_robe(fit):
    p = Piece("SK_legs_apprentice_robe")
    torso(p, fit, 0.345, 0.46, 0.008, BLUE, n=3)
    rings = mb.drape(p, fit, Z(0.40), Z(0.095), O(0.018), BLUE, flare=1.18, n=11, far=0.35)
    mb.hem(p, rings, CREAM, 0.045)
    return p.finish()


def _foot_line(s):
    f, b = B("foot_" + mb.side(s)), B("ball_" + mb.side(s))
    heel = V((f.x, f.y + 0.045, 0.058))
    toe = V((b.x, b.y - 0.02, 0.034))
    return heel, toe


def _foot(p, fit, s, offset, m, segs=16, n=9):
    """The foot: a shell of the MetaHuman's own foot (toes and all), ``offset``
    in Fable metres (a boot's leather is thinner than the Fable tube's air gap)."""
    return mb.skin_shell(p, m, mb.foot_bones(s), O(offset) * 0.4, ratio=0.5)


def _sole(p, fit, s, m, h=0.012):
    heel, toe = _foot_line(s)
    b = B("ball_" + mb.side(s))
    c = V((b.x, b.y + 0.02, 0.03))
    w = max(fit.radius(c, V((1, 0, 0)), 0.1) or 0.045, fit.radius(c, V((-1, 0, 0)), 0.1) or 0.045) + O(0.004)
    x = (heel.x + toe.x) / 2
    p.box((x - w, toe.y - O(0.014), 0.0), (x + w, heel.y + O(0.012), O(h)), m)


def _foot_point(s, t, z):
    heel, toe = _foot_line(s)
    q = lerp(heel, toe, t)
    return V((q.x, q.y, z))


FOOT_AXIS = (0, -1, -0.12)


def boots_ranger(fit):
    p = Piece("SK_boots_ranger")
    for s in (1, -1):
        a, b = mb.calf_axis(s, 0.2, 1.0)
        tube(p, fit, a, b, O(0.011), LEATHER, n=5, segs=18, max_r=0.12)                    # shaft
        band(p, fit, mb.calf_axis(s, 0.2, 0.2)[0], UP, O(0.020), O(0.035), LEATHER_D, segs=18, max_r=0.12)
        _foot(p, fit, s, 0.014, LEATHER)
        band(p, fit, _foot_point(s, 0.5, 0.075), FOOT_AXIS, O(0.014), O(0.02), LEATHER_D, segs=16, max_r=0.12,
             ref=(0, 0, 1))
        _sole(p, fit, s, LEATHER_D)
    return p.finish()


def boots_iron_sabatons(fit):
    p = Piece("SK_boots_iron_sabatons")
    for s in (1, -1):
        _foot(p, fit, s, 0.016, PLATE)
        for i, t in enumerate((0.44, 0.62, 0.80)):
            band(p, fit, _foot_point(s, t, 0.066 - 0.006 * i), FOOT_AXIS, O(0.015 - 0.001 * i), O(0.028), PLATE,
                 segs=16, max_r=0.12, ref=(0, 0, 1))
        a, b = mb.calf_axis(s, 0.8, 1.05)
        tube(p, fit, a, b, O(0.012), PLATE, n=2, segs=18, max_r=0.12)                      # ankle greave
        _sole(p, fit, s, IRON_D)
    return p.finish()


def boots_cloth_slippers(fit):
    p = Piece("SK_boots_cloth_slippers")
    for s in (1, -1):
        _foot(p, fit, s, 0.012, CREAM)
        band(p, fit, B("foot_" + mb.side(s)) + V((0, 0, 0.015)), UP, O(0.008), O(0.022), BROWN, segs=16, max_r=0.12)
        _sole(p, fit, s, BROWN, h=0.008)
    return p.finish()


# ── Hands ────────────────────────────────────────────────────────────────────

def _hand(p, fit, s, offset, m, segs=16, n=6):
    """The hand: a shell of the MetaHuman's own hand, fingers apart."""
    return mb.skin_shell(p, m, mb.hand_bones(s), O(offset) * 0.35, ratio=0.38)


def _fore(s, t):
    return mb.forearm_axis(s, t, t)[0]


def _fore_dir(s):
    a, b = mb.forearm_axis(s, 0.0, 1.0)
    return (b - a).normalized()


def gloves_ranger_bracers(fit):
    p = Piece("SK_gloves_ranger_bracers")
    for s in (1, -1):
        a, b = mb.forearm_axis(s, 0.12, 0.98)
        tube(p, fit, a, b, O(0.009), LEATHER, n=5, segs=16, max_r=0.10)
        for t in (0.3, 0.55, 0.8):
            band(p, fit, _fore(s, t), _fore_dir(s), O(0.013), O(0.018), LEATHER_D, segs=16, max_r=0.10)
        _hand(p, fit, s, 0.008, LEATHER)
    return p.finish()


def gloves_iron_gauntlets(fit):
    p = Piece("SK_gloves_iron_gauntlets")
    for s in (1, -1):
        a, b = mb.forearm_axis(s, 0.2, 0.98)
        tube(p, fit, a, b, O(0.011), PLATE, n=5, segs=16, max_r=0.10, flare=lambda t: O(0.022) * (1 - t) ** 2)
        band(p, fit, _fore(s, 0.98), _fore_dir(s), O(0.008), O(0.03), LEATHER_D, segs=16, max_r=0.10)
        _hand(p, fit, s, 0.016, PLATE)
    return p.finish()


def gloves_cloth_wraps(fit):
    p = Piece("SK_gloves_cloth_wraps")
    for s in (1, -1):
        _hand(p, fit, s, 0.007, CREAM)
        for i, t in enumerate((0.16, 0.34, 0.52, 0.70, 0.88)):
            band(p, fit, _fore(s, t), _fore_dir(s), O(0.008 + 0.003 * (i % 2)), O(0.032), CREAM, segs=16, max_r=0.10)
        band(p, fit, _fore(s, 0.97), _fore_dir(s), O(0.012), O(0.016), BROWN, segs=16, max_r=0.10)
    return p.finish()


# ── Head ─────────────────────────────────────────────────────────────────────

def _head_tube(p, fit, z0, z1, offset, m, n, segs=28, a0=0.0, a1=2 * PI, closed=True, cap_b=False, rim=0.006,
               flare=None):
    return tube(p, fit, H(Z(z0)), H(Z(z1)), O(offset), m, n=n, segs=segs, max_r=0.3, a0=a0, a1=a1, closed=closed,
                cap_b=cap_b, rim=O(rim), flare=flare)


CROWN = 1.795           # highest ring the head can be ray-cast at (the crown is 1.802)


def helm_iron_full(fit):
    p = Piece("SK_helm_iron_full")
    _head_tube(p, fit, 0.80, 0.865, 0.016, PLATE, 2, flare=lambda t: O(0.012) * (1 - t))            # neck guard
    _head_tube(p, fit, 0.865, 1.04, 0.014, PLATE, 4, a0=0.6, a1=2 * PI - 0.6, closed=False)          # face opening
    tube(p, fit, H(Z(1.04)), H(CROWN), O(0.014), PLATE, n=5, segs=28, max_r=0.3, cap_b=True, rim=O(0.006))  # skull
    fy = front_y(H(Z(0.95)), O(0.02))
    w = 0.012 * HS * 1.4
    p.box((-w, fy - O(0.012) * HS, Z(0.87)), (w, fy + O(0.03) * HS, Z(1.045)), IRON_D)             # nasal bar
    p.box((-0.075 * HS * 1.6, fy + 0.004, Z(0.955)), (0.075 * HS * 1.6, fy + O(0.04) * HS, Z(0.972)), IRON_D)
    top = CROWN + O(0.014) * 0.8
    p.box((-w, mb.HEAD_Y - 0.10, top - 0.012), (w, mb.HEAD_Y + 0.12, top + 0.025), IRON_D)         # crest
    return p.finish()


def hood_scout(fit):
    p = Piece("SK_hood_scout")
    tube(p, fit, H(Z(0.80)), H(CROWN), O(0.020), LEATHER, n=9, segs=28, max_r=0.3, a0=0.95, a1=2 * PI - 0.95,
         closed=False, cap_b=True, rim=O(0.006))
    # shoulder mantle: fitted rings from the neck over the shoulders (the
    # MetaHuman's shoulders are far wider than its neck, so a free-hanging
    # sheet from the collar would cut into them)
    tube(p, fit, C(Z(0.705)), C(1.385), O(0.028), LEATHER, n=5, segs=28, max_r=0.30,
         flare=lambda t: O(0.008) * t)
    band(p, fit, C(1.385 + O(0.005)), UP, O(0.038), O(0.012), LEATHER_D, segs=28, max_r=0.30)      # hem
    ring_band(p, fit, 0.712, 0.016, 0.03, LEATHER_D, segs=20, max_r=0.13)
    return p.finish()


def hat_adept(fit):
    p = Piece("SK_hat_adept")
    zb = Z(1.035)
    c0 = H(zb)
    inner = fit.ring(c0, UP, O(0.014), 28, 0.3)
    outer = [q + (q - c0).normalized() * 0.19 * HS * 1.25 - V((0, 0, 0.025 * HS)) for q in inner]
    mid = [(a + b) / 2 + V((0, 0, 0.012 * HS)) for a, b in zip(inner, outer)]
    p.loft([inner, mid, outer], BLUE, rim=0.0, orient=V((0, 0, 1)))                             # brim, top
    p.loft([inner, mid, outer], BLUE, rim=0.0, orient=V((0, 0, -1)))                            # brim, underside
    tube(p, fit, c0, H(CROWN - 0.02), O(0.014), BLUE, n=3, segs=28, max_r=0.3, rim=O(0.006))
    top = fit.ring(H(CROWN - 0.02), UP, O(0.014), 28, 0.3)
    c = sum(top, V()) / len(top)
    rings = [top]
    for i in range(1, 7):
        t = i / 6
        k = (1 - t) ** 1.3
        rings.append([c + (q - c) * k + V((0, 0.10 * t * t, 0.30 * t)) for q in top])
    p.loft(rings, BLUE, rim=0.0, cap_b=c + V((0, 0.10, 0.31)))                                  # bent cone
    band(p, fit, H(Z(1.065)), UP, O(0.024), O(0.035), LEATHER_D, segs=28, max_r=0.3)
    return p.finish()


def cloak_warden(fit):
    p = Piece("SK_cloak_warden")
    tube(p, fit, C(Z(0.705)), C(1.40), O(0.042), GREEN, n=4, segs=28, max_r=0.30,
         flare=lambda t: O(0.012) * t)                                                           # shoulder cape
    back = lambda z: V((0.0, mb.spine_y(z), z))
    rings = mb.drape(p, fit, 1.43, Z(0.11), O(0.036), GREEN, flare=1.2, n=12, segs=16, a0=PI - 1.25,
                     a1=PI + 1.25, closed=False, far=0.32, sway=lambda t: V((0, 0.07 * t * t, 0)), center=back)
    mb.hem(p, rings, LEATHER_D, 0.04, closed=False, center=back)
    z = Z(0.66)
    p.stud((0, front_y(C(z), O(0.034)), z), O(0.016), STEEL)                                   # clasp
    return p.finish()


PIECES = {
    "SK_chest_travelers_jerkin": chest_travelers_jerkin, "SK_chest_priests_chain": chest_priests_chain,
    "SK_chest_apprentice_robe": chest_apprentice_robe, "SK_legs_travelers": legs_travelers,
    "SK_legs_iron_plate": legs_iron_plate, "SK_legs_apprentice_robe": legs_apprentice_robe,
    "SK_boots_ranger": boots_ranger, "SK_boots_iron_sabatons": boots_iron_sabatons,
    "SK_boots_cloth_slippers": boots_cloth_slippers, "SK_gloves_ranger_bracers": gloves_ranger_bracers,
    "SK_gloves_iron_gauntlets": gloves_iron_gauntlets, "SK_gloves_cloth_wraps": gloves_cloth_wraps,
    "SK_helm_iron_full": helm_iron_full, "SK_hood_scout": hood_scout, "SK_hat_adept": hat_adept,
    "SK_cloak_warden": cloak_warden,
}

# Weighting: smoothing passes for hanging cloth; rigid head pieces.
SMOOTH = {"SK_chest_priests_chain": 4, "SK_chest_apprentice_robe": 8, "SK_legs_apprentice_robe": 10,
          "SK_cloak_warden": 10, "SK_hood_scout": 2}
RIGID = {"SK_helm_iron_full": {"head": 1.0}, "SK_hat_adept": {"head": 1.0}}
HEAD_ABOVE = {"SK_hood_scout": (1.54, 1.575)}       # hood: head bone above this blend


def _rigid_above(ob, z0, z1, bone="head"):
    """Blend vertices from their transferred weights (below z0) to ``bone`` only (above z1)."""
    g = ob.vertex_groups.get(bone) or ob.vertex_groups.new(name=bone)
    for v in ob.data.vertices:
        z = (ob.matrix_world @ v.co).z
        if z <= z0:
            continue
        t = min(1.0, (z - z0) / (z1 - z0))
        for vg in v.groups:
            if ob.vertex_groups[vg.group].name != bone:
                vg.weight *= (1.0 - t)
        cur = next((vg.weight for vg in v.groups if ob.vertex_groups[vg.group].name == bone), 0.0)
        tot = sum(vg.weight for vg in v.groups if ob.vertex_groups[vg.group].name != bone)
        g.add([v.index], max(0.0, 1.0 - tot) if t >= 1.0 else cur + t * (1.0 - tot - cur), "REPLACE")


SMOOTH_BELOW = {"SK_cloak_warden": 1.36, "SK_hood_scout": 1.36}   # capes keep their arm weights


def weight(ob):
    mb.transfer_weights(ob, smooth=SMOOTH.get(ob.name, 0), only=RIGID.get(ob.name),
                        smooth_below=SMOOTH_BELOW.get(ob.name, 9.0))
    if ob.name in HEAD_ABOVE:
        _rigid_above(ob, *HEAD_ABOVE[ob.name])
    return ob


fit_now = None


def build(names=None):
    global fit_now
    fo = bpy.data.objects[mb.FIT]
    fo.hide_viewport = False
    fo.hide_set(False)
    fit_now = Fit(fo)
    fo.hide_set(True)
    out = []
    for name in (names or PIECES):
        if name in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[name])
        ob = PIECES[name](fit_now)
        weight(ob)
        out.append(ob)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    return out


def report(ob):
    return dict(name=ob.name, tris=sum(len(p.vertices) - 2 for p in ob.data.polygons), verts=len(ob.data.vertices),
                slots=[m.name for m in ob.data.materials], groups=len(ob.vertex_groups))


def build_all(names=None, do_export=True, save_blend=True):
    pieces = build(names)
    if do_export:
        for ob in pieces:
            mb.export_fbx(ob, mb.EQUIP_OUT)
    if save_blend:
        mb.save()
    return [report(o) for o in pieces]
