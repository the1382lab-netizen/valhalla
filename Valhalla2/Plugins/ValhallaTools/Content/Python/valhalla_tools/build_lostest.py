"""Build /Game/Valhalla/Maps/L_LoSTest, the Phase 5 line-of-sight fixture.

Run from the editor console::

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/build_lostest.py"

L_GreyBox is a level you can play in. This is a level you can *argue* about: four
geometric cases, each isolated from the others, each with a known answer.

    the room       an 8 x 8 walled box with one 1-tile doorway. NPC A stands in
                   the far corner, out of the wedge the doorway opens, so a
                   player outside the door cannot see A and a player standing in
                   the doorway can. This is the "does a wall actually stop
                   replication" case.

    the corridor   a 12-tile L, one tile wide, open at its west mouth and capped
                   at its north end. NPC B is at the far end. From the mouth the
                   bend hides B; from the corner it does not. This is the "does
                   a corner work" case, and it is the one a naive distance check
                   gets wrong — B is 550 cm from the mouth, well inside a
                   warrior's 1200.

    the field      flat, empty, and measured. NPC C is exactly 1500 cm from
                   PS_Field and NPC D exactly 900 cm, on the same bearing with
                   nothing between. A ranger (1800) sees both; a warrior (1200)
                   sees only D. This is the "is vision range per class and is it
                   the *viewer's*" case, and it is the only one of the four that
                   does not involve a wall at all.

All four NPCs use the ``passive`` template, which in the ported NPCSystem never
acquires a proximity target and therefore never moves (ValhallaNPC.cpp, the
behaviour check in UpdateAggro). A chasing enemy would make "exactly 1500 cm"
true for about a second.

On the two PlayerStarts
-----------------------

``PS_Field`` and ``PS_RoomDoor`` are both real ``APlayerStart`` actors, but
``AGameModeBase::ChoosePlayerStart`` spreads joiners across unoccupied starts
and does not honour ``PlayerStartTag``, so the *second* client will land on
whichever one is free. The gate therefore teleports rather than trusting the
spawn:

    valhalla.DebugTeleport 32 672          both to PS_Field
    valhalla.DebugTeleport -800 -1056 warrior   the warrior to PS_RoomDoor

which is the same thing the Phase 2b gates do and has the advantage of being
repeatable without a respawn.

Geometry, in tiles
------------------

64 cm tiles, 40 x 40, centred on the origin, so tile (i, j) has its centre at
``((i - 19.5) * 64, (j - 19.5) * 64)`` and the map spans +/- 1280 cm.
"""

import unreal

from valhalla_tools import build_fog

# ── Constants ───────────────────────────────────────────────────────────

KIT = "/Game/Valhalla/Environment/Grassland/{0}/StaticMeshes/{0}"

LEVEL = "/Game/Valhalla/Maps/L_LoSTest"

#: constants.ts:2 TILE_SIZE. 1.0 pixels and 2.0 centimetres are 1:1.
TILE = 64.0

#: 40 x 40 tiles, so 2560 cm square.
N = 40

#: Top surface of a floor tile, cm. Everything stands on this.
FLOOR_TOP = 7.6

#: How far above the floor a spawner sits, cm. The NPC's capsule is 60 cm to its
#: centre; a little more than that keeps the spawn out of the floor without
#: relying on the collision handler to push it up.
SPAWN_Z = FLOOR_TOP + 90.0

#: npc-templates.json — the ``passive`` entry ("New NPC", 10 hp, no aggroRange).
#: Passive is what makes it stand still; see the module docstring.
NPC_TEMPLATE = "npc_1771781907844"

# ── The room ────────────────────────────────────────────────────────────
#: Wall ring occupies these tile indices on both axes; interior is 5..10.
ROOM_MIN, ROOM_MAX = 4, 11
#: The one-tile doorway, on the -Y side.
ROOM_DOOR_I = 7
#: NPC A: the far corner, deliberately out of the wedge the doorway opens.
ROOM_NPC = (5, 5)
#: Just outside the doorway. The gate teleports the warrior here.
ROOM_DOOR_OUTSIDE = (7, 3)

# ── The corridor ────────────────────────────────────────────────────────
#: Walkable lane: east along j = 10, then north along i = 30. Twelve tiles.
CORRIDOR_LANE_J = 10
CORRIDOR_I_RANGE = (24, 30)
CORRIDOR_LANE_I = 30
CORRIDOR_J_RANGE = (11, 15)
#: Open end. Standing here, the bend hides NPC B.
CORRIDOR_MOUTH = (23, 10)
#: The bend. Standing here, it does not.
CORRIDOR_CORNER = (30, 10)
#: NPC B, at the capped north end.
CORRIDOR_NPC = (30, 15)

# ── The field ───────────────────────────────────────────────────────────
#: PS_Field, and the bearing C and D are measured along (-Y).
FIELD_START = (20, 30)
FIELD_NPC_C_DISTANCE = 1500.0
FIELD_NPC_D_DISTANCE = 900.0


def tile_xy(i, j):
    """World centre of tile (i, j). The grid is centred on the origin."""
    half = (N - 1) / 2.0
    return ((i - half) * TILE, (j - half) * TILE)


class Builder(object):
    def __init__(self):
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.meshes = {}
        self.counts = {}
        self.notes = []

    # ── Placement helpers ───────────────────────────────────────────────

    def mesh(self, name):
        if name not in self.meshes:
            asset = unreal.EditorAssetLibrary.load_asset(KIT.format(name))
            if asset is None:
                raise RuntimeError("missing kit mesh {}".format(KIT.format(name)))
            self.meshes[name] = asset
        return self.meshes[name]

    def _tally(self, folder):
        self.counts[folder] = self.counts.get(folder, 0) + 1

    def place(self, mesh_name, label, folder, x, y, z, yaw=0.0):
        actor = self.actors.spawn_actor_from_object(
            self.mesh(mesh_name), unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw))
        actor.set_actor_label(label)
        actor.set_folder_path(folder)
        self._tally(folder)
        return actor

    def place_class(self, cls, label, folder, x, y, z, pitch=0.0, yaw=0.0):
        actor = self.actors.spawn_actor_from_class(
            cls, unreal.Vector(x, y, z), unreal.Rotator(0.0, pitch, yaw))
        actor.set_actor_label(label)
        actor.set_folder_path(folder)
        self._tally(folder)
        return actor

    def wall(self, label, i, j, yaw, corner=False):
        x, y = tile_xy(i, j)
        mesh_name = "VB_StoneWall_Corner" if corner else "VB_StoneWall_Straight"
        return self.place(mesh_name, label, "Walls", x, y, FLOOR_TOP, yaw)

    def spawner(self, label, x, y, template=NPC_TEMPLATE):
        actor = self.place_class(unreal.ValhallaNPCSpawner, "Spawn_" + label, "Gameplay",
                                 x, y, SPAWN_Z)
        actor.set_editor_property("template_id", template)
        # One spawn point, one NPC, standing exactly on the marker — so
        # "exactly 1500 cm" is exactly 1500 cm.
        actor.set_editor_property("debug_label", label)
        self.notes.append("{} at ({:.0f}, {:.0f})".format(label, x, y))
        return actor

    # ── Floor ───────────────────────────────────────────────────────────

    def build_floor(self):
        """Stone everywhere.

        One material for the whole map on purpose: the greybox mixes grass and
        stone so that a screenshot reads as a place, and this one must read as a
        measurement. A fog edge is far easier to judge as jagged or smooth over
        a uniform floor.
        """
        for i in range(N):
            for j in range(N):
                x, y = tile_xy(i, j)
                self.place("SM_Stone_Floor", "Floor_%02d_%02d" % (i, j), "Floor", x, y, 0.0)

    # ── The room ────────────────────────────────────────────────────────

    def build_room(self):
        lo, hi = ROOM_MIN, ROOM_MAX

        # Corner yaws as in build_greybox: at yaw 0 the corner joins an arm
        # pointing -X to an arm pointing +Y, and each 90 degrees rotates that
        # pair.
        for i, j, yaw in ((lo, lo, 270.0), (lo, hi, 180.0), (hi, lo, 0.0), (hi, hi, 90.0)):
            self.wall("RoomCorner_%02d_%02d" % (i, j), i, j, yaw, corner=True)

        for i in range(lo + 1, hi):
            if i != ROOM_DOOR_I:
                self.wall("RoomS_%02d" % i, i, lo, 0.0)
            self.wall("RoomN_%02d" % i, i, hi, 0.0)

        for j in range(lo + 1, hi):
            self.wall("RoomW_%02d" % j, lo, j, 90.0)
            self.wall("RoomE_%02d" % j, hi, j, 90.0)

    # ── The corridor ────────────────────────────────────────────────────

    def build_corridor(self):
        """An L one tile wide, walled on both sides, open only at the mouth.

        Laid out as four straight runs rather than as "every neighbour of the
        lane", because a wall piece is 64 long and 25 thick: pieces butt end to
        end along a run and leave a 39 cm gap if they are placed side by side.
        Runs, not cells.
        """
        i_lo, i_hi = CORRIDOR_I_RANGE
        j_lo, j_hi = CORRIDOR_J_RANGE

        # South side of the east-west leg, carried one tile past the bend so the
        # outside corner is closed.
        for i in range(i_lo, i_hi + 2):
            self.wall("CorridorS_%02d" % i, i, CORRIDOR_LANE_J - 1, 0.0)

        # North side of the east-west leg. Stops two tiles short: the lane turns
        # north at i = 30, and i = 29 is covered by the west side of the
        # north-south leg below.
        for i in range(i_lo, CORRIDOR_LANE_I - 1):
            self.wall("CorridorN_%02d" % i, i, CORRIDOR_LANE_J + 1, 0.0)

        # West side of the north-south leg, up to and including the cap row.
        for j in range(j_lo, j_hi + 2):
            self.wall("CorridorW_%02d" % j, CORRIDOR_LANE_I - 1, j, 90.0)

        # East side, from the bend up to the cap row.
        for j in range(CORRIDOR_LANE_J, j_hi + 2):
            self.wall("CorridorE_%02d" % j, CORRIDOR_LANE_I + 1, j, 90.0)

        # The cap. Without it the corridor is a through-route and NPC B is
        # visible from outside the north end.
        self.wall("CorridorCap", CORRIDOR_LANE_I, j_hi + 1, 0.0)

    # ── Gameplay ────────────────────────────────────────────────────────

    def build_gameplay(self):
        # NPC A — the room.
        ax, ay = tile_xy(*ROOM_NPC)
        self.spawner("NPC_A", ax, ay)

        # NPC B — the far end of the corridor.
        bx, by = tile_xy(*CORRIDOR_NPC)
        self.spawner("NPC_B", bx, by)

        # NPC C and D — the field, measured from PS_Field along -Y.
        fx, fy = tile_xy(*FIELD_START)
        self.spawner("NPC_C", fx, fy - FIELD_NPC_C_DISTANCE)
        self.spawner("NPC_D", fx, fy - FIELD_NPC_D_DISTANCE)

        # Facing -Y, which is where C and D are, so a screenshot from the
        # default spawn looks down the measured line.
        self.place_class(unreal.PlayerStart, "PS_Field", "Gameplay",
                         fx, fy, FLOOR_TOP + 100.0, yaw=-90.0)

        dx, dy = tile_xy(*ROOM_DOOR_OUTSIDE)
        door = self.place_class(unreal.PlayerStart, "PS_RoomDoor", "Gameplay",
                                dx, dy, FLOOR_TOP + 100.0, yaw=90.0)
        door.set_editor_property("player_start_tag", "RoomDoor")

        self.notes.append("PS_Field at ({:.0f}, {:.0f})".format(fx, fy))
        self.notes.append("PS_RoomDoor at ({:.0f}, {:.0f})".format(dx, dy))

        mx, my = tile_xy(*CORRIDOR_MOUTH)
        cx, cy = tile_xy(*CORRIDOR_CORNER)
        self.notes.append("corridor mouth ({:.0f}, {:.0f}), corner ({:.0f}, {:.0f})".format(
            mx, my, cx, cy))

        inside_x, inside_y = tile_xy(ROOM_DOOR_I, ROOM_MIN)
        self.notes.append("doorway ({:.0f}, {:.0f})".format(inside_x, inside_y + TILE * 0.5))

    def build_lighting(self):
        self.place_class(unreal.DirectionalLight, "Sun", "Lighting", 0.0, 0.0, 1600.0,
                         pitch=-50.0, yaw=-35.0)
        self.place_class(unreal.SkyLight, "SkyLight", "Lighting", 0.0, 0.0, 1200.0)
        self.place_class(unreal.SkyAtmosphere, "SkyAtmosphere", "Lighting", 0.0, 0.0, 0.0)


def build():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    level_subsystem.new_level(LEVEL)

    builder = Builder()
    builder.build_floor()
    builder.build_room()
    builder.build_corridor()
    builder.build_gameplay()
    builder.build_lighting()

    # The same post-process volume the greybox carries, so the editor viewport
    # matches PIE, and one fog bounds covering the whole 2560 cm square.
    from valhalla_tools import build_toon
    build_toon.place_post_process_volume(LEVEL)

    half = N * TILE * 0.5
    build_fog.place_fog_bounds(centre=(0.0, 0.0, 200.0), extent=(half, half, 400.0))

    level_subsystem.save_current_level()

    summary = ", ".join("%s=%d" % kv for kv in sorted(builder.counts.items()))
    unreal.log("VALHALLA_LOSTEST built: %s" % summary)
    for note in builder.notes:
        unreal.log("VALHALLA_LOSTEST   %s" % note)

    return {"counts": builder.counts, "notes": builder.notes}


if __name__ == "__main__":
    build()
