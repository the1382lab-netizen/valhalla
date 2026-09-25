"""Shared machinery for the zone builders and for `scaffold_zone`.

`build_grasslands.py` and `build_desert.py` are the *content* (retired as
rebuild tools in B-19, kept for history); this is the vocabulary they are
written in, and `scaffold_zone.py` uses the same vocabulary to start a NEW
zone. Keeping it in one place is what makes the two
zones structurally identical — same grid, same conventions, same actor
categories — so that a reader comparing them sees only the theme.

## The grid

1.0 tiles are 64 px and 2.0 centimetres are 1:1 with 1.0 pixels
(`constants.ts:4` `TILE_SIZE = 64`), so a tile is 64 cm and a zone is
`N x N = 64 x 64` tiles = 4096 cm square, exactly as `FallbackMapGenerator`'s
defaults and every Tiled map on disk.

**Zone-local coordinates.** Tile `(i, j)` has its centre at
`(i * 64 + 32, j * 64 + 32)`, so the zone spans `[0, 4096]` on both axes with
the *minimum corner at the origin*. That is not arbitrary: it makes a
zone-local coordinate (centimetres from the zone volume's min corner, which is
what the admin API and the Live Dashboard use) numerically identical to a
position in this script, and it makes the relationship to a 1.0 pixel
coordinate the identity. A grid centred on the origin, which
`build_greybox.py` uses, would have made every such number an offset nobody
could check by eye.

A level is authored entirely in zone-local coordinates. `L_Desert` is loaded at
`+40000` cm on X by the streaming transform, and no line of `build_desert.py`
knows that.

## Actor categories, and why floors are the odd one out

Floors go into `AValhallaTileField`s — one instanced component per tile mesh,
so 4096 tiles are a handful of actors. Everything else is an individual
`AStaticMeshActor`.

That split is forced by Phase 5, not chosen for tidiness. `GatherBlockerSegments`
reads the *component* bounding box of each `VisionBlocker` it overlaps; an
instanced component has one box for the whole field, so instancing the walls
would hand the visibility polygon a single box covering the zone and the fog
would hide the entire map. Floors cannot block sight — the `VisionBlocker`
channel's default response is Ignore — so instancing them breaks nothing. The
rule is "instance what cannot block sight", and it is the same rule stated in
`AValhallaTileField`'s class comment.

## Kit conventions, measured from the meshes

* Floor tiles are 64 cm square and about 7.5 cm thick, pivot bottom-centre, so
  they sit at `z = 0` and their top surface is `FLOOR_TOP`.
* Walls are 64 long x 25 thick x 180 tall, pivot bottom-centre. A wall running
  east-west is yaw 0; one running north-south is yaw 90.
* `VB_*Wall_Corner` at yaw 0 joins an arm pointing -X to an arm pointing +Y.
* `SM_Roof_Thatch` is a 128 cm square footprint, 70 cm tall, pivot at its own
  base, ridge along X — so it goes on at `z = FLOOR_TOP + 180`, the top of the
  walls.
"""

import json
import math
import os

import unreal

# ── The grid ────────────────────────────────────────────────────────────

#: constants.ts:4 TILE_SIZE. 1.0 pixels and 2.0 centimetres are 1:1.
TILE = 64.0

#: Zone is N x N tiles, matching every 1.0 map except grasslands_v2.
N = 64

#: Zone size, cm. 4096, the same number as `MAP_WIDTH_PX`.
ZONE_CM = N * TILE

#: Top surface of a floor tile, cm. Walls and props stand on this.
FLOOR_TOP = 7.6

#: Wall height, cm. Roofs go on at FLOOR_TOP + this.
WALL_HEIGHT = 180.0

#: Half-size of a zone volume's box. Z is generous so a knocked-back player is
#: still nominally inside it; only XY is ever tested.
ZONE_EXTENT = unreal.Vector(ZONE_CM / 2.0, ZONE_CM / 2.0, 500.0)


def tile_xy(i, j):
    """Zone-local centre of tile (i, j), cm. Min corner of the zone is (0, 0)."""
    return (i * TILE + TILE / 2.0, j * TILE + TILE / 2.0)


def rnd(seed):
    """Deterministic pseudo-random in [0, 1).

    The same sine hash `build_greybox.py` uses, for the same reason: the layout
    has to be byte-identical on every rebuild or the top-down captures drift
    apart from the level they describe.
    """
    value = math.sin(seed * 12.9898) * 43758.5453
    return value - math.floor(value)


def in_rect(i, j, i0, j0, i1, j1):
    """Inclusive tile-rectangle test."""
    return i0 <= i <= i1 and j0 <= j <= j1


def ring_tiles(i0, j0, i1, j1):
    """The perimeter tiles of a rectangle, as (i, j, side) with side in NSEW.

    `side` is which edge the tile is on, which is what decides a wall's yaw and
    which way a door faces. A corner is reported once, as `"corner"`.
    """
    out = []
    for i in range(i0, i1 + 1):
        for j in range(j0, j1 + 1):
            on_x_edge = i in (i0, i1)
            on_y_edge = j in (j0, j1)
            if not (on_x_edge or on_y_edge):
                continue
            if on_x_edge and on_y_edge:
                out.append((i, j, "corner"))
            elif on_y_edge:
                out.append((i, j, "S" if j == j0 else "N"))
            else:
                out.append((i, j, "W" if i == i0 else "E"))
    return out


# ── Where the repo root (shared/, maps/) is ───────────────────────────────────────────────

#: Fallback if the settings object cannot be read. Only used with a warning.
_REPO_FALLBACK = "C:/Users/music/game-project/Valhalla 2.0"


def repo_root():
    """The 1.0 checkout, derived from `ValhallaDataSettings.DataRoot`.

    One source of truth for where the repo is: the same `DataRoot` setting the
    C++ side reads its data files from.
    """
    try:
        settings = unreal.get_default_object(unreal.ValhallaDataSettings)
        data_root = str(settings.get_resolved_data_root())
        if data_root:
            # <repo>/shared/data -> <repo>
            return os.path.dirname(os.path.dirname(data_root)).replace("\\", "/")
    except Exception as exc:  # noqa: BLE001
        unreal.log_warning("VALHALLA_ZONE could not read DataRoot ({}); using {}".format(
            exc, _REPO_FALLBACK))
    return _REPO_FALLBACK


def thumbs_dir():
    return os.path.join(repo_root(), "maps", "thumbs").replace("\\", "/")


# ── The builder ─────────────────────────────────────────────────────────


class ZoneBuilder(object):
    """Places one zone's actors into whatever level is currently open.

    A subclass supplies `ZONE_ID`, `DISPLAY_NAME`, `KIT` paths and a `build`
    that calls the helpers below. Nothing here knows anything about grass or
    sand.
    """

    #: `zones.json` key.
    ZONE_ID = ""

    #: `ZoneConfig.name`.
    DISPLAY_NAME = ""

    #: Content path template for kit meshes, e.g.
    #: ``/Game/Valhalla/Environment/Desert/{0}/StaticMeshes/{0}``. A list, so a
    #: zone can draw on several kits — the town props live in their own.
    KITS = []

    #: Zone is SIZE_TILES x SIZE_TILES tiles. The two Phase 3 zones are `N`;
    #: `scaffold_zone` makes a subclass with the size it was asked for.
    SIZE_TILES = N

    def __init__(self):
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self._mesh_cache = {}
        self._fields = {}
        self._field_instances = {}
        self._claimed = {}
        self.counts = {}
        self.notes = {}

    # ── Assets ──────────────────────────────────────────────────────────

    def mesh(self, name):
        """Load a kit mesh by bare name, trying each kit in turn."""
        if name in self._mesh_cache:
            return self._mesh_cache[name]

        for kit in self.KITS:
            asset = unreal.EditorAssetLibrary.load_asset(kit.format(name))
            if asset is not None:
                self._mesh_cache[name] = asset
                return asset

        raise RuntimeError("mesh {} is in none of {}".format(
            name, [k.format(name) for k in self.KITS]))

    # ── Bookkeeping ─────────────────────────────────────────────────────

    def _tally(self, folder, count=1):
        self.counts[folder] = self.counts.get(folder, 0) + count

    def claim(self, i, j, what):
        """Mark a tile as taken by a feature, so later passes leave it alone.

        Every pass that scatters something — trees, rocks, cactus, fences —
        consults this. It is the whole defence against the class of bug that
        shows up as a tree growing through a roof or a barrel inside a lake:
        the passes run in order of importance and each one claims what it uses.
        """
        self._claimed[(i, j)] = what

    def claimed(self, i, j):
        return self._claimed.get((i, j))

    def free(self, i, j):
        return (i, j) not in self._claimed

    def claim_rect(self, i0, j0, i1, j1, what):
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                self.claim(i, j, what)

    # ── Floors: instanced ───────────────────────────────────────────────

    def floor(self, mesh_name, i, j, yaw=0.0, scale=1.0):
        """Queue one floor tile into that mesh's instanced field."""
        x, y = tile_xy(i, j)
        transform = unreal.Transform(
            location=unreal.Vector(x, y, 0.0),
            rotation=unreal.Rotator(0.0, 0.0, yaw),
            scale=unreal.Vector(scale, scale, scale),
        )
        self._field_instances.setdefault(mesh_name, []).append(transform)

    def flush_floors(self):
        """Create one `AValhallaTileField` per mesh and hand it its instances.

        Deferred to the end so each field is one batched `AddTiles` call: 4096
        single-instance calls from Python take minutes because each one rebuilds
        the render state, and one batched call takes no measurable time.
        """
        for mesh_name in sorted(self._field_instances):
            transforms = self._field_instances[mesh_name]
            field = self.actors.spawn_actor_from_class(
                unreal.ValhallaTileField, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
            field.set_actor_label("Tiles_{}".format(mesh_name))
            field.set_folder_path("Floor")
            field.set_tile_mesh(self.mesh(mesh_name))
            field.clear_tiles()
            field.add_tiles(transforms)
            self._fields[mesh_name] = field
            self._tally("Floor")

        self.notes["floorTiles"] = {k: len(v) for k, v in sorted(self._field_instances.items())}
        return self._fields

    # ── Everything else: individual actors ──────────────────────────────

    def place(self, mesh_name, label, folder, x, y, z, yaw=0.0, scale=1.0, scale3d=None):
        actor = self.actors.spawn_actor_from_object(
            self.mesh(mesh_name), unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw))
        actor.set_actor_label(label)
        actor.set_folder_path(folder)
        if scale3d is not None:
            actor.set_actor_scale3d(scale3d)
        elif scale != 1.0:
            actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        self._tally(folder)
        return actor

    def place_tile(self, mesh_name, label, folder, i, j, yaw=0.0, z=FLOOR_TOP, **kwargs):
        x, y = tile_xy(i, j)
        return self.place(mesh_name, label, folder, x, y, z, yaw=yaw, **kwargs)

    def place_class(self, cls, label, folder, x, y, z, pitch=0.0, yaw=0.0):
        actor = self.actors.spawn_actor_from_class(
            cls, unreal.Vector(x, y, z), unreal.Rotator(0.0, pitch, yaw))
        actor.set_actor_label(label)
        actor.set_folder_path(folder)
        self._tally(folder)
        return actor

    # ── Walls ───────────────────────────────────────────────────────────

    def wall_ring(self, straight, corner, i0, j0, i1, j1, gaps=(), folder="Walls",
                  prefix="Wall", end=None, doors=None, windows=None):
        """A rectangular wall ring, with named tiles left open or swapped out.

        `gaps` is a set of `(i, j)` tiles to leave empty. `end` names the
        `VB_*_End` mesh, used to cap the wall either side of a gap so a broken
        ring reads as deliberate ruin rather than as missing geometry.
        `doors` / `windows` swap the mesh at named tiles.
        """
        gaps = set(gaps)
        doors = set(doors or ())
        windows = set(windows or ())
        placed = 0

        yaw_for = {"S": 0.0, "N": 0.0, "W": 90.0, "E": 90.0}
        corner_yaw = {(i0, j0): 270.0, (i0, j1): 180.0, (i1, j0): 0.0, (i1, j1): 90.0}

        for i, j, side in ring_tiles(i0, j0, i1, j1):
            self.claim(i, j, "wall")
            if (i, j) in gaps:
                continue

            if side == "corner":
                self.place_tile(corner, "{}_C_{:02d}_{:02d}".format(prefix, i, j), folder,
                                i, j, yaw=corner_yaw.get((i, j), 0.0))
                placed += 1
                continue

            yaw = yaw_for[side]

            if (i, j) in doors:
                mesh_name = self._door_mesh
            elif (i, j) in windows:
                mesh_name = self._window_mesh
            else:
                mesh_name = straight

            # Cap a run that stops next to a gap.
            if end is not None and self._next_to_gap(i, j, side, gaps, i0, j0, i1, j1):
                mesh_name = end

            self.place_tile(mesh_name, "{}_{}_{:02d}_{:02d}".format(prefix, side, i, j),
                            folder, i, j, yaw=yaw)
            placed += 1

        return placed

    #: Overridden by a zone that has doors and windows in its kit.
    _door_mesh = None
    _window_mesh = None

    @staticmethod
    def _next_to_gap(i, j, side, gaps, i0, j0, i1, j1):
        if side in ("S", "N"):
            neighbours = [(i - 1, j), (i + 1, j)]
        else:
            neighbours = [(i, j - 1), (i, j + 1)]
        return any(n in gaps for n in neighbours)

    # ── Gameplay actors ─────────────────────────────────────────────────

    def zone_volume(self, spawn_i, spawn_j, spawn_yaw=0.0):
        """The zone's box and default spawn.

        Placed so the box's *minimum* Z is `FLOOR_TOP`, which is the contract
        `AValhallaZoneVolume::Extent` documents and the admin API relies on: a
        zone-local point's Z comes from `Bounds.Min.Z`, so a tile at zone-local
        (0, 0) lands on the floor rather than metres under it.
        """
        zone_cm = self.SIZE_TILES * TILE
        extent = unreal.Vector(zone_cm / 2.0, zone_cm / 2.0, ZONE_EXTENT.z)
        volume = self.place_class(
            unreal.ValhallaZoneVolume, "ZoneVolume_{}".format(self.ZONE_ID), "Zone",
            zone_cm / 2.0, zone_cm / 2.0, FLOOR_TOP + extent.z)
        volume.set_editor_property("zone_id", self.ZONE_ID)
        volume.set_editor_property("display_name", self.DISPLAY_NAME)
        volume.set_editor_property("extent", extent)

        # And onto the box directly. `OnConstruction` does the same thing, but
        # it is not guaranteed to re-run after a `set_editor_property` from
        # Python, and a zone volume whose box is still the class default is a
        # zone that quietly covers a quarter of what it should.
        volume.get_editor_property("box").set_box_extent(extent, False)

        # The default spawn arrow is a child component, so it is positioned
        # relative to the box — hence the subtraction of the box's own centre.
        spawn_x, spawn_y = tile_xy(spawn_i, spawn_j)
        arrow = volume.get_editor_property("default_spawn")
        arrow.set_relative_location_and_rotation(
            unreal.Vector(spawn_x - zone_cm / 2.0,
                          spawn_y - zone_cm / 2.0,
                          FLOOR_TOP - (FLOOR_TOP + extent.z)),
            unreal.Rotator(0.0, 0.0, spawn_yaw), False, False)

        self.notes["defaultSpawn"] = [spawn_x, spawn_y, FLOOR_TOP]
        return volume

    def player_starts(self, tiles, yaw_base=0.0):
        """Four `PlayerStart`s, tagged with the zone id.

        The tag is what `AValhallaGameMode::ChoosePlayerStart` filters on, and
        it is the only thing keeping the two zones' spawn points apart in one
        world. Untagged starts would mean a grasslands login could land in the
        desert, which is exactly the sort of bug that only shows up when a
        second zone is added.
        """
        for index, (i, j) in enumerate(tiles):
            x, y = tile_xy(i, j)
            start = self.place_class(
                unreal.PlayerStart, "PlayerStart_{}_{}".format(self.ZONE_ID, index),
                "Gameplay", x, y, FLOOR_TOP + 100.0, yaw=yaw_base + index * 90.0)
            start.set_editor_property("player_start_tag", self.ZONE_ID)

    def portal(self, i, j, target_zone, target_entry, label, yaw=0.0):
        x, y = tile_xy(i, j)
        actor = self.place_class(
            unreal.ValhallaPortal, "Portal_to_{}".format(target_zone), "Gameplay",
            x, y, FLOOR_TOP, yaw=yaw)
        actor.set_editor_property("target_zone_id", target_zone)
        actor.set_editor_property("target_entry_id", target_entry)

        # `AValhallaPortal::OnConstruction` assigns the marker mesh, but see
        # the note in `zone_volume`: belt and braces, because a portal with no
        # mesh is an invisible portal, which is the single most confusing thing
        # a zone can contain.
        marker = actor.get_editor_property("marker")
        if marker.get_editor_property("static_mesh") is None:
            marker.set_editor_property(
                "static_mesh",
                unreal.EditorAssetLibrary.load_asset(
                    "/Game/Valhalla/Props/SM_PortalMarker/StaticMeshes/SM_PortalMarker"))

        self.notes["portal"] = [x, y, target_zone, target_entry]
        return actor

    def zone_entry(self, i, j, entry_id, from_zone, label, yaw=0.0):
        x, y = tile_xy(i, j)
        actor = self.place_class(
            unreal.ValhallaZoneEntry, "Entry_{}".format(entry_id), "Gameplay",
            x, y, FLOOR_TOP, yaw=yaw)
        actor.set_editor_property("entry_id", entry_id)
        actor.set_editor_property("from_zone_id", from_zone)

        self.notes["entry"] = [x, y, entry_id, from_zone]
        return actor

    def enemy_spawn(self, spawn_id, i, j, template_id, count, radius, label):
        """Kept so the zone builders still read as a description of the zone.

        NPCs are placed in Unreal: every NPC comes from an NPC Spawn Point
        (`AValhallaNPCSpawner`) placed in the zone's *gameplay* sublevel,
        `L_<Zone>_Gameplay`, which this builder never touches — so rebuilding
        the zone keeps every hand-placed spawn. This records the intent in the
        build notes and places nothing.
        """
        self.notes.setdefault("legacySpawns", []).append(
            [spawn_id, "enemy_spawn", template_id, count, radius, label])

    def npc_spawn(self, spawn_id, i, j, template_id, label):
        """See `enemy_spawn`: recorded in the notes, placed in Unreal instead."""
        self.notes.setdefault("legacySpawns", []).append(
            [spawn_id, "npc_spawn", template_id, 1, 0, label])

    # ── Lighting is the persistent level's job ──────────────────────────
    #
    # A sublevel that brought its own sun would give L_World two of them, and
    # two directional lights is a bug that presents as "the shadows are wrong"
    # rather than as an error. `build_world.py` owns all of it.

    # ── Report ──────────────────────────────────────────────────────────

    def summary(self):
        return {
            "zoneId": self.ZONE_ID,
            "counts": dict(sorted(self.counts.items())),
            "notes": self.notes,
        }


# ── B-05 / B-19: the guarded entry point ────────────────────────────────


def _current_level_path():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).get_current_level()
    return level.get_outer().get_path_name().split(".")[0] if level else ""


def zone_volume_exists(zone_id):
    """True when an `AValhallaZoneVolume` with this id is in the loaded levels.

    The levels are the only record of which zones exist (the overlay JSON that
    once also said so is retired), so this is what "already exists" means.
    """
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    for actor in actors:
        if isinstance(actor, unreal.ValhallaZoneVolume):
            try:
                if str(actor.get_editor_property("zone_id")) == zone_id:
                    return True
            except Exception:  # noqa: BLE001
                continue
    return False


def run_builder(builder_cls, target_level, zone_id=None):
    """Run a retired zone builder into the open level — only as a NEW zone.

    What `build_grasslands.build()` / `build_desert.build()` call. B-19: these
    scripts never regenerate their own zone any more, so this refuses (places
    nothing, writes nothing, returns ``{"ok": False, "refused": ...}``) when

    * the open level is the script's own target (`target_level`), or any level
      listed in `maps/handedited.json` — unconditionally, there is no `force`;
    * `zone_id` is missing, is the builder's original zone id, or already has
      a zone volume in the loaded levels.

    Otherwise it rebuilds the historical layout into the open (new, empty)
    level under `zone_id`. That is the
    "recover an old layout" path: create an empty level by hand, open it, and
    run ``build_grasslands.build(zone_id="grasslands_v1")``. Portals still
    point at the original zones; fix them by hand. Prefer
    `ValhallaLevelTools.scaffold_zone` for a genuinely new zone.
    """
    from valhalla_tools import level_protection

    marker = level_protection.load_marker()
    current = _current_level_path()
    original = builder_cls.ZONE_ID
    target_level = target_level.split(".")[0]

    def refuse(message, **refused):
        unreal.log_warning("VALHALLA_ZONE " + message)
        return {"ok": False, "zoneId": zone_id or original, "refused": refused,
                "message": message}

    if not current:
        return refuse("refused: no level is open", levels=[])
    if current == target_level or level_protection.is_level_protected(current, marker):
        return refuse(level_protection.refusal_message(levels=[current], marker=marker),
                      levels=[current])
    if not zone_id or zone_id == original:
        return refuse("refused: {} is retired (B-19) and only builds its layout as a NEW "
                      "zone id, e.g. build(zone_id=\"{}_v1\"); use "
                      "ValhallaLevelTools.scaffold_zone for a new zone".format(
                          builder_cls.__name__, original), zones=[original])
    if zone_volume_exists(zone_id):
        return refuse("refused: a zone volume for {!r} already exists in the loaded "
                      "levels".format(zone_id), zones=[zone_id])

    renamed = type(builder_cls.__name__ + "_" + zone_id, (builder_cls,), {"ZONE_ID": zone_id})
    return renamed().build()
