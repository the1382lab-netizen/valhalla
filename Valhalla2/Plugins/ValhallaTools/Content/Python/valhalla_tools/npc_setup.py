"""NPC types and the gameplay sublevels NPC Spawn Points live in.

Run from the editor console, after the C++ that adds `AValhallaNPCSpawner`'s
spawn-point properties has been built:

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/npc_setup.py"

Two steps, each idempotent, so running it twice changes nothing the second
time:

1. **NPC types.** Create a `BP_NPC_*` Blueprint of `AValhallaNPC` per NPC type
   under `/Game/Valhalla/NPCs` and set its Class Defaults: which
   npc-templates.json entry it plays and how it looks. That folder is the
   library of pre-built NPCs a designer picks from on a spawn point.
2. **Gameplay sublevels.** Make sure `L_Grasslands_Gameplay` and
   `L_Desert_Gameplay` exist and are streamed into `L_World` at their zone's
   offset. Hand-placed content lives there, because `build_world.py` empties the
   zone levels themselves on every rebuild and never touches these.

(A third step once migrated the NPC spawns out of the zone overlay JSON into
NPC Spawn Points. That was a one-time move, long done, and the overlay files
are retired; git history has it.)
"""

import json

import unreal

from valhalla_tools import build_world

NPC_FOLDER = "/Game/Valhalla/NPCs"
EQUIPMENT = "/Game/Valhalla/Characters/Equipment/"

#: The starting library. `template` is the npc-templates.json id the type plays.
NPC_TYPES = [
    {
        "asset": "BP_NPC_TestEnemy",
        "template": "npc_1771431708366",
    },
    {
        "asset": "BP_NPC_ToughGuy",
        "template": "npc_1771709765831",
    },
    {
        "asset": "BP_NPC_BjornTheTrader",
        "template": "merchant_bjorn",
        "name_override": "Bjorn the Trader",
        "placeholder_kit": False,
        "look": {
            "chest_mesh_asset": EQUIPMENT + "SK_chest_travelers_jerkin",
            "legs_mesh_asset": EQUIPMENT + "SK_legs_travelers",
            "boots_mesh_asset": EQUIPMENT + "SK_boots_ranger",
        },
    },
]

#: zone id -> (gameplay sublevel, world offset of the zone's origin).
ZONES = {
    "grasslands": build_world.GAMEPLAY_SUBLEVELS[0],
    "desert": build_world.GAMEPLAY_SUBLEVELS[1],
}

#: The NPC's capsule (AValhallaNPC::CapsuleRadius / CapsuleHalfHeight), a little
#: smaller so touching a wall is not "inside" it.
CAPSULE_RADIUS = 28.0
CAPSULE_HALF_HEIGHT = 58.0


def _log(message):
    unreal.log("VALHALLA_NPC {}".format(message))


def _levels():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


# ── 1. NPC types ────────────────────────────────────────────────────────


def create_npc_types():
    """Create or update every `BP_NPC_*` in NPC_TYPES. Returns the asset paths."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    made = []
    for spec in NPC_TYPES:
        path = "{}/{}".format(NPC_FOLDER, spec["asset"])
        blueprint = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if blueprint is None:
            factory = unreal.BlueprintFactory()
            factory.set_editor_property("parent_class", unreal.ValhallaNPC)
            blueprint = tools.create_asset(spec["asset"], NPC_FOLDER, unreal.Blueprint, factory)
            if blueprint is None:
                raise RuntimeError("could not create {}".format(path))
            _log("created {}".format(path))

        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        defaults = unreal.get_default_object(blueprint.generated_class())
        defaults.set_editor_property("default_template_id", spec["template"])
        defaults.set_editor_property("name_override", spec.get("name_override", ""))
        defaults.set_editor_property("wear_placeholder_kit", spec.get("placeholder_kit", True))
        for prop, mesh_path in spec.get("look", {}).items():
            mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
            if mesh is None:
                unreal.log_warning("VALHALLA_NPC {} is missing; {} keeps that slot empty".format(mesh_path, path))
                continue
            defaults.set_editor_property(prop, mesh)

        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
        made.append(path)
    return made


def _type_class(asset_name):
    blueprint = unreal.EditorAssetLibrary.load_asset("{}/{}".format(NPC_FOLDER, asset_name))
    return blueprint.generated_class() if blueprint else None


# ── 2. Gameplay sublevels ───────────────────────────────────────────────


def ensure_world_has_gameplay_levels():
    """Create the gameplay sublevels if missing and stream them into L_World."""
    created = build_world.ensure_gameplay_levels()

    if not _levels().load_level(build_world.WORLD_PATH):
        raise RuntimeError("could not open {}".format(build_world.WORLD_PATH))

    world = unreal.EditorLevelLibrary.get_editor_world()
    present = set()
    for level in unreal.EditorLevelUtils.get_levels(world):
        present.add(level.get_outer().get_path_name().split(".")[0])

    attached = []
    for level_path, offset in build_world.GAMEPLAY_SUBLEVELS:
        if level_path in present:
            continue
        transform = unreal.Transform(location=offset, rotation=unreal.Rotator(), scale=unreal.Vector(1.0, 1.0, 1.0))
        streaming = unreal.EditorLevelUtils.add_level_to_world_with_transform(
            world, level_path, unreal.LevelStreamingAlwaysLoaded, transform)
        if streaming is None:
            raise RuntimeError("could not add {} to L_World".format(level_path))
        streaming.set_editor_property("level_transform", transform)
        attached.append(level_path)
        _log("streamed {} into L_World at {}".format(level_path, offset))

    _levels().save_all_dirty_levels()
    return {"created": created, "attached": attached}


def run_all():
    result = {
        "npcTypes": create_npc_types(),
        "sublevels": ensure_world_has_gameplay_levels(),
    }
    _log("VALHALLA_NPC_DONE " + json.dumps(result, default=str))
    return result


if __name__ == "__main__":
    run_all()
