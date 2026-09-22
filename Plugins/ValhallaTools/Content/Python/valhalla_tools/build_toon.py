"""The Phase 4c look: one flat-shaded master material and a Sobel outline.

Two assets, and between them they are the whole art direction:

``M_ValhallaToon``
    Lit, rough, unlit-looking. A single ``BaseColor`` vector parameter per
    instance, no textures, no normal maps, roughness pinned at 0.9 so the
    highlight never breaks the flat colour region. ``UseVertexColor`` folds
    COLOR_0 in for the hair, whose colour is painted into the mesh rather than
    carried by the material, and ``EmissiveStrength`` lights the staff orb and
    the mace head.

``PP_Outline``
    A post-process material that finds silhouettes the cheap, reliable way: a
    four-tap cross around each pixel, comparing scene depth (catches
    silhouettes against the background and against other characters) and world
    normal (catches creases inside one object, like an arm against a torso).
    The two edge terms are summed and thresholded. Depth is divided by the
    centre depth first, so a line is the same weight 200 cm away and 2000 cm
    away — without that, distant characters lose their outline entirely and
    near ones are drawn in a fat black smear.

The mask maths lives in one Custom node rather than twenty-five Multiply and
Subtract nodes. That is not a shortcut: the taps themselves are real
SceneTexture nodes (a Custom node cannot sample on its own), and putting only
the arithmetic in HLSL keeps the graph legible instead of turning it into a
wall of unlabelled two-input maths.
"""

import unreal

MAT_DIR = "/Game/Valhalla/Materials"
TOON = MAT_DIR + "/M_ValhallaToon"
OUTLINE = MAT_DIR + "/PP_Outline"

TAG = "LogValhallaImport:"


def _log(msg):
    unreal.log("{} {}".format(TAG, msg))


def _enum(enum_type, *candidates):
    """First candidate name that exists on an unreal enum. Names move."""
    for name in candidates:
        if hasattr(enum_type, name):
            return getattr(enum_type, name)
    raise RuntimeError("none of {} on {}".format(candidates, enum_type))


def _asset(package_path, factory):
    name = package_path.rsplit("/", 1)[1]
    folder = package_path.rsplit("/", 1)[0]
    existing = unreal.EditorAssetLibrary.load_asset(package_path)
    if existing is not None:
        return existing
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.Material, factory)


def _expr(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def _wire(src, src_out, dst, dst_in):
    """Connect two expressions, loudly.

    ``connect_material_expressions`` returns False and does nothing at all when
    an input name is wrong — which produces a material that compiles but reads
    the wrong thing, and is far harder to find later than an exception here.
    """
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            src, src_out, dst, dst_in):
        raise RuntimeError("could not wire {}.{} -> {}.{}".format(
            src.get_class().get_name(), src_out or "<default>",
            dst.get_class().get_name(), dst_in or "<default>"))


def _scalar(material, name, value, x, y):
    node = _expr(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def _vector(material, name, color, x, y):
    node = _expr(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", color)
    return node


# ── The master ───────────────────────────────────────────────────────────────

def build_toon():
    material = _asset(TOON, unreal.MaterialFactoryNew())

    # Rebuilding from scratch keeps this idempotent; an edited graph that half
    # matches the code is worse than no graph at all.
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    base = _vector(material, "BaseColor", unreal.LinearColor(0.5, 0.5, 0.5, 1.0), -900, -200)
    vertex = _expr(material, unreal.MaterialExpressionVertexColor, -900, 0)
    tinted = _expr(material, unreal.MaterialExpressionMultiply, -650, -100)
    _wire(base, "", tinted, "A")
    _wire(vertex, "", tinted, "B")

    use_vertex = _scalar(material, "UseVertexColor", 0.0, -900, 150)
    pick = _expr(material, unreal.MaterialExpressionLinearInterpolate, -400, -100)
    _wire(base, "", pick, "A")
    _wire(tinted, "", pick, "B")
    _wire(use_vertex, "", pick, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        pick, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Flat regions need a rough, unvarying surface. 0.9 is high enough that the
    # specular lobe never becomes a visible highlight at this camera distance.
    roughness = _scalar(material, "Roughness", 0.9, -400, 150)
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    metallic = _scalar(material, "Metallic", 0.0, -400, 250)
    unreal.MaterialEditingLibrary.connect_material_property(
        metallic, "", unreal.MaterialProperty.MP_METALLIC)

    specular = _scalar(material, "Specular", 0.1, -400, 350)
    unreal.MaterialEditingLibrary.connect_material_property(
        specular, "", unreal.MaterialProperty.MP_SPECULAR)

    emissive_color = _vector(material, "EmissiveColor",
                             unreal.LinearColor(0.0, 0.0, 0.0, 1.0), -900, 450)
    emissive_strength = _scalar(material, "EmissiveStrength", 0.0, -900, 600)
    emissive = _expr(material, unreal.MaterialExpressionMultiply, -500, 500)
    _wire(emissive_color, "", emissive, "A")
    _wire(emissive_strength, "", emissive, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(TOON)
    _log("built {}".format(TOON))
    return TOON


# ── The outline ──────────────────────────────────────────────────────────────

_OUTLINE_HLSL = """
// Four-tap cross. The depth term is curvature over slope, which is scale free.
//
// PHASE 8A ADDED TWO THINGS TO THIS. Both are here because the Phase 3 town
// shipped with a visible fault that no value of Threshold could fix.
//
// 1. The stencil test. The floors are AValhallaTileField instances on a 64 cm
//    grid, and at this camera distance a seam between two tiles and the edge of
//    a character are the *same size* of depth step: raising Threshold until the
//    seams disappeared also took the outline off the characters. So the floors
//    say what they are instead — every tile field renders to custom depth with
//    stencil 1 (ValhallaStencil::Floor) — and an edge is discarded only when
//    both of its taps *and* the centre are floor. That keeps the field's outer
//    boundary, where one tap is grass or a wall or the sky, and drops all 4096
//    interior seams.
//
// 2. The curvature noise floor. Flat-shaded roof facets are nearly face-on to
//    an isometric camera, so `slope` is small, the denominator falls onto its
//    own floor, and what came out was depth-buffer noise divided by a constant
//    — faint horizontal banding across every roof. Depth precision error scales
//    with distance, so the floor subtracted from the curvature does too. A real
//    silhouette's curvature is the whole depth step, hundreds of centimetres;
//    CurvatureNoise * dC at the town's camera distance is about eight.
//
// Two wrong versions were tried first and both are instructive.
//
// A plain Sobel, |dR - dL|, measures the depth *gradient*, and on an isometric
// camera every surface in the level has a large one: the ground is seen at 45
// degrees and the walls almost edge-on. That paints the whole level black and
// leaves the threshold choosing between "wireframe" and "no outline".
//
// The second difference, |dL + dR - 2*dC|, measures curvature and is correctly
// zero across a tilted plane — but only in exact arithmetic. Near-edge-on
// surfaces span hundreds of centimetres of depth per pixel, so the curvature
// estimate is swamped by depth-buffer precision and the walls stripe.
//
// Dividing curvature by slope fixes both, because the noise is in both terms.
// On any plane the ratio is ~0 whatever its tilt. At a genuine silhouette,
// where depth steps from near to far, curvature and slope are both that step
// and the ratio is ~1. So the result is a normalised 0..1 silhouette
// indicator, and Threshold is a fraction rather than a number that has to be
// retuned for the camera distance.
float dC = max(DepthC.r, 1.0);

// Is this pixel, and each axis's pair of neighbours, floor? The stencil comes
// back as the raw integer in .r, so 0.5 is the only threshold that matters.
float floorC = step(0.5, StencilC.r);
float keepX  = 1.0 - floorC * step(0.5, StencilL.r) * step(0.5, StencilR.r);
float keepY  = 1.0 - floorC * step(0.5, StencilU.r) * step(0.5, StencilD.r);

float slopeX = abs(DepthR.r - DepthL.r);
float slopeY = abs(DepthU.r - DepthD.r);
float curveX = abs(DepthL.r + DepthR.r - 2.0 * dC) * keepX;
float curveY = abs(DepthU.r + DepthD.r - 2.0 * dC) * keepY;

float slope = slopeX + slopeY;
// Subtract the depth buffer's own noise before dividing, or a near-face-on
// plane divides noise by the denominator's floor and stripes.
float curve = max(curveX + curveY - dC * CurvatureNoise, 0.0);

// The floor on the denominator is what keeps a flat, face-on surface — where
// both terms are zero — from dividing noise by noise.
float depthEdge = curve / max(slope, dC * 0.002);

// Normals catch the creases depth cannot see: an arm against a torso is
// continuous in depth and discontinuous in normal. Masked by the same floor
// test, so two adjacent tiles whose normals differ by a hair do not draw one
// either.
float3 nL = NormalL.rgb;
float3 nR = NormalR.rgb;
float3 nU = NormalU.rgb;
float3 nD = NormalD.rgb;
float normalEdge = (1.0 - saturate(dot(nR, nL))) * keepX
                 + (1.0 - saturate(dot(nU, nD))) * keepY;

float edge = depthEdge * DepthScale + normalEdge * NormalScale;
// A soft ramp rather than a hard step: a hard step crawls when the camera moves.
return smoothstep(Threshold, Threshold * 2.0, edge);
"""

_CUSTOM_INPUTS = ("DepthC", "DepthL", "DepthR", "DepthU", "DepthD",
                  "NormalL", "NormalR", "NormalU", "NormalD",
                  "StencilC", "StencilL", "StencilR", "StencilU", "StencilD",
                  "DepthScale", "NormalScale", "Threshold", "CurvatureNoise")

#: Left, right, up, down, in texels.
_OFFSETS = (("L", -1.0, 0.0), ("R", 1.0, 0.0), ("U", 0.0, -1.0), ("D", 0.0, 1.0))


def _scene_texture(material, texture_id, x, y):
    node = _expr(material, unreal.MaterialExpressionSceneTexture, x, y)
    node.set_editor_property("scene_texture_id", texture_id)
    return node


def build_outline():
    scene_depth = _enum(unreal.SceneTextureId, "PPI_SCENE_DEPTH", "SCENE_DEPTH")
    world_normal = _enum(unreal.SceneTextureId, "PPI_WORLD_NORMAL", "WORLD_NORMAL")
    custom_stencil = _enum(unreal.SceneTextureId, "PPI_CUSTOM_STENCIL",
                           "CUSTOM_STENCIL")
    scene_color = _enum(unreal.SceneTextureId, "PPI_POST_PROCESS_INPUT0",
                        "POST_PROCESS_INPUT0")

    material = _asset(OUTLINE, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # Before DOF, i.e. the old "Before Tonemapping" slot. It has to be this side
    # of the tonemapper: SceneDepth and WorldNormal are GBuffer reads, and the
    # GBuffer is gone by the time the tonemapped image exists — a post-process
    # material that samples them after tonemapping simply fails to compile.
    material.set_editor_property(
        "blendable_location",
        _enum(unreal.BlendableLocation, "BL_SCENE_COLOR_BEFORE_DOF",
              "BL_BEFORE_TONEMAPPING"))

    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    screen = _expr(material, unreal.MaterialExpressionScreenPosition, -1900, 0)

    # The centre depth tap doubles as the source of the texel size, which is
    # what turns "1.5 pixels" into a UV offset.
    centre = _scene_texture(material, scene_depth, -1500, 0)
    _wire(screen, "ViewportUV", centre, "UVs")

    centre_stencil = _scene_texture(material, custom_stencil, -1500, 150)
    _wire(screen, "ViewportUV", centre_stencil, "UVs")

    thickness = _scalar(material, "Thickness", 1.5, -1900, 200)
    texel = _expr(material, unreal.MaterialExpressionMultiply, -1250, 200)
    _wire(centre, "InvSize", texel, "A")
    _wire(thickness, "", texel, "B")

    depth_taps, normal_taps, stencil_taps = {}, {}, {}
    row = 400
    for label, dx, dy in _OFFSETS:
        direction = _expr(material, unreal.MaterialExpressionConstant2Vector, -1250, row)
        direction.set_editor_property("r", dx)
        direction.set_editor_property("g", dy)

        offset = _expr(material, unreal.MaterialExpressionMultiply, -1050, row)
        _wire(texel, "", offset, "A")
        _wire(direction, "", offset, "B")

        uv = _expr(material, unreal.MaterialExpressionAdd, -850, row)
        _wire(screen, "ViewportUV", uv, "A")
        _wire(offset, "", uv, "B")

        depth = _scene_texture(material, scene_depth, -620, row)
        _wire(uv, "", depth, "UVs")
        depth_taps[label] = depth

        normal = _scene_texture(material, world_normal, -620, row + 100)
        _wire(uv, "", normal, "UVs")
        normal_taps[label] = normal

        stencil = _scene_texture(material, custom_stencil, -620, row + 200)
        _wire(uv, "", stencil, "UVs")
        stencil_taps[label] = stencil

        row += 380

    custom = _expr(material, unreal.MaterialExpressionCustom, -250, 200)
    custom.set_editor_property("code", _OUTLINE_HLSL)
    custom.set_editor_property("description", "ValhallaSobelEdge")
    custom.set_editor_property("output_type",
                               _enum(unreal.CustomMaterialOutputType,
                                     "CMOT_FLOAT1", "FLOAT1"))
    inputs = []
    for name in _CUSTOM_INPUTS:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)

    _wire(centre, "Color", custom, "DepthC")
    _wire(centre_stencil, "Color", custom, "StencilC")
    for label in ("L", "R", "U", "D"):
        _wire(depth_taps[label], "Color", custom, "Depth" + label)
        _wire(normal_taps[label], "Color", custom, "Normal" + label)
        _wire(stencil_taps[label], "Color", custom, "Stencil" + label)

    # DepthScale is 1 because the depth term already comes out normalised; it
    # is a parameter so the two edge sources can be rebalanced per level
    # without editing the graph. The threshold is a fraction of a full depth
    # step, so 0.25 means "a quarter of the way to a true silhouette".
    depth_scale = _scalar(material, "DepthScale", 1.0, -600, 2000)
    normal_scale = _scalar(material, "NormalScale", 0.5, -600, 2100)
    threshold = _scalar(material, "Threshold", 0.25, -600, 2200)
    # 0.4% of the centre depth. Measured against the town: below 0.002 the roof
    # banding comes back, and above ~0.01 a distant character's silhouette
    # starts to break up where it crosses a wall at a shallow angle.
    curvature_noise = _scalar(material, "CurvatureNoise", 0.004, -600, 2300)
    _wire(depth_scale, "", custom, "DepthScale")
    _wire(normal_scale, "", custom, "NormalScale")
    _wire(threshold, "", custom, "Threshold")
    _wire(curvature_noise, "", custom, "CurvatureNoise")

    source = _scene_texture(material, scene_color, -600, 2450)
    # SceneTexture hands back float4 and a vector parameter is float3; Lerp does
    # not promote between them, so the alpha is masked off explicitly rather
    # than left to fail at shader-compile time with "Arithmetic between types
    # float4 and float3 are undefined".
    source_rgb = _expr(material, unreal.MaterialExpressionComponentMask, -350, 2450)
    source_rgb.set_editor_property("r", True)
    source_rgb.set_editor_property("g", True)
    source_rgb.set_editor_property("b", True)
    source_rgb.set_editor_property("a", False)
    _wire(source, "Color", source_rgb, "")

    line_color = _vector(material, "LineColor",
                         unreal.LinearColor(0.015, 0.015, 0.025, 1.0), -600, 2600)

    blend = _expr(material, unreal.MaterialExpressionLinearInterpolate, 0, 1000)
    _wire(source_rgb, "", blend, "A")
    _wire(line_color, "", blend, "B")
    _wire(custom, "", blend, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        blend, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(OUTLINE)
    _log("built {}".format(OUTLINE))
    return OUTLINE


# ── Level wiring ─────────────────────────────────────────────────────────────

def place_post_process_volume(level_path="/Game/Valhalla/Maps/L_GreyBox"):
    """An unbound PostProcessVolume in the level, carrying PP_Outline.

    Unbound so it applies everywhere rather than inside a box the player can
    walk out of. The player controller adds the same blendable to the pawn
    camera at BeginPlay, so a level without this volume still gets the outline;
    the volume is what makes the editor viewport match what PIE renders.
    """
    outline = unreal.EditorAssetLibrary.load_asset(OUTLINE)
    if outline is None:
        raise RuntimeError("PP_Outline missing")

    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = [a for a in subsystem.get_all_level_actors()
                if isinstance(a, unreal.PostProcessVolume)
                and a.get_actor_label() == "PPV_ValhallaOutline"]
    volume = existing[0] if existing else subsystem.spawn_actor_from_class(
        unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    volume.set_actor_label("PPV_ValhallaOutline")
    volume.set_editor_property("unbound", True)
    volume.set_editor_property("priority", 1.0)
    volume.set_editor_property("blend_weight", 1.0)

    settings = volume.get_editor_property("settings")

    weighted = unreal.WeightedBlendables()
    entry = unreal.WeightedBlendable()
    entry.set_editor_property("weight", 1.0)
    entry.set_editor_property("object", outline)
    weighted.set_editor_property("array", [entry])
    settings.set_editor_property("weighted_blendables", weighted)

    # Exposure is pinned, not adapted. Flat colour regions only read as flat
    # colour if the same base colour produces the same pixel every frame, and
    # eye adaptation makes a wall's brightness depend on where the player is
    # standing — which is exactly what washed the first pass of these
    # materials out to near-white.
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method",
                                 unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", 9.0)
    settings.set_editor_property("override_auto_exposure_min_brightness", True)
    settings.set_editor_property("auto_exposure_min_brightness", 1.0)
    settings.set_editor_property("override_auto_exposure_max_brightness", True)
    settings.set_editor_property("auto_exposure_max_brightness", 1.0)
    # No bloom either: a bloom halo on a flat colour region is the one thing
    # that makes it stop looking flat.
    settings.set_editor_property("override_bloom_intensity", True)
    settings.set_editor_property("bloom_intensity", 0.0)

    volume.set_editor_property("settings", settings)

    _log("PostProcessVolume PPV_ValhallaOutline configured in the open level")
    return volume.get_path_name()


def run_all():
    build_toon()
    build_outline()
    return {"toon": TOON, "outline": OUTLINE}
