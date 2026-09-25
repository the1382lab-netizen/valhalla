"""Level and material tools for the Valhalla 2.0 port, exposed to Unreal MCP.

The generic MCP toolsets can place actors into a level and can save assets, but
they cannot *create* a level or author a material, both of which Phase 2a needs
and both of which are one call to an editor subsystem.

B-19: no tool here regenerates an existing zone. ``L_World``, ``L_Grasslands``
and ``L_Desert`` are hand-edited (``<repo>/maps/handedited.json``); the old
``build_world_levels`` is gone and the scripts behind it refuse their target
levels. New zones start from ``scaffold_zone``.

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
    """Level creation and saving, new-zone scaffolding, and simple material authoring.

    B-19: nothing here rebuilds an existing zone. Levels listed in
    ``<repo>/maps/handedited.json`` (``L_World``, ``L_Grasslands``,
    ``L_Desert``, their ``_Gameplay`` sub-levels, and every zone
    ``scaffold_zone`` has made) are
    hand-edited: edit them in place in the editor. ``scaffold_zone`` makes a
    NEW zone and refuses anything that exists. Old layouts: git history for
    the ``.umap``, ``Valhalla2/Saved/LevelBackups/``, or a retired script
    (``build_grasslands.build(zone_id=...)``) run into a new level.
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
    def scaffold_zone(zone_id: str, theme: str = "grassland", size_tiles: int = 64) -> str:
        """Create a NEW zone: its sub-levels, zone volume, fog bounds and player starts.

        For new zones only (B-19). It never touches an existing level: it
        REFUSES, creating nothing, if ``/Game/Valhalla/Maps/Zones/L_<ZoneId>``
        or ``L_<ZoneId>_Gameplay`` exists, if a zone volume with this id is
        in the loaded levels, if ``maps/handedited.json`` lists either level,
        or if any map has unsaved
        changes. There is no force option. Existing zones (``L_Grasslands``,
        ``L_Desert``, ``L_World``) are edited by hand in the editor.

        Creates, with ``build_zone.ZoneBuilder`` (same grid and conventions as
        the Phase 3 zones; zone-local coordinates, min corner at the origin,
        64 cm tiles):

        * ``L_<ZoneId>`` (``north_woods`` -> ``L_NorthWoods``): a floor of the
          theme's ground tiles, an ``AValhallaZoneVolume`` with the zone id and
          a default spawn at the centre, an ``AValhallaFogBounds`` hugging the
          zone, four ``PlayerStart`` tagged with the zone id;
        * ``L_<ZoneId>_Gameplay``: empty, for hand-placed NPC Spawn Points.

        Portals, zone entries and player starts exist only as actors in these
        levels; there is no overlay JSON (retired).

        Each is added to ``maps/handedited.json`` as soon as it exists, so it is
        protected from then on (a second call refuses). The editor reopens the
        level that was open before; that level is not modified or saved.

        It does NOT add the zone to ``L_World``. Wiring a new zone into
        ``L_World`` is a hand step in the editor: open ``L_World``, Levels
        panel, add ``L_<ZoneId>`` and ``L_<ZoneId>_Gameplay`` as always-loaded
        streaming sub-levels at one offset clear of the other zones
        (grasslands X=0, desert X=+40000 cm), then save. A ``zones.json`` entry
        and portals are hand steps too. See ``valhalla_tools/scaffold_zone.py``.

        Args:
            zone_id: New zone id: lower case letters, digits, underscores,
                starting with a letter, e.g. ``north_woods``.
            theme: Kit for the ground: ``grassland``, ``desert`` or ``town``
                (cave comes later).
            size_tiles: Zone is ``size_tiles`` x ``size_tiles`` tiles of 64 cm,
                8 to 160. The Phase 3 zones are 64; Eldmoor (B-06) is 143.

        Returns:
            A JSON object as text. Refused: ``ok`` false and ``refused`` (the
            reasons); nothing was created. Created: ``ok``, ``zoneId``,
            ``level``, ``gameplayLevel``, ``marker``,
            ``registered``, ``counts`` (actors per outliner folder), ``notes``,
            ``reopened`` and ``nextSteps``.
        """
        from valhalla_tools import scaffold_zone as scaffold

        try:
            result = scaffold.scaffold(zone_id, theme, int(size_tiles))
        except Exception as exc:  # noqa: BLE001 - reported, never raised at MCP
            import traceback
            return _fail(str(exc), traceback=traceback.format_exc().splitlines()[-12:])
        if not result.get("ok"):
            return _fail("refused; nothing was created", **result)
        result.pop("ok", None)
        return _ok(**result)

    @toolset_registry.tool_call
    @staticmethod
    def capture_zone_topdown(zone_id: str, out_dir: str = "") -> str:
        """Orthographic top-down PNG of a zone, plus the metadata for its pixels.

        Writes ``<out_dir>/<zone_id>.png`` at 2048 x 2048 and
        ``<out_dir>/<zone_id>.json`` holding ``originX``, ``originY``,
        ``sizeX``, ``sizeY`` and ``pixelsPerCm`` — everything the editor's
        Live Dashboard needs to draw the picture under zone-local
        coordinates.

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
