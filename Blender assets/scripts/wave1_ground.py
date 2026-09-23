"""B-15 Wave 1: ground tiles (A-031 grass, A-032 dirt paths, A-033 stone floor,
A-047 cobble, A-048 wood floor).

Replaces the first-pass tiles in place: same names, 64 cm square, pivot at the
bottom centre. The top is flat at 6 cm for every ground type, so neighbouring
tiles meet without steps, and the look comes from world-aligned CC0 materials
that tile seamlessly across the grid. Paths blend grass into dirt through
vertex colour (M_ValhallaGroundBlend), with the same falloff at every tile
edge so straight and corner pieces join.

Run in Blender:  exec(open(<this file>).read()); build_all()
"""

import importlib.util
import os

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"
_spec = importlib.util.spec_from_file_location("valhalla_kit", os.path.join(_here, "valhalla_kit.py"))
kit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(kit)

GRASSLAND = os.path.join(kit.IMPORT, "Environment", "Grassland")
TOWN = os.path.join(kit.IMPORT, "Environment", "Town")

#: Roads are two tiles wide (build_grasslands.py). Each straight tile is dirt
#: on its local -x edge and fades to a grass verge on its local +x edge, and
#: each lane is turned so its +x faces *away* from the other lane
#: (fix_road_lanes in valhalla_tools). The lanes then meet dirt-to-dirt down
#: the middle and the verges only run along the outside of the road. Fully
#: dirt below x = VERGE_IN, fully grass at the tile edge.
VERGE_IN, VERGE_OUT = 0.10, 0.32


def straight_mask(x, y):
    # Path runs along the tile's glTF z axis (Blender Y); verge on local +x.
    return 1.0 - kit.smoothstep(VERGE_IN, VERGE_OUT, x)


def corner_mask(x, y):
    # Corner tiles only occur where roads cross or turn, with road on every
    # side, so they are dirt throughout.
    return 1.0


TILES = [
    # (name, folder, material, mask)
    ("SM_Grass_A", GRASSLAND, "MI_GrassGround", None),
    ("SM_Grass_B", GRASSLAND, "MI_GrassGround", None),
    ("SM_Grass_C", GRASSLAND, "MI_GrassGround", None),
    ("SM_Dirt_Straight", GRASSLAND, "MI_DirtPath", straight_mask),
    ("SM_Dirt_Corner", GRASSLAND, "MI_DirtPath", corner_mask),
    ("SM_Stone_Floor", GRASSLAND, "MI_StoneFloor", None),
    ("SM_Cobble_A", TOWN, "MI_CobbleFloor", None),
    ("SM_Cobble_B", TOWN, "MI_CobbleFloor", None),
    ("SM_WoodFloor", TOWN, "MI_WoodPlanks", None),
]


def build_all(export=True):
    kit.reset_scene()
    results = []
    for i, (name, folder, mat, mask) in enumerate(TILES):
        obj = kit.ground_tile(name, mat, mask=mask, res=16 if mask else 1)
        obj.location.x = (i % 5) * 0.8          # laid out for the review screenshot
        obj.location.y = -(i // 5) * 0.8
        path = os.path.join(folder, name + ".glb")
        if export:
            loc = obj.location.copy()
            obj.location = (0, 0, 0)            # pivot at the origin for export
            kit.export_glb(obj, path)
            obj.location = loc
        results.append((name, kit.tri_count(obj), path))
    return results
