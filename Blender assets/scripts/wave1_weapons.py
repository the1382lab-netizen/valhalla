"""B-15 Wave 1: weapons and shields (A-021..A-028).

Replaces SM_sword_iron, SM_staff_apprentice, SM_bow_hunting, SM_mace_priests,
SM_shield_buckler_iron, SM_shield_kite_iron, and adds SM_dagger_iron and
SM_totem_bone (the dagger and bone totem used to borrow the sword and mace).

Orientation is the first pass's, measured from the old .glb files, because the
hand socket (socket_weapon_r) is aligned to it:
- one-handed weapons point along Blender +Y (blade tip / head at +Y), grip
  centred on the origin, pommel just below it (-Y);
- the bow's limbs run along Y, its belly bulges to +Z and the string sits at -Z;
- shields face +Z (boss and painted face at +Z), handle behind at Z ~ 0.
Long weapons are modelled standing up along +Z and turned with
``kit.rotate_up_to_y``; shields are built in place.

Run in Blender: exec, then build_all().
"""

import importlib.util
import math
import os

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"
_spec = importlib.util.spec_from_file_location("valhalla_kit", os.path.join(_here, "valhalla_kit.py"))
kit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(kit)

EQUIP = os.path.join(kit.IMPORT, "Characters", "Equipment")

STEEL, IRON, LEATHER, GOLD = "MI_Steel", "M_IronDark", "MI_LeatherWrap", "MI_Gold"
WOOD, TIMBER, BONE, ROPE = "MI_PropWood", "MI_Timber", "MI_Bone", "M_Rope"
BLUE, MACE_GLOW, ORB_GLOW = "MI_ShieldBlue", "M_MaceGlow", "M_OrbGlow"


def _diamond(w, t):
    return [(-w, 0.0), (0.0, -t), (w, 0.0), (0.0, t)]


def _blade(b, z0, length, width, thick=0.006, mat=STEEL):
    """A diamond-section blade from z0, tapering to a point."""
    secs = [(z0, _diamond(width, thick)), (z0 + length * 0.75, _diamond(width * 0.85, thick * 0.9)),
            (z0 + length * 0.93, _diamond(width * 0.45, thick * 0.7))]
    b.loft(secs, mat, cap_bottom=True, tip=(0.0, 0.0, z0 + length))


def _grip(b, z0, z1, r=0.014, mat=LEATHER, rings=4):
    prof = []
    for i in range(rings + 1):
        z = z0 + (z1 - z0) * i / rings
        prof.append((r * (1.0 + (0.12 if i % 2 else 0.0)), z))
    b.lathe(prof, 8, mat)


def _ball(b, cz, r, mat, segs=10, cx=0.0, cy=0.0):
    prof = [(max(r * math.sin(math.pi * i / 6.0), 0.0005), cz - r * math.cos(math.pi * i / 6.0)) for i in range(7)]
    b.lathe(prof, segs, mat, cx=cx, cy=cy)


def sword():
    """62 cm arming sword: pommel at -0.06, grip -0.04..0.05, guard, blade to 0.56."""
    b = kit.MeshBuilder("SM_sword_iron")
    _ball(b, -0.045, 0.018, STEEL)
    _grip(b, -0.03, 0.05)
    b.box((-0.085, -0.016, 0.05), (0.085, 0.016, 0.068), STEEL, bevel=0.005)
    for s in (-1, 1):
        _ball(b, 0.059, 0.012, STEEL, segs=8, cx=s * 0.085)
    b.box((-0.022, -0.009, 0.068), (0.022, 0.009, 0.082), STEEL, bevel=0.003)   # ricasso block
    _blade(b, 0.082, 0.478, 0.024)
    return kit.rotate_up_to_y(b.finish())


def dagger():
    """New (A-027): 38 cm dagger, same grip position as the sword."""
    b = kit.MeshBuilder("SM_dagger_iron")
    _ball(b, -0.035, 0.014, STEEL)
    _grip(b, -0.022, 0.04, r=0.012, rings=3)
    b.box((-0.05, -0.013, 0.04), (0.05, 0.013, 0.054), STEEL, bevel=0.004)
    _blade(b, 0.054, 0.30, 0.02, thick=0.005)
    return kit.rotate_up_to_y(b.finish())


def mace():
    """Priest's flanged mace: pommel -0.09, grip -0.06..0.08, haft, flanged head
    at 0.33..0.43 and the glowing gem at the tip (0.42..0.46)."""
    b = kit.MeshBuilder("SM_mace_priests")
    _ball(b, -0.075, 0.016, STEEL)
    _grip(b, -0.06, 0.08, r=0.013)
    b.lathe([(0.013, 0.08), (0.011, 0.30), (0.02, 0.32)], 8, STEEL)
    b.lathe([(0.02, 0.32), (0.028, 0.34), (0.028, 0.41), (0.018, 0.425)], 10, STEEL)
    for k in range(6):
        a = 2 * math.pi * k / 6
        cx, cy = math.cos(a) * 0.04, math.sin(a) * 0.04
        # A flange: a flat blade radiating from the core.
        b.loft([(0.33, [(cx - math.sin(a) * 0.004, cy + math.cos(a) * 0.004),
                        (cx * 1.55 - math.sin(a) * 0.004, cy * 1.55 + math.cos(a) * 0.004),
                        (cx * 1.55 + math.sin(a) * 0.004, cy * 1.55 - math.cos(a) * 0.004),
                        (cx + math.sin(a) * 0.004, cy - math.cos(a) * 0.004)]),
                (0.415, [(cx - math.sin(a) * 0.004, cy + math.cos(a) * 0.004),
                         (cx * 1.4 - math.sin(a) * 0.004, cy * 1.4 + math.cos(a) * 0.004),
                         (cx * 1.4 + math.sin(a) * 0.004, cy * 1.4 - math.cos(a) * 0.004),
                         (cx + math.sin(a) * 0.004, cy - math.cos(a) * 0.004)])], STEEL)
    b.box((-0.04, -0.04, 0.33), (0.04, 0.04, 0.34), GOLD)
    _ball(b, 0.44, 0.02, MACE_GLOW, segs=10)
    return kit.rotate_up_to_y(b.finish())


def totem():
    """New (A-028): shaman's bone totem. A bone haft with a knuckle knob, a
    small skull-like head and two tusks, the leather grip where a mace's is."""
    b = kit.MeshBuilder("SM_totem_bone")
    _ball(b, -0.07, 0.018, BONE)
    _grip(b, -0.055, 0.08, r=0.014)
    b.lathe([(0.014, 0.08), (0.017, 0.12), (0.012, 0.20), (0.016, 0.28), (0.02, 0.30)], 8, BONE)
    for z in (0.14, 0.24):
        b.lathe([(0.018, z), (0.018, z + 0.012)], 8, ROPE)
    # Head: a rounded knob with a brow and a jaw.
    b.lathe([(0.02, 0.30), (0.045, 0.33), (0.05, 0.37), (0.04, 0.41), (0.015, 0.43), (0.002, 0.435)], 12, BONE)
    b.box((-0.03, 0.035, 0.345), (0.03, 0.052, 0.36), BONE, bevel=0.004)   # jaw ridge
    for s in (-1, 1):
        b.loft([(0.36, [(s * 0.035, -0.008), (s * 0.05, -0.008), (s * 0.05, 0.008), (s * 0.035, 0.008)]),
                (0.40, [(s * 0.06, -0.004), (s * 0.068, -0.004), (s * 0.068, 0.004), (s * 0.06, 0.004)])],
               BONE, tip=(s * 0.07, 0.0, 0.45))
    # Glowing gem on the brow (capped short cylinder; no sub-millimetre rings).
    b.lathe([(0.008, 0.374), (0.008, 0.386)], 6, MACE_GLOW, cx=0.0, cy=0.044, cap_bottom=True, cap_top=True)
    return kit.rotate_up_to_y(b.finish())


def staff():
    """Apprentice's staff: foot at -0.50, rope grip -0.08..0.07, shaft up to
    0.88 and an orb held in three prongs at 0.92..1.01."""
    b = kit.MeshBuilder("SM_staff_apprentice")
    prof = [(0.018, -0.50), (0.022, -0.47), (0.02, -0.2), (0.022, 0.2), (0.02, 0.6), (0.026, 0.84), (0.03, 0.88)]
    b.lathe(prof, 8, TIMBER)
    b.lathe([(0.025, -0.08), (0.025, 0.07)], 8, ROPE)
    for z in (0.3, 0.62):
        b.lathe([(0.026, z), (0.03, z + 0.02), (0.026, z + 0.04)], 8, TIMBER)
    for k in range(3):
        a = 2 * math.pi * k / 3
        c, s = math.cos(a), math.sin(a)
        b.loft([(0.86, [(c * 0.018 - s * 0.006, s * 0.018 + c * 0.006), (c * 0.018 + s * 0.006, s * 0.018 - c * 0.006),
                        (c * 0.026 + s * 0.006, s * 0.026 - c * 0.006), (c * 0.026 - s * 0.006, s * 0.026 + c * 0.006)]),
                (0.94, [(c * 0.04 - s * 0.005, s * 0.04 + c * 0.005), (c * 0.04 + s * 0.005, s * 0.04 - c * 0.005),
                        (c * 0.047 + s * 0.005, s * 0.047 - c * 0.005), (c * 0.047 - s * 0.005, s * 0.047 + c * 0.005)])],
               TIMBER, tip=(c * 0.03, s * 0.03, 1.0))
    _ball(b, 0.965, 0.042, ORB_GLOW, segs=12)
    return kit.rotate_up_to_y(b.finish())


def bow():
    """Hunting bow: limbs along Y (-0.5..0.5), belly to +Z (0.077), string at
    Z -0.03, leather grip in the middle. Modelled upright with the belly to -Y."""
    b = kit.MeshBuilder("SM_bow_hunting")
    n = 14
    secs = []
    for i in range(n + 1):
        t = i / n
        z = -0.5 + t
        bulge = 0.077 * math.cos(math.pi * (t - 0.5)) ** 0.8
        belly_y = -bulge
        w = 0.013 * (1.0 - 0.45 * abs(t - 0.5) * 2) + 0.004
        d = 0.009 * (1.0 - 0.4 * abs(t - 0.5) * 2) + 0.004
        secs.append((z, [(-w, belly_y - d), (w, belly_y - d), (w, belly_y + d), (-w, belly_y + d)]))
    b.loft(secs, TIMBER)
    # Horn nocks at the tips, and the grip wrap.
    b.lathe([(0.02, -0.07), (0.02, 0.07)], 8, LEATHER, cy=-0.072)
    b.box((-0.002, 0.026, -0.49), (0.002, 0.03, 0.49), ROPE)          # the string, at Z -0.03 once turned
    return kit.rotate_up_to_y(b.finish())


def buckler():
    """Round iron buckler, face to +Z: rim 0.178, domed face, boss to 0.119,
    a leather strap behind."""
    b = kit.MeshBuilder("SM_shield_buckler_iron")
    b.lathe([(0.178, 0.031), (0.178, 0.05), (0.165, 0.056)], 24, IRON, cap_bottom=True, cap_top=False)
    b.lathe([(0.165, 0.056), (0.12, 0.068), (0.06, 0.078)], 24, STEEL, cap_bottom=False, cap_top=False)
    b.lathe([(0.06, 0.078), (0.055, 0.1), (0.035, 0.115), (0.001, 0.119)], 16, IRON, cap_bottom=False)
    for k in range(8):
        a = 2 * math.pi * k / 8
        _ball(b, 0.056, 0.008, GOLD, segs=6, cx=math.cos(a) * 0.15, cy=math.sin(a) * 0.15)
    b.box((-0.015, -0.11, -0.005), (0.015, 0.11, 0.031), LEATHER)
    return b.finish()


def kite_shield():
    """Kite shield, face to +Z: flat top 0.5 wide at Y 0.42, point at Y -0.28.
    Oak body, iron rim, a blue field with our own gold chevron, iron boss."""
    b = kit.MeshBuilder("SM_shield_kite_iron")
    outline = [(-0.25, 0.42), (-0.25, 0.20), (-0.20, 0.02), (-0.10, -0.18), (0.0, -0.28),
               (0.10, -0.18), (0.20, 0.02), (0.25, 0.20), (0.25, 0.42)]
    outline = list(reversed(outline))
    b.prism_xy(outline, -0.005, 0.045, WOOD)
    inset = [(x * 0.9, y * 0.9 + 0.012) for x, y in outline]
    b.prism_xy(inset, 0.045, 0.06, BLUE)
    # Iron rim: short slabs following the outline.
    for (x0, y0), (x1, y1) in zip(outline, outline[1:] + outline[:1]):
        ln = math.hypot(x1 - x0, y1 - y0)
        nx, ny = (y1 - y0) / ln, -(x1 - x0) / ln
        t = 0.018
        quad = [(x0, y0), (x1, y1), (x1 - nx * t, y1 - ny * t), (x0 - nx * t, y0 - ny * t)]
        b.prism_xy(quad, -0.006, 0.066, IRON)
    # A gold chevron, then the boss.
    b.prism_xy([(-0.16, 0.02), (0.0, 0.2), (0.16, 0.02), (0.16, -0.04), (0.0, 0.14), (-0.16, -0.04)], 0.06, 0.07, GOLD)
    b.lathe([(0.05, 0.06), (0.045, 0.08), (0.03, 0.098), (0.001, 0.105)], 14, IRON, cy=0.22, cap_bottom=False)
    b.box((-0.015, 0.05, -0.006), (0.015, 0.33, 0.0), LEATHER)
    return b.finish()


PIECES = [sword, dagger, mace, totem, staff, bow, buckler, kite_shield]


def build_all(export=True):
    kit.reset_scene()
    import mathutils
    out = []
    for i, fn in enumerate(PIECES):
        obj = fn()
        if export:
            kit.export_glb(obj, os.path.join(EQUIP, obj.name + ".glb"))
        bb = [mathutils.Vector(c) for c in obj.bound_box]
        mn = [round(min(v[k] for v in bb), 3) for k in range(3)]
        mx = [round(max(v[k] for v in bb), 3) for k in range(3)]
        obj.location.x = i * 0.5
        out.append((obj.name, kit.tri_count(obj), mn, mx, [m.name for m in obj.data.materials]))
    return out
