"""Build the whole Phase 3 world: two zone sublevels and the level that hosts them.

Run from the editor console:

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/build_world.py"

Replaces `L_World`, `L_Grasslands` and `L_Desert` in that order, and writes both
zones' overlay files as a side effect of building them. Anything unsaved in
whatever level is open is lost, so save first.

## The structure, and why it is this one

    /Game/Valhalla/Maps/L_World                 persistent: lighting only
      +- Zones/L_Grasslands   always loaded, transform identity
      +- Zones/L_Desert       always loaded, transform (+40000, 0, 0)

`L_World` holds the sun, the sky, the atmosphere, the height fog and one
unbound `PostProcessVolume`, and no geometry at all. The two zones are
**always-loaded** streaming sublevels — not dynamically streamed, and not
sub-worlds of separate server processes.

That is 1.0's architecture, restated. 1.0 ran *one* Colyseus room holding every
player and swapped the tile map under them; `player.zoneId` said which map you
were on, and everything that had to be zone-aware — `general` chat, the party
XP split, `checkZoneTransitions` — compared that string. One process, one clock,
every player reachable from every other.

Always-loaded streaming reproduces exactly that: one server, one
`AValhallaGameState`, one `ServerFixedTick`, both zones resident from the first
frame. A party member in the desert is in the same `PlayerArray` as one in the
grasslands, so the XP split keeps working without a message bus; an NPC in the
desert keeps ticking while every player is in the grasslands, as 1.0's did; and
a zone change costs a teleport rather than a level load, which is why it is a
frame rather than a loading screen.

The rejected alternatives, and why:

* **Dynamic streaming keyed on the local player.** Would unload the desert
  while everyone is in the grasslands, which sounds like a saving until the
  server — which has no local player — has to decide what "the local player"
  means. It would also make an NPC's existence depend on who is looking.
* **One level per zone, travelled between with `ServerTravel`.** Every player
  changes level together in UE, so this is a zone change for everybody at
  once. It is not even wrong for a single-player game; it is unusable here.
* **A server process per zone.** Parties, whispers and the friends list all
  span zones, so this buys a cross-process bus to reproduce what one
  `TArray<APlayerState*>` already does.

## The 40000 cm offset

Each zone is 4096 cm square, so the zones could sit 5000 cm apart and never
touch. 40000 is deliberately far more than they need, for three reasons: it
keeps the *gap* between zones unambiguously nobody's — `GetZoneAt` returns null
there, and `Valhalla.Game.Zones.LookupByPoint` asserts it — it puts the zones
beyond each other's 1800 cm maximum vision range plus any plausible growth, and
it makes a stray world coordinate instantly attributable to a zone by eye. The
offset is applied by the *streaming transform*, so `build_desert.py` authors
`L_Desert` around its own origin and no line of it knows where the level ends
up. That is also what makes `AValhallaZoneVolume` the only honest source of
zone bounds: the box is a level actor, so it arrives already offset.
"""

import unreal

from valhalla_tools import build_desert, build_grasslands
from valhalla_tools.build_zone import ZONE_CM

WORLD_PATH = "/Game/Valhalla/Maps/L_World"

#: `(level path, world offset)`. The grasslands is the default zone, so it is
#: the one at the origin: a world coordinate under 4096 is in the zone a new
#: player is standing in, which makes a log line readable without arithmetic.
SUBLEVELS = [
    ("/Game/Valhalla/Maps/Zones/L_Grasslands", unreal.Vector(0.0, 0.0, 0.0)),
    ("/Game/Valhalla/Maps/Zones/L_Desert", unreal.Vector(40000.0, 0.0, 0.0)),
]

#: Hand-authored gameplay content per zone — today the NPC Spawn Points — at the
#: same offset as the zone it belongs to. These are created once, empty, and
#: **never cleared or rebuilt** by this script: they are where a designer works
#: in the Unreal editor, so a zone rebuild must not cost them anything.
GAMEPLAY_SUBLEVELS = [
    ("/Game/Valhalla/Maps/Zones/L_Grasslands_Gameplay", unreal.Vector(0.0, 0.0, 0.0)),
    ("/Game/Valhalla/Maps/Zones/L_Desert_Gameplay", unreal.Vector(40000.0, 0.0, 0.0)),
]


def ensure_gameplay_levels():
    """Create any missing gameplay sublevel, empty. Never touches an existing one.

    Leaves the editor on whatever level `new_level` opened, so call it before
    opening `L_World`.
    """
    created = []
    for level_path, _offset in GAMEPLAY_SUBLEVELS:
        if unreal.EditorAssetLibrary.does_asset_exist(level_path):
            continue
        if not _levels().new_level(level_path):
            raise RuntimeError("new_level({}) returned False".format(level_path))
        if not _levels().save_current_level():
            raise RuntimeError("could not save {}".format(level_path))
        created.append(level_path)
        _log("created empty gameplay sublevel {}".format(level_path))
    return created

#: The toon outline. Authored by `build_toon.py`.
OUTLINE_MATERIAL = "/Game/Valhalla/Materials/PP_Outline"

#: B-15 Wave 0: build the remaster lighting and post (no outline).
REMASTER_LOOK = True


def _log(message):
    unreal.log("VALHALLA_WORLD {}".format(message))


def _levels():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


# ── The two zone sublevels ──────────────────────────────────────────────


#: Actors a level cannot be without, skipped when clearing one.
#:
#: Deliberately short, and `unreal.Brush` is deliberately **not** in it. The
#: obvious way to write this list is "WorldSettings, Brush, AbstractNavData",
#: because the default builder brush is an `ABrush` and cannot be destroyed —
#: but every *volume* is also an `ABrush`, `APostProcessVolume` included, so
#: skipping `Brush` silently exempts the global post-process volume from being
#: cleared. It then survives every rebuild and the level accumulates one more
#: each time; three of them were in `L_World` before this was noticed, and
#: three unbound post-process volumes at the same priority is a coin toss over
#: which one wins.
#:
#: The builder brush is handled instead by `destroy_actor` simply returning
#: False for it, which is what it does, and costs a line in nobody's log.
_UNDELETABLE = (
    unreal.WorldSettings,
    unreal.AbstractNavData,
)


def _make_persistent_level_current():
    """Put the editor's "current level" back to the persistent one.

    `add_level_to_world` makes the level it just added current, so anything
    spawned afterwards lands in the desert rather than in `L_World`. Nothing in
    this script does spawn afterwards — the lighting goes in before the
    sublevels are attached, deliberately — but the current level is *saved* in
    the editor's state, and leaving the desert current means the next person to
    drag an actor into `L_World` puts it in the desert instead.

    Best-effort, and it has to be: `EditorLevelUtils.MakeLevelCurrent` takes a
    `ULevelStreaming`, so it cannot name the persistent level at all, and
    `UWorld::PersistentLevel` is not a UPROPERTY so Python cannot reach it
    either. `SetCurrentLevelByName` is the one route that works, and it takes
    the level's short name.
    """
    try:
        if _levels().set_current_level_by_name(WORLD_PATH.rsplit("/", 1)[-1]):
            return True
    except Exception as exc:  # noqa: BLE001
        unreal.log_warning("VALHALLA_WORLD could not restore the current level: {}".format(exc))
        return False

    unreal.log_warning(
        "VALHALLA_WORLD could not restore the current level; whatever was added "
        "last is still current. Harmless for this build, untidy for the next edit.")
    return False


def _open_empty_level(level_path):
    """Open `level_path` empty, whether or not it already exists.

    `ULevelEditorSubsystem::NewLevel` **refuses a path that already holds a
    level** and returns False rather than overwriting it, so "rebuild from
    scratch" cannot be a single call. Deleting the asset first is not an answer
    either: `delete_asset` on a `.umap` only marks the package for deletion, so
    the file is still on disk, `does_asset_exist` still says yes, and
    `new_level` still refuses.

    So: create it if it is new, and otherwise open it and empty it. Emptying
    means detaching every streaming sublevel *first* — `get_all_level_actors`
    reaches into loaded sublevels, and clearing `L_World` without detaching
    would delete the contents of both zones — and then destroying everything
    that can be destroyed.

    Then it **confirms the editor really opened it**, which is the part that
    matters most. A silent failure here does not error, it just means the next
    `spawn_actor_from_class` goes into whatever level *was* open — which is
    exactly how an earlier run of this script put `L_World`'s sun, sky, fog and
    post-process volume inside `L_Desert`. Nothing errored; the shadows were
    just wrong.
    """
    existed = unreal.EditorAssetLibrary.does_asset_exist(level_path)

    if not existed:
        if not _levels().new_level(level_path):
            raise RuntimeError("new_level({}) returned False".format(level_path))
    else:
        if not _levels().load_level(level_path):
            raise RuntimeError("load_level({}) returned False".format(level_path))

        world = unreal.EditorLevelLibrary.get_editor_world()

        # Sublevels before actors. See the docstring.
        detached = 0
        for level in unreal.EditorLevelUtils.get_levels(world)[1:]:
            unreal.EditorLevelUtils.remove_level_from_world(level)
            detached += 1
        if detached:
            _levels().set_current_level_by_name(level_path.rsplit("/", 1)[-1])

        actors = _actors()
        destroyed = 0
        for actor in actors.get_all_level_actors():
            if isinstance(actor, _UNDELETABLE):
                continue
            if actors.destroy_actor(actor):
                destroyed += 1

        _log("emptied existing {}: {} sublevel(s) detached, {} actor(s) destroyed".format(
            level_path, detached, destroyed))

    current = _levels().get_current_level()
    current_path = current.get_outer().get_path_name() if current else "<none>"
    if not current_path.startswith(level_path):
        raise RuntimeError(
            "opening {} left {} current; refusing to build into the wrong level".format(
                level_path, current_path))

    return current_path


def build_zone_level(level_path, builder_module):
    """Create one zone sublevel from scratch and run its builder into it."""
    _log("building {}".format(level_path))

    _open_empty_level(level_path)
    result = builder_module.build()

    if not _levels().save_current_level():
        raise RuntimeError("could not save {}".format(level_path))

    _log("{} saved: {}".format(level_path, result["counts"]))
    return result


# ── The persistent level ────────────────────────────────────────────────


def build_lighting():
    """Everything that lights the world, once, in the persistent level.

    In the persistent level rather than in the sublevels because two
    always-loaded sublevels each carrying a sun would give `L_World` two
    directional lights — which does not error, it just makes the shadows wrong
    in a way nobody traces back to a duplicated actor.
    """
    actors = _actors()

    # The greybox's angle, kept: pitch -50 and yaw -35 is what puts the long
    # shadows across the tiles that make the flat-shaded kit read as 3D at the
    # isometric camera angle Phase 4 settled on.
    sun = actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 2000.0),
        unreal.Rotator(0.0, -55.0, -35.0))
    sun.set_actor_label("Sun")
    sun.set_folder_path("Lighting")

    sky_light = actors.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 1800.0), unreal.Rotator())
    sky_light.set_actor_label("SkyLight")
    sky_light.set_folder_path("Lighting")

    # Real-time capture, and this is the line that makes the world legible.
    #
    # A `SkyLight` spawned into a fresh level captures nothing: its default
    # source is the scene, and the scene has not been captured, so it
    # contributes zero. With a directional light and nothing else, every
    # surface the sun does not reach is *pure black* — which in a top-down
    # view of a town means four houses' worth of hard black quadrilateral
    # where the roofs' shadows fall, and the well and the market stalls
    # invisible inside them.
    #
    # `bRealTimeCapture` captures the `SkyAtmosphere` above instead, every
    # frame, so shadowed ground is lit by the sky rather than by nothing. It
    # is also what makes the flat toon colours read as one palette: the
    # ambient term is the sky's blue, not black.
    sky_component = sky_light.get_editor_property("light_component")
    sky_component.set_editor_property("real_time_capture", True)
    sky_component.set_editor_property("intensity", 1.5)

    # A second directional light, shadowless, from the opposite azimuth.
    #
    # The sky light alone is not enough, and the capture proves it: with the
    # sun and a real-time sky light, every pixel in shadow in the top-down
    # capture of the town measured exactly (0, 0, 0). A real-time sky capture
    # is a reflection probe — it lands in the frame eventually and does not
    # necessarily land in a one-shot `SceneCapture2D` at all — so relying on
    # it for the *ambient* term makes the thumbnails the Phase 6 editor
    # depends on look like a lighting bug.
    #
    # A fill light is the standard fix for a stylised flat-shaded look and it
    # is deterministic: no capture, no probe, identical in the editor
    # viewport, in a scene capture and in PIE. Shadows off, because a second
    # shadow-caster would give every wall two shadows in different directions
    # and destroy exactly the readability this is here to buy. Dim and
    # slightly blue, so a shadowed cobble reads as shadowed cobble rather than
    # as a hole in the floor.
    fill = actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 2000.0),
        unreal.Rotator(0.0, -35.0, 145.0))
    fill.set_actor_label("Fill")
    fill.set_folder_path("Lighting")

    fill_component = fill.get_editor_property("light_component")
    # 5 lux against the sun's 10: measured, not guessed. At 2 lux a shadowed
    # cobble read (1, 2, 13) against a lit (114, 115, 115) — still a hole. At 5
    # it lands around a third of the lit value, which is the ratio that reads
    # as "in shadow" rather than as "missing".
    fill_component.set_editor_property("intensity", 5.0)
    fill_component.set_editor_property("cast_shadows", False)
    fill_component.set_editor_property("light_color", unreal.Color(r=175, g=200, b=255, a=255))

    # Two directional lights in one world make the renderer print
    # "Multiple directional lights are competing to be the single one used for
    # forward shading..." across the viewport — which is not a problem with the
    # lighting, but it *is* a problem with every screenshot taken of it. Saying
    # explicitly which light wins silences it, and picks the right winner: the
    # sun, not the fill.
    sun_component = sun.get_editor_property("light_component")
    sun_component.set_editor_property("forward_shading_priority", 1)
    fill_component.set_editor_property("forward_shading_priority", 0)

    atmosphere = actors.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    atmosphere.set_actor_label("SkyAtmosphere")
    atmosphere.set_folder_path("Lighting")

    fog = actors.spawn_actor_from_class(
        unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, 200.0), unreal.Rotator())
    fog.set_actor_label("HeightFog")
    fog.set_folder_path("Lighting")

    # B-15 Wave 0: the remaster look overrides the Phase 4 numbers above (warm
    # sun, Lumen-lit shadows with a faint fill, haze). One source of truth, so
    # a rebuild and the in-place Wave 0 edit of L_World agree.
    from . import lighting_remaster
    lighting_remaster.configure_sun(sun)
    lighting_remaster.configure_sky_light(sky_light)
    lighting_remaster.configure_fill(fill)
    lighting_remaster.configure_fog(fog)

    return [sun, sky_light, atmosphere, fog]


def build_post_process():
    """One unbound `PostProcessVolume` carrying `PP_Outline`.

    `PP_Outline` only. `AValhallaPlayerController::ApplyOutlinePostProcess`
    already puts it on the pawn's camera at runtime, so this volume is not what
    makes the game look right — it is what makes the *editor viewport* look
    right, which matters because every screenshot Phase 3 is judged on is taken
    from the editor viewport with no pawn in the world.

    `PP_Fog` is deliberately **not** here, and that is not an omission.
    `AValhallaFogRenderer` creates it as a dynamic instance and binds two live
    render targets and the current zone's bounds to it; a copy in a level
    volume would have no masks bound, so it would read black and dim the whole
    world to 0.05 — including in the editor, where there is no pawn to explore
    anything. Fog is per-viewer state and belongs on the viewer's camera.
    """
    volume = _actors().spawn_actor_from_class(
        unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    volume.set_actor_label("GlobalPostProcess")
    volume.set_folder_path("Lighting")

    # Unbound: applies everywhere rather than inside a box, which is what
    # "global" has to mean when the world is 44000 cm wide.
    volume.set_editor_property("unbound", True)
    volume.set_editor_property("priority", 0.0)

    # B-15 Wave 0 retired the outline: the volume now carries the remaster's
    # exposure and grade instead. The Phase 4c path below is kept for
    # reference and only runs if REMASTER_LOOK is switched off.
    if REMASTER_LOOK:
        from . import lighting_remaster
        lighting_remaster.configure_post(volume)
        return volume

    outline = unreal.EditorAssetLibrary.load_asset(OUTLINE_MATERIAL)
    if outline is None:
        unreal.log_warning(
            "VALHALLA_WORLD {} is missing; run build_toon first. "
            "Editor screenshots will have no outline.".format(OUTLINE_MATERIAL))
        return volume

    # `FWeightedBlendables` wraps a single `TArray` whose UPROPERTY is literally
    # named `Array`, so it is `array` here — not `blendables`, which is what the
    # struct is called from the outside.
    blendables = unreal.WeightedBlendables()
    blendables.set_editor_property(
        "array", [unreal.WeightedBlendable(weight=1.0, object=outline)])

    settings = volume.get_editor_property("settings")
    settings.set_editor_property("weighted_blendables", blendables)
    volume.set_editor_property("settings", settings)

    return volume


def attach_sublevels():
    """Add both zones, and their gameplay sublevels, as always-loaded streaming sublevels at their offsets."""
    world = unreal.EditorLevelLibrary.get_editor_world()
    added = []

    for level_path, offset in SUBLEVELS + GAMEPLAY_SUBLEVELS:
        transform = unreal.Transform(location=offset,
                                     rotation=unreal.Rotator(),
                                     scale=unreal.Vector(1.0, 1.0, 1.0))

        streaming = unreal.EditorLevelUtils.add_level_to_world_with_transform(
            world, level_path, unreal.LevelStreamingAlwaysLoaded, transform)

        if streaming is None:
            raise RuntimeError("could not add {} to {}".format(level_path, WORLD_PATH))

        # `add_level_to_world_with_transform` applies the transform, but set it
        # again explicitly: this is the one number the whole zone layout
        # depends on and it is worth being able to read it off the asset.
        streaming.set_editor_property("level_transform", transform)
        streaming.set_editor_property("should_be_loaded", True)
        streaming.set_editor_property("should_be_visible", True)

        added.append((level_path, [offset.x, offset.y, offset.z]))
        _log("attached {} at ({:.0f}, {:.0f}, {:.0f})".format(
            level_path, offset.x, offset.y, offset.z))

    _make_persistent_level_current()

    return added


def build_world():
    _log("building {}".format(WORLD_PATH))

    _open_empty_level(WORLD_PATH)

    build_lighting()
    build_post_process()
    added = attach_sublevels()

    _levels().save_all_dirty_levels()
    _log("{} saved with {} sublevel(s)".format(WORLD_PATH, len(added)))

    return {"world": WORLD_PATH, "sublevels": added, "zoneSizeCm": ZONE_CM}


# ── Everything ──────────────────────────────────────────────────────────


#: Somewhere to park the editor before rebuilding.
#:
#: Necessary, not tidiness. `EditorStartupMap` is `L_World`, so a freshly opened
#: editor has `L_Grasslands` and `L_Desert` *loaded* — they are always-loaded
#: streaming sublevels, that is the whole design — and `new_level` on a level
#: that is currently loaded returns False. So the first thing a rebuild has to
#: do is open a level that has no sublevels at all. The greybox is the obvious
#: choice: it is a fixture, nothing here touches it, and it is small.
PARK_LEVEL = "/Game/Valhalla/Maps/L_GreyBox"


def build_all():
    """The sublevels first, then the level that references them.

    Order matters: `add_level_to_world_with_transform` needs the sublevel asset
    to exist, and building a zone *after* attaching it would mean opening the
    sublevel on its own, which detaches it from the editor's idea of the world
    and is the usual way a streaming setup ends up with an empty sublevel.
    """
    # Get off `L_World` first, or its two always-loaded sublevels are the two
    # levels this is about to replace. See `PARK_LEVEL`.
    _levels().load_level(PARK_LEVEL)
    _log("parked on {}".format(PARK_LEVEL))

    # And nothing may be dirty, because `NewLevel` offers to save before it
    # overwrites and an unattended script declines that prompt, failing the
    # whole call. So commit anything outstanding first — and with
    # `EditorLoadingAndSavingUtils`, not `save_all_dirty_levels`, which only
    # reaches the levels of the world that happens to be open and would
    # therefore silently skip the very level a failed earlier run left dirty.
    #
    # Saving rather than refusing is safe here because all three levels this
    # touches are generated and about to be overwritten; the only thing being
    # committed is work this run is going to replace.
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    if dirty:
        _log("committing {} unsaved level(s) before rebuilding: {}".format(
            len(dirty), ", ".join(dirty)))
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(
            save_map_packages=True, save_content_packages=False)

        still_dirty = [p.get_name()
                       for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
        if still_dirty:
            raise RuntimeError(
                "could not save {}; `NewLevel` will refuse to overwrite while they "
                "are dirty. Restart the editor and run this again.".format(
                    ", ".join(still_dirty)))

    result = {
        "grasslands": build_zone_level(build_grasslands.LEVEL_PATH, build_grasslands),
        "desert": build_zone_level(build_desert.LEVEL_PATH, build_desert),
    }
    result["gameplayLevelsCreated"] = ensure_gameplay_levels()
    result["world"] = build_world()

    _log("VALHALLA_WORLD_DONE " + str(result))
    return result
