"""B-15 Wave 0: the "modern Neverwinter Nights remaster" lighting and post.

The Phase 4c look was flat colour, a black Sobel outline and lighting tuned so
flat regions stayed flat. The remaster retires the outline and lights the world
physically: a warm late-afternoon sun, a sky that fills the shadows through
Lumen, a light atmospheric haze, and a gentle filmic grade.

Everything is expressed as ``configure_*`` functions over existing actors, so
the same numbers are used by:

- ``apply_to_open_world()``: edit L_World in place (hand-placed content in the
  zone sublevels is untouched), which is how Wave 0 was applied;
- ``build_world.build_lighting()`` / ``build_post_process()``: a from-scratch
  rebuild produces the same look.

The outline is also switched off at runtime: AValhallaPlayerController only adds
PP_Outline to the player camera when ``valhalla.Visual.Outline 1`` (default 0).
"""

import unreal

WORLD = "/Game/Valhalla/Maps/L_World"
OUTLINE_MATERIAL = "/Game/Valhalla/Materials/PP_Outline"

#: The look, in one place. Tuned from in-game captures of Bjorn's market square.
LOOK = {
    # Late afternoon from the south-west: long shadows that read at the
    # isometric camera, warm light, cool shadow.
    "sun_rotation": unreal.Rotator(0.0, -48.0, -35.0),   # roll, pitch, yaw
    "sun_intensity": 10.0,                               # lux
    "sun_color": unreal.Color(r=255, g=232, b=198, a=255),
    "sun_source_angle": 1.2,                             # degrees: softer shadow edges
    "sky_intensity": 1.0,
    # The Phase 4 fill existed so shadowed ground was not pure black in the
    # flat-shaded look. Lumen GI and the sky now do that job; a little of it is
    # kept, cool and shadowless, so the top-down editor captures stay readable.
    "fill_intensity": 1.5,
    "fill_color": unreal.Color(r=170, g=195, b=255, a=255),
    # Haze: starts past the gameplay camera so the play area stays crisp.
    "fog_density": 0.012,
    "fog_falloff": 0.2,
    "fog_start": 1800.0,
    "fog_inscatter": unreal.LinearColor(0.30, 0.36, 0.46, 1.0),
    # Grade.
    "exposure_bias": 0.6,
    "saturation": 0.95,
    "contrast": 1.06,
    "gain": unreal.Vector4(1.02, 1.0, 0.97, 1.0),        # a touch warm
    "bloom": 0.35,
    "vignette": 0.25,
}

TAG = "LogValhallaImport:"


def _log(msg):
    unreal.log("{} lighting: {}".format(TAG, msg))


def _actors_in_world():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def _level_name(actor):
    level = actor.get_level()
    return level.get_outer().get_name() if level else ""


def configure_sun(sun):
    sun.set_actor_rotation(LOOK["sun_rotation"], False)
    lc = sun.get_editor_property("light_component")
    lc.set_editor_property("intensity", LOOK["sun_intensity"])
    lc.set_editor_property("light_color", LOOK["sun_color"])
    lc.set_editor_property("light_source_angle", LOOK["sun_source_angle"])
    lc.set_editor_property("cast_shadows", True)
    lc.set_editor_property("atmosphere_sun_light", True)
    lc.set_editor_property("forward_shading_priority", 1)


def configure_sky_light(sky):
    lc = sky.get_editor_property("light_component")
    lc.set_editor_property("real_time_capture", True)
    lc.set_editor_property("intensity", LOOK["sky_intensity"])


def configure_fill(fill):
    lc = fill.get_editor_property("light_component")
    lc.set_editor_property("intensity", LOOK["fill_intensity"])
    lc.set_editor_property("light_color", LOOK["fill_color"])
    lc.set_editor_property("cast_shadows", False)
    # Only the sun drives the sky colour; a second "atmosphere sun" would add
    # a second sun disc and brighten the sky from the wrong side.
    lc.set_editor_property("atmosphere_sun_light", False)
    lc.set_editor_property("forward_shading_priority", 0)


def configure_fog(fog):
    fc = fog.get_editor_property("component")
    fc.set_editor_property("fog_density", LOOK["fog_density"])
    fc.set_editor_property("fog_height_falloff", LOOK["fog_falloff"])
    fc.set_editor_property("start_distance", LOOK["fog_start"])
    fc.set_editor_property("fog_inscattering_luminance", LOOK["fog_inscatter"])


def configure_post(volume):
    volume.set_editor_property("unbound", True)
    s = volume.get_editor_property("settings")

    # The outline is retired: no blendables in the global volume.
    blendables = unreal.WeightedBlendables()
    blendables.set_editor_property("array", [])
    s.set_editor_property("weighted_blendables", blendables)

    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", LOOK["exposure_bias"])

    s.set_editor_property("override_color_saturation", True)
    s.set_editor_property("color_saturation", unreal.Vector4(1.0, 1.0, 1.0, LOOK["saturation"]))
    s.set_editor_property("override_color_contrast", True)
    s.set_editor_property("color_contrast", unreal.Vector4(1.0, 1.0, 1.0, LOOK["contrast"]))
    s.set_editor_property("override_color_gain", True)
    s.set_editor_property("color_gain", LOOK["gain"])

    s.set_editor_property("override_bloom_intensity", True)
    s.set_editor_property("bloom_intensity", LOOK["bloom"])
    s.set_editor_property("override_vignette_intensity", True)
    s.set_editor_property("vignette_intensity", LOOK["vignette"])

    volume.set_editor_property("settings", s)


def apply_to_open_world(remove_zone_suns=True):
    """Apply LOOK to the lighting actors of the open L_World.

    ``remove_zone_suns`` deletes stray DirectionalLights in the zone sublevels
    (they stack with the Sun in L_World and double every shadow).
    """
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world.get_path_name().startswith(WORLD):
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(WORLD)

    done = []
    removed = []
    for actor in _actors_in_world():
        cls = actor.get_class().get_name()
        label = actor.get_actor_label()
        level = _level_name(actor)
        if cls == "DirectionalLight" and level != "L_World":
            if remove_zone_suns:
                removed.append("{} in {}".format(label, level))
                unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(actor)
            continue
        if level != "L_World":
            continue
        if cls == "DirectionalLight" and label == "Sun":
            configure_sun(actor); done.append(label)
        elif cls == "DirectionalLight" and label == "Fill":
            configure_fill(actor); done.append(label)
        elif cls == "SkyLight":
            configure_sky_light(actor); done.append(label)
        elif cls == "ExponentialHeightFog":
            configure_fog(actor); done.append(label)
        elif cls == "PostProcessVolume" and label == "GlobalPostProcess":
            configure_post(actor); done.append(label)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
    _log("configured {}; removed {}".format(done, removed))
    return done, removed
