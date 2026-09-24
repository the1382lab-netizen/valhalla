"""B-19: start a NEW zone — the only thing the zone generator is still for.

`L_World`, `L_Grasslands` and `L_Desert` are hand-edited, so the from-scratch
builders (`build_world.py`, `build_grasslands.py`, `build_desert.py`) are
retired. What survives is the part that is useful for a zone that does not
exist yet: a level laid out on the same grid, with the same gameplay actors, as
the two Phase 3 zones — built with `build_zone.ZoneBuilder`, so it follows the
same conventions (zone-local coordinates with the min corner at the origin,
64 cm tiles, floors in `AValhallaTileField`s, starts tagged with the zone id).

`scaffold(zone_id, theme, size_tiles)` creates

* `/Game/Valhalla/Maps/Zones/L_<ZoneId>`: a floor of the theme's ground tiles,
  an `AValhallaZoneVolume` (id, display name, box, default spawn at the
  centre), an `AValhallaFogBounds` hugging the zone, and four `PlayerStart`s
  tagged with the zone id;
* `/Game/Valhalla/Maps/Zones/L_<ZoneId>_Gameplay`: empty, the project's home
  for hand-placed NPC Spawn Points (see `npc_setup.py`);
* `<repo>/maps/overlays-2.0/<zone_id>.json`: the four player spawns;

and registers both levels and the overlay in `maps/handedited.json` as soon as
each exists, so nothing regenerates them afterwards.

It **refuses** — creates nothing — if either level, the overlay file, or a
marker entry for any of them already exists, or if a map is unsaved (creating
a level would otherwise prompt to save it). There is no `force`.

It does **not** add the zone to `L_World`. That is a hand step in the editor:
Levels panel, add `L_<ZoneId>` and `L_<ZoneId>_Gameplay` as always-loaded
streaming sub-levels with the same offset, far from the other zones (the
Phase 3 zones sit at X = 0 and X = +40000 cm; see `build_world.py`). A
`zones.json` entry and portals to and from the new zone are hand steps too.
"""

import json
import os
import re

import unreal

from valhalla_tools import level_protection
from valhalla_tools.build_zone import FLOOR_TOP, TILE, ZoneBuilder, rnd, tile_xy

ZONES_FOLDER = "/Game/Valhalla/Maps/Zones"

_ENV = "/Game/Valhalla/Environment/{}/{{0}}/StaticMeshes/{{0}}"
_PROPS = "/Game/Valhalla/Props/{0}/StaticMeshes/{0}"

#: theme -> kits searched in order, and the ground tiles with cumulative weights.
#: A cave theme is a later backlog item: it needs its own kit first.
THEMES = {
    "grassland": {
        "kits": [_ENV.format("Grassland"), _ENV.format("Town"), _PROPS],
        "ground": [("SM_Grass_A", 0.62), ("SM_Grass_B", 0.88), ("SM_Grass_C", 1.0)],
    },
    "desert": {
        "kits": [_ENV.format("Desert"), _ENV.format("Grassland"), _PROPS],
        "ground": [("SM_Sand_A", 0.55), ("SM_Sand_B", 0.85), ("SM_Sand_C", 1.0)],
    },
    "town": {
        "kits": [_ENV.format("Town"), _ENV.format("Grassland"), _PROPS],
        "ground": [("SM_Cobble_A", 0.65), ("SM_Cobble_B", 1.0)],
    },
}

#: 8 tiles is the smallest box a party fits in; 128 (8192 cm) keeps the fog's
#: 1024-texel masks at 8 cm a texel or better.
MIN_TILES = 8
MAX_TILES = 128

ZONE_ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]{1,39}$")


def _log(message):
    unreal.log("VALHALLA_SCAFFOLD {}".format(message))


def _levels():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _current_level_path():
    level = _levels().get_current_level()
    return level.get_outer().get_path_name().split(".")[0] if level else ""


def level_paths(zone_id):
    """`(L_<ZoneId>, L_<ZoneId>_Gameplay)`: `north_woods` -> `L_NorthWoods`."""
    name = "L_" + "".join(part[:1].upper() + part[1:] for part in zone_id.split("_") if part)
    return ("{}/{}".format(ZONES_FOLDER, name), "{}/{}_Gameplay".format(ZONES_FOLDER, name))


def display_name(zone_id):
    return " ".join(part[:1].upper() + part[1:] for part in zone_id.split("_") if part)


def _level_exists(level_path):
    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        return True
    # The registry can lag a file copied in from outside the editor.
    return bool(level_protection.level_files(level_path))


def refusals(zone_id, theme, size_tiles):
    """Every reason `scaffold` would refuse, as sentences. Empty means go. Touches nothing."""
    reasons = []
    if not isinstance(zone_id, str) or not ZONE_ID_PATTERN.match(zone_id):
        return ["zone_id must be lower case letters, digits and underscores, starting "
                "with a letter (2-40 characters), e.g. 'north_woods'; got {!r}".format(zone_id)]
    if theme not in THEMES:
        reasons.append("theme must be one of {}; got {!r}".format(sorted(THEMES), theme))
    if not isinstance(size_tiles, int) or not MIN_TILES <= size_tiles <= MAX_TILES:
        reasons.append("size_tiles must be an integer from {} to {}; got {!r}".format(
            MIN_TILES, MAX_TILES, size_tiles))

    marker = level_protection.load_marker()
    for level_path in level_paths(zone_id):
        if _level_exists(level_path):
            reasons.append("level {} already exists".format(level_path))
        if level_protection.is_level_protected(level_path, marker):
            reasons.append("level {} is listed in {}".format(level_path, marker["path"]))
    overlay = level_protection.overlay_file(zone_id)
    if os.path.isfile(overlay):
        reasons.append("overlay {} already exists".format(overlay))
    if level_protection.is_overlay_protected(zone_id, marker):
        reasons.append("overlay {!r} is listed in {}".format(zone_id, marker["path"]))
    return reasons


class ScaffoldBuilder(ZoneBuilder):
    """A zone with ground, a zone volume, fog bounds and four starts. Nothing else.

    `scaffold` makes a subclass per call with the zone id, theme and size.
    """

    GROUND = []

    def build(self):
        n = self.SIZE_TILES
        for i in range(n):
            for j in range(n):
                seed = i * 131.0 + j * 17.0
                roll = rnd(seed)
                mesh_name = next(name for name, upto in self.GROUND if roll < upto)
                self.floor(mesh_name, i, j, 90.0 * int(rnd(seed + 3.0) * 4.0))
        self.flush_floors()

        centre = n // 2
        self.zone_volume(centre, centre, spawn_yaw=0.0)
        self.player_starts([(centre - 2, centre - 2), (centre + 1, centre - 2),
                            (centre - 2, centre + 1), (centre + 1, centre + 1)],
                           yaw_base=45.0)

        from valhalla_tools import build_fog
        half = n * TILE / 2.0
        self.notes["fogBounds"] = build_fog.place_fog_bounds(
            centre=(half, half, 200.0), extent=(half, half, 400.0),
            label="FogBounds_{}".format(self.ZONE_ID))
        self._tally("Fog")
        self.notes["defaultSpawnTile"] = [centre, centre]
        self.notes["centre"] = list(tile_xy(centre, centre)) + [FLOOR_TOP]
        return self.summary()


def _new_level(level_path):
    if not _levels().new_level(level_path):
        raise RuntimeError("new_level({}) returned False".format(level_path))
    current = _current_level_path()
    if current != level_path:
        raise RuntimeError("creating {} left {} open; refusing to build into the wrong "
                           "level".format(level_path, current or "<none>"))
    if not _levels().save_current_level():
        raise RuntimeError("could not save {}".format(level_path))


def scaffold(zone_id, theme="grassland", size_tiles=64):
    """Create a new zone's two levels and its overlay. See the module docstring.

    Returns:
        On refusal ``{"ok": False, "refused": [reasons], "zoneId": ...}`` with
        nothing created. Otherwise ``{"ok": True, "zoneId", "theme",
        "sizeTiles", "sizeCm", "level", "gameplayLevel", "overlay", "marker",
        "registered", "counts", "notes", "reopened", "nextSteps"}``.
    """
    reasons = refusals(zone_id, theme, size_tiles)
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    if dirty:
        reasons.append("unsaved map(s) {}: save or discard them first (creating a level "
                       "would prompt to save them)".format(", ".join(dirty)))
    if reasons:
        for reason in reasons:
            unreal.log_warning("VALHALLA_SCAFFOLD refused: " + reason)
        return {"ok": False, "zoneId": zone_id, "refused": reasons}

    level_path, gameplay_path = level_paths(zone_id)
    config = THEMES[theme]
    builder_cls = type("Scaffold_" + zone_id, (ScaffoldBuilder,), {
        "ZONE_ID": zone_id,
        "DISPLAY_NAME": display_name(zone_id),
        "KITS": config["kits"],
        "GROUND": config["ground"],
        "SIZE_TILES": size_tiles,
    })

    previous = _current_level_path()
    registered = {"levels": [], "overlays": []}
    result = {"ok": True, "zoneId": zone_id, "theme": theme, "sizeTiles": size_tiles,
              "sizeCm": size_tiles * TILE, "level": level_path,
              "gameplayLevel": gameplay_path}
    try:
        _new_level(level_path)
        registered["levels"] += level_protection.register(levels=[level_path])["added"]["levels"]
        _log("created {}".format(level_path))

        builder = builder_cls()
        summary = builder.build()
        if not _levels().save_current_level():
            raise RuntimeError("could not save {}".format(level_path))

        _new_level(gameplay_path)
        registered["levels"] += level_protection.register(
            levels=[gameplay_path])["added"]["levels"]
        _log("created {}".format(gameplay_path))

        overlay = builder.write_overlay()
        if not overlay:
            raise RuntimeError("overlay for {} was not written".format(zone_id))
        marker = level_protection.register(overlays=[zone_id])
        registered["overlays"] += marker["added"]["overlays"]

        result.update(overlay=overlay, marker=marker["path"], counts=summary["counts"],
                      notes=summary["notes"])
    except Exception:
        # Whatever was created is already registered, so it is protected and a
        # retry refuses; say what exists so it can be finished or removed by hand.
        unreal.log_error("VALHALLA_SCAFFOLD failed part-way; registered so far: {}".format(
            registered))
        raise
    finally:
        if previous and previous != _current_level_path():
            try:
                _levels().load_level(previous)
            except Exception as exc:  # noqa: BLE001
                unreal.log_warning("VALHALLA_SCAFFOLD could not reopen {}: {}".format(
                    previous, exc))

    result["registered"] = registered
    result["reopened"] = _current_level_path()
    result["nextSteps"] = (
        "Hand steps in the editor: open L_World, Levels panel, add {} and {} as "
        "always-loaded streaming sub-levels at one offset clear of the other zones "
        "(they sit at X=0 and X=+40000 cm); add a zones.json entry and portals if the "
        "zone needs them.").format(level_path.rsplit("/", 1)[-1],
                                   gameplay_path.rsplit("/", 1)[-1])
    _log("done " + json.dumps(result, default=str))
    return result
