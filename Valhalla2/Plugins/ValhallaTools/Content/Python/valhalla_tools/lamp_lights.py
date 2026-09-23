"""B-15 Wave 1: real light for every lamp post (Docs/ArtBible.md, section 7).

Each ``SM_LampPost`` actor gets one attached ``PointLight`` at the lantern
(about 2 m up): warm (~2700 K), no shadows (dozens of lamps must stay cheap),
a 5 m radius. The lights live in the lamp's own level and are tagged
``ValhallaLampLight``, so re-running adds nothing and ``remove_all()`` takes
them away again.
"""

import unreal

TAG = "ValhallaLampLight"
LAMP_MESH = "SM_LampPost"
LANTERN_Z = 203.0
LOOK = {
    "intensity_cd": 18.0,
    "color": unreal.Color(r=255, g=176, b=104, a=255),
    "radius": 500.0,
    "source_radius": 6.0,
}

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _lamps():
    for actor in EAS.get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        smc = actor.static_mesh_component
        mesh = smc.get_editor_property("static_mesh") if smc else None
        if mesh is not None and mesh.get_name() == LAMP_MESH:
            yield actor


def _has_light(lamp):
    for child in lamp.get_attached_actors():
        if TAG in [str(t) for t in child.tags]:
            return child
    return None


def configure(light):
    lc = light.get_editor_property("point_light_component")
    lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    lc.set_editor_property("intensity", LOOK["intensity_cd"])
    lc.set_editor_property("light_color", LOOK["color"])
    lc.set_editor_property("attenuation_radius", LOOK["radius"])
    lc.set_editor_property("source_radius", LOOK["source_radius"])
    lc.set_editor_property("cast_shadows", False)
    lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)


def add_all():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    current = les.get_current_level()
    added, updated = 0, 0
    for lamp in list(_lamps()):
        existing = _has_light(lamp)
        if existing is not None:
            configure(existing)
            updated += 1
            continue
        level = lamp.get_level()
        les.set_current_level_by_name(level.get_outer().get_name())
        loc = lamp.get_actor_location() + unreal.Vector(0.0, 0.0, LANTERN_Z)
        light = EAS.spawn_actor_from_class(unreal.PointLight, loc, unreal.Rotator())
        light.set_actor_label("LampLight_" + lamp.get_actor_label())
        light.tags = [TAG]
        configure(light)
        light.attach_to_actor(lamp, "", unreal.AttachmentRule.KEEP_WORLD,
                              unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
        light.set_folder_path(lamp.get_folder_path())
        added += 1
    if current is not None:
        les.set_current_level_by_name(current.get_outer().get_name())
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
    unreal.log("LogValhallaImport: lamp lights: {} added, {} updated".format(added, updated))
    return {"added": added, "updated": updated}


def remove_all():
    removed = 0
    for actor in EAS.get_all_level_actors():
        if TAG in [str(t) for t in actor.tags]:
            EAS.destroy_actor(actor)
            removed += 1
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
    return {"removed": removed}
