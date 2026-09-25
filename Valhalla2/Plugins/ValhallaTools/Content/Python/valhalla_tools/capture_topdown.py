"""Orthographic top-down captures of a zone, for the Phase 6 map editor.

The 1.0 web editor drew the zone itself, from the Tiled JSON, and let the user
click a tile to place a spawn point. 2.0's zones are Unreal levels, so there is
nothing the editor can parse to draw them — it needs a picture, and it needs to
know what the picture's pixels mean in centimetres or a click cannot become a
coordinate.

This produces both: `<zone>.png` and `<zone>.json`, side by side under
`<1.0 repo>/maps/thumbs/`.

## The projection, and why the metadata is only five numbers

The capture is **orthographic**, not perspective, with its ortho width set to
the zone's own size. That makes the picture an affine map of the zone's XY
plane, so converting a pixel to a world coordinate is a scale and an offset and
nothing else. A perspective capture would need the editor to know the camera's
height and FOV and to undo a divide per pixel — and would be wrong anyway,
because a perspective view of a 4096 cm zone shows the near wall's outside face
and the far wall's inside face, so the same wall would be at two different
pixels depending on which end of the zone it was at.

## Image axes

The camera is placed above the zone's centre at `pitch = -90, yaw = -90`. Those
two numbers are the whole of the coordinate convention, and they were chosen
rather than defaulted:

    pitch -90, yaw   0  ->  image right = +Y world, image down = -X world
    pitch -90, yaw -90  ->  image right = +X world, image down = +Y world

The second is 1.0's canvas convention — x to the right, y downward, origin at
the top-left — so the arithmetic in the Phase 6 editor is the same arithmetic
the 1.0 editor already does, and a screenshot of a 2.0 zone can be laid over a
screenshot of the 1.0 one and compared.

So, with `pixelsPerCm = resolution / sizeX`:

    worldX = originX + px / pixelsPerCm
    worldY = originY + py / pixelsPerCm

and the *zone-local* coordinate the admin API and Live Dashboard use —
centimetres from the zone volume's min corner — is simply `px / pixelsPerCm`,
`py / pixelsPerCm`, with no origin at all. That is the point of anchoring
`originX`/`originY` to the zone volume's min corner: the dashboard can draw
zone-local positions without ever knowing that the desert lives at X = +40000.
"""

import json
import os

import unreal

from valhalla_tools.build_zone import thumbs_dir

#: Square, and a power of two so it can be a texture if the editor wants one.
#: 2048 over a 4096 cm zone is 0.5 px/cm, i.e. 32 px per 64 cm tile — enough to
#: see a single wall segment, which is the smallest thing worth clicking.
RESOLUTION = 2048


def _log(message):
    unreal.log("VALHALLA_CAPTURE {}".format(message))


#: The toon outline, authored by `build_toon.py` and applied at runtime to the
#: pawn's camera by `AValhallaPlayerController::ApplyOutlinePostProcess`.
OUTLINE_MATERIAL = "/Game/Valhalla/Materials/PP_Outline"


def _apply_outline(component):
    """Put `PP_Outline` on a scene capture component.

    Explicitly, rather than relying on the unbound `PostProcessVolume` in
    `L_World`: a `SceneCaptureComponent2D` does not pick that volume up, so
    every capture came out unoutlined while the same view in PIE — where the
    player controller puts the material on the pawn's camera — was outlined.
    A screenshot that does not look like the game is worse than no screenshot,
    because it is the thing a reader will believe.
    """
    outline = unreal.EditorAssetLibrary.load_asset(OUTLINE_MATERIAL)
    if outline is None:
        unreal.log_warning(
            "VALHALLA_CAPTURE {} is missing; captures will have no outline. "
            "Run build_toon first.".format(OUTLINE_MATERIAL))
        return False

    blendables = unreal.WeightedBlendables()
    blendables.set_editor_property(
        "array", [unreal.WeightedBlendable(weight=1.0, object=outline)])

    settings = component.get_editor_property("post_process_settings")
    settings.set_editor_property("weighted_blendables", blendables)
    component.set_editor_property("post_process_settings", settings)
    component.set_editor_property("post_process_blend_weight", 1.0)
    return True


def _find_zone_volume(zone_id):
    """The `AValhallaZoneVolume` for a zone, in whatever level is open."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.ValhallaZoneVolume):
            if str(actor.get_editor_property("zone_id")) == zone_id:
                return actor
    return None


def _zone_bounds(volume):
    """World-space min/max of the volume's box.

    The component's own scaled extent about its world location, matching
    `AValhallaZoneVolume::GetZoneBounds` exactly — *not* the component's
    rendered bounds, which a `UBoxComponent` pads. Using the padded bounds here
    would make the picture a few centimetres wider than the zone and put every
    pixel-to-centimetre conversion slightly out, which is the kind of error
    that shows up as "the editor places things half a tile off".
    """
    box = volume.get_editor_property("box")
    centre = box.get_world_location()
    extent = box.get_scaled_box_extent()
    return (
        unreal.Vector(centre.x - extent.x, centre.y - extent.y, centre.z - extent.z),
        unreal.Vector(centre.x + extent.x, centre.y + extent.y, centre.z + extent.z),
    )


def capture(zone_id, out_dir=None):
    """Capture one zone. Returns the metadata dict that was written."""
    volume = _find_zone_volume(zone_id)
    if volume is None:
        raise RuntimeError(
            "no AValhallaZoneVolume with ZoneId '{}' in the open level; "
            "open L_World first".format(zone_id))

    minimum, maximum = _zone_bounds(volume)
    size_x = maximum.x - minimum.x
    size_y = maximum.y - minimum.y

    if abs(size_x - size_y) > 1.0:
        unreal.log_warning(
            "VALHALLA_CAPTURE zone '{}' is {:.0f} x {:.0f}, not square; the capture "
            "is square so the shorter axis will have margin.".format(zone_id, size_x, size_y))

    out_dir = out_dir or thumbs_dir()
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    world = unreal.EditorLevelLibrary.get_editor_world()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # RGBA8 rather than a float format, because `export_render_target` writes
    # PNG for an 8-bit target and Radiance HDR for a float one, and the editor
    # wants a PNG.
    render_target = unreal.RenderingLibrary.create_render_target2d(
        world, RESOLUTION, RESOLUTION, unreal.TextureRenderTargetFormat.RTF_RGBA8)

    # High enough to be above anything in the zone (walls are 180, roofs 250)
    # with room to spare; an orthographic capture's height does not affect
    # scale, only what falls inside the near/far planes.
    height = maximum.z + 4000.0
    centre = unreal.Vector((minimum.x + maximum.x) / 2.0,
                           (minimum.y + maximum.y) / 2.0,
                           height)

    capture_actor = actors.spawn_actor_from_class(
        unreal.SceneCapture2D, centre, unreal.Rotator(0.0, -90.0, -90.0))
    capture_actor.set_actor_label("ZoneCapture_{}".format(zone_id))

    try:
        component = capture_actor.get_editor_property("capture_component2d")
        component.set_editor_property("texture_target", render_target)
        component.set_editor_property("projection_type",
                                      unreal.CameraProjectionMode.ORTHOGRAPHIC)
        component.set_editor_property("ortho_width", max(size_x, size_y))

        # Final colour, so the picture is what the zone looks like — the toon
        # materials and the global PostProcessVolume's outline included. A
        # scene-colour source would give an unlit albedo pass, which is
        # arguably more useful for a map but does not match what the player
        # sees, and the editor's job is to show the user their level.
        component.set_editor_property(
            "capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)

        # The fog of war is a per-viewer post-process on a pawn's camera, so it
        # is not here to be excluded; but `show_flags` defaults are a viewport's,
        # and a capture wants the geometry rather than the editor's icons.
        component.set_editor_property("capture_every_frame", False)
        component.set_editor_property("capture_on_movement", False)
        _apply_outline(component)

        component.capture_scene()

        file_name = "{}.png".format(zone_id)
        unreal.RenderingLibrary.export_render_target(
            world, render_target, out_dir, file_name)
    finally:
        # Always, even if the capture threw: a SceneCapture2D left in the level
        # would be saved into `L_World` and would then render the whole zone
        # every frame for every client, forever.
        actors.destroy_actor(capture_actor)

    pixels_per_cm = RESOLUTION / max(size_x, size_y)

    metadata = {
        "zoneId": zone_id,
        "originX": round(minimum.x, 1),
        "originY": round(minimum.y, 1),
        "sizeX": round(size_x, 1),
        "sizeY": round(size_y, 1),
        "pixelsPerCm": pixels_per_cm,
        # Everything below is documentation the editor can assert against
        # rather than data it needs, and it is here because a bare
        # `pixelsPerCm` does not say which way up the picture is.
        "resolution": RESOLUTION,
        "units": "cm",
        "image": "{}.png".format(zone_id),
        "imageRightAxis": "+X",
        "imageDownAxis": "+Y",
        "pixelToWorld": "worldX = originX + px / pixelsPerCm; worldY = originY + py / pixelsPerCm",
        "pixelToZoneLocal": "x = px / pixelsPerCm; y = py / pixelsPerCm",
    }

    metadata_path = os.path.join(out_dir, "{}.json".format(zone_id)).replace("\\", "/")
    with open(metadata_path, "w", encoding="utf-8") as handle:
        json.dump(metadata, handle, indent=2)
        handle.write("\n")

    _log("{}: {} px over {:.0f} x {:.0f} cm at ({:.0f}, {:.0f}) -> {}".format(
        zone_id, RESOLUTION, size_x, size_y, minimum.x, minimum.y, out_dir))

    return metadata


def capture_all(zone_ids=("grasslands", "desert"), out_dir=None):
    return {zone_id: capture(zone_id, out_dir) for zone_id in zone_ids}


# ── Perspective shots, for looking at the place rather than mapping it ──


def capture_view(name, target, distance, out_dir,
                 pitch=-45.0, yaw=45.0, fov=45.0, width=1600, height=900):
    """One perspective screenshot aimed at a world point, written as a PNG.

    A `SceneCapture2D` rather than the editor viewport, for three reasons that
    all matter when the point of the picture is to *judge the level*: the
    resolution does not depend on the size of somebody's editor window, no
    selection outline or transform gizmo can end up in the frame, and the
    camera is a pose rather than wherever the viewport happened to be left.

    The camera is placed by aiming *backwards* along its own forward vector
    from `target`, which is how you get "look at that, from over there" without
    setting a location and a rotation that disagree with each other.

    Args:
        name: File stem; the PNG is `<out_dir>/<name>.png`.
        target: The world point to look at, as an `unreal.Vector`.
        distance: How far back along the view direction to stand, cm.
        out_dir: Directory to write into; created if missing.
        pitch: Downward angle. -45 with yaw 45 is the isometric angle the kit
            was modelled for and the one `eldmoor-iso-preview.png` shows.
        yaw: Compass direction the camera faces.
    """
    import math

    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    pitch_rad = math.radians(pitch)
    yaw_rad = math.radians(yaw)
    forward = unreal.Vector(
        math.cos(pitch_rad) * math.cos(yaw_rad),
        math.cos(pitch_rad) * math.sin(yaw_rad),
        math.sin(pitch_rad))

    location = unreal.Vector(target.x - forward.x * distance,
                             target.y - forward.y * distance,
                             target.z - forward.z * distance)

    world = unreal.EditorLevelLibrary.get_editor_world()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    render_target = unreal.RenderingLibrary.create_render_target2d(
        world, width, height, unreal.TextureRenderTargetFormat.RTF_RGBA8)

    capture_actor = actors.spawn_actor_from_class(
        unreal.SceneCapture2D, location, unreal.Rotator(0.0, pitch, yaw))
    capture_actor.set_actor_label("ViewCapture_{}".format(name))

    try:
        component = capture_actor.get_editor_property("capture_component2d")
        component.set_editor_property("texture_target", render_target)
        component.set_editor_property("projection_type",
                                      unreal.CameraProjectionMode.PERSPECTIVE)
        component.set_editor_property("fov_angle", fov)
        component.set_editor_property(
            "capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        component.set_editor_property("capture_every_frame", False)
        component.set_editor_property("capture_on_movement", False)
        _apply_outline(component)
        component.capture_scene()

        unreal.RenderingLibrary.export_render_target(
            world, render_target, out_dir, "{}.png".format(name))
    finally:
        actors.destroy_actor(capture_actor)

    _log("{}: {}x{} from ({:.0f}, {:.0f}, {:.0f}) at pitch {:.0f} yaw {:.0f}".format(
        name, width, height, location.x, location.y, location.z, pitch, yaw))

    return {"name": name, "file": "{}.png".format(name),
            "camera": [location.x, location.y, location.z],
            "target": [target.x, target.y, target.z],
            "pitch": pitch, "yaw": yaw, "fov": fov}
