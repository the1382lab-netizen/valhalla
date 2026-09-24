"""B-13: export the Unreal-side references the data validator can't read.

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/export_unreal_refs.py"

Writes ``maps/unreal-refs.json`` (repo root) with:

* every NPC Type (``BP_NPC_*`` under /Game/Valhalla/NPCs) and its Default
  Template Id, and
* every NPC Spawn Point (``AValhallaNPCSpawner``) in the levels currently
  loaded in the editor, with its NPC Type and Template Override.

``npm run validate`` (and the web editor) then check those against
npc-templates.json. Open ``L_World`` first so the gameplay sublevels are
loaded; the file lists which levels were read, and spawn points in levels
that weren't loaded are simply not in it. Re-run after changing NPC Types or
spawn points, and commit the file with them.
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


def _spawn_points():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    points, levels = [], set()
    for actor in actors:
        level = actor.get_level()
        level_name = level.get_outer().get_name() if level and level.get_outer() else "?"
        levels.add(level_name)
        if not isinstance(actor, unreal.ValhallaNPCSpawner):
            continue
        template = _prop(actor, "template_id", "TemplateId")
        points.append({
            "level": level_name,
            "name": actor.get_actor_label(),
            "npcType": _class_asset_name(_prop(actor, "npc_class", "NPCClass")),
            "templateOverride": str(template) if template and str(template) != "None" else "",
        })
    return sorted(points, key=lambda p: (p["level"], p["name"])), sorted(levels)


def run():
    types = _npc_types()
    points, levels = _spawn_points()
    doc = {
        "generatedAt": datetime.datetime.now().isoformat(timespec="seconds"),
        "levels": levels,
        "npcTypes": types,
        "spawnPoints": points,
    }
    path = os.path.join(_repo_root(), "maps", "unreal-refs.json")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    _log("wrote {}: {} NPC types, {} spawn points from {} level(s)".format(path, len(types), len(points), len(levels)))
    return doc


if __name__ == "__main__":
    run()
