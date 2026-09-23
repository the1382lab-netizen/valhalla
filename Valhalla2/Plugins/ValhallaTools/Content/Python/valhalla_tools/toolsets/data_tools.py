"""Valhalla 2.0 data and asset tools, exposed to Unreal MCP.

Three jobs, all of them things a human would otherwise do by hand between the
1.0 TypeScript repo and the 2.0 Unreal project:

* validate the seven JSON data files before the C++ loader sees them,
* pull a directory of exported ``.glb`` meshes into the content browser,
* kick off the ``Valhalla.*`` automation suite.

Every tool returns a JSON *string* rather than a dict, so the MCP layer gets a
stable, self-describing payload it can hand straight back to a model.
"""

import json
import os

import unreal
import toolset_registry

#: The seven data files and the top-level collection key inside each one.
#: Mirrors the shapes ValhallaDataSubsystem.cpp parses.
_DATA_FILES = {
    "classes.json": "classes",
    "items.json": "items",
    "skills.json": "skills",
    "npc-templates.json": "templates",
    "loot-tables.json": "tables",
    "zones.json": "zones",
    "ui-config.json": None,  # not a keyed collection; validated as parseable
}

#: Static meshes whose name starts with this are line-of-sight blockers.
#:
#: This prefix is the whole contract. Phase 5 has no list of wall assets and no
#: per-level setup step: a mesh called ``VB_*`` gets the ``VisionBlocker``
#: collision profile at import, and anything placed from it blocks sight. Phase
#: 3's map importer inherits that for free as long as it keeps the kit's names.
_VISION_BLOCKER_PREFIX = "VB_"

#: The collision profile declared in ``Config/DefaultEngine.ini`` under
#: ``[/Script/Engine.CollisionProfile]``. WorldStatic, and blocking the
#: ``VisionBlocker`` trace channel as well as Visibility, Camera and Pawn.
_VISION_BLOCKER_PROFILE = "VisionBlocker"


def _set_collision_profile(body_instance_owner, profile_name):
    """Write a collision profile onto a BodySetup or a PrimitiveComponent.

    UE structs come back from Python *by value*, so this has to be a
    read-modify-write: mutating the returned BodyInstance in place would
    silently do nothing and leave the caller convinced it had worked. That is
    the single most common way an automated collision change fails.

    Returns the profile name that was actually there before, so a caller can
    report what it changed.
    """
    body_instance = body_instance_owner.get_editor_property("default_instance")
    previous = str(body_instance.get_editor_property("collision_profile_name"))
    body_instance.set_editor_property("collision_profile_name", profile_name)
    body_instance_owner.set_editor_property("default_instance", body_instance)
    return previous


@unreal.uclass()
class ValhallaDataTools(unreal.ToolsetDefinition):
    """Tools for the Valhalla 2.0 port: data validation, mesh import, tests."""

    @toolset_registry.tool_call
    @staticmethod
    def validate_data_json(data_root: str) -> str:
        """Parse the seven Valhalla 1.0 JSON data files and report on them.

        Reads every file the C++ ValhallaDataSubsystem reads, in the same order,
        and reports per-file record counts plus any file that is missing,
        unparseable, or lacks its expected top-level collection key. Run this
        before blaming the C++ loader for an empty table.

        Args:
            data_root: Absolute path to the directory holding classes.json and
                its six siblings, e.g.
                ``C:/Users/music/game-project/Valhalla 2.0/shared/data``.

        Returns:
            A JSON object as text with keys ``dataRoot``, ``ok`` (bool),
            ``counts`` (filename -> record count) and ``errors`` (list of
            human-readable problems, empty when everything parsed).
        """
        result = {"dataRoot": data_root, "ok": True, "counts": {}, "errors": []}

        if not os.path.isdir(data_root):
            result["ok"] = False
            result["errors"].append("Data root does not exist: {}".format(data_root))
            return json.dumps(result, indent=2)

        for file_name, collection_key in _DATA_FILES.items():
            path = os.path.join(data_root, file_name)
            if not os.path.isfile(path):
                result["ok"] = False
                result["errors"].append("Missing file: {}".format(file_name))
                continue

            try:
                with open(path, "r", encoding="utf-8") as handle:
                    payload = json.load(handle)
            except (OSError, ValueError) as error:
                result["ok"] = False
                result["errors"].append("{}: {}".format(file_name, error))
                continue

            if collection_key is None:
                # ui-config.json is consumed raw; parsing it is the whole check.
                result["counts"][file_name] = len(payload)
                continue

            collection = payload.get(collection_key)
            if not isinstance(collection, dict):
                result["ok"] = False
                result["errors"].append(
                    "{}: no '{}' object at the top level".format(file_name, collection_key)
                )
                continue

            result["counts"][file_name] = len(collection)

        return json.dumps(result, indent=2)

    @toolset_registry.tool_call
    @staticmethod
    def import_gltf_batch(source_dir: str, dest_path: str) -> str:
        """Import every ``.glb`` under a directory into a content path.

        Imports are automated and replace existing assets, so re-running after a
        re-export in Blender refreshes the meshes in place. Any imported static
        mesh whose name starts with ``VB_`` is treated as a line-of-sight
        blocker and has its default collision profile set to ``VisionBlocker``.

        Args:
            source_dir: Absolute path to search, recursively, for ``*.glb``.
            dest_path: Content browser destination, e.g. ``/Game/Valhalla/Meshes``.

        Returns:
            A JSON object as text with keys ``sourceDir``, ``destPath``,
            ``imported`` (list of created asset paths), ``visionBlockers``
            (the subset that got the VisionBlocker profile) and ``errors``.
        """
        result = {
            "sourceDir": source_dir,
            "destPath": dest_path,
            "imported": [],
            "visionBlockers": [],
            "errors": [],
        }

        if not os.path.isdir(source_dir):
            result["errors"].append("Source directory does not exist: {}".format(source_dir))
            return json.dumps(result, indent=2)

        glb_files = []
        for dir_path, _dir_names, file_names in os.walk(source_dir):
            for file_name in file_names:
                if file_name.lower().endswith(".glb"):
                    glb_files.append(os.path.join(dir_path, file_name))

        if not glb_files:
            result["errors"].append("No .glb files found under {}".format(source_dir))
            return json.dumps(result, indent=2)

        tasks = []
        for glb_path in sorted(glb_files):
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", glb_path)
            task.set_editor_property("destination_path", dest_path)
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("save", True)
            tasks.append(task)

        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

        for task in tasks:
            for asset_path in task.get_editor_property("imported_object_paths") or []:
                result["imported"].append(asset_path)

        for asset_path in result["imported"]:
            asset = unreal.EditorAssetLibrary.load_asset(asset_path)
            if not isinstance(asset, unreal.StaticMesh):
                continue
            if not asset.get_name().startswith(_VISION_BLOCKER_PREFIX):
                continue

            # Phase 5: the dedicated channel now exists, so this is the real
            # thing rather than the BlockAll approximation Phase 4 left behind.
            # BlockAll was not merely imprecise — because a new trace channel
            # defaults to Ignore for every profile that does not name it, a
            # BlockAll wall is *invisible* to the VisionBlocker trace and would
            # have blocked nothing at all.
            body_setup = asset.get_editor_property("body_setup")
            if body_setup is None:
                result["errors"].append("No body setup on {}".format(asset_path))
                continue

            _set_collision_profile(body_setup, _VISION_BLOCKER_PROFILE)

            unreal.EditorAssetLibrary.save_asset(asset_path)
            result["visionBlockers"].append(asset_path)

        return json.dumps(result, indent=2)

    @toolset_registry.tool_call
    @staticmethod
    def consolidate_materials(content_root: str) -> str:
        """Collapse Interchange's per-mesh material copies into one asset each.

        Interchange imports each ``.glb`` on its own and cannot know that the
        ``M_Grass`` in ``SM_Grass_A.glb`` is the same material as the one in
        ``SM_Grass_B.glb``, so it makes a copy per mesh folder — five
        ``M_Grass``, four ``M_Stone``, six ``M_Wood`` once the three kits are
        in. Every copy is a drift risk, and every copy is an instance of
        Interchange's *PBR* glTF master rather than of ``M_ValhallaToon``, so
        the kit renders shiny instead of flat.

        This keeps one canonical
        ``/Game/Valhalla/Environment/Materials/M_<Name>`` per name, as an
        instance of ``M_ValhallaToon`` carrying the glTF import's own
        ``BaseColorFactor`` / ``EmissiveFactor`` / ``EmissiveStrength``, then
        repoints every static mesh slot at it, deletes the copies and resolves
        the redirectors.

        A material it cannot read those factors off — not a glTF instance — is
        *moved* to the canonical path rather than replaced, so nothing is ever
        lost to a shading model it did not expect.

        Deletion is last and is skipped entirely if any slot was left
        unassigned: sixty redundant assets are a much better outcome than one
        mesh rendering as the grey world-grid checker.

        Idempotent. See ``valhalla_tools/consolidate_env_materials.py``.

        Args:
            content_root: Content folder to sweep, e.g.
                ``/Game/Valhalla/Environment/Desert``. Several may be given
                comma-separated, which is what lets the grassland, desert, town
                and prop kits share one ``M_Stone``.

        Returns:
            A JSON object as text with keys ``ok``, ``uniqueMaterials``,
            ``importedCopies``, ``created``, ``moved``, ``existed``,
            ``meshes``, ``slotsAssigned``, ``deleted``, ``unmatchedSlots``,
            ``failedSlots`` and ``redirectors``.
        """
        from valhalla_tools import consolidate_env_materials

        roots = [r.strip() for r in content_root.split(",") if r.strip()]
        if not roots:
            return json.dumps({"ok": False, "error": "no content_root given"}, indent=2)

        try:
            counts = consolidate_env_materials.consolidate(roots)
        except Exception as exc:  # noqa: BLE001 - reported, never raised at MCP
            return json.dumps({"ok": False, "error": str(exc), "roots": roots}, indent=2)

        counts["ok"] = not counts["failedSlots"]
        return json.dumps(counts, indent=2)

    @toolset_registry.tool_call
    @staticmethod
    def apply_vision_blocker_profile(content_root: str, level_path: str = "") -> str:
        """Put the ``VisionBlocker`` collision profile on every ``VB_`` mesh.

        Two passes, because a collision profile lives in two places and fixing
        one without the other is the usual way this goes wrong:

        * the **asset** — each ``VB_*`` static mesh's BodySetup default
          instance, which is what any actor placed from it inherits; and
        * the **level** — every static mesh component already placed from one
          of those meshes, which kept whatever profile it had when it was
          placed and would otherwise stay deaf to the trace forever.

        The level pass only writes a component whose profile is not already
        ``VisionBlocker``, so re-running it is free, and it saves the level only
        if it actually changed something.

        Idempotent, and safe to run after any re-import.

        Args:
            content_root: Content folder to search for ``VB_`` static meshes,
                e.g. ``/Game/Valhalla/Environment``.
            level_path: Level whose placed walls to fix, e.g.
                ``/Game/Valhalla/Maps/L_GreyBox``. Empty means "whatever is
                open", which is what you want when you have just built a level
                and have not saved it yet.

        Returns:
            A JSON object as text with keys ``ok``, ``meshes`` (asset paths
            updated, with the profile each one had before), ``components``
            (how many placed components were rewritten), ``level`` and
            ``errors``.
        """
        result = {
            "ok": True,
            "contentRoot": content_root,
            "level": level_path,
            "meshes": [],
            "components": 0,
            "errors": [],
        }

        # ── Pass 1: the assets ──────────────────────────────────────────
        asset_paths = unreal.EditorAssetLibrary.list_assets(
            content_root, recursive=True, include_folder=False)

        for asset_path in asset_paths:
            name = asset_path.rsplit("/", 1)[-1].split(".")[0]
            if not name.startswith(_VISION_BLOCKER_PREFIX):
                continue

            asset = unreal.EditorAssetLibrary.load_asset(asset_path)
            if not isinstance(asset, unreal.StaticMesh):
                continue

            body_setup = asset.get_editor_property("body_setup")
            if body_setup is None:
                result["ok"] = False
                result["errors"].append("No body setup on {}".format(asset_path))
                continue

            previous = _set_collision_profile(body_setup, _VISION_BLOCKER_PROFILE)
            unreal.EditorAssetLibrary.save_asset(asset_path)
            result["meshes"].append({"asset": asset_path, "was": previous})

        # ── Pass 2: the level ───────────────────────────────────────────
        level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if level_path:
            level_subsystem.load_level(level_path)

        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        changed = 0

        for actor in actor_subsystem.get_all_level_actors():
            for component in actor.get_components_by_class(unreal.StaticMeshComponent):
                mesh = component.get_editor_property("static_mesh")
                if mesh is None or not mesh.get_name().startswith(_VISION_BLOCKER_PREFIX):
                    continue
                if str(component.get_collision_profile_name()) == _VISION_BLOCKER_PROFILE:
                    continue
                component.set_collision_profile_name(_VISION_BLOCKER_PROFILE)
                changed += 1

        result["components"] = changed
        if changed:
            if not level_subsystem.save_current_level():
                result["ok"] = False
                result["errors"].append("save_current_level returned False")

        current = level_subsystem.get_current_level()
        result["currentLevel"] = current.get_outer().get_path_name() if current else ""

        unreal.log("{} apply_vision_blocker_profile: {} mesh(es), {} component(s)".format(
            "LogValhallaImport:", len(result["meshes"]), changed))

        return json.dumps(result, indent=2)

    @toolset_registry.tool_call
    @staticmethod
    def import_character_glb(source_dir: str, stage: str = "all") -> str:
        """Import the Valhalla character art so every mesh shares one skeleton.

        ``import_gltf_batch`` cannot do this job: it runs one automated
        ``AssetImportTask`` per file with default settings, and Interchange's
        default is to mint a fresh skeleton per source file. Twenty-two private
        skeletons would each be *correct* — same bones, same names — and
        entirely useless, because ``SetLeaderPoseComponent`` refuses a follower
        whose skeleton is not literally the leader's asset, and the failure
        looks like armour rendering in its reference pose beside a walking
        character rather than like an error.

        This drives Interchange directly instead, with an override pipeline
        whose ``common_skeletal_meshes_and_animations_properties.skeleton``
        slot names the body's skeleton for every later import, and then checks
        the result rather than trusting it.

        The body must be imported before the equipment; ``all`` does both in
        order and is what you want unless you are re-running one step.

        Args:
            source_dir: The ``Import/Characters`` directory, holding
                ``SK_Valhalla_Body.glb`` and an ``Equipment`` subdirectory.
            stage: ``body``, ``equipment``, ``materials``, ``verify`` or
                ``all``.

        Returns:
            A JSON object as text. ``all`` reports every created asset path,
            the material consolidation counts and the verification result;
            ``verify`` reports ``ok`` plus any mesh whose skeleton disagrees.
        """
        from valhalla_tools import character_import

        stages = {
            "body": lambda: character_import.import_body(source_dir),
            "equipment": lambda: character_import.import_equipment(source_dir),
            "materials": lambda: {
                "instances": character_import.build_material_instances(),
                "consolidated": character_import.consolidate_materials(),
            },
            "verify": character_import.verify,
            "all": lambda: character_import.run_all(source_dir),
        }

        if stage not in stages:
            return json.dumps(
                {"ok": False, "error": "unknown stage '{}'; expected one of {}".format(
                    stage, sorted(stages))},
                indent=2,
            )

        return json.dumps({"stage": stage, "result": stages[stage]()},
                          indent=2, default=str)

    @toolset_registry.tool_call
    @staticmethod
    def run_valhalla_tests(test_filter: str = "Valhalla.") -> str:
        """Start the automation tests whose names match a filter.

        Runs asynchronously via the editor console: this returns as soon as the
        run has been requested, not when it finishes. Read the results in the
        Session Frontend's Automation tab or in the editor log.

        Args:
            test_filter: Test name prefix to run. Defaults to ``Valhalla.``, which
                covers the whole ported suite.

        Returns:
            A JSON object as text with keys ``status`` (always ``started``),
            ``filter`` (the value of test_filter) and ``command`` (the console command that was executed).
        """
        command = "Automation RunTests {}".format(test_filter)
        unreal.SystemLibrary.execute_console_command(None, command)
        return json.dumps(
            {"status": "started", "filter": test_filter, "command": command},
            indent=2,
        )
