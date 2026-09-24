"""B-15 Wave 4 (A-059): item icons rendered from the game's own models.

A fixed "icon studio": an orthographic camera, a warm key light from the upper
left, a cool rim light from behind and a soft fill, transparent film. Each icon
imports the model the game uses (weapons and shields from
``Import/Characters/Equipment/*.glb``, armour from the MetaHuman FBX in
``Import/Characters/MetaHuman/Equipment``), rebuilds its materials from
``Import/UI/icon_materials.json`` (exported from the Unreal material instances
by ``valhalla_tools/export_icon_materials.py``, so the icons match the game),
poses it by category, frames it and renders 256 px to
``Valhalla2/Saved/IconStudio/<name>_raw.png``. ``wave4_icon_finish.py`` (plain
Python; Blender has no Pillow) turns those into the 128 px icons in
``Import/UI/Icons/<name>.png`` with a thin dark outline and a soft shadow.

Items with no model yet (rings, potions, the rat tail, coins) are modelled
here, small and only for their icons.

Run in Blender (5.x): exec, then render_all() or render_one("sword_iron").
"""

import json
import math
import os

import bmesh
import bpy
import mathutils
from mathutils import Matrix, Vector

REPO = os.environ.get("VALHALLA_REPO", r"C:\Users\music\game-project\Valhalla2.0")
IMPORT = os.path.join(REPO, "Import")
TEX = os.path.join(IMPORT, "Textures")
EQUIP = os.path.join(IMPORT, "Characters", "Equipment")
MH_EQUIP = os.path.join(IMPORT, "Characters", "MetaHuman", "Equipment")
OUT = os.path.join(IMPORT, "UI", "Icons")
WORK = os.path.join(REPO, "Valhalla2", "Saved", "IconStudio")
MATS = json.load(open(os.path.join(IMPORT, "UI", "icon_materials.json")))["materials"]

RENDER_PX, ICON_PX = 256, 128

# name -> (source, pose, extra)
#   source: ("glb", file) / ("fbx", file) / ("make", function name)
#   pose:   "diag" (long weapons, grip lower left), "shield", "front", "back",
#           "glove", "boots", "prop"
ICONS = {
    "sword_iron": (("glb", "SM_sword_iron"), "diag", {}),
    "dagger_iron": (("glb", "SM_dagger_iron"), "diag", {}),
    "mace_priests": (("glb", "SM_mace_priests"), "diag", {}),
    "totem_bone": (("glb", "SM_totem_bone"), "diag", {}),
    "staff_apprentice": (("glb", "SM_staff_apprentice"), "diag", {}),
    "bow_hunting": (("glb", "SM_bow_hunting"), "diag", {"spin": 90}),
    "shield_buckler_iron": (("glb", "SM_shield_buckler_iron"), "shield", {"flip": True}),
    "shield_kite_iron": (("glb", "SM_shield_kite_iron"), "shield", {"flip": True}),
    "helm_iron_full": (("fbx", "SK_helm_iron_full"), "front", {}),
    "hood_scout": (("fbx", "SK_hood_scout"), "front", {}),
    "hat_adept": (("fbx", "SK_hat_adept"), "front", {}),
    "chest_travelers_jerkin": (("fbx", "SK_chest_travelers_jerkin"), "front", {}),
    "chest_priests_chain": (("fbx", "SK_chest_priests_chain"), "front", {}),
    "chest_apprentice_robe": (("fbx", "SK_chest_apprentice_robe"), "front", {}),
    "legs_travelers": (("fbx", "SK_legs_travelers"), "front", {}),
    "legs_iron_plate": (("fbx", "SK_legs_iron_plate"), "front", {}),
    "legs_apprentice_robe": (("fbx", "SK_legs_apprentice_robe"), "front", {}),
    "boots_ranger": (("fbx", "SK_boots_ranger"), "boots", {}),
    "boots_iron_sabatons": (("fbx", "SK_boots_iron_sabatons"), "boots", {}),
    "boots_cloth_slippers": (("fbx", "SK_boots_cloth_slippers"), "boots", {}),
    "gloves_ranger_bracers": (("fbx", "SK_gloves_ranger_bracers"), "glove", {}),
    "gloves_iron_gauntlets": (("fbx", "SK_gloves_iron_gauntlets"), "glove", {}),
    "gloves_cloth_wraps": (("fbx", "SK_gloves_cloth_wraps"), "glove", {}),
    "cloak_warden": (("fbx", "SK_cloak_warden"), "front", {"yaw": 40}),
    "ring_strength": (("make", "ring"), "prop", {"gem": (0.6, 0.01, 0.01)}),
    "ring_wisdom": (("make", "ring"), "prop", {"gem": (0.02, 0.10, 0.65)}),
    "potion_health": (("make", "potion"), "prop", {"liquid": (0.45, 0.0, 0.01)}),
    "potion_mana": (("make", "potion"), "prop", {"liquid": (0.01, 0.06, 0.55)}),
    "rat_tail": (("make", "rat_tail"), "prop", {}),
    "gold_coin": (("make", "coins"), "prop", {}),
}


# ── Scene ────────────────────────────────────────────────────────────────────

def _active():
    return bpy.context.view_layer.objects.active


def ctx():
    """Operators need a window/area when run from the MCP connector's timer."""
    win = bpy.context.window or bpy.context.window_manager.windows[0]
    for area in win.screen.areas:
        if area.type == "VIEW_3D":
            region = next(r for r in area.regions if r.type == "WINDOW")
            return dict(window=win, screen=win.screen, area=area, region=region,
                        scene=bpy.context.scene, view_layer=bpy.context.view_layer)
    return dict(window=win, screen=win.screen, scene=bpy.context.scene, view_layer=bpy.context.view_layer)


def studio():
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = sc.render.resolution_y = RENDER_PX
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    # Standard, not AgX: icons want the materials' own saturated colours.
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = -0.15
    world = bpy.data.worlds.new("IconWorld")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    # A light grey world: metal reflects it, and dark iron still reads.
    bg.inputs["Color"].default_value = (0.42, 0.44, 0.48, 1.0)
    bg.inputs["Strength"].default_value = 0.8
    sc.world = world
    cam = bpy.data.objects.new("IconCam", bpy.data.cameras.new("IconCam"))
    cam.data.type = "ORTHO"
    cam.rotation_euler = (math.radians(90), 0.0, 0.0)      # looks along +Y; right = +X, up = +Z
    cam.location = (0.0, -20.0, 0.0)
    cam.data.clip_end = 100.0
    sc.collection.objects.link(cam)
    sc.camera = cam

    def light(name, kind, energy, color, rot, size=None):
        l = bpy.data.lights.new(name, kind)
        l.energy = energy
        l.color = color
        if size is not None:
            l.angle = size
        o = bpy.data.objects.new(name, l)
        o.rotation_euler = [math.radians(a) for a in rot]
        sc.collection.objects.link(o)
        return o
    # Suns: rotation (x, y, z) degrees; a sun shines along its local -Z.
    light("Key", "SUN", 4.2, (1.0, 0.93, 0.82), (55, -35, 0), size=math.radians(8))    # upper left, in front
    light("Rim", "SUN", 3.0, (0.75, 0.85, 1.0), (-120, 30, 0), size=math.radians(4))   # behind, upper right
    light("Fill", "SUN", 0.9, (0.9, 0.92, 1.0), (80, 40, 0), size=math.radians(30))    # low, from the right
    return sc


# ── Materials ────────────────────────────────────────────────────────────────

def _set_name(tex_path):
    # /Game/Valhalla/Textures/Leather/T_Leather_BC.T_Leather_BC -> Leather
    base = tex_path.split("/")[-1].split(".")[0]
    return base[2:-3] if base.startswith("T_") and base.endswith("_BC") else None


def build_material(name):
    key = name.split(".")[0]
    mat = bpy.data.materials.get("icon_" + key)
    if mat:
        return mat
    e = MATS.get(key, {})
    sc = e.get("scalars", {})
    vc = e.get("vectors", {})
    tx = e.get("textures", {})
    sw = e.get("switches", {})
    mat = bpy.data.materials.new("icon_" + key)
    mat.use_nodes = True
    nt = mat.node_tree
    N, L = nt.nodes, nt.links
    bsdf = N["Principled BSDF"]
    tint = vc.get("BaseColor", [0.6, 0.6, 0.6, 1])[:3]
    rough = sc.get("Roughness", 0.6)
    metal = sc.get("Metallic", 0.0)
    set_name = _set_name(tx.get("BaseColorMap") or "") if sw.get("UseTextures") else None
    folder = os.path.join(TEX, set_name or "")
    if set_name and os.path.exists(os.path.join(folder, "T_%s_BC.png" % set_name)):
        uvn = N.new("ShaderNodeTexCoord")
        mp = N.new("ShaderNodeMapping")
        s = sc.get("UVScale", 1.0)
        mp.inputs["Scale"].default_value = (s, s, 1.0)
        L.new(uvn.outputs["UV"], mp.inputs["Vector"])

        def img(suffix, color):
            n = N.new("ShaderNodeTexImage")
            n.image = bpy.data.images.load(os.path.join(folder, "T_%s_%s.png" % (set_name, suffix)), check_existing=True)
            n.image.colorspace_settings.name = "sRGB" if color else "Non-Color"
            L.new(mp.outputs["Vector"], n.inputs["Vector"])
            return n
        bc, orm, nm = img("BC", True), img("ORM", False), img("N", False)
        mul = N.new("ShaderNodeMix")
        mul.data_type = "RGBA"
        mul.blend_type = "MULTIPLY"
        mul.inputs["Factor"].default_value = 1.0
        L.new(bc.outputs["Color"], mul.inputs[6])
        mul.inputs[7].default_value = (*tint, 1.0)
        sep = N.new("ShaderNodeSeparateColor")
        L.new(orm.outputs["Color"], sep.inputs["Color"])
        # AO darkens the base colour a little (M_ValhallaPBR's AOStrength).
        ao = N.new("ShaderNodeMix")
        ao.data_type = "RGBA"
        ao.blend_type = "MULTIPLY"
        ao.inputs["Factor"].default_value = 0.6
        L.new(mul.outputs[2], ao.inputs[6])
        L.new(sep.outputs["Red"], ao.inputs[7])
        L.new(ao.outputs[2], bsdf.inputs["Base Color"])
        rs = N.new("ShaderNodeMath")
        rs.operation = "MULTIPLY"
        rs.inputs[1].default_value = sc.get("RoughnessScale", 1.0)
        L.new(sep.outputs["Green"], rs.inputs[0])
        L.new(rs.outputs[0], bsdf.inputs["Roughness"])
        ms = N.new("ShaderNodeMath")
        ms.operation = "MULTIPLY"
        ms.inputs[1].default_value = sc.get("MetallicScale", 1.0)
        L.new(sep.outputs["Blue"], ms.inputs[0])
        L.new(ms.outputs[0], bsdf.inputs["Metallic"])
        # DirectX normal -> OpenGL: flip green.
        ns = N.new("ShaderNodeSeparateColor")
        L.new(nm.outputs["Color"], ns.inputs["Color"])
        inv = N.new("ShaderNodeMath")
        inv.operation = "SUBTRACT"
        inv.inputs[0].default_value = 1.0
        L.new(ns.outputs["Green"], inv.inputs[1])
        nc = N.new("ShaderNodeCombineColor")
        L.new(ns.outputs["Red"], nc.inputs["Red"])
        L.new(inv.outputs[0], nc.inputs["Green"])
        L.new(ns.outputs["Blue"], nc.inputs["Blue"])
        nmap = N.new("ShaderNodeNormalMap")
        L.new(nc.outputs["Color"], nmap.inputs["Color"])
        L.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    else:
        bsdf.inputs["Base Color"].default_value = (*tint, 1.0)
        bsdf.inputs["Roughness"].default_value = rough
        bsdf.inputs["Metallic"].default_value = metal
    es = sc.get("EmissiveStrength", 0.0)
    if es > 0.0:
        ec = vc.get("EmissiveColor", [1, 1, 1, 1])[:3]
        bsdf.inputs["Emission Color"].default_value = (*ec, 1.0)
        bsdf.inputs["Emission Strength"].default_value = es * 0.5
    return mat


def flat(name, color, rough=0.5, metal=0.0, alpha=1.0, emit=0.0, transmission=0.0):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    b = mat.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    b.inputs["Alpha"].default_value = alpha
    b.inputs["Transmission Weight"].default_value = transmission
    if emit:
        b.inputs["Emission Color"].default_value = (*color, 1.0)
        b.inputs["Emission Strength"].default_value = emit
    if alpha < 1.0:
        mat.surface_render_method = "BLENDED"
    return mat


# ── Models ───────────────────────────────────────────────────────────────────

def _new_objects(fn):
    before = set(bpy.data.objects)
    fn()
    return [o for o in bpy.data.objects if o not in before]


def import_model(kind, stem):
    if kind == "glb":
        new = _new_objects(lambda: bpy.ops.import_scene.gltf(filepath=os.path.join(EQUIP, stem + ".glb")))
    else:
        new = _new_objects(lambda: bpy.ops.import_scene.fbx(filepath=os.path.join(MH_EQUIP, stem + ".fbx"),
                                                            use_anim=False, ignore_leaf_bones=True))
    meshes = [o for o in new if o.type == "MESH"]
    for o in meshes:
        for m in list(o.modifiers):
            if m.type == "ARMATURE":
                o.modifiers.remove(m)
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
    for o in new:
        if o.type != "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for i, slot in enumerate(ob.material_slots):
        if slot.material:
            ob.material_slots[i].material = build_material(slot.material.name)
    return ob


def _mesh(name, bm, mats):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    for m in mats:
        me.materials.append(m)
    for p in me.polygons:
        p.use_smooth = True
    return ob


def _lathe(bm, profile, segs, mat_index=0):
    rings = []
    for r, z in profile:
        rings.append([bm.verts.new((r * math.cos(2 * math.pi * k / segs), r * math.sin(2 * math.pi * k / segs), z))
                      for k in range(segs)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segs):
            f = bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
            f.material_index = mat_index
    return rings


def make_ring(gem=(0.7, 0.05, 0.05)):
    gold = flat("icon_gold", (0.85, 0.55, 0.18), rough=0.25, metal=1.0)
    stone = flat("icon_gem", gem, rough=0.05, emit=0.4)
    # Band: a torus standing up (axis along Y), a little taller than wide.
    R, r = 0.30, 0.045
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=r, major_segments=64, minor_segments=16,
                                     rotation=(math.radians(90), 0, 0))
    band = _active()
    band.scale = (1.0, 1.35, 1.0)
    band.data.materials.append(gold)
    # Bezel and gem on top.
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.15, depth=0.08, location=(0, 0, R + 0.06))
    bezel = _active()
    bezel.data.materials.append(gold)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.14, location=(0, 0, R + 0.13))
    g = _active()
    g.scale = (1.0, 1.0, 0.75)
    g.data.materials.append(stone)
    for o in (band, bezel):
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.shade_smooth()
    bpy.ops.object.select_all(action="DESELECT")
    for o in (band, bezel, g):
        o.select_set(True)
    bpy.context.view_layer.objects.active = band
    bpy.ops.object.join()
    ob = _active()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Tilt the top towards the camera so the stone faces out.
    ob.rotation_euler = (math.radians(-40), math.radians(-20), math.radians(25))
    bpy.ops.object.transform_apply(rotation=True)
    return ob


def make_potion(liquid=(0.7, 0.02, 0.03)):
    glass = flat("icon_glass", (0.85, 0.92, 0.95), rough=0.05, alpha=0.22)
    liq = flat("icon_liquid_%d" % int(liquid[2] * 100), liquid, rough=0.1, emit=0.25)
    cork = flat("icon_cork", (0.45, 0.30, 0.16), rough=0.8)
    bm = bmesh.new()
    prof = [(0.001, 0.0), (0.26, 0.01), (0.36, 0.12), (0.38, 0.26), (0.33, 0.42), (0.20, 0.52), (0.10, 0.58),
            (0.09, 0.78), (0.12, 0.82), (0.12, 0.86)]
    _lathe(bm, prof, 40, 0)
    glass_ob = _mesh("potion_glass", bm, [glass])
    bm = bmesh.new()
    lp = [(0.001, 0.03), (0.24, 0.035), (0.33, 0.13), (0.35, 0.26), (0.30, 0.38), (0.001, 0.40)]
    _lathe(bm, lp, 40, 0)
    liq_ob = _mesh("potion_liquid", bm, [liq])
    bm = bmesh.new()
    _lathe(bm, [(0.075, 0.74), (0.085, 0.86), (0.095, 0.95), (0.001, 0.96)], 20, 0)
    cork_ob = _mesh("potion_cork", bm, [cork])
    bpy.ops.object.select_all(action="DESELECT")
    for o in (glass_ob, liq_ob, cork_ob):
        o.select_set(True)
    bpy.context.view_layer.objects.active = glass_ob
    bpy.ops.object.join()
    ob = _active()
    ob.rotation_euler = (math.radians(12), 0, math.radians(-10))
    bpy.ops.object.transform_apply(rotation=True)
    return ob


def make_rat_tail():
    skin = flat("icon_tail", (0.42, 0.24, 0.22), rough=0.5)
    fur = flat("icon_fur", (0.12, 0.10, 0.08), rough=0.9)
    bm = bmesh.new()
    segs, n = 10, 40
    rings = []
    for i in range(n + 1):
        t = i / n
        # A loose S-curl, thick at the root.
        p = Vector((0.9 * t - 0.45, 0.0, 0.18 * math.sin(t * math.pi * 1.6) + 0.1 * t))
        tang = Vector((0.9, 0.0, 0.18 * math.pi * 1.6 * math.cos(t * math.pi * 1.6) + 0.1)).normalized()
        side = Vector((0, 1, 0))
        up = side.cross(tang).normalized()
        r = 0.065 * (1.0 - 0.8 * t) * (1.0 + 0.08 * math.cos(t * n * math.pi))   # scaly rings
        rings.append([bm.verts.new(p + (up * math.cos(2 * math.pi * k / segs) + side * math.sin(2 * math.pi * k / segs)) * r)
                      for k in range(segs)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segs):
            bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
    bm.faces.new(list(reversed(rings[0])))
    ob = _mesh("rat_tail", bm, [skin])
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.1, location=(-0.47, 0, 0.0))
    tuft = _active()
    tuft.scale = (0.8, 1.0, 1.0)
    tuft.data.materials.append(fur)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    tuft.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.join()
    ob = _active()
    bpy.ops.object.transform_apply(location=True, scale=True)
    ob.rotation_euler = (0, math.radians(-25), 0)
    bpy.ops.object.transform_apply(rotation=True)
    return ob


def make_coins():
    gold = flat("icon_gold", (0.85, 0.55, 0.18), rough=0.25, metal=1.0)
    parts = []
    import random
    rng = random.Random(3)
    for i in range(5):
        bpy.ops.mesh.primitive_cylinder_add(vertices=40, radius=0.30, depth=0.07,
                                            location=(rng.uniform(-0.03, 0.03), rng.uniform(-0.03, 0.03), 0.035 + i * 0.072))
        c = _active()
        c.rotation_euler = (0, 0, rng.uniform(0, 3))
        parts.append(c)
    # One coin leaning against the stack, face on.
    bpy.ops.mesh.primitive_cylinder_add(vertices=40, radius=0.30, depth=0.07, location=(0.36, -0.2, 0.28),
                                        rotation=(math.radians(75), 0, math.radians(-20)))
    parts.append(_active())
    for c in parts:
        bev = c.modifiers.new("b", "BEVEL")
        bev.width = 0.015
        bev.segments = 2
        c.data.materials.append(gold)
        bpy.context.view_layer.objects.active = c
        bpy.ops.object.modifier_apply(modifier="b")
        bpy.ops.object.shade_smooth()
    bpy.ops.object.select_all(action="DESELECT")
    for c in parts:
        c.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    ob = _active()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    ob.rotation_euler = (math.radians(-30), 0, math.radians(20))
    bpy.ops.object.transform_apply(rotation=True)
    return ob


MAKERS = {"ring": make_ring, "potion": make_potion, "rat_tail": make_rat_tail, "coins": make_coins}


# ── Posing and framing ───────────────────────────────────────────────────────

def _bounds(ob):
    vs = [ob.matrix_world @ v.co for v in ob.data.vertices]
    mn = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    mx = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    return mn, mx


def _keep(ob, pred):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not pred(v.co)], context="VERTS")
    bm.to_mesh(ob.data)
    bm.free()


def pose(ob, how, extra):
    def rot(m):
        ob.data.transform(m)
        ob.data.update()
    if how == "diag":
        # Built along +Y (grip at the origin). Stand it up (+Y -> +Z), turn the
        # flat of the blade to the camera, then lean the tip to the upper right.
        rot(Matrix.Rotation(math.radians(90), 4, "X"))
        if extra.get("spin"):
            rot(Matrix.Rotation(math.radians(extra["spin"]), 4, "Z"))
        rot(Matrix.Rotation(math.radians(12), 4, "Z"))
        rot(Matrix.Rotation(math.radians(45), 4, "Y"))
    elif how == "shield":
        mn, mx = _bounds(ob)
        size = mx - mn
        thin = min(range(3), key=lambda i: size[i])
        # Turn the thin axis to face the camera (-Y), front out.
        if thin == 0:
            rot(Matrix.Rotation(math.radians(-90), 4, "Z"))
        elif thin == 2:
            rot(Matrix.Rotation(math.radians(-90), 4, "X"))
        if extra.get("flip"):
            rot(Matrix.Rotation(math.radians(180), 4, "Z"))
        rot(Matrix.Rotation(math.radians(-22), 4, "Z"))
        rot(Matrix.Rotation(math.radians(8), 4, "X"))
    elif how in ("front", "back"):
        yaw = extra.get("yaw", 28) + (180 if how == "back" else 0)
        if extra.get("flip"):
            yaw += 180
        rot(Matrix.Rotation(math.radians(yaw), 4, "Z"))
        rot(Matrix.Rotation(math.radians(-12), 4, "X"))
    elif how == "boots":
        # One boot (the left), in profile with the toe to the left, a little from above.
        _keep(ob, lambda co: co.x > 0.0)
        rot(Matrix.Rotation(math.radians(-70), 4, "Z"))
        rot(Matrix.Rotation(math.radians(-15), 4, "X"))
    elif how == "glove":
        # Only the left hand; the pair sits a body-width apart.
        _keep(ob, lambda co: co.x > 0.0)
        rot(Matrix.Rotation(math.radians(28), 4, "Z"))
        rot(Matrix.Rotation(math.radians(-12), 4, "X"))
    # "prop": posed when made


def frame(ob, pad=1.14):
    mn, mx = _bounds(ob)
    c = (mn + mx) / 2
    cam = bpy.context.scene.camera
    cam.location = (c.x, mn.y - 10.0, c.z)
    cam.data.ortho_scale = max(mx.x - mn.x, mx.z - mn.z) * pad


def render_one(name):
    src, how, extra = ICONS[name]
    studio()
    with bpy.context.temp_override(**ctx()):
        return _render(name, src, how, extra)


def _render(name, src, how, extra):
    kind, stem = src
    if kind == "make":
        ob = MAKERS[stem](**{k: v for k, v in extra.items() if k in ("gem", "liquid")})
    else:
        ob = import_model(kind, stem)
    pose(ob, how, extra)
    frame(ob)
    os.makedirs(WORK, exist_ok=True)
    raw = os.path.join(WORK, name + "_raw.png")
    bpy.context.scene.render.filepath = raw
    bpy.ops.render.render(write_still=True)
    return raw


def render_all(names=None):
    done = []
    for n in names or ICONS:
        done.append(render_one(n))
    return done
