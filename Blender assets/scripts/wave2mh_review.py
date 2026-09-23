"""Viewport review helpers for the MetaHuman rework (workbench, material colours)."""
import math
import bpy
import mathutils

SETS = {
    "ranger": ["SK_hood_scout", "SK_chest_travelers_jerkin", "SK_legs_travelers", "SK_boots_ranger",
               "SK_gloves_ranger_bracers", "SK_cloak_warden"],
    "plate": ["SK_helm_iron_full", "SK_chest_priests_chain", "SK_legs_iron_plate", "SK_boots_iron_sabatons",
              "SK_gloves_iron_gauntlets"],
    "mage": ["SK_hat_adept", "SK_chest_apprentice_robe", "SK_legs_apprentice_robe", "SK_boots_cloth_slippers",
             "SK_gloves_cloth_wraps"],
}
ALWAYS = ["MH_Body", "MH_Face"]


def _view3d():
    win = bpy.context.window_manager.windows[0]
    for area in win.screen.areas:
        if area.type == "VIEW_3D":
            return win, area, next(r for r in area.regions if r.type == "WINDOW")
    raise RuntimeError("no 3D view")


def show(names):
    keep = set(names) | set(ALWAYS)
    for o in bpy.data.objects:
        if o.type == "MESH":
            o.hide_viewport = False
            o.hide_set(o.name not in keep)
    skin = bpy.data.materials.get("_ReviewSkin") or bpy.data.materials.new("_ReviewSkin")
    skin.diffuse_color = (0.72, 0.5, 0.4, 1)
    for n in ALWAYS:
        ob = bpy.data.objects.get(n)
        if ob:
            for i in range(len(ob.material_slots)):
                ob.material_slots[i].link = "OBJECT"
                ob.material_slots[i].material = skin


def frame(yaw_deg=0.0, pitch_deg=0.0, center=(0, 0, 0.95), dist=2.6, ortho=True):
    win, area, region = _view3d()
    sp = area.spaces.active
    sp.shading.type = "SOLID"
    sp.shading.color_type = "MATERIAL"
    sp.shading.light = "STUDIO"
    sp.overlay.show_overlays = False
    r3 = sp.region_3d
    r3.view_perspective = "ORTHO" if ortho else "PERSP"
    # yaw 0 = looking at the front (character faces -Y)
    q = mathutils.Euler((math.radians(90 - pitch_deg), 0, math.radians(yaw_deg)), "XYZ").to_quaternion()
    r3.view_rotation = q
    r3.view_location = mathutils.Vector(center)
    r3.view_distance = dist
