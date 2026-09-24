"""B-15 Wave 5: moving fire for the fire props (campfires, braziers, the
fireplace, candles) and the flickering light that goes with it.

``build()`` authors three things:

``/Game/Valhalla/Materials/M_ValhallaFire`` + ``Sets/MI_Fire``
    The flame material. The props carry their flames as three crossed vertical
    cards with a full 0..1 UV each (``wave5_kit.flame``); this material cuts a
    teardrop out of every card and eats into it with 3D value noise that rises
    through the prop's *object space* over time, so the crossed cards sample one
    volume of noise and read as a single, moving flame. Unlit, masked (Nanite
    draws it), two-sided. The object's world position offsets the noise so two
    fires side by side do not flicker in step.

``/Game/Valhalla/Materials/M_FireFlicker`` + ``Sets/MI_FireFlicker_A/B/C``
    A light-function material: a sum of three sines, 0.72..1.0. The three
    instances differ only in phase; ``fire_lights`` hands them out by actor so
    neighbouring fires' lights do not pulse together.

Emissive gain: an emissive of 1.0 is *dark* in this game's sunlit levels (see
build_vfx_materials); flames run at Gain 5 with a red-orange
ramp whose green stays low, so the hue survives the exposure; only the root goes
yellow-white.
"""

import unreal

from valhalla_tools import build_vfx_materials as bvm

MAT_DIR = "/Game/Valhalla/Materials"
SETS = MAT_DIR + "/Sets"
FIRE = MAT_DIR + "/M_ValhallaFire"
FLICKER = MAT_DIR + "/M_FireFlicker"
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

_FIRE_HLSL = """
#define H3(v) frac(sin(dot(v, float3(127.1, 311.7, 74.7))) * 43758.5453)
float3 p = P * NoiseScale + float3(O.x * 97.0, O.y * 89.0, O.z * 13.0);
p.z -= T * Rise;
// A: fine noise that eats the flame; B: coarse noise that bends it.
float3 q = p;
float n = 0.0;
float amp = 0.5;
for (int oa = 0; oa < 3; oa++)
{
    float3 i = floor(q);
    float3 f = frac(q);
    f = f * f * (3.0 - 2.0 * f);
    float c0 = H3(i);
    float c1 = H3(i + float3(1, 0, 0));
    float c2 = H3(i + float3(0, 1, 0));
    float c3 = H3(i + float3(1, 1, 0));
    float c4 = H3(i + float3(0, 0, 1));
    float c5 = H3(i + float3(1, 0, 1));
    float c6 = H3(i + float3(0, 1, 1));
    float c7 = H3(i + float3(1, 1, 1));
    n += amp * lerp(lerp(lerp(c0, c1, f.x), lerp(c2, c3, f.x), f.y), lerp(lerp(c4, c5, f.x), lerp(c6, c7, f.x), f.y), f.z);
    q = q * 2.03 + 17.0;
    amp *= 0.5;
}
float nA = n / 0.875;
q = p * 0.5 + float3(31.7, 11.3, 5.1);
n = 0.0;
amp = 0.5;
for (int ob = 0; ob < 3; ob++)
{
    float3 i = floor(q);
    float3 f = frac(q);
    f = f * f * (3.0 - 2.0 * f);
    float c0 = H3(i);
    float c1 = H3(i + float3(1, 0, 0));
    float c2 = H3(i + float3(0, 1, 0));
    float c3 = H3(i + float3(1, 1, 0));
    float c4 = H3(i + float3(0, 0, 1));
    float c5 = H3(i + float3(1, 0, 1));
    float c6 = H3(i + float3(0, 1, 1));
    float c7 = H3(i + float3(1, 1, 1));
    n += amp * lerp(lerp(lerp(c0, c1, f.x), lerp(c2, c3, f.x), f.y), lerp(lerp(c4, c5, f.x), lerp(c6, c7, f.x), f.y), f.z);
    q = q * 2.03 + 17.0;
    amp *= 0.5;
}
float nB = n / 0.875;
float h = saturate(FlipV > 0.5 ? 1.0 - UV.y : UV.y);           // 0 at the base, 1 at the tip
float xs = UV.x - 0.5 + (nB - 0.5) * 0.6 * h;                  // the flame leans and licks
float w = abs(xs) * 2.0;
float width = max(0.03, pow(saturate(1.0 - h), 0.5)) * (0.9 + 0.4 * nA);
float body = saturate(1.0 - w / width);
float m = body * (2.0 * nA - 0.3) + body * (1.0 - h) * 0.5 - h * 0.3;    // holes and tongues
float t = saturate(body * (1.0 - h) * (0.55 + 0.7 * nA) * 1.25);   // hot at the root and centre, cool at edges and tips
float3 col = lerp(ColorLow, ColorHigh, t);
col = lerp(col, float3(1.0, 0.85, 0.5), saturate(t * t * t * 1.2));  // yellow-white core
return float4(col * Gain * (0.5 + 0.8 * t), saturate((m - 0.05) * 2.2));  // soft edge, dithered
"""

_FLICKER_HLSL = """
float t = T + Phase;
float f = 0.86 + 0.06 * sin(t * 9.3) + 0.045 * sin(t * 23.1 + 1.7) + 0.035 * sin(t * 4.1 + 0.3)
        + 0.02 * sin(t * 41.0 + 2.2);
return f.xxx;
"""


def _scalar(material, name, value, x, y):
    node = bvm._expr(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", float(value))
    return node


def _vector(material, name, rgb, x, y):
    node = bvm._expr(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return node


def _custom(material, code, desc, names, out, x, y):
    node = bvm._expr(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("description", desc)
    node.set_editor_property("output_type", bvm._enum(unreal.CustomMaterialOutputType, out))
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    node.set_editor_property("inputs", ins)
    return node


def build_fire_material():
    bvm._refuse_during_pie()
    mat = bvm._asset(FIRE)
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.35)
    # Dithered coverage: the soft 0..1 mask becomes a stipple that TSR resolves
    # into a soft, see-through edge, which a hard mask cannot give a flame.
    mat.set_editor_property("dither_opacity_mask", True)
    for flag in ("MATUSAGE_NANITE", "MATUSAGE_STATIC_MESH"):
        u = getattr(unreal.MaterialUsage, flag, None)
        if u is not None:
            MEL.set_material_usage(mat, u)

    names = ["UV", "P", "O", "T", "NoiseScale", "Rise", "Gain", "ColorLow", "ColorHigh", "FlipV"]
    node = _custom(mat, _FIRE_HLSL, "ValhallaFire", names, "CMOT_FLOAT4", -500, 0)
    uv = bvm._expr(mat, unreal.MaterialExpressionTextureCoordinate, -1100, -300)
    local = bvm._expr(mat, unreal.MaterialExpressionLocalPosition, -1100, -200)
    objpos = bvm._expr(mat, unreal.MaterialExpressionObjectPositionWS, -1300, -100)
    div = bvm._expr(mat, unreal.MaterialExpressionDivide, -1150, -100)
    k = bvm._expr(mat, unreal.MaterialExpressionConstant, -1300, -20)
    k.set_editor_property("r", 997.0)
    bvm._wire(objpos, "", div, "A")
    bvm._wire(k, "", div, "B")
    frac = bvm._expr(mat, unreal.MaterialExpressionFrac, -1000, -100)
    bvm._wire(div, "", frac, "")
    time = bvm._expr(mat, unreal.MaterialExpressionTime, -1100, 0)
    params = {
        "NoiseScale": _scalar(mat, "NoiseScale", 0.07, -1100, 80),
        "Rise": _scalar(mat, "Rise", 3.5, -1100, 160),
        "Gain": _scalar(mat, "Gain", 5.0, -1100, 240),
        "ColorLow": _vector(mat, "ColorLow", (1.0, 0.09, 0.0), -1100, 320),
        "ColorHigh": _vector(mat, "ColorHigh", (1.0, 0.42, 0.04), -1100, 420),
        "FlipV": _scalar(mat, "FlipV", 1.0, -1100, 520),
    }
    bvm._wire(uv, "", node, "UV")
    bvm._wire(local, "", node, "P")
    bvm._wire(frac, "", node, "O")
    bvm._wire(time, "", node, "T")
    for n, p in params.items():
        bvm._wire(p, "", node, n)
    rgb = bvm._expr(mat, unreal.MaterialExpressionComponentMask, -200, -60)
    for c, v in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(c, v)
    alpha = bvm._expr(mat, unreal.MaterialExpressionComponentMask, -200, 80)
    for c, v in (("r", False), ("g", False), ("b", False), ("a", True)):
        alpha.set_editor_property(c, v)
    bvm._wire(node, "", rgb, "")
    bvm._wire(node, "", alpha, "")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.recompile_material(mat)
    EAL.save_asset(FIRE)
    return mat


def build_flicker_material():
    bvm._refuse_during_pie()
    mat = bvm._asset(FLICKER)
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    node = _custom(mat, _FLICKER_HLSL, "FireFlicker", ["T", "Phase"], "CMOT_FLOAT3", -400, 0)
    bvm._wire(bvm._expr(mat, unreal.MaterialExpressionTime, -700, 0), "", node, "T")
    bvm._wire(_scalar(mat, "Phase", 0.0, -700, 100), "", node, "Phase")
    MEL.connect_material_property(node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_asset(FLICKER)
    return mat


def _instance(name, parent, scalars=None, vectors=None):
    path = "{}/{}".format(SETS, name)
    mi = EAL.load_asset(path)
    if mi is None:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, SETS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


FLICKER_INSTANCES = ("MI_FireFlicker_A", "MI_FireFlicker_B", "MI_FireFlicker_C")


def build(flip_v=1.0):
    fire = build_fire_material()
    _instance("MI_Fire", fire, {"FlipV": flip_v})
    flicker = build_flicker_material()
    for i, name in enumerate(FLICKER_INSTANCES):
        _instance(name, flicker, {"Phase": 2.37 * i})
    return True
