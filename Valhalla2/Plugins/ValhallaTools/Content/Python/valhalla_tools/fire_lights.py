"""B-15 Wave 5: a flickering light for every fire prop in the open level,
the way ``lamp_lights`` lights the lamp posts.

Each fire actor (a StaticMeshActor whose mesh is in FIRES) gets one attached
movable ``PointLight`` at its flame, warm, with a flicker light function
(``MI_FireFlicker_A/B/C`` from ``build_fire``, picked per actor so neighbours
do not pulse in step). Tagged ``ValhallaFireLight``: re-running updates rather
than adds, ``remove_all()`` takes them away.

Offsets are in the prop's own frame (Unreal axes; Blender +Y is Unreal -Y).
"""

import zlib

import unreal

TAG = "ValhallaFireLight"
#: mesh -> (local offset cm, candelas, attenuation radius cm, cast shadows)
#: B-27 Phase 4 (decision 6): only the big fires, the campfires, cast shadows.
#: The braziers, fireplaces, forges and torches lit Eldmoor with 17 more
#: shadowed movable lights (1.9 ms of GPU with the flicker, measured 2026-09-25).
FIRES = {
    "SM_Campfire": ((0.0, 0.0, 30.0), 70.0, 650.0, True),
    "SM_CampfireCooking": ((0.0, 0.0, 28.0), 70.0, 650.0, True),
    "SM_Brazier": ((0.0, 0.0, 100.0), 50.0, 550.0, False),
    "SM_Fireplace": ((0.0, 35.0, 35.0), 45.0, 500.0, False),
    "SM_Candle": ((0.0, 0.0, 30.0), 3.0, 160.0, False),
    "SM_TempleAltar": ((0.0, -5.0, 98.0), 6.0, 260.0, False),
    "SM_Forge": ((0.0, 0.0, 65.0), 40.0, 500.0, False),             # B-06 Eldmoor smithy
    "SM_TorchSconce": ((0.0, 15.0, 30.0), 14.0, 420.0, False),      # B-06 undercroft wall torch
}
#: B-27 Phase 4: the flicker (light function) is drawn only within this distance
#: of the camera, cm; further away the light burns steady. The camera sits
#: 1500 cm from the player, so every fire on screen is inside it.
FLICKER_FADE_CM = 3500.0
COLOR = unreal.Color(r=255, g=150, b=70, a=255)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
FLICKER = ("MI_FireFlicker_A", "MI_FireFlicker_B", "MI_FireFlicker_C")


def _fires():
    for actor in EAS.get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        smc = actor.static_mesh_component
        mesh = smc.get_editor_property("static_mesh") if smc else None
        if mesh is not None and mesh.get_name() in FIRES:
            yield actor, mesh.get_name()


def _existing(actor):
    for child in actor.get_attached_actors():
        if TAG in [str(t) for t in child.tags]:
            return child
    return None


def configure(light, actor, mesh_name):
    offset, cd, radius, shadows = FIRES[mesh_name]
    lc = light.get_editor_property("point_light_component")
    lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    lc.set_editor_property("intensity", cd)
    lc.set_editor_property("light_color", COLOR)
    lc.set_editor_property("attenuation_radius", radius)
    lc.set_editor_property("source_radius", 8.0)
    lc.set_editor_property("cast_shadows", shadows)
    pick = FLICKER[zlib.crc32(actor.get_path_name().encode()) % len(FLICKER)]
    mi = unreal.EditorAssetLibrary.load_asset("/Game/Valhalla/Materials/Sets/" + pick)
    lc.set_editor_property("light_function_material", mi)
    lc.set_editor_property("light_function_fade_distance", FLICKER_FADE_CM)
    loc = actor.get_actor_transform().transform_location(unreal.Vector(*offset))
    light.set_actor_location(loc, False, False)


def add_all():
    added, updated = 0, 0
    for actor, name in list(_fires()):
        light = _existing(actor)
        if light is None:
            light = EAS.spawn_actor_from_class(unreal.PointLight, actor.get_actor_location(), unreal.Rotator(0, 0, 0))
            light.tags = [TAG]
            light.set_actor_label("{}_Light".format(actor.get_actor_label()))
            configure(light, actor, name)
            light.attach_to_actor(actor, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                                  unreal.AttachmentRule.KEEP_WORLD, False)
            added += 1
        else:
            configure(light, actor, name)
            updated += 1
    return {"added": added, "updated": updated}


def remove_all():
    n = 0
    for actor in list(EAS.get_all_level_actors()):
        if TAG in [str(t) for t in actor.tags]:
            EAS.destroy_actor(actor)
            n += 1
    return n
