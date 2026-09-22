"""The two materials every Phase 8a Niagara system renders with.

``M_ValhallaVfxSprite``
    Unlit, translucent, two-sided. A soft-edged disc generated from the
    sprite's own UVs — no texture at all, which is both cheaper than sampling
    the engine default particle texture and the only way to be sure a
    flat-shaded game never picks up a photographic-looking smoke puff.
    Emissive is ``ParticleColor.rgb * glow`` and Opacity ``mask * a``; see
    ``_build`` for why this one is alpha-blended and the ring is not.

``M_ValhallaVfxRing``
    Unlit, *additive*, two-sided, with an annulus mask: Emissive is
    ``rgb * glow * mask * a``, the alpha folded in because additive blending ignores
    Opacity. Used by every flat
    ground ring — the AoE telegraph, the buff aura, the impact shockwave — all
    of which are one camera-locked quad lying on the ground rather than a
    circle of particles, because one quad is one particle and a ring of forty
    sprites is forty.

Both are marked *Used with Niagara Sprites* and *Used with Niagara Ribbons*.
A material without those flags renders as the default checker and the failure
message is buried in the shader-compile log, so the flags are set explicitly
and verified rather than assumed.

The mask arithmetic is a single Custom node for the same reason
``build_toon.py``'s outline is: the alternative is a dozen unlabelled two-input
maths nodes that nobody can read six months later.
"""

import unreal

MAT_DIR = "/Game/Valhalla/Materials"
SPRITE = MAT_DIR + "/M_ValhallaVfxSprite"
RING = MAT_DIR + "/M_ValhallaVfxRing"

TAG = "LogValhallaImport:"


def _log(msg):
    unreal.log("{} {}".format(TAG, msg))


def _enum(enum_type, *candidates):
    for name in candidates:
        if hasattr(enum_type, name):
            return getattr(enum_type, name)
    raise RuntimeError("none of {} on {}".format(candidates, enum_type))


def _refuse_during_pie():
    """Refuse to run while a Play-In-Editor session is up.

    Every ``EditorAssetLibrary`` call answers "The Editor is currently in a
    play mode" and returns None during PIE. ``_asset`` then took the None for
    "no such material" and asked AssetTools to *create* one over the existing
    asset, which raises a modal "Overwrite Existing Object" dialog on the game
    thread — freezing the editor and the MCP until someone clicks it — followed
    by "is in use" and "wasn't created" boxes and an AttributeError on the None.
    That is the failure the Phase 8a hand-off recorded; it was never the graph.
    """
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor is not None and editor.get_game_world() is not None:
        raise RuntimeError("build_vfx_materials: stop Play-In-Editor first")


def _asset(package_path):
    name = package_path.rsplit("/", 1)[1]
    folder = package_path.rsplit("/", 1)[0]
    existing = unreal.EditorAssetLibrary.load_asset(package_path)
    if existing is not None:
        return existing
    if unreal.EditorAssetLibrary.does_asset_exist(package_path):
        # Exists but would not load: never fall through to create_asset, which
        # would prompt to overwrite it.
        raise RuntimeError("{} exists but did not load".format(package_path))
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.Material, unreal.MaterialFactoryNew())


def _expr(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def _wire(src, src_out, dst, dst_in):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            src, src_out, dst, dst_in):
        raise RuntimeError("could not wire {}.{} -> {}.{}".format(
            src.get_class().get_name(), src_out or "<default>",
            dst.get_class().get_name(), dst_in or "<default>"))


#: A soft-edged disc: full strength out to half the radius, then a smoothstep
#: down to 0 at the inscribed circle, so the sprite has no visible square
#: boundary and still fills the size it was given.
#:
#: THIS MASK WAS THE REASON EVERY SPRITE EFFECT WAS INVISIBLE.
#:
#: The first version was ``(1 - d)^2`` — a cone squared. It is 1 only at the
#: exact centre, 0.25 at half the radius, and its average over the disc is
#: about a sixth, so an additive sprite drawn with it is a pinprick of light
#: roughly a fifth of its nominal size with a halo too dim to read over sunlit
#: grass. With 11-34 cm sprites that is a 2-7 cm dot seen from 1100+ cm: nothing.
#:
#: Bisected in PIE with three copies of one NS_Bolt core particle, identical
#: except for the renderer material: the engine DefaultSpriteMaterial drew a
#: bright blob, M_ValhallaVfxRing drew a crisp ring the full size of the quad,
#: and this material drew a faint dot. Same emitter, same size, same bindings
#: — so the system was fine and the mask was not. (The ribbon trail looked fine
#: with the old mask only because a ribbon is a long strip whose V runs across
#: it, so the cone's bright centre line is the whole length of the trail.)
_BLOB_HLSL = """
float d = saturate(length(UV - 0.5) * 2.0);
float m = saturate((1.0 - d) * 2.0);
return m * m * (3.0 - 2.0 * m);
"""

#: An annulus at 82% of the radius. Squaring sharpens it into a line rather
#: than a doughnut; the outer clamp stops the quad's corners lighting up.
_RING_HLSL = """
float d = length(UV - 0.5) * 2.0;
float m = saturate(1.0 - abs(d - 0.82) / 0.18);
return m * m * step(d, 1.0);
"""


def _set_usage(material):
    """Niagara sprite + ribbon usage, however this engine version spells it."""
    for flag in ("MATUSAGE_NIAGARA_SPRITES", "MATUSAGE_NIAGARA_RIBBONS"):
        usage = getattr(unreal.MaterialUsage, flag, None)
        if usage is not None:
            unreal.MaterialEditingLibrary.set_material_usage(material, usage)
    # The library call is the supported route, but it is a no-op on some
    # versions when the material has never been compiled; the properties are
    # the belt to its braces.
    for prop in ("used_with_niagara_sprites", "used_with_niagara_ribbons"):
        try:
            material.set_editor_property(prop, True)
        except Exception:
            pass


def _hlsl_mask(hlsl, description):
    """A mask built as one Custom node, as the ring's annulus is."""
    def build(material):
        uv = _expr(material, unreal.MaterialExpressionTextureCoordinate, -900, 0)
        mask = _expr(material, unreal.MaterialExpressionCustom, -600, 0)
        mask.set_editor_property("code", hlsl)
        mask.set_editor_property("description", description)
        mask.set_editor_property(
            "output_type", _enum(unreal.CustomMaterialOutputType, "CMOT_FLOAT1", "FLOAT1"))
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", "UV")
        mask.set_editor_property("inputs", [entry])
        _wire(uv, "", mask, "UV")
        return mask
    return build


def _build(package_path, make_mask, translucent=False, glow=1.0):
    """One unlit, two-sided particle material around a mask.

    ``translucent`` picks the blend, and with it where the mask goes:

    * **Additive** (the ring). Emissive is ``rgb * mask * a`` and Opacity is
      ignored by the blend, so alpha has to be folded into the emissive or a
      fade-out does nothing. Right for a thin bright line, which stays legible
      because it is a *line*.
    * **Translucent** (the sprite). Emissive is ``rgb`` and Opacity is
      ``mask * a``, so the particle's own colour is what reaches the screen.
      Additive can only ever *brighten*: a gold or orange disc added to this
      game's sunlit, saturated grass comes out as a pale yellow-green smudge,
      and the heal, impact and debuff motes were close to invisible even at the
      right size. Alpha-blended, they keep their colour against any ground.

    ``glow`` scales the emissive. An unlit emissive of 1.0 is *dark* in this
    level: the sun is bright enough that auto-exposure maps sunlit grass well
    above 1.0, so a particle tinted with a 0-1 colour reads as a muddy brown
    disc (translucent) or a barely-there wash (additive). See SPRITE_GLOW.
    """
    _refuse_during_pie()
    material = _asset(package_path)

    # Rebuild from scratch: a half-edited graph is worse than no graph.
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property(
        "blend_mode", _enum(unreal.BlendMode,
                            "BLEND_TRANSLUCENT" if translucent else "BLEND_ADDITIVE"))
    material.set_editor_property(
        "shading_model", _enum(unreal.MaterialShadingModel, "MSM_UNLIT"))
    material.set_editor_property("two_sided", True)
    _set_usage(material)

    mask = make_mask(material)

    # ParticleColor's *named* outputs, not a ComponentMask.
    #
    # Its default output is `RGB`, a float3, not the float4 the node's name
    # suggests — masking `.a` off it is an invalid mask and the whole material
    # fails to translate. The failure arrives as "Failed to compile Material
    # for platform PCD3D_SM6, Default Material will be used in game" with no
    # node named and no error listed, which is an afternoon's bisection if you
    # do not already know. The node publishes RGB / R / G / B / A / RGBA; ask
    # for the two that are wanted.
    particle = _expr(material, unreal.MaterialExpressionParticleColor, -900, 300)

    # mask * alpha, then tinted. Additive blending never reads Opacity, so the
    # particle's own alpha has to reach the image through Emissive or a
    # fade-out does nothing at all.
    shape = _expr(material, unreal.MaterialExpressionMultiply, -350, 200)
    _wire(mask, "", shape, "A")
    _wire(particle, "A", shape, "B")

    gain = _expr(material, unreal.MaterialExpressionConstant, -350, 420)
    gain.set_editor_property("r", float(glow))
    tint = _expr(material, unreal.MaterialExpressionMultiply, -350, 330)
    _wire(particle, "RGB", tint, "A")
    _wire(gain, "", tint, "B")

    if translucent:
        # Opacity carries the shape and the fade; the colour is neither masked
        # nor faded, or the disc's edge and its fade-out would both be applied
        # twice.
        emissive = tint
    else:
        emissive = _expr(material, unreal.MaterialExpressionMultiply, -120, 250)
        _wire(tint, "", emissive, "A")
        _wire(shape, "", emissive, "B")

    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(
        shape, "", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(package_path)
    _log("built {}".format(package_path))
    return package_path


#: Emissive gain for the sprite. Tuned in PIE against sunlit grass at the
#: shipping camera: enough that a gold heal mote reads gold rather than brown,
#: not so much that a white-hot colour clips everything to white.
SPRITE_GLOW = 2.5
#: The ring is additive and already reads as a line; it only needs a lift.
RING_GLOW = 2.0


def build_sprite():
    return _build(SPRITE, _hlsl_mask(_BLOB_HLSL, "ValhallaVfxBlob"),
                  translucent=True, glow=SPRITE_GLOW)


def build_ring():
    return _build(RING, _hlsl_mask(_RING_HLSL, "ValhallaVfxRing"), glow=RING_GLOW)


def run_all():
    return {"sprite": build_sprite(), "ring": build_ring()}
