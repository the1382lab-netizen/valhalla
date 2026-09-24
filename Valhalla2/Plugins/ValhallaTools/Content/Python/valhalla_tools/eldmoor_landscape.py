"""B-06 1.4: the Eldmoor Grasslands Landscape's material and layer data.

* `build_material()` authors `/Game/Valhalla/Materials/Landscape/M_EldmoorLandscape`
  (only if missing): four weight-blended paint layers - Grass, Moss, Dirt, Rock -
  on the same world-aligned texture sets and sizes as the tile kit
  (`MI_GrassGround` 140 cm, `MI_DirtPath` 148 cm, `MI_Rock` 216 cm; Moss is the
  grass set tinted dark for Thornwood and the vale), the ground blend's macro
  variation, and a Landscape Visibility Mask on Opacity Mask for the undercroft
  hole. PBR-lit like `M_ValhallaPBR`, so it reads with the kit under L_World's light.
* `heights_from_terrain()` / the PNGs from `eldmoor_terrain.py` are what the
  Landscape was imported from (Landscape mode > New > Import from File, see
  `create_notes()`); the editor has no Python API that creates Landscape
  components, so that one step is a UI step.
"""

import unreal

MAT_DIR = "/Game/Valhalla/Materials/Landscape"
MATERIAL = MAT_DIR + "/M_EldmoorLandscape"
LAYERS = (
    # name, texture set, world size cm, tint
    ("Grass", "GrassGround", 140.0, (1.0, 1.0, 1.0)),
    ("Moss", "GrassGround", 170.0, (0.55, 0.62, 0.45)),
    ("Dirt", "DirtPath", 148.0, (1.0, 1.0, 1.0)),
    ("Rock", "Rock", 216.0, (0.95, 0.95, 0.92)),
)
MEL = unreal.MaterialEditingLibrary


def _log(msg):
    unreal.log("VALHALLA_ELDMOOR " + msg)


def _expr(m, cls, x, y):
    return MEL.create_material_expression(m, cls, x, y)


def _wire(a, a_out, b, b_in):
    if not MEL.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError("wire {}.{} -> {}.{}".format(a.get_class().get_name(), a_out,
                                                         b.get_class().get_name(), b_in))


def _scalar(m, name, v, x, y, group):
    n = _expr(m, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", v)
    n.set_editor_property("group", group)
    return n


def build_material():
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        _log("{} exists; left alone".format(MATERIAL))
        return MATERIAL
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_EldmoorLandscape", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    wp = _expr(m, unreal.MaterialExpressionWorldPosition, -3000, 0)
    xy = _expr(m, unreal.MaterialExpressionComponentMask, -2800, 0)
    for ch, on in (("r", True), ("g", True), ("b", False), ("a", False)):
        xy.set_editor_property(ch, on)
    _wire(wp, "", xy, "")

    blends = {}
    for k, (prop, y) in enumerate((("BaseColor", -1200), ("Normal", 0), ("ORM", 1200))):
        node = _expr(m, unreal.MaterialExpressionLandscapeLayerBlend, -800, y)
        inputs = []
        for name, _, _, _ in LAYERS:
            li = unreal.LayerBlendInput()
            li.set_editor_property("layer_name", name)
            li.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
            li.set_editor_property("preview_weight", 1.0 if name == "Grass" else 0.0)
            inputs.append(li)
        node.set_editor_property("layers", inputs)
        blends[prop] = node

    for i, (name, tex_set, size, tint) in enumerate(LAYERS):
        y = -1500 + i * 800
        s = _scalar(m, name + "WorldSize", size, -2600, y, name)
        uv = _expr(m, unreal.MaterialExpressionDivide, -2400, y)
        _wire(xy, "", uv, "A")
        _wire(s, "", uv, "B")
        samples = {}
        for j, (suffix, st) in enumerate((("BC", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                                          ("N", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                                          ("ORM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS))):
            t = _expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -2200, y + j * 220)
            t.set_editor_property("parameter_name", "{}_{}".format(name, suffix))
            t.set_editor_property("group", name)
            t.set_editor_property("texture", unreal.EditorAssetLibrary.load_asset(
                "/Game/Valhalla/Textures/{0}/T_{0}_{1}".format(tex_set, suffix)))
            t.set_editor_property("sampler_type", st)
            _wire(uv, "", t, "UVs")
            samples[suffix] = t
        tv = _expr(m, unreal.MaterialExpressionVectorParameter, -2000, y - 120)
        tv.set_editor_property("parameter_name", name + "Tint")
        tv.set_editor_property("group", name)
        tv.set_editor_property("default_value", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        tinted = _expr(m, unreal.MaterialExpressionMultiply, -1800, y)
        _wire(samples["BC"], "RGB", tinted, "A")
        _wire(tv, "", tinted, "B")
        _wire(tinted, "", blends["BaseColor"], "Layer " + name)
        _wire(samples["N"], "RGB", blends["Normal"], "Layer " + name)
        _wire(samples["ORM"], "RGB", blends["ORM"], "Layer " + name)

    # macro variation (as M_ValhallaGroundBlend)
    ms = _scalar(m, "MacroScale", 900.0, -1000, -2000, "Wear")
    mp = _expr(m, unreal.MaterialExpressionDivide, -800, -2000)
    _wire(wp, "", mp, "A")
    _wire(ms, "", mp, "B")
    mn = _expr(m, unreal.MaterialExpressionNoise, -600, -2000)
    mn.set_editor_property("levels", 2)
    mn.set_editor_property("output_min", -1.0)
    mn.set_editor_property("output_max", 1.0)
    _wire(mp, "", mn, "World Position")
    ma = _scalar(m, "MacroVariation", 0.12, -600, -1800, "Wear")
    mt = _expr(m, unreal.MaterialExpressionMultiply, -400, -1950)
    _wire(mn, "", mt, "A")
    _wire(ma, "", mt, "B")
    one = _expr(m, unreal.MaterialExpressionConstant, -400, -1800)
    one.set_editor_property("r", 1.0)
    gain = _expr(m, unreal.MaterialExpressionAdd, -250, -1900)
    _wire(one, "", gain, "A")
    _wire(mt, "", gain, "B")
    base = _expr(m, unreal.MaterialExpressionMultiply, -300, -1200)
    _wire(blends["BaseColor"], "", base, "A")
    _wire(gain, "", base, "B")
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(blends["Normal"], "", unreal.MaterialProperty.MP_NORMAL)
    orm = blends["ORM"]
    for pin, prop in (("R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),
                      ("G", unreal.MaterialProperty.MP_ROUGHNESS)):
        mask = _expr(m, unreal.MaterialExpressionComponentMask, -400, 1200 + (0 if pin == "R" else 150))
        for ch in ("r", "g", "b", "a"):
            mask.set_editor_property(ch, ch == pin.lower())
        _wire(orm, "", mask, "")
        MEL.connect_material_property(mask, "", prop)
    zero = _expr(m, unreal.MaterialExpressionConstant, -300, 1600)
    MEL.connect_material_property(zero, "", unreal.MaterialProperty.MP_METALLIC)
    spec = _expr(m, unreal.MaterialExpressionConstant, -300, 1700)
    spec.set_editor_property("r", 0.5)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    vis = _expr(m, unreal.MaterialExpressionLandscapeVisibilityMask, -300, 1900)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    MEL.connect_material_property(vis, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    MEL.recompile_material(m)
    unreal.EditorAssetLibrary.save_asset(MATERIAL)
    _log("built {}".format(MATERIAL))
    return MATERIAL
