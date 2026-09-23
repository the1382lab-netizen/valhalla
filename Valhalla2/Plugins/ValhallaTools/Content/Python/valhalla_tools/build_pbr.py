"""B-15 Wave 0: the physically based master material that replaces M_ValhallaToon.

``M_ValhallaPBR``
    The "modern Neverwinter Nights remaster" master. Every environment, prop,
    equipment and character material is a MaterialInstanceConstant of it.

    It keeps the three parameter names the game reads at runtime, so no C++
    changes are needed to swap masters:

    - ``BaseColor``        the tint. AValhallaNPC's skin tint writes it on a
                           dynamic instance of M_Skin, and every first-pass
                           flat-colour instance carries its colour in it.
    - ``UseVertexColor``   folds COLOR_0 in (the first-pass hair is painted).
    - ``EmissiveColor`` / ``EmissiveStrength``  the staff orb, mace head,
                           lanterns and portal glow.

    New for the remaster:

    - ``UseTextures`` (static switch). Off: the flat-colour first-pass look,
      lit physically. On: ``BaseColorMap`` x ``BaseColor``, ``NormalMap`` and
      ``ORMMap`` (R = ambient occlusion, G = roughness, B = metallic — the
      packing Poly Haven's and ambientCG's "arm" maps already use).
    - ``WorldAlignedUV`` (static switch). Off: mesh UVs (props, walls,
      characters). On: top-down world-space UVs, so ground tiles on the 64 cm
      grid tile seamlessly across tile boundaries instead of repeating per tile.
      ``TextureWorldSize`` is how many centimetres one texture repeat covers.
    - ``UVScale`` tiles mesh UVs.
    - ``Roughness`` / ``Metallic`` / ``Specular`` when there are no textures;
      ``RoughnessScale`` / ``MetallicScale`` scale the ORM channels when there
      are.
    - ``AOStrength`` how much the ORM ambient-occlusion channel darkens.
    - ``MacroVariation`` large-scale world-space brightness variation (0 = off),
      the cheapest way to stop a tiled surface reading as a repeat.
      ``MacroScale`` is its size in centimetres.
    - ``Grime`` darkens and desaturates toward ``GrimeColor`` where the
      ambient-occlusion channel says the surface is recessed.

Re-parenting (``reparent_to_pbr``) moves every instance of M_ValhallaToon to
M_ValhallaPBR, keeps its colour, and sets roughness and metallic by material
family (iron is metal, water is glossy, thatch is rough). ``reparent_to_toon``
is the rollback: it moves them back. M_ValhallaToon itself is left in place.
"""

import unreal

MAT_DIR = "/Game/Valhalla/Materials"
PBR = MAT_DIR + "/M_ValhallaPBR"
TOON = MAT_DIR + "/M_ValhallaToon"
SEARCH_ROOTS = ("/Game/Valhalla",)

TAG = "LogValhallaImport:"

MEL = unreal.MaterialEditingLibrary


def _log(msg):
    unreal.log("{} {}".format(TAG, msg))


def _asset(package_path):
    existing = unreal.EditorAssetLibrary.load_asset(package_path)
    if existing is not None:
        return existing
    name = package_path.rsplit("/", 1)[1]
    folder = package_path.rsplit("/", 1)[0]
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.Material, unreal.MaterialFactoryNew())


def _expr(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


def _wire(src, src_out, dst, dst_in):
    if not MEL.connect_material_expressions(src, src_out, dst, dst_in):
        raise RuntimeError("could not wire {}.{} -> {}.{}".format(
            src.get_class().get_name(), src_out or "<default>",
            dst.get_class().get_name(), dst_in or "<default>"))


def _out(node, prop):
    if not MEL.connect_material_property(node, "", prop):
        raise RuntimeError("could not connect {} to {}".format(node.get_class().get_name(), prop))


def _scalar(material, name, value, x, y, group="Surface"):
    node = _expr(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", group)
    return node


def _vector(material, name, color, x, y, group="Surface"):
    node = _expr(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", color)
    node.set_editor_property("group", group)
    return node


def _texture(material, name, default_path, sampler_type, x, y):
    node = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("group", "Textures")
    tex = unreal.EditorAssetLibrary.load_asset(default_path)
    if tex is None:
        raise RuntimeError("default texture missing: " + default_path)
    node.set_editor_property("texture", tex)
    node.set_editor_property("sampler_type", sampler_type)
    return node


def _switch(material, name, default, x, y, group="Switches"):
    node = _expr(material, unreal.MaterialExpressionStaticSwitchParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    node.set_editor_property("group", group)
    return node


def _const(material, value, x, y):
    node = _expr(material, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", value)
    return node


def _mul(material, a, b, x, y, a_out="", b_out=""):
    node = _expr(material, unreal.MaterialExpressionMultiply, x, y)
    _wire(a, a_out, node, "A")
    _wire(b, b_out, node, "B")
    return node


def _lerp(material, a, b, alpha, x, y, a_out="", b_out="", alpha_out=""):
    node = _expr(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    _wire(a, a_out, node, "A")
    _wire(b, b_out, node, "B")
    _wire(alpha, alpha_out, node, "Alpha")
    return node


# Engine textures that make "no texture assigned" a no-op.
WHITE = "/Engine/EngineResources/WhiteSquareTexture"
FLAT_NORMAL = "/Engine/EngineMaterials/DefaultNormal"
#: The ORM slot is linear (Masks); an sRGB default fails to compile there, so
#: the project ships its own 4x4 linear white. Created on demand.
DEFAULT_ORM = "/Game/Valhalla/Textures/_Defaults/T_Default_ORM"


def _ensure_default_orm():
    existing = unreal.EditorAssetLibrary.load_asset(DEFAULT_ORM)
    if existing is not None:
        return existing
    import os, struct, zlib
    folder = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "ValhallaDefaults")
    os.makedirs(folder, exist_ok=True)
    png = os.path.join(folder, "T_Default_ORM.png")

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + b"\xff\xff\xff" * 4 for _ in range(4))
    with open(png, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0))
                 + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", DEFAULT_ORM.rsplit("/", 1)[0])
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(DEFAULT_ORM)
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def _find_texture_param(material, prop, name):
    """Walk the graph back from a material output to a named texture parameter."""
    seen = set()
    stack = [MEL.get_material_property_input_node(material, prop)]
    while stack:
        node = stack.pop()
        if node is None or node.get_path_name() in seen:
            continue
        seen.add(node.get_path_name())
        if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D) \
                and str(node.get_editor_property("parameter_name")) == name:
            return node
        stack.extend(MEL.get_inputs_for_material_expression(material, node))
    return None


def upgrade_pbr():
    """Patch an existing M_ValhallaPBR in place.

    Clearing a master that material instances already use and rebuilding it
    (``delete_all_material_expressions``) trips an engine assertion
    (``!IsRooted()``) and crashes the editor, so changes to a live master are
    made as targeted patches here instead. Each patch is idempotent.
    """
    material = unreal.EditorAssetLibrary.load_asset(PBR)
    if material is None:
        return build_pbr()
    changed = []
    # Wave 0 fix: ORMMap needs a linear default and the Masks sampler.
    orm = _find_texture_param(material, unreal.MaterialProperty.MP_ROUGHNESS, "ORMMap")
    if orm is not None and orm.get_editor_property("sampler_type") != unreal.MaterialSamplerType.SAMPLERTYPE_MASKS:
        orm.set_editor_property("texture", _ensure_default_orm())
        orm.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        changed.append("ORMMap -> linear default, Masks sampler")
    if not material.get_editor_property("used_with_nanite"):
        material.set_editor_property("used_with_nanite", True)
        changed.append("used_with_nanite")
    if changed:
        MEL.recompile_material(material)
        unreal.EditorAssetLibrary.save_asset(PBR)
    _log("upgrade_pbr: {}".format(changed or "already current"))
    return changed


def build_pbr():
    """Build M_ValhallaPBR from scratch. Only when it does not exist yet: a
    live master is changed with ``upgrade_pbr`` (see there for why)."""
    if unreal.EditorAssetLibrary.does_asset_exist(PBR):
        raise RuntimeError("M_ValhallaPBR exists; use upgrade_pbr() to change it in place")
    material = _asset(PBR)

    # ── UVs: mesh UVs x UVScale, or top-down world space ────────────────
    texcoord = _expr(material, unreal.MaterialExpressionTextureCoordinate, -2600, -400)
    uv_scale = _scalar(material, "UVScale", 1.0, -2600, -300, "UV")
    mesh_uv = _mul(material, texcoord, uv_scale, -2400, -380)

    world_pos = _expr(material, unreal.MaterialExpressionWorldPosition, -2600, -150)
    world_xy = _expr(material, unreal.MaterialExpressionComponentMask, -2400, -150)
    world_xy.set_editor_property("r", True)
    world_xy.set_editor_property("g", True)
    world_xy.set_editor_property("b", False)
    world_xy.set_editor_property("a", False)
    _wire(world_pos, "", world_xy, "")
    world_size = _scalar(material, "TextureWorldSize", 256.0, -2600, -50, "UV")
    world_uv = _expr(material, unreal.MaterialExpressionDivide, -2200, -150)
    _wire(world_xy, "", world_uv, "A")
    _wire(world_size, "", world_uv, "B")

    uv_switch = _switch(material, "WorldAlignedUV", False, -2000, -300)
    _wire(world_uv, "", uv_switch, "True")
    _wire(mesh_uv, "", uv_switch, "False")

    # ── Textures ─────────────────────────────────────────────────────────
    base_map = _texture(material, "BaseColorMap", WHITE,
                        unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1700, -600)
    normal_map = _texture(material, "NormalMap", FLAT_NORMAL,
                          unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1700, -300)
    _ensure_default_orm()
    orm_map = _texture(material, "ORMMap", DEFAULT_ORM,
                       unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1700, 0)
    for tex in (base_map, normal_map, orm_map):
        _wire(uv_switch, "", tex, "UVs")

    use_tex_base = _switch(material, "UseTextures", False, -900, -700)
    use_tex_normal = _switch(material, "UseTextures", False, -900, -250)
    use_tex_rough = _switch(material, "UseTextures", False, -900, 150)
    use_tex_metal = _switch(material, "UseTextures", False, -900, 350)
    use_tex_ao = _switch(material, "UseTextures", False, -900, 550)

    # ── Base colour: tint x (vertex colour) x (map) ──────────────────────
    tint = _vector(material, "BaseColor", unreal.LinearColor(0.5, 0.5, 0.5, 1.0), -1700, -900)
    vertex = _expr(material, unreal.MaterialExpressionVertexColor, -1500, -800)
    use_vertex = _scalar(material, "UseVertexColor", 0.0, -1500, -700)
    tint_vc = _mul(material, tint, vertex, -1300, -850)
    tint_final = _lerp(material, tint, tint_vc, use_vertex, -1100, -850)

    textured = _mul(material, tint_final, base_map, -1100, -650, b_out="RGB")
    _wire(textured, "", use_tex_base, "True")
    _wire(tint_final, "", use_tex_base, "False")

    # ── Ambient occlusion (ORM.R), with strength ─────────────────────────
    one = _const(material, 1.0, -1300, 500)
    ao_strength = _scalar(material, "AOStrength", 1.0, -1300, 600)
    ao_mix = _lerp(material, one, orm_map, ao_strength, -1100, 520, b_out="R")
    _wire(ao_mix, "", use_tex_ao, "True")
    _wire(one, "", use_tex_ao, "False")

    # ── Grime: darken / tint toward GrimeColor where AO is low ───────────
    grime = _scalar(material, "Grime", 0.0, -700, -500, "Wear")
    grime_color = _vector(material, "GrimeColor", unreal.LinearColor(0.12, 0.10, 0.08, 1.0),
                          -700, -400, "Wear")
    one_minus_ao = _expr(material, unreal.MaterialExpressionOneMinus, -700, -300)
    _wire(use_tex_ao, "", one_minus_ao, "")
    grime_alpha = _mul(material, one_minus_ao, grime, -500, -350)
    grimed = _lerp(material, use_tex_base, grime_color, grime_alpha, -300, -600)

    # ── Macro variation: large world-space brightness noise ──────────────
    macro_amount = _scalar(material, "MacroVariation", 0.0, -700, -1000, "Wear")
    macro_scale = _scalar(material, "MacroScale", 900.0, -900, -1100, "Wear")
    macro_pos = _expr(material, unreal.MaterialExpressionDivide, -700, -1150)
    _wire(world_pos, "", macro_pos, "A")
    _wire(macro_scale, "", macro_pos, "B")
    noise = _expr(material, unreal.MaterialExpressionNoise, -500, -1150)
    noise.set_editor_property("scale", 1.0)
    noise.set_editor_property("levels", 2)
    noise.set_editor_property("quality", 1)
    noise.set_editor_property("output_min", -1.0)
    noise.set_editor_property("output_max", 1.0)
    _wire(macro_pos, "", noise, "World Position")
    macro_term = _mul(material, noise, macro_amount, -300, -1100)
    macro_gain = _expr(material, unreal.MaterialExpressionAdd, -150, -1050)
    _wire(one, "", macro_gain, "A")
    _wire(macro_term, "", macro_gain, "B")
    base_final = _mul(material, grimed, macro_gain, 0, -700)
    _out(base_final, unreal.MaterialProperty.MP_BASE_COLOR)

    # ── Normal ───────────────────────────────────────────────────────────
    flat = _expr(material, unreal.MaterialExpressionConstant3Vector, -1300, -150)
    flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    _wire(normal_map, "RGB", use_tex_normal, "True")
    _wire(flat, "", use_tex_normal, "False")
    _out(use_tex_normal, unreal.MaterialProperty.MP_NORMAL)

    # ── Roughness / metallic / specular ──────────────────────────────────
    rough = _scalar(material, "Roughness", 0.75, -1300, 100)
    rough_scale = _scalar(material, "RoughnessScale", 1.0, -1300, 200)
    rough_tex = _mul(material, orm_map, rough_scale, -1100, 150, a_out="G")
    _wire(rough_tex, "", use_tex_rough, "True")
    _wire(rough, "", use_tex_rough, "False")
    _out(use_tex_rough, unreal.MaterialProperty.MP_ROUGHNESS)

    metal = _scalar(material, "Metallic", 0.0, -1300, 300)
    metal_scale = _scalar(material, "MetallicScale", 1.0, -1300, 400)
    metal_tex = _mul(material, orm_map, metal_scale, -1100, 350, a_out="B")
    _wire(metal_tex, "", use_tex_metal, "True")
    _wire(metal, "", use_tex_metal, "False")
    _out(use_tex_metal, unreal.MaterialProperty.MP_METALLIC)

    specular = _scalar(material, "Specular", 0.5, -700, 700)
    _out(specular, unreal.MaterialProperty.MP_SPECULAR)

    _out(use_tex_ao, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    # ── Emissive ─────────────────────────────────────────────────────────
    emissive_color = _vector(material, "EmissiveColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0),
                             -700, 850, "Emissive")
    emissive_strength = _scalar(material, "EmissiveStrength", 0.0, -700, 950, "Emissive")
    emissive = _mul(material, emissive_color, emissive_strength, -500, 900)
    _out(emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # Skeletal meshes (characters, equipment) and instanced tile fields.
    material.set_editor_property("used_with_skeletal_mesh", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)

    MEL.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(PBR)
    _log("built {}".format(PBR))
    return PBR


# ── Re-parenting ─────────────────────────────────────────────────────────────

#: (keyword in the material name, roughness, metallic, macro variation, grime).
#: First match wins, so the specific names come before the general ones.
FAMILIES = (
    ("Glow",      0.40, 0.0, 0.00, 0.0),
    ("Water",     0.06, 0.0, 0.05, 0.0),
    ("Chain",     0.45, 1.0, 0.00, 0.0),
    ("Iron",      0.40, 1.0, 0.00, 0.0),
    ("Skin",      0.55, 0.0, 0.00, 0.0),
    ("Hair",      0.50, 0.0, 0.00, 0.0),
    ("Leather",   0.60, 0.0, 0.00, 0.0),
    ("Cloth",     0.85, 0.0, 0.00, 0.0),
    ("Awning",    0.85, 0.0, 0.05, 0.0),
    ("Rope",      0.85, 0.0, 0.00, 0.0),
    ("Thatch",    0.90, 0.0, 0.10, 0.0),
    ("Plaster",   0.85, 0.0, 0.08, 0.0),
    ("Timber",    0.75, 0.0, 0.05, 0.0),
    ("Bark",      0.85, 0.0, 0.05, 0.0),
    ("PalmTrunk", 0.85, 0.0, 0.05, 0.0),
    ("Wood",      0.75, 0.0, 0.05, 0.0),
    ("Sandstone", 0.85, 0.0, 0.10, 0.0),
    ("Stone",     0.80, 0.0, 0.10, 0.0),
    ("Rock",      0.85, 0.0, 0.10, 0.0),
    ("Cobble",    0.80, 0.0, 0.10, 0.0),
    ("Leaf",      0.70, 0.0, 0.10, 0.0),
    ("Grass",     0.80, 0.0, 0.15, 0.0),
    ("Cactus",    0.65, 0.0, 0.05, 0.0),
    ("Sand",      0.90, 0.0, 0.12, 0.0),
    ("Dirt",      0.90, 0.0, 0.12, 0.0),
    ("CrackedEarth", 0.90, 0.0, 0.12, 0.0),
    ("Pad",       0.60, 0.0, 0.00, 0.0),
)
DEFAULT_FAMILY = ("", 0.75, 0.0, 0.0, 0.0)


def _family(name):
    for fam in FAMILIES:
        if fam[0].lower() in name.lower():
            return fam
    return DEFAULT_FAMILY


def _instances_of(parent_path):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    found = []
    for root in SEARCH_ROOTS:
        for data in registry.get_assets_by_path(root, recursive=True):
            if str(data.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            asset = data.get_asset()
            parent = asset.get_editor_property("parent")
            if parent is not None and parent.get_path_name().split(".")[0] == parent_path:
                found.append(asset)
    return found


def reparent_to_pbr(dry_run=False):
    master = unreal.EditorAssetLibrary.load_asset(PBR)
    if master is None:
        raise RuntimeError("build M_ValhallaPBR first")
    moved = []
    for mi in _instances_of(TOON):
        name = mi.get_name()
        fam, rough, metal, macro, grime = _family(name)
        moved.append("{} ({})".format(name, fam or "default"))
        if dry_run:
            continue
        MEL.set_material_instance_parent(mi, master)
        MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", rough)
        MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metal)
        MEL.set_material_instance_scalar_parameter_value(mi, "Specular", 0.5)
        MEL.set_material_instance_scalar_parameter_value(mi, "MacroVariation", macro)
        MEL.set_material_instance_scalar_parameter_value(mi, "Grime", grime)
        MEL.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
    _log("reparent_to_pbr {}: {} instance(s): {}".format(
        "dry run" if dry_run else "done", len(moved), ", ".join(moved)))
    return moved


def reparent_to_toon():
    """Rollback: every M_ValhallaPBR instance goes back to M_ValhallaToon."""
    toon = unreal.EditorAssetLibrary.load_asset(TOON)
    moved = []
    for mi in _instances_of(PBR):
        MEL.set_material_instance_parent(mi, toon)
        MEL.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
        moved.append(mi.get_name())
    _log("reparent_to_toon: {} instance(s)".format(len(moved)))
    return moved


# ── Ground blend master (Wave 1) ─────────────────────────────────────────────

GROUND_BLEND = MAT_DIR + "/M_ValhallaGroundBlend"


def build_ground_blend():
    """``M_ValhallaGroundBlend``: two world-aligned texture layers blended by
    vertex colour red (0 = layer A, 1 = layer B), with the edge broken up by
    world-space noise so a grass verge along a dirt path is ragged rather than
    ruled. Built only when it does not exist (see ``upgrade_pbr`` for why a live
    master is never rebuilt).

    Parameters: ``BaseColorMapA/B``, ``NormalMapA/B``, ``ORMMapA/B``,
    ``TextureWorldSizeA/B`` (cm per repeat), ``TintA/B``, ``BlendSharpness``,
    ``BlendNoise``, ``BlendNoiseScale``, ``MacroVariation``, ``MacroScale``.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(GROUND_BLEND):
        _log("M_ValhallaGroundBlend exists; left alone")
        return GROUND_BLEND
    material = _asset(GROUND_BLEND)
    _ensure_default_orm()

    world_pos = _expr(material, unreal.MaterialExpressionWorldPosition, -2800, 0)
    world_xy = _expr(material, unreal.MaterialExpressionComponentMask, -2600, 0)
    for ch, on in (("r", True), ("g", True), ("b", False), ("a", False)):
        world_xy.set_editor_property(ch, on)
    _wire(world_pos, "", world_xy, "")

    layers = {}
    for i, layer in enumerate(("A", "B")):
        y = -900 + i * 1200
        size = _scalar(material, "TextureWorldSize" + layer, 200.0, -2600, y, "Layer " + layer)
        uv = _expr(material, unreal.MaterialExpressionDivide, -2400, y)
        _wire(world_xy, "", uv, "A")
        _wire(size, "", uv, "B")
        bc = _texture(material, "BaseColorMap" + layer, WHITE,
                      unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -2100, y)
        nm = _texture(material, "NormalMap" + layer, FLAT_NORMAL,
                      unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -2100, y + 300)
        orm = _texture(material, "ORMMap" + layer, DEFAULT_ORM,
                       unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -2100, y + 600)
        for tex in (bc, nm, orm):
            tex.set_editor_property("group", "Layer " + layer)
            _wire(uv, "", tex, "UVs")
        tint = _vector(material, "Tint" + layer, unreal.LinearColor(1, 1, 1, 1), -2100, y - 200, "Layer " + layer)
        tinted = _mul(material, bc, tint, -1800, y, a_out="RGB")
        layers[layer] = (tinted, nm, orm)

    # Blend alpha = saturate((VC.R - 0.5 + noise * BlendNoise) * BlendSharpness + 0.5)
    vc = _expr(material, unreal.MaterialExpressionVertexColor, -2100, 1500)
    half = _const(material, 0.5, -2100, 1650)
    centred = _expr(material, unreal.MaterialExpressionSubtract, -1900, 1520)
    _wire(vc, "R", centred, "A")
    _wire(half, "", centred, "B")
    noise_scale = _scalar(material, "BlendNoiseScale", 60.0, -2400, 1800, "Blend")
    noise_pos = _expr(material, unreal.MaterialExpressionDivide, -2200, 1800)
    _wire(world_pos, "", noise_pos, "A")
    _wire(noise_scale, "", noise_pos, "B")
    noise = _expr(material, unreal.MaterialExpressionNoise, -2000, 1800)
    noise.set_editor_property("levels", 3)
    noise.set_editor_property("output_min", -1.0)
    noise.set_editor_property("output_max", 1.0)
    _wire(noise_pos, "", noise, "World Position")
    noise_amt = _scalar(material, "BlendNoise", 0.3, -2000, 2000, "Blend")
    noisy = _mul(material, noise, noise_amt, -1800, 1850)
    summed = _expr(material, unreal.MaterialExpressionAdd, -1650, 1600)
    _wire(centred, "", summed, "A")
    _wire(noisy, "", summed, "B")
    sharp = _scalar(material, "BlendSharpness", 4.0, -1650, 1800, "Blend")
    scaled = _mul(material, summed, sharp, -1500, 1650)
    shifted = _expr(material, unreal.MaterialExpressionAdd, -1350, 1650)
    _wire(scaled, "", shifted, "A")
    _wire(half, "", shifted, "B")
    alpha = _expr(material, unreal.MaterialExpressionSaturate, -1200, 1650)
    _wire(shifted, "", alpha, "")

    (bc_a, nm_a, orm_a), (bc_b, nm_b, orm_b) = layers["A"], layers["B"]
    base = _lerp(material, bc_a, bc_b, alpha, -1000, -600)
    normal = _lerp(material, nm_a, nm_b, alpha, -1000, -300, a_out="RGB", b_out="RGB")
    rough = _lerp(material, orm_a, orm_b, alpha, -1000, 0, a_out="G", b_out="G")
    ao = _lerp(material, orm_a, orm_b, alpha, -1000, 200, a_out="R", b_out="R")

    one = _const(material, 1.0, -900, -1100)
    macro_amount = _scalar(material, "MacroVariation", 0.12, -900, -1000, "Wear")
    macro_scale = _scalar(material, "MacroScale", 900.0, -1100, -1100, "Wear")
    macro_pos = _expr(material, unreal.MaterialExpressionDivide, -900, -1200)
    _wire(world_pos, "", macro_pos, "A")
    _wire(macro_scale, "", macro_pos, "B")
    macro_noise = _expr(material, unreal.MaterialExpressionNoise, -700, -1200)
    macro_noise.set_editor_property("levels", 2)
    macro_noise.set_editor_property("output_min", -1.0)
    macro_noise.set_editor_property("output_max", 1.0)
    _wire(macro_pos, "", macro_noise, "World Position")
    macro_term = _mul(material, macro_noise, macro_amount, -500, -1150)
    gain = _expr(material, unreal.MaterialExpressionAdd, -350, -1100)
    _wire(one, "", gain, "A")
    _wire(macro_term, "", gain, "B")
    base_final = _mul(material, base, gain, -200, -700)

    _out(base_final, unreal.MaterialProperty.MP_BASE_COLOR)
    _out(normal, unreal.MaterialProperty.MP_NORMAL)
    _out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    _out(ao, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    zero = _const(material, 0.0, -600, 400)
    _out(zero, unreal.MaterialProperty.MP_METALLIC)
    spec = _const(material, 0.5, -600, 500)
    _out(spec, unreal.MaterialProperty.MP_SPECULAR)

    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)
    MEL.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(GROUND_BLEND)
    _log("built {}".format(GROUND_BLEND))
    return GROUND_BLEND


def _out_pin(node, pin, prop):
    if not MEL.connect_material_property(node, pin, prop):
        raise RuntimeError("could not connect {}.{} to {}".format(node.get_class().get_name(), pin, prop))


FOLIAGE = MAT_DIR + "/M_ValhallaFoliage"
WATER = MAT_DIR + "/M_ValhallaWater"


def build_foliage():
    """``M_ValhallaFoliage`` (B-15 Wave 1 trees): masked, two-sided, Two Sided
    Foliage shading. Base colour alpha is the opacity mask (cards); an opaque
    texture gives alpha 1, so the canopy cores use the same master. Each
    instance of an instanced mesh picks a green between ``TintA`` and ``TintB``
    (PerInstanceRandom), so a field of the same tree doesn't repeat exactly.

    Parameters: ``BaseColorMap``, ``NormalMap``, ``ORMMap``, ``UVScale``,
    ``TintA``, ``TintB``, ``SubsurfaceColor``, ``RoughnessScale``,
    ``NormalStrength``. Built only when missing.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(FOLIAGE):
        _log("M_ValhallaFoliage exists; left alone")
        return FOLIAGE
    material = _asset(FOLIAGE)
    _ensure_default_orm()
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    material.set_editor_property("opacity_mask_clip_value", 0.4)

    tc = _expr(material, unreal.MaterialExpressionTextureCoordinate, -2000, 0)
    uv_scale = _scalar(material, "UVScale", 1.0, -2000, 150)
    uv = _mul(material, tc, uv_scale, -1800, 50)
    bc = _texture(material, "BaseColorMap", WHITE, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1500, -300)
    nm = _texture(material, "NormalMap", FLAT_NORMAL, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1500, 50)
    orm = _texture(material, "ORMMap", DEFAULT_ORM, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1500, 400)
    for tex in (bc, nm, orm):
        _wire(uv, "", tex, "UVs")

    tint_a = _vector(material, "TintA", unreal.LinearColor(0.55, 0.75, 0.35, 1), -1500, -650)
    tint_b = _vector(material, "TintB", unreal.LinearColor(0.75, 0.80, 0.35, 1), -1500, -500)
    tint = _lerp(material, tint_a, tint_b, _variation_alpha(material, -1300, -450), -1100, -600)
    base = _mul(material, bc, tint, -900, -400, a_out="RGB")
    _out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    _out(_edge_fade(material, bc, -900, 1100), unreal.MaterialProperty.MP_OPACITY_MASK)

    strength = _scalar(material, "NormalStrength", 1.0, -1300, 200)
    flat = _expr(material, unreal.MaterialExpressionConstant3Vector, -1300, 300)
    flat.set_editor_property("constant", unreal.LinearColor(0, 0, 1, 1))
    normal = _lerp(material, flat, nm, strength, -900, 100, b_out="RGB")
    _out(normal, unreal.MaterialProperty.MP_NORMAL)

    rscale = _scalar(material, "RoughnessScale", 1.0, -1300, 550)
    rough = _mul(material, orm, rscale, -900, 450, a_out="G")
    _out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    _out_pin(orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    spec = _const(material, 0.35, -900, 650)
    _out(spec, unreal.MaterialProperty.MP_SPECULAR)

    sss = _vector(material, "SubsurfaceColor", unreal.LinearColor(0.35, 0.45, 0.08, 1), -1100, 800)
    sss_tex = _mul(material, sss, bc, -900, 800, b_out="RGB")
    _out(sss_tex, unreal.MaterialProperty.MP_SUBSURFACE_COLOR)

    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)
    MEL.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(FOLIAGE)
    _log("built {}".format(FOLIAGE))
    return FOLIAGE


def build_water():
    """``M_ValhallaWater`` (B-15 Wave 1): opaque, glossy water for the tile
    kit. Two world-aligned samples of ``NormalMap`` pan in different
    directions and are added, so the ripples never visibly slide as one sheet;
    colour runs from ``DeepColor`` (looking down) to ``EdgeColor`` (grazing),
    which from the game camera reads as depth.

    Parameters: ``NormalMap``, ``RippleSize`` (cm per repeat), ``RippleSpeed``,
    ``NormalStrength``, ``DeepColor``, ``EdgeColor``, ``Roughness``,
    ``Specular``. Built only when missing.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(WATER):
        _log("M_ValhallaWater exists; left alone")
        return WATER
    material = _asset(WATER)

    world_pos = _expr(material, unreal.MaterialExpressionWorldPosition, -2600, 0)
    world_xy = _expr(material, unreal.MaterialExpressionComponentMask, -2400, 0)
    for ch, on in (("r", True), ("g", True), ("b", False), ("a", False)):
        world_xy.set_editor_property(ch, on)
    _wire(world_pos, "", world_xy, "")
    size = _scalar(material, "RippleSize", 300.0, -2400, 150, "Water")
    uv = _expr(material, unreal.MaterialExpressionDivide, -2200, 50)
    _wire(world_xy, "", uv, "A")
    _wire(size, "", uv, "B")
    time = _expr(material, unreal.MaterialExpressionTime, -2400, 300)
    speed = _scalar(material, "RippleSpeed", 0.02, -2400, 400, "Water")
    t = _mul(material, time, speed, -2200, 350)

    samples = []
    for i, (dx, dy, scale) in enumerate(((1.0, 0.35, 1.0), (-0.45, 1.0, 1.7))):
        y = -200 + i * 450
        direction = _expr(material, unreal.MaterialExpressionConstant2Vector, -2000, y + 150)
        direction.set_editor_property("r", dx)
        direction.set_editor_property("g", dy)
        offset = _mul(material, t, direction, -1850, y + 150)
        k = _const(material, scale, -2000, y)
        scaled = _mul(material, uv, k, -1850, y)
        moved = _expr(material, unreal.MaterialExpressionAdd, -1700, y)
        _wire(scaled, "", moved, "A")
        _wire(offset, "", moved, "B")
        tex = _texture(material, "NormalMap", FLAT_NORMAL, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1500, y)
        tex.set_editor_property("group", "Water")
        _wire(moved, "", tex, "UVs")
        samples.append(tex)
    added = _expr(material, unreal.MaterialExpressionAdd, -1200, 0)
    _wire(samples[0], "RGB", added, "A")
    _wire(samples[1], "RGB", added, "B")
    strength = _scalar(material, "NormalStrength", 0.6, -1200, 200, "Water")
    flat = _expr(material, unreal.MaterialExpressionConstant3Vector, -1200, 300)
    flat.set_editor_property("constant", unreal.LinearColor(0, 0, 1, 1))
    mixed = _lerp(material, flat, added, strength, -1000, 100)
    normal = _expr(material, unreal.MaterialExpressionNormalize, -800, 100)
    _wire(mixed, "", normal, "")
    _out(normal, unreal.MaterialProperty.MP_NORMAL)

    deep = _vector(material, "DeepColor", unreal.LinearColor(0.012, 0.045, 0.06, 1), -1200, -500, "Water")
    edge = _vector(material, "EdgeColor", unreal.LinearColor(0.06, 0.16, 0.17, 1), -1200, -350, "Water")
    fres = _expr(material, unreal.MaterialExpressionFresnel, -1200, -200)
    fres.set_editor_property("exponent", 3.0)
    color = _lerp(material, deep, edge, fres, -900, -400)
    _out(color, unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _scalar(material, "Roughness", 0.05, -900, 350, "Water")
    _out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    spec = _scalar(material, "Specular", 0.6, -900, 450, "Water")
    _out(spec, unreal.MaterialProperty.MP_SPECULAR)
    zero = _const(material, 0.0, -900, 550)
    _out(zero, unreal.MaterialProperty.MP_METALLIC)

    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)
    MEL.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(WATER)
    _log("built {}".format(WATER))
    return WATER


def _variation_alpha(material, x, y):
    """0..1 per placed tree: PerInstanceRandom for instanced meshes, plus a
    hash of the object position so individually placed actors vary too."""
    rnd = _expr(material, unreal.MaterialExpressionPerInstanceRandom, x - 600, y - 150)
    pos = _expr(material, unreal.MaterialExpressionObjectPositionWS, x - 800, y)
    xy = _expr(material, unreal.MaterialExpressionComponentMask, x - 650, y)
    for ch, on in (("r", True), ("g", True), ("b", False), ("a", False)):
        xy.set_editor_property(ch, on)
    _wire(pos, "", xy, "")
    k = _expr(material, unreal.MaterialExpressionConstant2Vector, x - 650, y + 100)
    k.set_editor_property("r", 0.0129898)
    k.set_editor_property("g", 0.078233)
    dot = _expr(material, unreal.MaterialExpressionDotProduct, x - 500, y)
    _wire(xy, "", dot, "A")
    _wire(k, "", dot, "B")
    sine = _expr(material, unreal.MaterialExpressionSine, x - 380, y)
    sine.set_editor_property("period", 6.283185)
    _wire(dot, "", sine, "")
    big = _mul(material, sine, _const(material, 43758.547, x - 380, y + 100), x - 260, y)
    added = _expr(material, unreal.MaterialExpressionAdd, x - 140, y - 60)
    _wire(big, "", added, "A")
    _wire(rnd, "", added, "B")
    frac = _expr(material, unreal.MaterialExpressionFrac, x, y - 60)
    _wire(added, "", frac, "")
    return frac


def upgrade_foliage():
    """Patch a live M_ValhallaFoliage: tint variation also from the object
    position (trees in the levels are single actors, where PerInstanceRandom
    is always 0). Idempotent."""
    material = unreal.EditorAssetLibrary.load_asset(FOLIAGE)
    if material is None:
        return build_foliage()
    seen, stack, lerp = set(), [MEL.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)], None
    has_hash = False
    while stack:
        node = stack.pop()
        if node is None or node.get_path_name() in seen:
            continue
        seen.add(node.get_path_name())
        if isinstance(node, unreal.MaterialExpressionObjectPositionWS):
            has_hash = True
        if isinstance(node, unreal.MaterialExpressionLinearInterpolate) and lerp is None:
            lerp = node
        stack.extend(MEL.get_inputs_for_material_expression(material, node))
    if has_hash or lerp is None:
        _log("upgrade_foliage: already current" if has_hash else "upgrade_foliage: tint lerp not found")
        return []
    _wire(_variation_alpha(material, -1300, -450), "", lerp, "Alpha")
    MEL.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(FOLIAGE)
    _log("upgrade_foliage: tint variation from object position")
    return ["object-position tint variation"]


def _edge_fade(material, bc, x, y):
    """Opacity = BaseColor alpha, faded out where a card is seen edge-on
    (``EdgeFade`` 1 on cards, 0 on the opaque cores, whose rounded silhouettes
    must not be clipped)."""
    nrm = _expr(material, unreal.MaterialExpressionVertexNormalWS, x - 700, y)
    cam = _expr(material, unreal.MaterialExpressionCameraVectorWS, x - 700, y + 100)
    dot = _expr(material, unreal.MaterialExpressionDotProduct, x - 550, y)
    _wire(nrm, "", dot, "A")
    _wire(cam, "", dot, "B")
    ab = _expr(material, unreal.MaterialExpressionAbs, x - 430, y)
    _wire(dot, "", ab, "")
    lo = _scalar(material, "EdgeFadeStart", 0.15, x - 430, y + 100, "Foliage")
    sub = _expr(material, unreal.MaterialExpressionSubtract, x - 320, y)
    _wire(ab, "", sub, "A")
    _wire(lo, "", sub, "B")
    gain = _mul(material, sub, _const(material, 4.0, x - 320, y + 100), x - 220, y)
    sat = _expr(material, unreal.MaterialExpressionSaturate, x - 120, y)
    _wire(gain, "", sat, "")
    amount = _scalar(material, "EdgeFade", 0.0, x - 120, y + 100, "Foliage")
    one = _const(material, 1.0, x - 120, y + 200)
    fade = _lerp(material, one, sat, amount, x, y)
    return _mul(material, bc, fade, x + 150, y, a_out="A")


def upgrade_foliage_edges():
    """Patch a live M_ValhallaFoliage: edge-on card fade. Idempotent."""
    material = unreal.EditorAssetLibrary.load_asset(FOLIAGE)
    node = MEL.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY_MASK)
    if isinstance(node, unreal.MaterialExpressionMultiply):
        _log("upgrade_foliage_edges: already current")
        return []
    bc = _find_texture_param(material, unreal.MaterialProperty.MP_OPACITY_MASK, "BaseColorMap")
    masked = _edge_fade(material, bc, -900, 1100)
    _out(masked, unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(FOLIAGE)
    _log("upgrade_foliage_edges: edge-on card fade")
    return ["edge fade"]

