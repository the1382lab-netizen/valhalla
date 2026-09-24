"""NPC types and NPC Spawn Points: the one-time move from overlay spawns to Unreal.

Run from the editor console, after the C++ that adds `AValhallaNPCSpawner`'s
spawn-point properties has been built:

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/npc_setup.py"

Three steps, each idempotent, so running it twice changes nothing the second
time:

1. **NPC types.** Create a `BP_NPC_*` Blueprint of `AValhallaNPC` per NPC type
   under `/Game/Valhalla/NPCs` and set its Class Defaults: which
   npc-templates.json entry it plays and how it looks. That folder is the
   library of pre-built NPCs a designer picks from on a spawn point.
2. **Gameplay sublevels.** Make sure `L_Grasslands_Gameplay` and
   `L_Desert_Gameplay` exist and are streamed into `L_World` at their zone's
   offset. Hand-placed content lives there, because `build_world.py` empties the
   zone levels themselves on every rebuild and never touches these.
3. **Migrate.** Turn every `enemy_spawn` / `npc_spawn` left in
   `maps/overlays-2.0/<zone>.json` into individual NPC Spawn Points — a group of
   N becomes N spawn points on the ring the old spawner used — each moved to the
   nearest spot where the NPC's capsule is clear of level geometry, then strip
   those entries from the overlay (the original is kept beside it as
   `<zone>.pre-spawnpoints.json`).
"""

import json
import math
import os
import shutil

import unreal

from valhalla_tools import build_world
from valhalla_tools.build_zone import FLOOR_TOP, overlay_dir

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

#: Which NPC type an old overlay point becomes: by point id first, then by template.
POINT_TO_TYPE = {"npc_merchant": "BP_NPC_BjornTheTrader"}
TEMPLATE_TO_TYPE = {
    "npc_1771431708366": "BP_NPC_TestEnemy",
    "npc_1771709765831": "BP_NPC_ToughGuy",
}

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


# ── 3. Migration ────────────────────────────────────────────────────────


def _capsule_blocked(world, centre):
    object_types = [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2]
    result = unreal.SystemLibrary.capsule_overlap_actors(
        world, centre, CAPSULE_RADIUS, CAPSULE_HALF_HEIGHT, object_types, None, [])
    # (bool, [actors]) in most engine versions, a bare list in some.
    actors = result[1] if isinstance(result, tuple) else result
    blockers = [a for a in (actors or []) if not isinstance(a, (unreal.ValhallaZoneVolume, unreal.ValhallaNPCSpawner))]
    return blockers


def _clear_spot(world, x, y, z_feet):
    """The nearest (x, y) within 4 m whose NPC capsule touches nothing."""
    centre_z = z_feet + CAPSULE_HALF_HEIGHT + 6.0
    rings = [(0.0, 1)] + [(r, 12) for r in (48.0, 96.0, 144.0, 192.0, 256.0, 320.0, 400.0)]
    first_blockers = None
    for radius, steps in rings:
        for step in range(steps):
            angle = 2.0 * math.pi * step / steps
            cx, cy = x + math.cos(angle) * radius, y + math.sin(angle) * radius
            blockers = _capsule_blocked(world, unreal.Vector(cx, cy, centre_z))
            if first_blockers is None:
                first_blockers = blockers
            if not blockers:
                return cx, cy, radius, first_blockers
    return x, y, -1.0, first_blockers


def _ring(point):
    """The home points the old AValhallaNPCSpawner put a group on (ComputeHomeLocation)."""
    count = max(1, int(point.get("count", 1)))
    radius = float(point.get("radius", 0))
    if count <= 1 or radius <= 0.0:
        return [(point["x"], point["y"])]
    return [(point["x"] + math.cos(2.0 * math.pi * i / count) * radius,
             point["y"] + math.sin(2.0 * math.pi * i / count) * radius) for i in range(count)]


def migrate_overlay_spawns(force=False):
    """Replace overlay enemy/NPC spawns with NPC Spawn Points. Needs L_World open.

    B-05: an overlay listed in `maps/handedited.json` is hand-edited, so its
    zone is skipped (reported as such, nothing placed or rewritten) unless
    `force=True`.
    """
    from valhalla_tools import level_protection

    world = unreal.EditorLevelLibrary.get_editor_world()
    report = {}
    marker = level_protection.load_marker()

    for zone_id, (level_path, offset) in ZONES.items():
        if not force and level_protection.is_overlay_protected(zone_id, marker):
            report[zone_id] = level_protection.refusal_message(overlays=[zone_id], marker=marker)
            continue
        overlay_path = os.path.join(overlay_dir(), "{}.json".format(zone_id))
        if not os.path.isfile(overlay_path):
            report[zone_id] = "no overlay"
            continue
        with open(overlay_path, encoding="utf-8") as handle:
            overlay = json.load(handle)

        spawns = [p for p in overlay.get("spawnPoints", []) if p.get("type") in ("enemy_spawn", "npc_spawn")]
        if not spawns:
            report[zone_id] = "nothing to migrate"
            continue

        level_name = level_path.rsplit("/", 1)[-1]
        if not _levels().set_current_level_by_name(level_name):
            raise RuntimeError("could not make {} current; is it streamed into L_World?".format(level_name))

        existing = {a.get_actor_label() for a in _actors().get_all_level_actors()
                    if isinstance(a, unreal.ValhallaNPCSpawner)}
        placed, moved, skipped = [], [], []

        for point in spawns:
            type_name = POINT_TO_TYPE.get(point["id"]) or TEMPLATE_TO_TYPE.get(point.get("templateId"))
            npc_class = _type_class(type_name) if type_name else None
            if npc_class is None:
                skipped.append("{} (no NPC type for template {})".format(point["id"], point.get("templateId")))
                continue

            group = point.get("label") or point["id"]
            homes = _ring(point)
            for index, (lx, ly) in enumerate(homes):
                label = group if len(homes) == 1 else "{} {}".format(group, index + 1)
                if label in existing:
                    continue

                wx, wy = offset.x + lx, offset.y + ly
                cx, cy, distance, blockers = _clear_spot(world, wx, wy, offset.z + FLOOR_TOP)
                if distance != 0.0:
                    moved.append("{}: {} by {:.0f} cm (was inside {})".format(
                        label, "moved" if distance > 0 else "NOT moved, no clear spot",
                        max(distance, 0.0), ", ".join(sorted({b.get_actor_label() for b in blockers or []}))))

                spawner = _actors().spawn_actor_from_class(
                    unreal.ValhallaNPCSpawner, unreal.Vector(cx, cy, offset.z + FLOOR_TOP),
                    unreal.Rotator(yaw=180.0 if point["type"] == "npc_spawn" else 0.0))
                spawner.set_editor_property("npc_class", npc_class)
                spawner.set_actor_label(label)
                spawner.set_folder_path("NPC Spawns/{}".format(group))
                placed.append(label)

        _levels().save_current_level()

        backup = overlay_path.replace(".json", ".pre-spawnpoints.json")
        if not os.path.isfile(backup):
            shutil.copyfile(overlay_path, backup)
        overlay["spawnPoints"] = [p for p in overlay.get("spawnPoints", [])
                                  if p.get("type") not in ("enemy_spawn", "npc_spawn")]
        with open(overlay_path, "w", encoding="utf-8") as handle:
            json.dump(overlay, handle, indent=2)
            handle.write("\n")

        report[zone_id] = {"placed": placed, "moved": moved, "skipped": skipped, "backup": backup}
        _log("{}: {} spawn point(s) placed, {} moved clear of geometry".format(zone_id, len(placed), len(moved)))

    build_world._make_persistent_level_current()
    _levels().save_all_dirty_levels()
    return report


def run_all(force=False):
    result = {
        "npcTypes": create_npc_types(),
        "sublevels": ensure_world_has_gameplay_levels(),
    }
    result["migration"] = migrate_overlay_spawns(force=force)
    _log("VALHALLA_NPC_DONE " + json.dumps(result, default=str))
    return result


if __name__ == "__main__":
    run_all()
