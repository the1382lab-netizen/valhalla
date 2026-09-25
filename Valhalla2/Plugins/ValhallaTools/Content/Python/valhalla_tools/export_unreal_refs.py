"""B-13: export the Unreal-side references the data validator can't read.

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/export_unreal_refs.py"

Writes ``maps/unreal-refs.json`` (repo root) with:

* every NPC Type (``BP_NPC_*`` under /Game/Valhalla/NPCs) and its Default
  Template Id, and
* every NPC Spawn Point (``AValhallaNPCSpawner``) in the levels currently
  loaded in the editor, with its NPC Type and Template Override (and, B-10
  part 2, its patrol mode and point count, the spawn point it follows, its
  roam radius and its rare spawn), and
* every zone actor in those levels: zone volumes (``AValhallaZoneVolume``),
  portals (``AValhallaPortal``), zone entries (``AValhallaZoneEntry``) and
  player starts (``APlayerStart`` and its tag). Unreal is the only source of
  truth for these; the old ``maps/overlays-2.0`` JSON is retired.

``npm run validate`` (and the web editor) then check those against
npc-templates.json and zones.json. Open ``L_World`` first so the gameplay sublevels are
loaded; the file lists which levels were read, and spawn points in levels
that weren't loaded are simply not in it. Re-run after changing NPC Types or
spawn points, portals, zone entries or player starts, and commit the file
with them.
"""

import datetime
import json
import os

import unreal

NPC_FOLDER = "/Game/Valhalla/NPCs"


def _log(msg):
    unreal.log("VALHALLA_REFS " + msg)


def _prop(obj, *names):
    """The first of `names` that reads (Python and C++ spellings differ)."""
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception:
            continue
    return None


def _repo_root():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(project, ".."))


def _npc_types():
    types = []
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for data in registry.get_assets_by_path(NPC_FOLDER, recursive=True):
        asset = data.get_asset()
        if not isinstance(asset, unreal.Blueprint):
            continue
        generated = asset.generated_class()
        if generated is None:
            continue
        defaults = unreal.get_default_object(generated)
        if not isinstance(defaults, unreal.ValhallaNPC):
            continue
        template = _prop(defaults, "default_template_id", "DefaultTemplateId")
        types.append({
            "asset": str(data.asset_name),
            "defaultTemplateId": str(template) if template and str(template) != "None" else "",
        })
    return sorted(types, key=lambda t: t["asset"])


def _class_asset_name(cls):
    """`BP_NPC_TestEnemy` for the class `BP_NPC_TestEnemy_C`."""
    if cls is None:
        return ""
    name = cls.get_name()
    return name[:-2] if name.endswith("_C") else name


def _name(value):
    """An FName property as text, '' for None / NAME_None."""
    return str(value) if value is not None and str(value) != "None" else ""


def _level_actors():
    """Every actor in the loaded levels, collected once for all the lists below."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    out, levels = [], set()
    for actor in actors:
        level = actor.get_level()
        level_name = level.get_outer().get_name() if level and level.get_outer() else "?"
        levels.add(level_name)
        out.append((level_name, actor))
    return out, sorted(levels)


def _by_level(rows):
    return sorted(rows, key=lambda r: (r["level"], r["name"]))


def _enum_name(value):
    """'pingpong' for ValhallaPatrolMode.PING_PONG (however this UE version prints it), '' for None."""
    if value is None:
        return ""
    name = getattr(value, "name", None) or str(value)
    name = str(name).split(".")[-1].split(":")[0].strip("<> ")
    return name.replace("_", "").lower()


def _number(value, default=0.0):
    try:
        return round(float(value), 3)
    except (TypeError, ValueError):
        return default


def _spawn_points(actors):
    points = []
    for level_name, actor in actors:
        if not isinstance(actor, unreal.ValhallaNPCSpawner):
            continue
        template = _prop(actor, "template_id", "TemplateId")
        # B-10 part 2 (new keys only; the four above keep their meaning, so
        # older readers of this file are unaffected).
        patrol_points = _prop(actor, "patrol_points", "PatrolPoints") or []
        follow = _prop(actor, "follow_spawner", "FollowSpawner")
        points.append({
            "level": level_name,
            "name": actor.get_actor_label(),
            "npcType": _class_asset_name(_prop(actor, "npc_class", "NPCClass")),
            "templateOverride": _name(template),
            "patrolMode": _enum_name(_prop(actor, "patrol_mode", "PatrolMode")),
            "patrolPoints": len(patrol_points),
            "followSpawner": follow.get_actor_label() if follow is not None else "",
            "wanderRadius": _number(_prop(actor, "wander_radius", "WanderRadius")),
            "rareNpcType": _class_asset_name(_prop(actor, "rare_npc_class", "RareNPCClass")),
            "rareTemplateId": _name(_prop(actor, "rare_template_id", "RareTemplateId")),
            "rareChance": _number(_prop(actor, "rare_chance", "RareChance")),
        })
    return _by_level(points)


def _zone_actors(actors):
    """Zone volumes, portals, zone entries and player starts."""
    volumes, portals, entries, starts = [], [], [], []
    for level_name, actor in actors:
        row = {"level": level_name, "name": actor.get_actor_label()}
        if isinstance(actor, unreal.ValhallaZoneVolume):
            row["zoneId"] = _name(_prop(actor, "zone_id", "ZoneId"))
            volumes.append(row)
        elif isinstance(actor, unreal.ValhallaPortal):
            row["targetZoneId"] = _name(_prop(actor, "target_zone_id", "TargetZoneId"))
            row["targetEntryId"] = _name(_prop(actor, "target_entry_id", "TargetEntryId"))
            portals.append(row)
        elif isinstance(actor, unreal.ValhallaZoneEntry):
            row["entryId"] = _name(_prop(actor, "entry_id", "EntryId"))
            row["fromZoneId"] = _name(_prop(actor, "from_zone_id", "FromZoneId"))
            entries.append(row)
        elif isinstance(actor, unreal.PlayerStart):
            row["tag"] = _name(_prop(actor, "player_start_tag", "PlayerStartTag"))
            starts.append(row)
    return _by_level(volumes), _by_level(portals), _by_level(entries), _by_level(starts)


def run():
    types = _npc_types()
    actors, levels = _level_actors()
    points = _spawn_points(actors)
    volumes, portals, entries, starts = _zone_actors(actors)
    doc = {
        "generatedAt": datetime.datetime.now().isoformat(timespec="seconds"),
        "levels": levels,
        "npcTypes": types,
        "spawnPoints": points,
        "zoneVolumes": volumes,
        "portals": portals,
        "zoneEntries": entries,
        "playerStarts": starts,
    }
    path = os.path.join(_repo_root(), "maps", "unreal-refs.json")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    _log("wrote {}: {} NPC types, {} spawn points, {} zone volumes, {} portals, {} zone entries, {} player starts "
         "from {} level(s)".format(path, len(types), len(points), len(volumes), len(portals), len(entries),
                                   len(starts), len(levels)))
    return doc


if __name__ == "__main__":
    run()
