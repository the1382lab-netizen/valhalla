"""Review renders for wave5_kit.py pieces (Workbench, material colours).

    exec(open(<this>).read()); review(objs, path)
lays the objects out in a row (widest first), frames them and renders a PNG.
"""
import math
import bpy
import mathutils
from mathutils import Vector


def _bounds(o):
    vs = [o.matrix_world @ Vector(c) for c in o.bound_box]
    return Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs))), \
           Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))


def review(objs, path, cols=6, gap=0.4, pitch=58, yaw=25, res=(1600, 900), preview=None):
    sc = bpy.context.scene
    for m in bpy.data.materials:
        if preview and m.name in preview:
            c = preview[m.name]
            m.diffuse_color = (c[0], c[1], c[2], 1)
    x = y = 0.0
    row_h = 0.0
    for i, o in enumerate(objs):
        o.location = (0, 0, 0)
        bpy.context.view_layer.update()
        mn, mx = _bounds(o)
        w, d = mx.x - mn.x, mx.y - mn.y
        if i and i % cols == 0:
            x = 0.0
            y -= row_h + gap
            row_h = 0.0
        o.location = (x - mn.x, y - mx.y, 0)
        bpy.context.view_layer.update()
        x += w + gap
        row_h = max(row_h, d)
    allmn = Vector((1e9, 1e9, 1e9)); allmx = -allmn
    for o in objs:
        mn, mx = _bounds(o)
        allmn = Vector(map(min, allmn, mn)); allmx = Vector(map(max, allmx, mx))
    cam = bpy.data.objects.get("ReviewCam") or bpy.data.objects.new("ReviewCam", bpy.data.cameras.new("ReviewCam"))
    if cam.name not in sc.collection.objects:
        sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    rot = mathutils.Euler((math.radians(pitch), 0, math.radians(yaw)))
    cam.rotation_euler = rot
    c = (allmn + allmx) / 2
    cam.location = c + rot.to_matrix() @ Vector((0, 0, 30))
    span = max(allmx.x - allmn.x, (allmx.y - allmn.y) * 0.85 + (allmx.z - allmn.z) * 0.9)
    cam.data.ortho_scale = span * 1.08
    cam.data.clip_end = 100
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.show_shadows = True
    sc.display.shading.show_cavity = True
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.film_transparent = False
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return path
