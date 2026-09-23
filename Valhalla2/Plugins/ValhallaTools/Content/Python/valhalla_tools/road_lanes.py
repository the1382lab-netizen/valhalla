"""B-15 Wave 1: point every road tile's grass verge away from the road.

The Wave 1 ``SM_Dirt_Straight`` is dirt on its local -X edge and fades to a grass
verge on its local +X edge (Blender +X imports as Unreal local +X). Roads are
two tiles wide, so each lane must turn its +X edge *away* from the other lane:
then the two lanes meet dirt-to-dirt and the verges run only along the outside
of the road.

``fix_open_world()`` edits the placed tile-field instances in place (a quarter
turn is never needed, only 0 or 180 degrees on top of whatever yaw the builder
gave them) and saves the level that owns them. It is idempotent: a lane whose
verge already faces grass is left alone. ``build_grasslands`` applies the same
rule when it builds the level from scratch.
"""

import math

import unreal

TILE = 64.0
STRAIGHT = "SM_Dirt_Straight"
ROAD_MESHES = ("SM_Dirt_Straight", "SM_Dirt_Corner")


def _key(x, y):
    return (int(round(x / 8.0)), int(round(y / 8.0)))


def _fields():
    cls = getattr(unreal, "ValhallaTileField")
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if not isinstance(actor, cls):
            continue
        ism = actor.get_component_by_class(unreal.InstancedStaticMeshComponent)
        mesh = ism.get_editor_property("static_mesh") if ism else None
        if mesh is not None:
            yield actor, ism, mesh.get_name()


def fix_open_world():
    roads = set()
    straights = []
    for actor, ism, mesh_name in _fields():
        if mesh_name not in ROAD_MESHES:
            continue
        for i in range(ism.get_instance_count()):
            t = ism.get_instance_transform(i, True)
            roads.add(_key(t.translation.x, t.translation.y))
            if mesh_name == STRAIGHT:
                straights.append((actor, ism, i, t))

    flipped = 0
    levels = set()
    for actor, ism, i, t in straights:
        yaw = t.rotation.rotator().yaw
        rad = math.radians(yaw)
        nx = t.translation.x + TILE * math.cos(rad)
        ny = t.translation.y + TILE * math.sin(rad)
        if _key(nx, ny) not in roads:
            continue                                   # verge already faces grass
        rot = unreal.Rotator(0.0, 0.0, yaw + 180.0)    # roll, pitch, yaw
        new_t = unreal.Transform(t.translation, rot, t.scale3d)
        actor.modify()
        ism.update_instance_transform(i, new_t, True, True, True)
        levels.add(actor.get_level().get_outer().get_name())
        flipped += 1

    if flipped:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, False)
    unreal.log("LogValhallaImport: road lanes: {} straight tile(s), {} flipped, levels {}".format(
        len(straights), flipped, sorted(levels)))
    return {"straights": len(straights), "flipped": flipped, "levels": sorted(levels), "roadTiles": len(roads)}
