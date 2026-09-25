"""B-15 Wave 5b: a plate cuirass (chest slot) and an iron crossbow (weapon).

* ``SK_chest_iron_cuirass`` — steel breast- and backplate with a central keel,
  a gorget, a two-lame fauld, a belt and three-lame pauldrons, gold trim on
  the edges (ArtBible: "plate with gold trim"). It is built for the
  MetaHuman body only (Kevin, 2026-09-25: the MetaHuman is the only
  body type, so new armour is never built for the old body):
    - ``wave2mh_armour`` pipeline: ``Z``/``O`` landmark mapping, MetaHuman
      weights, ``Import/Characters/MetaHuman/Equipment/
      SK_chest_iron_cuirass.fbx``, saved in ``valhalla_mh_equipment.blend``.
  ``build_fable()`` (the old-body variant) is kept for reference only and is
  not part of ``build_all()``.
  Materials are the existing MI_IronPlate / MI_Gold / MI_LeatherDark slots.
* ``SM_crossbow_iron`` — wooden stock, iron prod, string, trigger, stirrup.
  Weapon convention (wave1_weapons.py, ValhallaVisuals.h): long axis along
  Blender +Y (prod at +Y), flat top (bolt groove) to +Z, grip at the origin
  with the trigger just below it. ``Import/Characters/Equipment/
  SM_crossbow_iron.glb``, saved in ``Blender assets/Characters/valhalla_crossbow.blend``.

Run in Blender (5.x): exec this file, then ``build_all()`` or the two
steps ``build_mh()``, ``build_crossbow()``. Each opens the
.blend it needs, so run them in that order or one at a time.
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


kit = _load("valhalla_kit")
wa = _load("wave2_armour")
w2 = _load("wave2_body")
mb = _load("wave2mh_base")
ma = _load("wave2mh_armour")

V = mathutils.Vector
PI = math.pi
UP = V((0, 0, 1))
Fit, Piece, tube, band = wa.Fit, wa.Piece, wa.tube, wa.band
PLATE, GOLD, LEATHER_D = wa.PLATE, wa.GOLD, wa.LEATHER_D

CUIRASS = "SK_chest_iron_cuirass"
CROSSBOW = "SM_crossbow_iron"
CROSSBOW_BLEND = os.path.join(kit.REPO, "Blender assets", "Characters", "valhalla_crossbow.blend")


# ── The cuirass design, body-agnostic ────────────────────────────────────────

class _MH:
    """Fitting environment on the MetaHuman (heights and thicknesses are
    Fable numbers, mapped by wave2mh_base)."""
    Z, O = staticmethod(mb.Z), staticmethod(mb.O)
    torso_r, neck_r, arm_r = ma.TORSO_R, 0.13, 0.14
    # pauldron lames below the cap: (t0, t1 along the upper arm, offset)
    pauldron = [(0.06, 0.26, 0.022), (0.24, 0.42, 0.015)]

    def C(self, z):
        return mb.C(mb.Z(z))

    def arm(self, s, t0, t1):
        return mb.arm_axis(s, t0, t1)

    def cap(self, p, fit, s):
        _cap_mh(p, fit, s)


class _Fable:
    """Fitting environment on the 122 cm Fable body (raw metres)."""
    Z = staticmethod(lambda z: z)
    O = staticmethod(lambda x: x)
    torso_r, neck_r, arm_r = 0.19, 0.09, 0.09
    pauldron = [(0.20, 0.40, 0.019), (0.38, 0.56, 0.013)]

    def C(self, z):
        return V((0.0, 0.0, z))

    def arm(self, s, t0, t1):
        return wa.arm_axis(s, t0, t1)

    def cap(self, p, fit, s):
        _cap_fable(p, fit, s)


def _outer(s):
    return (-PI, 0.0) if s > 0 else (0.0, PI)


def cuirass(fit, e):
    """Breastplate + backplate, gorget, fauld, belt and pauldrons on ``fit``
    using environment ``e``. Heights are Fable landmarks: hip 0.385, waist
    0.455, chest 0.58, shoulder 0.655, neck 0.685-0.71."""
    p = Piece(CUIRASS)
    O, C = e.O, e.C
    # the globose breast (tight waist, fullest at the lower ribs) as a function
    # of Fable height, so every plate stacked on the body bulges the same way
    belly_z = lambda z: O(0.009) * math.sin(PI * min(1.0, (z - 0.43) / 0.27 * 1.15))
    belly = lambda z0, z1: (lambda t: belly_z(z0 + (z1 - z0) * t))
    # body plates, waist to the base of the neck (the top ring runs out over
    # the trapezius so the gorget has something to sit on at the back)
    tube(p, fit, C(0.43), C(0.70), O(0.017), PLATE, n=10, segs=28, max_r=e.torso_r, flare=belly(0.43, 0.70),
         rim=O(0.006))
    # the keel down the front
    tube(p, fit, C(0.455), C(0.665), O(0.025), PLATE, n=3, segs=2, max_r=e.torso_r, a0=-0.09, a1=0.09,
         closed=False, rim=O(0.004), flare=belly(0.455, 0.665))
    # plackart: the lower front plate overlaps the breastplate at the ribs
    tube(p, fit, C(0.43), C(0.535), O(0.024), PLATE, n=3, segs=14, max_r=e.torso_r, a0=-1.45, a1=1.45,
         closed=False, rim=O(0.004), flare=belly(0.43, 0.535))
    # gorget with a gold lip: its bottom ring follows the trapezius and tucks
    # well under the top of the plates, its top ring stands off the neck
    tube(p, fit, C(0.69), C(0.745), O(0.007), PLATE, n=2, segs=20, max_r=e.torso_r, rim=O(0.005), offset_b=O(0.016))
    band(p, fit, C(0.7485), UP, O(0.018), O(0.012), GOLD, segs=20, max_r=e.neck_r, rim=0.0)
    # fauld: one lame below the plates, gold edge, rivets across the front
    band(p, fit, C(0.405), UP, O(0.027), O(0.05), PLATE, segs=28, max_r=e.torso_r, rim=O(0.006))
    band(p, fit, C(0.383), UP, O(0.030), O(0.012), GOLD, segs=28, max_r=e.torso_r, rim=0.0)
    c = C(0.405)
    for a in (-1.0, -0.5, 0.0, 0.5, 1.0):
        d = V((math.sin(a), -math.cos(a), 0.0))
        r = fit.radius(c, d, e.torso_r) or e.torso_r * 0.5
        p.stud(c + d * (r + O(0.027)), O(0.007), GOLD)
    # belt over the plates with a gold buckle
    band(p, fit, C(0.45), UP, O(0.024), O(0.03), LEATHER_D, segs=28, max_r=e.torso_r)
    c = C(0.45)
    fy = c.y - (fit.radius(c, V((0, -1, 0)), e.torso_r) or e.torso_r * 0.5) - O(0.024)
    p.box((-O(0.02), fy - O(0.008), c.z - O(0.017)), (O(0.02), fy + O(0.004), c.z + O(0.017)), GOLD)
    # pauldrons: a cap over the shoulder and lames stepping down the upper arm
    for s in (1, -1):
        # the lames wrap the outer half of the arm and a little of the front
        # and back (ring angle 0 is the front, so the inner quadrant next to
        # the torso is the one left open)
        a0, a1 = (-PI - 0.3, 0.9) if s > 0 else (-0.9, PI + 0.3)
        for (t0, t1, off) in e.pauldron:
            a, b = e.arm(s, t0, t1)
            tube(p, fit, a, b, O(off), PLATE, n=2, segs=12, max_r=e.arm_r, a0=a0, a1=a1, closed=False, rim=O(0.006))
        e.cap(p, fit, s)
    return p.finish()


def _cap_mh(p, fit, s):
    """MetaHuman: the top lame is a partial tube above the shoulder joint (the
    arm hangs in an A-pose, so its rings reach over the deltoid), with a gold
    ridge along its crown."""
    O = mb.O
    a0, a1 = _outer(s)
    a, b = mb.arm_axis(s, -0.32, 0.08)
    tube(p, fit, a, b, O(0.030), PLATE, n=2, segs=10, max_r=0.14, a0=a0, a1=a1, closed=False, rim=O(0.006))
    a, b = mb.arm_axis(s, -0.30, 0.06)
    mid = -PI / 2 if s > 0 else PI / 2
    tube(p, fit, a, b, O(0.036), GOLD, n=2, segs=2, max_r=0.14, a0=mid - 0.16, a1=mid + 0.16, closed=False,
         rim=O(0.003))


def _cap_fable(p, fit, s):
    """Fable body: the arm hangs straight down, so no ring around it can reach
    over the deltoid; the cap is a shell of the body's own shoulder skin
    (top and outer side of the deltoid) pushed out, with a gold boss on top."""
    region = lambda q: (q.x * s) > 0.15 and 0.585 < q.z < 0.745 and ((q.x * s) > 0.215 or q.z > 0.625 or (q.y < -0.02 and q.z > 0.60))
    mb.skin_shell(p, PLATE, None, 0.024, body_name=w2.BODY, ratio=1.0, region=region)     # ~72 quads a side
    _dome(p, V((s * 0.235, 0.0, 0.708 + 0.020)), V((s * 0.3, 0.0, 1.0)), 0.014, GOLD)


def _dome(p, c, n, r, m, segs=8):
    """``Piece.stud`` with an explicit normal (a stud's normal is radial from the
    body axis, useless on top of a shoulder)."""
    idx = p._mat(m)
    n = V(n).normalized()
    u = n.cross(V((0, 0, 1)) if abs(n.z) < 0.9 else V((0, -1, 0))).normalized()
    v = n.cross(u)
    ring = [c + u * (r * math.cos(2 * PI * k / segs)) + v * (r * math.sin(2 * PI * k / segs)) for k in range(segs)]
    verts = [p.bm.verts.new(q) for q in ring]
    t = p.bm.verts.new(c + n * (r * 0.6))
    for k in range(segs):
        f = p.bm.faces.new((verts[k], verts[(k + 1) % segs], t))
        f.material_index = idx
        f.normal_update()
        if f.normal.dot(n) < 0:
            f.normal_flip()
        for loop in f.loops:
            q = loop.vert.co
            loop[p.uv].uv = (q.x + q.y, q.z)


def _drop(name):
    """Remove an earlier build and its mesh, so the new mesh datablock gets the
    exact name (the glTF/FBX mesh name is what Unreal names the asset after)."""
    if name in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects[name])
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)


def report(ob):
    return dict(name=ob.name, tris=kit.tri_count(ob), verts=len(ob.data.vertices),
                slots=[m.name for m in ob.data.materials], groups=len(ob.vertex_groups),
                bbox=[tuple(round(c, 3) for c in ob.bound_box[0]), tuple(round(c, 3) for c in ob.bound_box[6])])


# ── MetaHuman variant ────────────────────────────────────────────────────────

def build_mh(do_export=True, save_blend=True, open_file=True):
    if open_file:
        bpy.ops.wm.open_mainfile(filepath=mb.BLEND, load_ui=False)
    fo = bpy.data.objects[mb.FIT]
    fo.hide_viewport = False
    fo.hide_set(False)
    fit = Fit(fo)
    fo.hide_set(True)
    ma.fit_now = fit
    _drop(CUIRASS)
    ob = cuirass(fit, _MH())
    mb.transfer_weights(ob)                       # rigid plate: no smoothing
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    if do_export:
        mb.export_fbx(ob, mb.EQUIP_OUT)
    if save_blend:
        mb.save()
    return report(ob)


# ── Fable-body variant ───────────────────────────────────────────────────────

def build_fable(do_export=True, save_blend=True, open_file=True):
    if open_file:
        bpy.ops.wm.open_mainfile(filepath=wa.EQUIP_BLEND, load_ui=False)
        arm = bpy.data.objects[w2.ARMATURE]
        if bpy.context.mode != "OBJECT":
            with bpy.context.temp_override(**w2._ctx()):
                bpy.ops.object.mode_set(mode="OBJECT")
        for pb in arm.pose.bones:
            pb.matrix_basis = mathutils.Matrix.Identity(4)
        bpy.context.view_layer.update()
    body, arm = bpy.data.objects[w2.BODY], bpy.data.objects[w2.ARMATURE]
    fit = Fit(body)
    _drop(CUIRASS)
    ob = cuirass(fit, _Fable())
    wa.weight(ob, arm)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    if do_export:
        wa.export(ob)
    if save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=wa.EQUIP_BLEND)
    return report(ob)


# ── Crossbow ─────────────────────────────────────────────────────────────────

TIMBER, IRON, STEEL, ROPE, LEATHER = "MI_Timber", "M_IronDark", "MI_Steel", "M_Rope", "MI_LeatherWrap"
kit.PREVIEW.update({TIMBER: (0.35, 0.24, 0.15), IRON: (0.10, 0.11, 0.12), STEEL: (0.55, 0.57, 0.60),
                    ROPE: (0.50, 0.38, 0.18), LEATHER: (0.20, 0.12, 0.07)})


def crossbow():
    """Built lying flat: stock along +Y (prod at the front, +Y), top to +Z,
    grip at the origin, trigger lever below it. 58 cm long, 50 cm prod span
    (a 0.85 m crossbow at the 0.68 prop scale)."""
    b = kit.MeshBuilder(CROSSBOW)
    # stock: butt at -0.20, tiller to 0.36; a tapered profile in y
    y0, y1 = -0.20, 0.36
    prof = [(y0, 0.024, 0.030), (-0.10, 0.021, 0.028), (-0.02, 0.019, 0.026), (0.06, 0.018, 0.028),
            (0.20, 0.015, 0.024), (y1, 0.014, 0.020)]        # (y, half-width, height)
    secs = []
    for y, w, h in prof:
        secs.append((y, [(-w, -h * 0.55), (w, -h * 0.55), (w, h * 0.45), (-w, h * 0.45)]))
    b.loft_y(secs, TIMBER)
    # bolt groove: a shallow strip inset in the top of the tiller
    b.box((-0.005, -0.03, 0.006), (0.005, y1 - 0.01, 0.014), IRON)
    # the nut (roller) that holds the string, and the iron band around it
    b.lathe_x([(0.016, -0.014), (0.016, 0.014)], 10, STEEL, cy=0.0, cz=0.012)
    b.box((-0.022, -0.05, -0.02), (0.022, 0.05, 0.006), STEEL)              # lock plate
    # trigger lever below the grip
    b.box((-0.005, -0.045, -0.055), (0.005, 0.0, -0.014), IRON)
    b.box((-0.005, -0.06, -0.06), (0.005, -0.04, -0.05), IRON)
    # leather wrap on the grip behind the trigger
    b.lathe_y([(0.03, -0.155), (0.03, -0.08)], 10, LEATHER, cx=0.0, cz=-0.002)
    # prod: a shallow "D" bow across the front, in two arms
    n = 8
    for s in (-1, 1):
        secs = []
        for i in range(n + 1):
            t = i / n
            x = s * 0.25 * t
            y = 0.33 - 0.08 * t * t                                       # arms sweep back
            w = 0.006 * (1.0 - 0.5 * t) + 0.002                           # thick at the centre
            d = 0.020 * (1.0 - 0.55 * t) + 0.004
            secs.append((x, [(y - w, -d), (y + w, -d), (y + w, d), (y - w, d)]))
        b.loft_x(secs, IRON)
    b.box((-0.03, 0.31, -0.02), (0.03, 0.35, 0.02), STEEL)                  # prod bracket
    b.box((-0.028, 0.29, -0.012), (0.028, 0.37, 0.012), IRON, bevel=0.003)
    # the string: from tip to tip, resting on the nut
    tip = 0.33 - 0.08
    for s in (-1, 1):
        b.loft_x([(s * 0.25, [(tip - 0.003, 0.011), (tip + 0.003, 0.011), (tip + 0.003, 0.017), (tip - 0.003, 0.017)]),
                  (0.0, [(-0.003, 0.021), (0.003, 0.021), (0.003, 0.027), (-0.003, 0.027)])], ROPE)
    # stirrup at the nose
    b.box((-0.03, 0.34, -0.06), (-0.024, 0.37, 0.0), IRON)
    b.box((0.024, 0.34, -0.06), (0.03, 0.37, 0.0), IRON)
    b.box((-0.03, 0.34, -0.066), (0.03, 0.37, -0.06), IRON)
    # a bolt in the groove: shaft, iron head past the nose, two vanes
    b.lathe_y([(0.004, -0.01), (0.004, 0.34)], 6, TIMBER, cx=0.0, cz=0.019)
    b.lathe_y([(0.006, 0.34), (0.006, 0.37), (0.001, 0.395)], 6, STEEL, cx=0.0, cz=0.019)
    for s in (-1, 1):
        b.loft_y([(0.01, [(s * 0.004, 0.019), (s * 0.02, 0.023), (s * 0.02, 0.019 + 0.0005), (s * 0.004, 0.019 + 0.0005)]),
                  (0.07, [(s * 0.004, 0.019), (s * 0.006, 0.02), (s * 0.006, 0.019 + 0.0005), (s * 0.004, 0.019 + 0.0005)])],
                 LEATHER)
    return b.finish()


def _loft_y(self, sections, m):
    """``sections`` = [(y, [(x, z), ...]), ...] along +Y: a stock, a haft."""
    before = set(self.bm.faces)
    rings = [[self.bm.verts.new((x, y, z)) for x, z in pts] for y, pts in sections]
    n = len(rings[0])
    for a, c in zip(rings, rings[1:]):
        for k in range(n):
            self.bm.faces.new((a[k], a[(k + 1) % n], c[(k + 1) % n], c[k]))
    self.bm.faces.new(list(reversed(rings[0])))
    self.bm.faces.new(rings[-1])
    return self._tag(before, m)


def _loft_x(self, sections, m):
    """``sections`` = [(x, [(y, z), ...]), ...] along X: bow arms, strings."""
    before = set(self.bm.faces)
    rings = [[self.bm.verts.new((x, y, z)) for y, z in pts] for x, pts in sections]
    n = len(rings[0])
    for a, c in zip(rings, rings[1:]):
        for k in range(n):
            self.bm.faces.new((a[k], a[(k + 1) % n], c[(k + 1) % n], c[k]))
    self.bm.faces.new(list(reversed(rings[0])))
    self.bm.faces.new(rings[-1])
    return self._tag(before, m)


def _revolve(self, rings, segs, m):
    before = set(self.bm.faces)
    for a, c in zip(rings, rings[1:]):
        for k in range(segs):
            self.bm.faces.new((a[k], a[(k + 1) % segs], c[(k + 1) % segs], c[k]))
    self.bm.faces.new(list(reversed(rings[0])))
    self.bm.faces.new(rings[-1])
    return self._tag(before, m)


def _lathe_x(self, profile, segs, m, cy=0.0, cz=0.0):
    """Revolve ``profile`` [(r, x), ...] around an axis parallel to X through
    (cy, cz): the roller nut across a stock."""
    rings = [[self.bm.verts.new((x, cy + r * math.cos(2 * PI * k / segs), cz + r * math.sin(2 * PI * k / segs)))
              for k in range(segs)] for r, x in profile]
    return _revolve(self, rings, segs, m)


def _lathe_y(self, profile, segs, m, cx=0.0, cz=0.0):
    """Revolve ``profile`` [(r, y), ...] around an axis parallel to Y through
    (cx, cz): a grip wrap on a horizontal stock."""
    rings = [[self.bm.verts.new((cx + r * math.cos(2 * PI * k / segs), y, cz + r * math.sin(2 * PI * k / segs)))
              for k in range(segs)] for r, y in profile]
    return _revolve(self, rings, segs, m)


kit.MeshBuilder.loft_y = _loft_y
kit.MeshBuilder.loft_x = _loft_x
kit.MeshBuilder.lathe_x = _lathe_x
kit.MeshBuilder.lathe_y = _lathe_y


def build_crossbow(do_export=True, save_blend=True):
    bpy.ops.wm.read_homefile(use_empty=True)
    ob = crossbow()
    bmesh_fix_normals(ob)
    if do_export:
        kit.export_glb(ob, os.path.join(wa.EQUIP_DIR, ob.name + ".glb"))
    if save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=CROSSBOW_BLEND)
    return report(ob)


def bmesh_fix_normals(ob):
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()


def build_all():
    out = {}
    out["mh"] = build_mh()
    out["crossbow"] = build_crossbow()
    return out
