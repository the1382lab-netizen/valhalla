"""B-15 Wave 2: Workbench review renders of the character work.

The MCP viewport screenshot does not refresh while Blender is driven from
outside, so review images are rendered to files instead:
``render(path, objects, view)`` puts an orthographic camera in front of the
character (``front`` looks at the face, the model faces -Y), or ``side``,
``back``, ``three`` (3/4) or ``iso`` (the game's high angle), and writes a PNG.
"""

import math
import os

import bpy


def render(path, objs, view="front", size=(900, 1100), center=(0.0, 0.0, 0.6), ortho=1.4,
           engine="BLENDER_WORKBENCH", color="MATERIAL"):
    scene = bpy.context.scene
    cam = bpy.data.objects.get("_ReviewCam")
    if cam is None:
        cam = bpy.data.objects.new("_ReviewCam", bpy.data.cameras.new("_ReviewCam"))
        scene.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = ortho
    yaw = {"front": 0, "side": 90, "back": 180, "three": 35, "iso": 30, "face": 0}[view]
    pitch = 50 if view == "iso" else 0
    d = 5.0
    a, p = math.radians(yaw), math.radians(pitch)
    cam.location = (center[0] + d * math.sin(a) * math.cos(p),
                    center[1] - d * math.cos(a) * math.cos(p),
                    center[2] + d * math.sin(p))
    cam.rotation_euler = (math.radians(90 - pitch), 0.0, a)
    scene.camera = cam
    scene.render.engine = engine
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = color
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.render.filepath = path
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    for o in scene.objects:
        o.hide_render = o not in objs
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.render.render(write_still=True)
    return path


def sheet(prefix, objs, views=("front", "side", "three"), **kw):
    """Render several views: ``<prefix>_<view>.png``."""
    return [render("{}_{}.png".format(prefix, v), objs, v, **kw) for v in views]


def pose_sheet(prefix, objs, arm, actions=("A_Idle", "A_Walk", "A_Attack", "A_Cast"), fractions=(0.25, 0.6),
               view="three", **kw):
    """Render the character mid-action: each action at the given fractions of
    its frame range. Restores the armature's action afterwards."""
    if arm.animation_data is None:
        arm.animation_data_create()
    old = arm.animation_data.action
    old_frame = bpy.context.scene.frame_current
    out = []
    for name in actions:
        act = bpy.data.actions.get(name)
        if act is None:
            continue
        arm.animation_data.action = act
        try:
            slot = act.slots[0] if len(act.slots) else None
            if slot is not None:
                arm.animation_data.action_slot = slot
        except Exception:
            pass
        f0, f1 = act.frame_range
        for fr in fractions:
            f = int(round(f0 + (f1 - f0) * fr))
            bpy.context.scene.frame_set(f)
            out.append(render("{}_{}_{}.png".format(prefix, name, f), objs, view, **kw))
    arm.animation_data.action = old
    if old is None:
        # the IK target bones keep their last keyed transform once the action
        # is gone; put them back so later renders and fits are at rest
        import mathutils
        for pb in arm.pose.bones:
            pb.matrix_basis = mathutils.Matrix.Identity(4)
    bpy.context.scene.frame_set(old_frame)
    bpy.context.view_layer.update()
    return out


def strip(out_path, frame_paths, remove=True):
    """Paste rendered frames side by side into one PNG (numpy, no PIL)."""
    import numpy as np
    tiles = []
    for p in frame_paths:
        img = bpy.data.images.load(p)
        w, h = img.size
        px = np.empty(w * h * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        tiles.append(px.reshape(h, w, 4))
        bpy.data.images.remove(img)
    h = max(t.shape[0] for t in tiles)
    canvas = np.zeros((h, sum(t.shape[1] for t in tiles), 4), dtype=np.float32)
    x = 0
    for t in tiles:
        canvas[:t.shape[0], x:x + t.shape[1]] = t
        x += t.shape[1]
    name = os.path.splitext(os.path.basename(out_path))[0]
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    out = bpy.data.images.new(name, width=canvas.shape[1], height=canvas.shape[0], alpha=False)
    out.pixels.foreach_set(canvas.ravel())
    out.filepath_raw = out_path
    out.file_format = "PNG"
    out.save()
    bpy.data.images.remove(out)
    if remove:
        for p in frame_paths:
            os.remove(p)
    return out_path


def action_strips(prefix, objs, arm, actions, fractions=(0.0, 0.2, 0.4, 0.6, 0.8, 1.0), **kw):
    """One strip PNG per action: ``<prefix>_<action>.png``."""
    out = []
    for name in actions:
        frames = pose_sheet(prefix + "_tmp", objs, arm, actions=(name,), fractions=fractions, **kw)
        out.append(strip("{}_{}.png".format(prefix, name), frames))
    return out
