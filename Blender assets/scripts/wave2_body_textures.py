"""B-15 Wave 2, Stage A: baked procedural texture sets for the body and hair.

No CC0 library has a usable stylised skin, so the skin set is *procedural,
baked* (ArtBible section 5 allows either): Cycles bakes node trees into the
body's UVs. Two sets come out of here, in the usual ``Import/Textures/<Set>/``
layout (``T_<Set>_BC/_N/_ORM.png``, DirectX normals, ORM = AO / roughness /
metallic) so ``valhalla_tools.import_texture_sets.import_set`` imports them:

``SkinBase`` (2K, body)
    The game tints the skin at runtime by writing ``BaseColor`` on a dynamic
    instance of ``M_Skin`` (fair, tan, olive, dark, elder), and M_ValhallaPBR
    multiplies that tint by the base-colour map. So the map is *neutral*: near
    white with a few percent of mottling, warmer (redder) over the cheeks,
    knuckles, elbows and knees, darker in the creases, and the face is painted
    in as multipliers — eyebrows, lash lines, dark irises and pupils, a
    brighter sclera, rosier lips, shaded nostrils — placed in the body's
    object space with ellipsoid masks, at the same coordinates
    ``wave2_body.FEATURES`` sculpts. Every skin tint gets the same face.
    The normal map is baked from the dense sculpted high-res
    (``_BodyHighRes``) with a fine pore noise on top; AO is baked; roughness
    is 0.55 with the lips and eyes glossier.

``HairStrands`` (1K, all three hair meshes)
    A tiling strand texture: streaks along V (the clumps' UVs run root->tip in
    V), neutral grey-white so the ``M_Hair`` vertex colour still decides the
    colour, with a groove normal map and roughness 0.45.

Run after ``wave2_body.build_all()`` with the .blend open:
``bake_skin()``, ``bake_hair()`` (CPU Cycles, a few minutes), then
``manifest()`` records both in ``Import/Textures/texture_sets.json``.
"""

import importlib.util
import json
import os

import bpy
import numpy as np

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


vt = _load("valhalla_textures")
OUT_ROOT = vt.OUT_ROOT


# ── Node helpers ─────────────────────────────────────────────────────────────

class NT:
    """Thin wrapper: ``n = nt.add("ShaderNodeMath", operation="ADD", inputs={0: a, 1: 0.5})``."""

    def __init__(self, tree):
        self.tree = tree
        self.x = 0

    def add(self, kind, inputs=None, **props):
        n = self.tree.nodes.new(kind)
        n.location = (self.x, 0)
        self.x += 200
        for k, v in props.items():
            setattr(n, k, v)
        for key, val in (inputs or {}).items():
            sock = n.inputs[key]
            if isinstance(val, bpy.types.NodeSocket):
                self.tree.links.new(val, sock)
            elif isinstance(val, tuple) and len(val) == 2 and isinstance(val[0], bpy.types.Node):
                self.tree.links.new(val[0].outputs[val[1]], sock)
            else:
                sock.default_value = val
        return n

    def math(self, op, a, b=None, c=None):
        inputs = {0: a}
        if b is not None:
            inputs[1] = b
        if c is not None:
            inputs[2] = c
        return self.add("ShaderNodeMath", inputs, operation=op)

    def vmath(self, op, a, b=None):
        inputs = {0: a}
        if b is not None:
            inputs[1] = b
        return self.add("ShaderNodeVectorMath", inputs, operation=op)

    def mix(self, fac, a, b, kind="RGBA"):
        """Returns the result *socket* (a colour or a float)."""
        n = self.add("ShaderNodeMix", data_type=kind, blend_type="MIX")
        ia, ib, io = (6, 7, 2) if kind == "RGBA" else (2, 3, 0)
        for key, val in ((0, fac), (ia, a), (ib, b)):
            sock = n.inputs[key]
            if isinstance(val, bpy.types.NodeSocket):
                self.tree.links.new(val, sock)
            elif isinstance(val, tuple) and len(val) == 2 and isinstance(val[0], bpy.types.Node):
                self.tree.links.new(val[0].outputs[val[1]], sock)
            else:
                sock.default_value = val
        return n.outputs[io]

    def ellipse(self, vec, center, radii, soft=0.35, mirror=False):
        """1 inside an ellipsoid at ``center`` with ``radii`` (object space), 0
        outside, with a soft edge. ``mirror`` adds the X-mirrored twin."""
        d = self.vmath("SUBTRACT", vec, center)
        d = self.vmath("DIVIDE", (d, 0), radii)
        ln = self.vmath("LENGTH", (d, 0))
        m = self.add("ShaderNodeMapRange", {0: (ln, "Value"), 1: 1.0, 2: 1.0 - soft, 3: 0.0, 4: 1.0},
                     interpolation_type="SMOOTHSTEP", clamp=True)
        if mirror:
            m2 = self.ellipse(vec, (-center[0], center[1], center[2]), radii, soft)
            return self.math("MAXIMUM", (m, "Result"), (m2, "Result"))
        return m


def _fresh_material(name):
    mat = bpy.data.materials.get(name)
    if mat is not None:
        bpy.data.materials.remove(mat)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.node_tree.nodes.clear()
    return mat


def _bake_target(mat, image):
    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.location = (0, -600)
    mat.node_tree.nodes.active = tex
    tex.select = True
    return tex


def _image(name, size):
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    img = bpy.data.images.new(name, width=size, height=size, alpha=False, float_buffer=False)
    return img


def _save(img, path, colorspace):
    w, h = img.size
    arr = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(arr)
    vt._save(arr.reshape(h, w, 4), w, h, path, colorspace)
    return arr.reshape(h, w, 4)


# ── Skin ─────────────────────────────────────────────────────────────────────

# Feature placement (object space, metres) — the Stage A head by default;
# ``set_face`` swaps in another body's landmarks (wave2_body2.FACE), with
# ``FACE_SCALE`` shrinking every painted feature (eyes, brows, lips) to it.
EYE = (0.075, -0.205, 0.962)
BROW = (0.08, -0.20, 1.005)
MOUTH = (0.0, -0.215, 0.868)
NOSTRIL = (0.017, -0.222, 0.905)
FACE_SCALE = 1.0
# warm zones: cheeks, nose tip, ears, knuckles, elbows, knees, fingertips
WARM = [((0.13, -0.15, 0.925), (0.07, 0.06, 0.05), True), ((0.0, -0.235, 0.92), (0.035, 0.03, 0.03), False),
        ((0.21, 0.015, 0.955), (0.03, 0.04, 0.05), True), ((0.235, -0.02, 0.325), (0.06, 0.03, 0.03), True),
        ((0.235, 0.06, 0.53), (0.05, 0.04, 0.05), True), ((0.085, -0.06, 0.215), (0.05, 0.04, 0.05), True),
        ((0.235, -0.02, 0.29), (0.06, 0.03, 0.02), True)]


def set_face(face):
    """``face`` = dict(EYE=, BROW=, MOUTH=, NOSTRIL=, SCALE=, WARM=[...])."""
    global EYE, BROW, MOUTH, NOSTRIL, FACE_SCALE, WARM
    EYE, BROW, MOUTH, NOSTRIL = face["EYE"], face["BROW"], face["MOUTH"], face["NOSTRIL"]
    FACE_SCALE = face.get("SCALE", 1.0)
    WARM = face.get("WARM", WARM)


def _r(x, y, z):
    """Feature radii at FACE_SCALE; the depth (y) radius keeps at least 2 cm so
    the mask reaches the skin whichever few mm the surface sits from the
    landmark."""
    return (x * FACE_SCALE, max(y * FACE_SCALE, 0.02), z * FACE_SCALE)


def skin_color_tree(nt, out_socket_target):
    """Neutral tint-multiplier colour: mottling, warm zones, painted face."""
    tc = nt.add("ShaderNodeTexCoord")
    obj = tc.outputs["Object"]
    # mottling: two noise scales, ±4%
    n1 = nt.add("ShaderNodeTexNoise", {"Vector": obj, "Scale": 60.0, "Detail": 4.0, "Roughness": 0.6})
    n2 = nt.add("ShaderNodeTexNoise", {"Vector": obj, "Scale": 300.0, "Detail": 2.0, "Roughness": 0.5})
    mot = nt.math("ADD", nt.math("MULTIPLY", nt.math("SUBTRACT", (n1, "Fac"), 0.5).outputs[0], 0.08).outputs[0],
                  nt.math("MULTIPLY", nt.math("SUBTRACT", (n2, "Fac"), 0.5).outputs[0], 0.04).outputs[0])
    base_v = nt.math("ADD", (mot, 0), 0.955)
    base = nt.add("ShaderNodeCombineColor", {0: (base_v, 0), 1: (base_v, 0), 2: (base_v, 0)}, mode="RGB")
    col = base.outputs[0]
    # warm zones: cheeks, nose tip, ears, knuckles, elbows, knees, fingertips
    warm = (1.0, 0.86, 0.84, 1.0)
    for c, r, mir in WARM:
        m = nt.ellipse(obj, c, r, soft=0.6, mirror=mir)
        col = nt.mix(nt.math("MULTIPLY", (m, 0 if mir else "Result"), 0.7).outputs[0], col, warm)
    # eyebrows: elongated, slightly arched (two overlapping ellipsoids)
    brow_col = (0.36, 0.30, 0.26, 1.0)
    for dx, dz, rx in ((0.0, 0.0, 0.045), (0.035, 0.008, 0.03)):
        m = nt.ellipse(obj, (BROW[0] + dx * FACE_SCALE, BROW[1], BROW[2] + dz * FACE_SCALE), _r(rx, 0.03, 0.009), soft=0.45, mirror=True)
        col = nt.mix((m, 0), col, brow_col)
    # eye: lid line (dark), sclera (bright), iris, pupil
    lid = nt.ellipse(obj, (EYE[0], EYE[1] + 0.005 * FACE_SCALE, EYE[2] + 0.014 * FACE_SCALE), _r(0.028, 0.02, 0.0045), soft=0.5, mirror=True)
    col = nt.mix((lid, 0), col, (0.42, 0.34, 0.32, 1.0))
    sclera = nt.ellipse(obj, EYE, _r(0.027, 0.025, 0.015), soft=0.25, mirror=True)
    col = nt.mix((sclera, 0), col, (1.0, 1.0, 1.0, 1.0))
    iris = nt.ellipse(obj, EYE, _r(0.013, 0.025, 0.013), soft=0.2, mirror=True)
    col = nt.mix((iris, 0), col, (0.22, 0.30, 0.42, 1.0))
    pupil = nt.ellipse(obj, EYE, _r(0.006, 0.025, 0.006), soft=0.3, mirror=True)
    col = nt.mix((pupil, 0), col, (0.04, 0.04, 0.05, 1.0))
    # nostrils and the mouth
    nos = nt.ellipse(obj, NOSTRIL, _r(0.008, 0.012, 0.007), soft=0.5, mirror=True)
    col = nt.mix(nt.math("MULTIPLY", (nos, 0), 0.6).outputs[0], col, (0.45, 0.35, 0.33, 1.0))
    lips = nt.ellipse(obj, MOUTH, _r(0.046, 0.02, 0.014), soft=0.45)
    col = nt.mix(nt.math("MULTIPLY", (lips, "Result"), 0.8).outputs[0], col, (0.95, 0.62, 0.60, 1.0))
    line = nt.ellipse(obj, (MOUTH[0], MOUTH[1], MOUTH[2] + 0.004 * FACE_SCALE), _r(0.040, 0.02, 0.0035), soft=0.6)
    col = nt.mix((line, "Result"), col, (0.55, 0.40, 0.40, 1.0))
    nt.tree.links.new(col, out_socket_target)
    return col


def skin_rough_tree(nt):
    tc = nt.add("ShaderNodeTexCoord")
    obj = tc.outputs["Object"]
    n = nt.add("ShaderNodeTexNoise", {"Vector": obj, "Scale": 80.0, "Detail": 3.0})
    r = nt.math("ADD", nt.math("MULTIPLY", (n, "Fac"), 0.12).outputs[0], 0.50).outputs[0]
    lips = nt.ellipse(obj, MOUTH, _r(0.046, 0.02, 0.014), soft=0.45)
    r = nt.mix((lips, "Result"), r, 0.38, kind="FLOAT")
    eye = nt.ellipse(obj, EYE, _r(0.027, 0.025, 0.015), soft=0.25, mirror=True)
    r = nt.mix((eye, 0), r, 0.15, kind="FLOAT")
    return r


def pore_bump(nt, strength=0.15):
    """Fine pore noise for the normal bake, on the high-res."""
    tc = nt.add("ShaderNodeTexCoord")
    n = nt.add("ShaderNodeTexNoise", {"Vector": tc.outputs["Object"], "Scale": 900.0, "Detail": 2.0, "Roughness": 0.5})
    n2 = nt.add("ShaderNodeTexNoise", {"Vector": tc.outputs["Object"], "Scale": 120.0, "Detail": 3.0})
    h = nt.math("ADD", nt.math("MULTIPLY", (n, "Fac"), 0.6).outputs[0], nt.math("MULTIPLY", (n2, "Fac"), 0.4).outputs[0])
    return nt.add("ShaderNodeBump", {"Strength": strength, "Distance": 0.002, "Height": (h, 0)})


def _ctx():
    win = bpy.context.window or bpy.context.window_manager.windows[0]
    for area in win.screen.areas:
        if area.type == "VIEW_3D":
            region = next(r for r in area.regions if r.type == "WINDOW")
            return dict(window=win, screen=win.screen, area=area, region=region,
                        scene=bpy.context.scene, view_layer=bpy.context.view_layer)
    return dict(window=win, screen=win.screen, scene=bpy.context.scene, view_layer=bpy.context.view_layer)


def _bake(**kw):
    with bpy.context.temp_override(**_ctx()):
        bpy.ops.object.bake(**kw)


def _deselect_all():
    for o in bpy.data.objects:
        try:
            o.select_set(False)
        except RuntimeError:
            pass


def _select(objs, active):
    _deselect_all()
    for o in objs:
        o.hide_set(False)
        o.hide_render = False
        o.select_set(True)
    bpy.context.view_layer.objects.active = active


def _cycles(samples):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.render.bake.margin = 32
    scene.render.bake.use_clear = True


def bake_skin(size=2048, set_name="SkinBase", low="SK_Valhalla_Body", high="_BodyHighRes"):
    lo, hi = bpy.data.objects[low], bpy.data.objects[high]
    folder = os.path.join(OUT_ROOT, set_name)
    os.makedirs(folder, exist_ok=True)
    scene = bpy.context.scene
    old_mats = [m for m in lo.data.materials]

    # Colour
    mat = _fresh_material("_bake_skin_color")
    nt = NT(mat.node_tree)
    out = nt.add("ShaderNodeOutputMaterial")
    bsdf = nt.add("ShaderNodeBsdfDiffuse")
    mat.node_tree.links.new(bsdf.outputs[0], out.inputs[0])
    skin_color_tree(nt, bsdf.inputs["Color"])
    img_bc = _image("_bake_bc", size)
    _bake_target(mat, img_bc)
    lo.data.materials.clear()
    lo.data.materials.append(mat)
    _select([lo], lo)
    _cycles(1)
    scene.render.bake.use_selected_to_active = False
    _bake(type="DIFFUSE", pass_filter={"COLOR"}, use_clear=True, margin=32)
    _save(img_bc, os.path.join(folder, "T_%s_BC.png" % set_name), "sRGB")

    # Roughness
    mat_r = _fresh_material("_bake_skin_rough")
    nt = NT(mat_r.node_tree)
    out = nt.add("ShaderNodeOutputMaterial")
    bsdf = nt.add("ShaderNodeBsdfPrincipled")
    mat_r.node_tree.links.new(bsdf.outputs[0], out.inputs[0])
    r = skin_rough_tree(nt)
    mat_r.node_tree.links.new(r, bsdf.inputs["Roughness"])
    img_r = _image("_bake_r", size)
    _bake_target(mat_r, img_r)
    lo.data.materials.clear()
    lo.data.materials.append(mat_r)
    _select([lo], lo)
    _bake(type="ROUGHNESS", use_clear=True, margin=32)
    rough = _save(img_r, os.path.join(folder, "_rough.png"), "Non-Color")

    # Normal: high-res (with pore bump) -> low-res tangent space
    mat_h = _fresh_material("_bake_skin_pores")
    nt = NT(mat_h.node_tree)
    out = nt.add("ShaderNodeOutputMaterial")
    bsdf = nt.add("ShaderNodeBsdfDiffuse")
    mat_h.node_tree.links.new(bsdf.outputs[0], out.inputs[0])
    bump = pore_bump(nt)
    mat_h.node_tree.links.new(bump.outputs[0], bsdf.inputs["Normal"])
    hi.data.materials.clear()
    hi.data.materials.append(mat_h)
    mat_n = _fresh_material("_bake_skin_normal")
    img_n = _image("_bake_n", size)
    _bake_target(mat_n, img_n)
    lo.data.materials.clear()
    lo.data.materials.append(mat_n)
    _select([hi, lo], lo)
    scene.render.bake.use_selected_to_active = True
    scene.render.bake.cage_extrusion = 0.02
    scene.render.bake.max_ray_distance = 0.06
    scene.render.bake.normal_space = "TANGENT"
    _bake(type="NORMAL", use_clear=True, margin=32, use_selected_to_active=True,
                        cage_extrusion=0.02, max_ray_distance=0.06)
    n = _save(img_n, os.path.join(folder, "_normal_gl.png"), "Non-Color")
    n = n.copy()
    n[..., 1] = 1.0 - n[..., 1]      # OpenGL -> DirectX
    vt._save(n, size, size, os.path.join(folder, "T_%s_N.png" % set_name), "Non-Color")

    # AO on the low-res alone
    scene.render.bake.use_selected_to_active = False
    img_ao = _image("_bake_ao", size)
    mat_ao = _fresh_material("_bake_skin_ao")
    _bake_target(mat_ao, img_ao)
    lo.data.materials.clear()
    lo.data.materials.append(mat_ao)
    _select([lo], lo)
    _cycles(24)
    scene.world = scene.world or bpy.data.worlds.new("World")
    _bake(type="AO", use_clear=True, margin=32)
    ao = _save(img_ao, os.path.join(folder, "_ao.png"), "Non-Color")

    orm = np.ones((size, size, 4), dtype=np.float32)
    orm[..., 0] = 0.35 + 0.65 * ao[..., 0]     # never fully black in the creases
    orm[..., 1] = rough[..., 0]
    orm[..., 2] = 0.0
    vt._save(orm, size, size, os.path.join(folder, "T_%s_ORM.png" % set_name), "Non-Color")

    for f in ("_rough.png", "_normal_gl.png", "_ao.png"):
        try:
            os.remove(os.path.join(folder, f))
        except OSError:
            pass
    # restore the preview materials
    lo.data.materials.clear()
    for m in old_mats:
        lo.data.materials.append(m)
    hi.data.materials.clear()
    for m in (mat, mat_r, mat_h, mat_n, mat_ao):
        bpy.data.materials.remove(m)
    for im in (img_bc, img_r, img_n, img_ao):
        bpy.data.images.remove(im)
    return folder


# ── Hair strands ─────────────────────────────────────────────────────────────

def strand_height(nt, uv):
    """Streaks along V: a distorted wave in U."""
    w = nt.add("ShaderNodeTexWave", {"Vector": uv, "Scale": 28.0, "Distortion": 1.6, "Detail": 3.0, "Detail Roughness": 0.6},
               wave_type="BANDS", bands_direction="X", wave_profile="SIN")
    n = nt.add("ShaderNodeTexNoise", {"Vector": uv, "Scale": 18.0, "Detail": 4.0})
    h = nt.math("ADD", nt.math("MULTIPLY", (w, "Fac"), 0.7).outputs[0], nt.math("MULTIPLY", (n, "Fac"), 0.3).outputs[0])
    return h


def bake_hair(size=1024, set_name="HairStrands"):
    folder = os.path.join(OUT_ROOT, set_name)
    os.makedirs(folder, exist_ok=True)
    scene = bpy.context.scene
    # A unit plane with 0..1 UVs; UVs must tile in V, so the wave runs in U
    # and the noise is cheap enough that the V seam is hidden by the strands.
    me = bpy.data.meshes.new("_strand_plane")
    me.from_pydata([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)], [], [(0, 1, 2, 3)])
    uv = me.uv_layers.new(name="UVMap")
    for i, co in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
        uv.data[i].uv = co
    ob = bpy.data.objects.new("_strand_plane", me)
    scene.collection.objects.link(ob)

    # colour + roughness from one material, normal from a bump on it
    mat = _fresh_material("_bake_hair")
    nt = NT(mat.node_tree)
    out = nt.add("ShaderNodeOutputMaterial")
    bsdf = nt.add("ShaderNodeBsdfPrincipled")
    mat.node_tree.links.new(bsdf.outputs[0], out.inputs[0])
    tc = nt.add("ShaderNodeTexCoord")
    uvs = tc.outputs["UV"]
    h = strand_height(nt, uvs)
    v = nt.math("ADD", nt.math("MULTIPLY", (h, 0), 0.35).outputs[0], 0.72)
    colr = nt.add("ShaderNodeCombineColor", {0: (v, 0), 1: (v, 0), 2: (v, 0)}, mode="RGB")
    mat.node_tree.links.new(colr.outputs[0], bsdf.inputs["Base Color"])
    r = nt.math("ADD", nt.math("MULTIPLY", (h, 0), 0.2).outputs[0], 0.38)
    mat.node_tree.links.new(r.outputs[0], bsdf.inputs["Roughness"])
    bump = nt.add("ShaderNodeBump", {"Strength": 0.6, "Distance": 0.01, "Height": (h, 0)})
    mat.node_tree.links.new(bump.outputs[0], bsdf.inputs["Normal"])
    me.materials.append(mat)
    _select([ob], ob)
    _cycles(1)
    scene.render.bake.use_selected_to_active = False

    img = _image("_bake_hair", size)
    _bake_target(mat, img)
    _bake(type="DIFFUSE", pass_filter={"COLOR"}, use_clear=True, margin=0)
    _save(img, os.path.join(folder, "T_%s_BC.png" % set_name), "sRGB")
    _bake(type="ROUGHNESS", use_clear=True, margin=0)
    rough = _save(img, os.path.join(folder, "_rough.png"), "Non-Color")
    scene.render.bake.normal_space = "TANGENT"
    _bake(type="NORMAL", use_clear=True, margin=0)
    n = _save(img, os.path.join(folder, "_n.png"), "Non-Color").copy()
    n[..., 1] = 1.0 - n[..., 1]
    vt._save(n, size, size, os.path.join(folder, "T_%s_N.png" % set_name), "Non-Color")
    orm = np.ones((size, size, 4), dtype=np.float32)
    orm[..., 0] = 0.6 + 0.4 * np.clip((rough[..., 0] - 0.38) / 0.2, 0, 1)   # grooves darker
    orm[..., 1] = rough[..., 0]
    orm[..., 2] = 0.0
    vt._save(orm, size, size, os.path.join(folder, "T_%s_ORM.png" % set_name), "Non-Color")
    for f in ("_rough.png", "_n.png"):
        try:
            os.remove(os.path.join(folder, f))
        except OSError:
            pass
    bpy.data.objects.remove(ob)
    bpy.data.meshes.remove(me)
    bpy.data.materials.remove(mat)
    bpy.data.images.remove(img)
    return folder


def manifest():
    data = {}
    if os.path.exists(vt.MANIFEST):
        with open(vt.MANIFEST, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    data["SkinBase"] = {"source": "procedural", "id": "wave2_body_textures.bake_skin", "resolution": "2k",
                        "real_size_m": 0.0, "url": "", "license": "own work (B-15 Wave 2)",
                        "note": "Baked from node trees on SK_Valhalla_Body / _BodyHighRes in valhalla_body.blend. "
                                "Neutral tint-multiplier colour with the face painted in; normal from the sculpted "
                                "high-res plus pore noise; AO baked; roughness 0.55."}
    data["HairStrands"] = {"source": "procedural", "id": "wave2_body_textures.bake_hair", "resolution": "1k",
                           "real_size_m": 0.0, "url": "", "license": "own work (B-15 Wave 2)",
                           "note": "Tiling strand streaks along V; multiplied by the M_Hair vertex colour."}
    with open(vt.MANIFEST, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
    return vt.MANIFEST


# ── Review only: see the baked set on the mesh (EEVEE) ───────────────────────

def preview(ob, set_name, tint=(1.0, 1.0, 1.0), vertex_color=False):
    """Swap the object's slot 0 for a temporary textured material. Never
    export with this on: ``unpreview`` puts the flat slot material back."""
    folder = os.path.join(OUT_ROOT, set_name)
    mat = _fresh_material("_preview_%s_%s" % (set_name, ob.name))
    nt = NT(mat.node_tree)
    out = nt.add("ShaderNodeOutputMaterial")
    bsdf = nt.add("ShaderNodeBsdfPrincipled")
    mat.node_tree.links.new(bsdf.outputs[0], out.inputs[0])
    imgs = {}
    for key, cs in (("BC", "sRGB"), ("N", "Non-Color"), ("ORM", "Non-Color")):
        path = os.path.join(folder, "T_%s_%s.png" % (set_name, key))
        img = bpy.data.images.load(path, check_existing=True)
        img.colorspace_settings.name = cs
        imgs[key] = nt.add("ShaderNodeTexImage", image=img)
    tint_node = nt.add("ShaderNodeRGB")
    tint_node.outputs[0].default_value = (tint[0], tint[1], tint[2], 1.0)
    col = nt.mix(1.0, (imgs["BC"], "Color"), (tint_node, 0))
    col_node = col.node
    col_node.blend_type = "MULTIPLY"
    if vertex_color:
        vc = nt.add("ShaderNodeVertexColor", layer_name="Col")
        col = nt.mix(1.0, col, (vc, "Color"))
        col.node.blend_type = "MULTIPLY"
    mat.node_tree.links.new(col, bsdf.inputs["Base Color"])
    sep = nt.add("ShaderNodeSeparateColor", {0: (imgs["ORM"], "Color")}, mode="RGB")
    mat.node_tree.links.new(sep.outputs[1], bsdf.inputs["Roughness"])
    # DirectX map back to OpenGL for Blender
    sepn = nt.add("ShaderNodeSeparateColor", {0: (imgs["N"], "Color")}, mode="RGB")
    inv = nt.math("SUBTRACT", 1.0, (sepn, 1))
    comb = nt.add("ShaderNodeCombineColor", {0: (sepn, 0), 1: (inv, 0), 2: (sepn, 2)}, mode="RGB")
    nmap = nt.add("ShaderNodeNormalMap", {"Color": (comb, 0), "Strength": 1.0})
    mat.node_tree.links.new(nmap.outputs[0], bsdf.inputs["Normal"])
    ob["_preview_slot0"] = ob.data.materials[0].name if ob.data.materials else ""
    if ob.data.materials:
        ob.data.materials[0] = mat
    else:
        ob.data.materials.append(mat)
    return mat


def unpreview(ob):
    name = ob.get("_preview_slot0")
    if name:
        ob.data.materials[0] = bpy.data.materials[name]
        del ob["_preview_slot0"]
    for m in list(bpy.data.materials):
        if m.name.startswith("_preview_"):
            bpy.data.materials.remove(m)
    for img in list(bpy.data.images):
        if img.name.startswith("T_") and img.users == 0:
            bpy.data.images.remove(img)
