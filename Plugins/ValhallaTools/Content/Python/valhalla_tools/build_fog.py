"""Phase 5's fog of war post-process material, ``/Game/Valhalla/Materials/PP_Fog``.

Run from the editor console::

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/build_fog.py"

or, to rebuild just this one asset from an already-imported module::

    py "import valhalla_tools.build_fog as f; f.build_fog()"

What the material does
----------------------

``AValhallaFogRenderer`` keeps two 1024x1024 masks in **world** space, stretched
over the level's ``AValhallaFogBounds``: ``VisibleMask`` is this frame's
visibility polygon, ``ExploredMask`` is the union of every frame so far. This
material is the only thing that reads them.

Per pixel:

1. Recover the world position from the scene depth. The ``WorldPosition`` node
   does this itself in a post-process material — it is not the vertex's
   position, because there are no vertices here, it is the depth buffer
   reprojected — which is what makes the fog stick to the ground as the camera
   moves instead of sliding across the screen.
2. Turn its XY into a fog UV with two vector parameters the renderer writes,
   ``FogOrigin`` (the bounds' min corner) and ``FogInvSize``.
3. Sample both masks nine times in a 3x3 cross at 8 texels' spacing and average.
   That is the soft edge: a single tap gives a hard, visibly pixelated polygon
   boundary — at a 2560 cm map over 1024 texels, one texel is 2.5 cm, and the
   polygon's own straight chords make the seams line up into something that
   reads as a jagged rip. Sixteen texels of blur is 40 cm of world, which is
   under a tile and is the smallest amount that stops the eye finding the edge.
4. Pick the brightness: hidden 0.05, explored 0.5, visible 1.0, interpolated by
   the (now smooth) masks rather than switched, so the transitions are ramps.
   These are ``FogOfWar.ts``'s three states with its alphas written from the
   other end — 1.0 drew black at alpha 0.95 / 0.5 / 0 over the scene, which is
   the same picture as multiplying the scene by 0.05 / 0.5 / 1.

Ordering against ``PP_Outline`` is ``blendable_priority``, not the order the two
are added to the camera. Both sit at ``BL_SCENE_COLOR_BEFORE_DOF`` (they both
read the GBuffer, which does not survive the tonemapper); blendables at one
location run in priority order, so the outline at 0 draws its silhouettes and
the fog at 1 then dims them along with everything else. The other way round
would leave crisp black outlines floating in the dark over unexplored ground,
which is exactly the information the fog is there to withhold.
"""

import unreal

from valhalla_tools.build_toon import (
    MAT_DIR,
    _asset,
    _enum,
    _expr,
    _log,
    _scalar,
    _vector,
    _wire,
)

FOG = MAT_DIR + "/PP_Fog"

#: Edge of the masks, texels. Must match AValhallaFogRenderer::MaskResolution.
MASK_RESOLUTION = 1024

#: Blur tap spacing, texels. Nine taps at +/- this is a ~16 texel kernel.
BLUR_TEXELS = 8.0

#: FogOfWar.ts:6-7, written as multipliers rather than as fog alphas.
HIDDEN_BRIGHTNESS = 0.05
EXPLORED_BRIGHTNESS = 0.5
VISIBLE_BRIGHTNESS = 1.0

#: Candidates for the parameters' default texture. The renderer overrides both
#: with its render targets at runtime; this only has to exist and compile, and
#: black is the right default because "no masks bound" should mean "all hidden"
#: rather than a bright screen with no fog at all.
_DEFAULT_TEXTURES = (
    "/Engine/EngineResources/Black",
    "/Engine/EngineResources/DefaultTexture",
    "/Engine/EngineResources/WhiteSquareTexture",
    "/Engine/EditorResources/S_Actor",
)

#: The 3x3 tap grid, in units of BLUR_TEXELS.
_TAPS = [(dx, dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1)]


def _default_texture():
    for path in _DEFAULT_TEXTURES:
        texture = unreal.EditorAssetLibrary.load_asset(path)
        if texture is not None:
            return texture, path
    raise RuntimeError("no usable default texture found among {}".format(_DEFAULT_TEXTURES))


def _const2(material, x, y, rx, ry):
    node = _expr(material, unreal.MaterialExpressionConstant2Vector, x, y)
    node.set_editor_property("r", rx)
    node.set_editor_property("g", ry)
    return node


def _const(material, x, y, value):
    node = _expr(material, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", value)
    return node


def _mask(material, x, y, node, r=True, g=True, b=False, a=False):
    """Component mask. Used to pull XY out of a world position and RG out of a
    vector parameter — a VectorParameter is float4 and the maths below is
    float2, and Unreal does not promote between them silently."""
    component_mask = _expr(material, unreal.MaterialExpressionComponentMask, x, y)
    component_mask.set_editor_property("r", r)
    component_mask.set_editor_property("g", g)
    component_mask.set_editor_property("b", b)
    component_mask.set_editor_property("a", a)
    _wire(node, "", component_mask, "")
    return component_mask


def _sampler(material, x, y, parameter_name, texture, uvs):
    node = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    node.set_editor_property("parameter_name", parameter_name)
    node.set_editor_property("texture", texture)
    _wire(uvs, "", node, "UVs")
    return node


def _average(material, x, y, nodes, output="R"):
    """Sum a list of expression outputs and divide by their count.

    Written as a left-fold of two-input Adds rather than anything cleverer
    because that is all the material graph has; the shader compiler flattens it
    back into a single dot product anyway.
    """
    total, total_output = None, ""
    row = y

    for node in nodes:
        if total is None:
            total, total_output = node, output
            continue
        add = _expr(material, unreal.MaterialExpressionAdd, x, row)
        _wire(total, total_output, add, "A")
        _wire(node, output, add, "B")
        total, total_output = add, ""
        row += 60

    scale = _expr(material, unreal.MaterialExpressionMultiply, x + 220, y)
    _wire(total, total_output, scale, "A")
    scale.set_editor_property("const_b", 1.0 / float(len(nodes)))
    return scale


def build_fog(debug=""):
    """Author PP_Fog.

    Args:
        debug: ``""`` for the real material; ``"uv"`` to output the fog UV as
            red/green, which shows at a glance whether the world position is
            being reconstructed from scene depth at all; ``"masks"`` to output
            the two mask values as red (visible) and green (explored).
    """
    texture, texture_path = _default_texture()
    _log("PP_Fog default mask texture: {}".format(texture_path))

    scene_color = _enum(unreal.SceneTextureId, "PPI_POST_PROCESS_INPUT0",
                        "POST_PROCESS_INPUT0")

    material = _asset(FOG, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property(
        "blendable_location",
        _enum(unreal.BlendableLocation, "BL_SCENE_COLOR_BEFORE_DOF",
              "BL_BEFORE_TONEMAPPING"))
    # After PP_Outline, which leaves this at its default of 0. See the module
    # docstring for why the order matters.
    material.set_editor_property("blendable_priority", 1)

    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    # ── World XY -> fog UV ──────────────────────────────────────────────
    world = _expr(material, unreal.MaterialExpressionWorldPosition, -2400, 0)
    world_xy = _mask(material, -2150, 0, world)

    origin = _vector(material, "FogOrigin", unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -2400, 200)
    origin_xy = _mask(material, -2150, 200, origin)

    local = _expr(material, unreal.MaterialExpressionSubtract, -1900, 50)
    _wire(world_xy, "", local, "A")
    _wire(origin_xy, "", local, "B")

    inv_size = _vector(material, "FogInvSize",
                       unreal.LinearColor(1.0 / 2560.0, 1.0 / 2560.0, 0.0, 0.0), -2400, 400)
    inv_size_xy = _mask(material, -2150, 400, inv_size)

    base_uv = _expr(material, unreal.MaterialExpressionMultiply, -1700, 50)
    _wire(local, "", base_uv, "A")
    _wire(inv_size_xy, "", base_uv, "B")

    # ── Nine taps of each mask ──────────────────────────────────────────
    step = BLUR_TEXELS / float(MASK_RESOLUTION)
    visible_taps, explored_taps = [], []
    row = -600

    for dx, dy in _TAPS:
        if dx == 0 and dy == 0:
            uv = base_uv
        else:
            offset = _const2(material, -1500, row, dx * step, dy * step)
            uv = _expr(material, unreal.MaterialExpressionAdd, -1300, row)
            _wire(base_uv, "", uv, "A")
            _wire(offset, "", uv, "B")

        visible_taps.append(_sampler(material, -1050, row, "VisibleMask", texture, uv))
        explored_taps.append(_sampler(material, -1050, row + 160, "ExploredMask", texture, uv))
        row += 340

    visible = _average(material, -700, -600, visible_taps)
    explored = _average(material, -700, 400, explored_taps)

    # ── Hidden -> explored -> visible ───────────────────────────────────
    hidden_level = _scalar(material, "HiddenBrightness", HIDDEN_BRIGHTNESS, -400, 1400)
    explored_level = _scalar(material, "ExploredBrightness", EXPLORED_BRIGHTNESS, -400, 1500)
    visible_level = _scalar(material, "VisibleBrightness", VISIBLE_BRIGHTNESS, -400, 1600)

    explored_mix = _expr(material, unreal.MaterialExpressionLinearInterpolate, -150, 1450)
    _wire(hidden_level, "", explored_mix, "A")
    _wire(explored_level, "", explored_mix, "B")
    _wire(explored, "", explored_mix, "Alpha")

    brightness = _expr(material, unreal.MaterialExpressionLinearInterpolate, 100, 1500)
    _wire(explored_mix, "", brightness, "A")
    _wire(visible_level, "", brightness, "B")
    _wire(visible, "", brightness, "Alpha")

    # ── Dim the scene ───────────────────────────────────────────────────
    source = _expr(material, unreal.MaterialExpressionSceneTexture, -400, 1800)
    source.set_editor_property("scene_texture_id", scene_color)

    # SceneTexture is float4 and the multiply below is float3; the alpha is
    # masked off rather than left to fail at shader-compile time. Same reason as
    # PP_Outline's source mask — see build_toon.build_outline.
    source_rgb = _expr(material, unreal.MaterialExpressionComponentMask, -150, 1800)
    source_rgb.set_editor_property("r", True)
    source_rgb.set_editor_property("g", True)
    source_rgb.set_editor_property("b", True)
    source_rgb.set_editor_property("a", False)
    _wire(source, "Color", source_rgb, "")

    dimmed = _expr(material, unreal.MaterialExpressionMultiply, 350, 1650)
    _wire(source_rgb, "", dimmed, "A")
    _wire(brightness, "", dimmed, "B")

    output = dimmed
    if debug == "uv":
        # Red/green = the fog UV. A smooth gradient across the ground means the
        # world position really is being rebuilt from scene depth; a flat
        # colour means it is not, and the whole material is reading one texel.
        output = _expr(material, unreal.MaterialExpressionAppendVector, 350, 2400)
        _wire(base_uv, "", output, "A")
        zero = _const(material, 100, 2500, 0.0)
        _wire(zero, "", output, "B")
    elif debug == "tap":
        # Red   = VisibleMask at the pixel's own fog UV, single tap, no blur.
        # Green = VisibleMask at a fixed UV in the map's corner, which the
        #         player has never stood anywhere near. Green anywhere on
        #         screen means the render target itself is white, not that the
        #         UV or the blur is wrong.
        corner = _const2(material, -1500, 2600, 0.02, 0.02)
        corner_tap = _sampler(material, -1200, 2600, "VisibleMask", texture, corner)
        output = _expr(material, unreal.MaterialExpressionAppendVector, 350, 2400)
        pair = _expr(material, unreal.MaterialExpressionAppendVector, 150, 2400)
        _wire(visible_taps[4], "R", pair, "A")
        _wire(corner_tap, "R", pair, "B")
        _wire(pair, "", output, "A")
        zero = _const(material, 100, 2500, 0.0)
        _wire(zero, "", output, "B")
    elif debug == "masks":
        output = _expr(material, unreal.MaterialExpressionAppendVector, 350, 2400)
        pair = _expr(material, unreal.MaterialExpressionAppendVector, 150, 2400)
        _wire(visible, "", pair, "A")
        _wire(explored, "", pair, "B")
        _wire(pair, "", output, "A")
        zero = _const(material, 100, 2500, 0.0)
        _wire(zero, "", output, "B")

    unreal.MaterialEditingLibrary.connect_material_property(
        output, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(FOG)
    _log("built {}".format(FOG))
    return FOG


def place_fog_bounds(centre=(0.0, 0.0, 0.0), extent=(1280.0, 1280.0, 400.0),
                     label="FogBounds"):
    """Put a single ``AValhallaFogBounds`` in the level that is currently open.

    Idempotent: an existing actor with the same label is moved and resized
    rather than duplicated, so re-running a level builder does not leave three
    of them fighting over which one ``AValhallaFogBounds::Find`` returns.

    The extent is a *half* size, and it should hug the playable area. The whole
    box is stretched over 1024 texels, so a box twice as big as it needs to be
    halves the fog's resolution for nothing.
    """
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    existing = [a for a in subsystem.get_all_level_actors()
                if isinstance(a, unreal.ValhallaFogBounds)
                and a.get_actor_label() == label]

    actor = existing[0] if existing else subsystem.spawn_actor_from_class(
        unreal.ValhallaFogBounds, unreal.Vector(*centre))

    actor.set_actor_label(label)
    actor.set_actor_location(unreal.Vector(*centre), False, False)
    actor.set_editor_property("extent", unreal.Vector(*extent))
    actor.set_folder_path("Fog")

    _log("fog bounds {} at {} extent {}".format(label, centre, extent))
    return actor.get_path_name()


if __name__ == "__main__":
    build_fog()
