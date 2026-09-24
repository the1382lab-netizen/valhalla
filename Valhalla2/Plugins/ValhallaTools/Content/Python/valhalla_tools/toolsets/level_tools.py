"""Level and material tools for the Valhalla 2.0 port, exposed to Unreal MCP.

The generic MCP toolsets can place actors into a level and can save assets, but
they cannot *create* a level or author a material, both of which Phase 2a needs
and both of which are one call to an editor subsystem. They live here rather
than in a throwaway script so that rebuilding the greybox is a tool call and
not a hunt through the editor UI.

Multiplayer Play-In-Editor settings are deliberately not here.
ULevelEditorPlaySettings is not exposed to Python (`unreal.LevelEditorPlaySettings`
does not exist), so use the ConfigSettingsToolset instead:

    ConfigSettingsToolset.SetSectionProperties(
        container="Editor", category="LevelEditor", section="PlayIn",
        propertiesJson='{"PlayNetMode":"PIE_ListenServer",'
                       '"PlayNumberOfClients":2,"RunUnderOneProcess":true}')

Every tool returns a JSON *string*, matching data_tools.py.
"""

import json

import unreal
import toolset_registry


def _ok(**kwargs) -> str:
    payload = {"ok": True}
    payload.update(kwargs)
    return json.dumps(payload, indent=2)


def _fail(message: str, **kwargs) -> str:
    payload = {"ok": False, "error": message}
    payload.update(kwargs)
    return json.dumps(payload, indent=2)


@unreal.uclass()
class ValhallaLevelTools(unreal.ToolsetDefinition):
    """Level creation and saving, and simple material authoring.

    Levels and overlays listed in ``<repo>/maps/handedited.json`` are
    hand-edited: ``build_world_levels`` skips them unless ``force=True`` (B-05).
    """

    @toolset_registry.tool_call
    @staticmethod
    def create_level(level_path: str) -> str:
        """Create a new empty level asset and open it in the editor.

        Replaces whatever level is currently loaded. Anything unsaved in the
        current level is lost, so save first.

        Args:
            level_path: Content path for the new level, e.g.
                ``/Game/Valhalla/Maps/L_GreyBox``.

        Returns:
            A JSON object as text with keys ``ok``, ``levelPath`` and
            ``currentLevel`` (what the editor has open afterwards).
        """
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        created = subsystem.new_level(level_path)
        current = subsystem.get_current_level()
        current_path = current.get_outer().get_path_name() if current else ""
        if not created:
            return _fail("new_level failed for {}".format(level_path), currentLevel=current_path)
        return _ok(levelPath=level_path, currentLevel=current_path)

    @toolset_registry.tool_call
    @staticmethod
    def save_level() -> str:
        """Save the level currently open in the editor.

        Returns:
            A JSON object as text with keys ``ok`` and ``currentLevel``.
        """
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        saved = subsystem.save_current_level()
        current = subsystem.get_current_level()
        current_path = current.get_outer().get_path_name() if current else ""
        if not saved:
            return _fail("save_current_level returned False", currentLevel=current_path)
        return _ok(currentLevel=current_path)

    @toolset_registry.tool_call
    @staticmethod
    def build_world_levels(force: bool = False) -> str:
        """Rebuild `L_World`, `L_Grasslands` and `L_Desert`, and both overlays — except hand-edited ones.

        HAND-EDITED LEVELS ARE PROTECTED (B-05). Every level or overlay listed
        in ``<repo>/maps/handedited.json`` — today ``L_World``,
        ``L_Grasslands``, ``L_Desert`` and the ``grasslands`` / ``desert``
        overlays, i.e. everything this tool builds — is SKIPPED unless
        ``force=True``: not opened, not emptied, not saved. The result's
        ``skipped`` names each one; anything not listed (a new zone) still
        builds. With nothing left to build it returns without touching the
        editor. Do not pass ``force=True`` unless Kevin explicitly asks for
        his hand edits to be thrown away. A forced run first copies the
        existing ``.umap`` / ``_BuiltData.uasset`` / overlay JSON files to
        ``Valhalla2/Saved/LevelBackups/<YYYYMMDD-HHMMSS>/`` (paths relative to
        the repo root) and returns that folder as ``backup``. To protect or
        release a level, edit the marker file.

        An unprotected level is replaced from scratch, and building a zone
        writes ``<repo>/maps/overlays-2.0/<zone>.json`` as a side effect
        (unless that overlay is protected), because the overlay's portal
        coordinates are checked against the level's portal actors and the only
        way to keep those in step across a rebuild is for one pass to emit
        both.

        Replaces whatever level is currently open when anything is built, so
        save first. See ``valhalla_tools/build_world.py`` for the streaming
        layout, ``build_grasslands.py`` / ``build_desert.py`` for the two
        themes and ``level_protection.py`` for the marker.

        Args:
            force: Overwrite levels and overlays listed in
                ``maps/handedited.json`` (after backing them up). Default
                False.

        Returns:
            A JSON object as text with keys ``ok``, ``force``, ``skipped``
            (``levels`` and ``overlays`` left alone), ``built``,
            ``overlaysWritten``, ``message`` (when anything was skipped),
            ``backup`` (forced runs), and per built zone ``grasslands`` /
            ``desert`` plus ``world`` — each carrying its actor counts per
            outliner folder and the measured positions the gate needs.
        """
        from valhalla_tools import build_world

        try:
            return _ok(**build_world.build_all(force=force))
        except Exception as exc:  # noqa: BLE001 - reported, never raised at MCP
            import traceback
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])

    @toolset_registry.tool_call
    @staticmethod
    def capture_zone_topdown(zone_id: str, out_dir: str = "") -> str:
        """Orthographic top-down PNG of a zone, plus the metadata for its pixels.

        Writes ``<out_dir>/<zone_id>.png`` at 2048 x 2048 and
        ``<out_dir>/<zone_id>.json`` holding ``originX``, ``originY``,
        ``sizeX``, ``sizeY`` and ``pixelsPerCm`` — everything the Phase 6
        editor needs to turn a click on the picture into a coordinate in an
        overlay file.

        The capture is orthographic with its ortho width set to the zone's own
        size, so pixel-to-centimetre is a scale and an offset; and the camera
        is at ``pitch -90, yaw -90``, which makes image-right ``+X`` and
        image-down ``+Y`` — 1.0's canvas convention, so the editor's arithmetic
        does not change. See ``valhalla_tools/capture_topdown.py``.

        Requires the level holding the zone to be open, because the zone's
        bounds come from its ``AValhallaZoneVolume``: open ``L_World``, which
        has both.

        Args:
            zone_id: ``grasslands`` or ``desert``.
            out_dir: Where to write. Empty means
                ``<1.0 repo>/maps/thumbs``, derived from
                ``ValhallaDataSettings.DataRoot`` — the same place the Phase 6
                editor will look.

        Returns:
            A JSON object as text: ``ok`` plus the metadata that was written.
        """
        from valhalla_tools import capture_topdown

        try:
            return _ok(**capture_topdown.capture(zone_id, out_dir or None))
        except Exception as exc:  # noqa: BLE001
            import traceback
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])

    @toolset_registry.tool_call
    @staticmethod
    def create_tint_material(package_path: str, asset_name: str, parameter_name: str) -> str:
        """Create a flat, unlit-looking material driven by one colour parameter.

        The material has a single VectorParameter wired to both Base Color and
        Emissive Color, so a dynamic material instance can tint a mesh at
        runtime by setting that one parameter. Phase 2a colours the placeholder
        player proxies with it; Phase 4 deletes it along with the proxies.

        Existing assets at the path are left alone and reported as ``existed``.

        Args:
            package_path: Content folder for the asset, e.g. ``/Game/Valhalla/Materials``.
            asset_name: Asset name, e.g. ``M_ClassProxy``.
            parameter_name: Name of the colour parameter, e.g. ``ClassColor``.

        Returns:
            A JSON object as text with keys ``ok``, ``assetPath``, ``existed``
            and ``parameter``.
        """
        asset_path = "{}/{}".format(package_path.rstrip("/"), asset_name)

        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            return _ok(assetPath=asset_path, existed=True, parameter=parameter_name)

        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        material = asset_tools.create_asset(
            asset_name, package_path, unreal.Material, unreal.MaterialFactoryNew()
        )
        if material is None:
            return _fail("create_asset returned None for {}".format(asset_path))

        editing = unreal.MaterialEditingLibrary
        colour = editing.create_material_expression(
            material, unreal.MaterialExpressionVectorParameter, -400, 0
        )
        colour.set_editor_property("parameter_name", parameter_name)
        colour.set_editor_property("default_value", unreal.LinearColor(0.8, 0.8, 0.8, 1.0))

        editing.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)

        # A little emissive keeps the proxy readable from the isometric camera
        # even where the directional light does not reach it.
        scale = editing.create_material_expression(
            material, unreal.MaterialExpressionMultiply, -200, 200
        )
        scale.set_editor_property("const_b", 0.35)
        editing.connect_material_expressions(colour, "", scale, "A")
        editing.connect_material_property(scale, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

        editing.recompile_material(material)
        unreal.EditorAssetLibrary.save_asset(asset_path)

        return _ok(assetPath=asset_path, existed=False, parameter=parameter_name)

    # ── Phase 5 ─────────────────────────────────────────────────────────

    @toolset_registry.tool_call
    @staticmethod
    def build_fog_material() -> str:
        """Author ``/Game/Valhalla/Materials/PP_Fog``, the fog of war post-process.

        Rebuilds the graph from scratch every time — an edited graph that half
        matches the code is worse than no graph at all, which is the same rule
        ``build_toon`` follows. See ``valhalla_tools/build_fog.py`` for what the
        material does and why it sits after ``PP_Outline``.

        Returns:
            A JSON object as text with keys ``ok`` and ``assetPath``.
        """
        from valhalla_tools import build_fog

        return _ok(assetPath=build_fog.build_fog())

    @toolset_registry.tool_call
    @staticmethod
    def place_fog_bounds(level_path: str = "", extent_cm: float = 1280.0) -> str:
        """Put one ``AValhallaFogBounds`` in a level, covering the playable area.

        The fog's two masks are 1024 texels square and are stretched over this
        box, so it should hug the map: doubling the box halves the resolution.
        A level without one still gets fog — ``AValhallaFogRenderer`` falls back
        to the bounds of every ``VB_``/``SM_`` static mesh plus ten per cent —
        but the actor is the level's own statement of where it stops.

        Idempotent: an existing ``FogBounds`` actor is moved and resized rather
        than duplicated.

        Args:
            level_path: Level to open first, or empty for whatever is open.
            extent_cm: Half-size on X and Y, centimetres. The greybox is 24
                tiles (768) and L_LoSTest is 40 (1280).

        Returns:
            A JSON object as text with keys ``ok``, ``actor`` and ``level``.
        """
        from valhalla_tools import build_fog

        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if level_path:
            subsystem.load_level(level_path)

        actor = build_fog.place_fog_bounds(
            centre=(0.0, 0.0, 200.0), extent=(extent_cm, extent_cm, 400.0))

        if not subsystem.save_current_level():
            return _fail("save_current_level returned False", actor=actor)

        current = subsystem.get_current_level()
        return _ok(actor=actor,
                   level=current.get_outer().get_path_name() if current else "")

    @toolset_registry.tool_call
    @staticmethod
    def build_los_test_level() -> str:
        """Build ``/Game/Valhalla/Maps/L_LoSTest`` from scratch and save it.

        Replaces whatever level is currently open, so save first. The level is
        the Phase 5 fixture: a walled room with one doorway, a one-tile L
        corridor, and an open field with two NPCs at measured distances. See
        ``valhalla_tools/build_lostest.py`` for the geometry and for why each
        piece is shaped the way it is.

        Returns:
            A JSON object as text with keys ``ok``, ``counts`` (actors per
            outliner folder) and ``notes`` (the measured positions the gate
            needs, so they do not have to be recomputed by hand).
        """
        from valhalla_tools import build_lostest

        result = build_lostest.build()
        return _ok(**result)

    @toolset_registry.tool_call
    @staticmethod
    def run_console_command(command: str) -> str:
        """Execute one editor / PIE console command.

        This exists for the same reason the ``valhalla.Debug*`` commands
        themselves do: PIE cannot deliver a keystroke to a particular one of
        several clients from a script, so the gate drives the game through the
        console instead. It is a different *input*, not a different code path —
        every ``valhalla.*`` command ends in the same server RPC or server-side
        write a real click would have reached.

        Read the result in the output log (``LogsToolset.GetLogEntries``); this
        returns as soon as the command has been dispatched.

        Args:
            command: The command line, e.g. ``valhalla.DebugListActors`` or
                ``valhalla.DebugTeleport 32 672 warrior``.

        Returns:
            A JSON object as text with keys ``ok`` and ``command``.
        """
        unreal.SystemLibrary.execute_console_command(None, command)
        return _ok(command=command)
