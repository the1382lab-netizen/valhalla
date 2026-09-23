"""Rebuild the Phase 2a greybox level, /Game/Valhalla/Maps/L_GreyBox.

Run from the editor console:

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/build_greybox.py"

The level is a test fixture, not art. Its job is to give the netcode something
with a floor, a wall to be blocked by, a doorway to funnel through and props to
lose sight behind — the geometry Phase 2b's combat and Phase 5's line of sight
will be argued about on. Keeping it as a script rather than as hand-placed
actors means it can be regenerated identically after any change to the kit.

Everything is laid out on the 1.0 tile grid: 64 cm tiles, 24 x 24, centred on
the world origin, so a 1.0 pixel coordinate divided by 64 is a tile index here.
Kit pivots are at bottom-centre and the floor meshes are ~7.5 cm thick, so
props sit at FLOOR_TOP rather than at zero.
"""

import math

import unreal

# ── Constants ───────────────────────────────────────────────────────────

KIT = "/Game/Valhalla/Environment/Grassland/{0}/StaticMeshes/{0}"
GRASS = ["SM_Grass_A", "SM_Grass_B", "SM_Grass_C"]

#: constants.ts:2 TILE_SIZE. 1.0 pixels and 2.0 centimetres are 1:1.
TILE = 64.0

#: Grid is 24 x 24 tiles, so 1536 cm square.
N = 24

#: Top surface of a floor tile, cm. Walls and props stand on this.
FLOOR_TOP = 7.6

#: The stone plaza occupies tile indices [PLAZA_MIN, PLAZA_MAX] on both axes.
PLAZA_MIN, PLAZA_MAX = 9, 14

#: The wall ring sits one tile outside the plaza.
RING_MIN, RING_MAX = 8, 15

#: Tile indices of the two-tile doorway on the south (-Y) side of the ring.
SOUTH_GAP = (11, 12)


def tile_xy(i, j):
    """World centre of tile (i, j). The grid is centred on the origin."""
    half = (N - 1) / 2.0
    return ((i - half) * TILE, (j - half) * TILE)


def rnd(seed):
    """Deterministic pseudo-random in [0, 1). Same layout on every rebuild."""
    v = math.sin(seed * 12.9898) * 43758.5453
    return v - math.floor(v)


class Builder(object):
    def __init__(self):
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.meshes = {}
        self.counts = {}

    def mesh(self, name):
        if name not in self.meshes:
            self.meshes[name] = unreal.EditorAssetLibrary.load_asset(KIT.format(name))
        return self.meshes[name]

    def _tally(self, folder):
        self.counts[folder] = self.counts.get(folder, 0) + 1

    def place(self, mesh_name, label, folder, x, y, z, yaw=0.0, scale=1.0):
        actor = self.actors.spawn_actor_from_object(
            self.mesh(mesh_name),
            unreal.Vector(x, y, z),
            unreal.Rotator(0.0, 0.0, yaw),
        )
        actor.set_actor_label(label)
        actor.set_folder_path(folder)
        if scale != 1.0:
            actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        self._tally(folder)
        return actor

    def place_class(self, cls, label, folder, x, y, z, pitch=0.0, yaw=0.0):
        actor = self.actors.spawn_actor_from_class(
            cls, unreal.Vector(x, y, z), unreal.Rotator(0.0, pitch, yaw)
        )
        actor.set_actor_label(label)
        actor.set_folder_path(folder)
        self._tally(folder)
        return actor

    # ── Floor ───────────────────────────────────────────────────────────

    def build_floor(self):
        for i in range(N):
            for j in range(N):
                x, y = tile_xy(i, j)
                seed = i * 131.0 + j * 17.0
                yaw = 90.0 * int(rnd(seed + 3.0) * 4.0)
                in_plaza = (PLAZA_MIN <= i <= PLAZA_MAX) and (PLAZA_MIN <= j <= PLAZA_MAX)
                if in_plaza:
                    self.place("SM_Stone_Floor", "Plaza_%02d_%02d" % (i, j), "Floor", x, y, 0.0, yaw)
                else:
                    variant = GRASS[int(rnd(seed) * 3.0) % 3]
                    self.place(variant, "Floor_%02d_%02d" % (i, j), "Floor", x, y, 0.0, yaw)

    # ── Walls ───────────────────────────────────────────────────────────
    #
    # VB_StoneWall_Straight is 64 long on X, 25 thick on Y, 180 tall, so a wall
    # running east-west is yaw 0 and one running north-south is yaw 90.
    #
    # VB_StoneWall_Corner at yaw 0 joins an arm pointing -X to an arm pointing
    # +Y. Each 90 degrees of yaw rotates that pair, which is where the corner
    # yaws below come from.

    def build_ring(self):
        lo, hi = RING_MIN, RING_MAX

        corners = [
            (lo, lo, 270.0),   # arms to +X and +Y
            (lo, hi, 180.0),   # arms to +X and -Y
            (hi, lo, 0.0),     # arms to -X and +Y
            (hi, hi, 90.0),    # arms to -X and -Y
        ]
        for i, j, yaw in corners:
            x, y = tile_xy(i, j)
            self.place("VB_StoneWall_Corner", "RingCorner_%02d_%02d" % (i, j), "Walls",
                       x, y, FLOOR_TOP, yaw)

        for i in range(lo + 1, hi):
            # South edge (-Y), with the two-tile doorway left open.
            if i not in SOUTH_GAP:
                x, y = tile_xy(i, lo)
                self.place("VB_StoneWall_Straight", "RingS_%02d" % i, "Walls", x, y, FLOOR_TOP, 0.0)
            # North edge (+Y).
            x, y = tile_xy(i, hi)
            self.place("VB_StoneWall_Straight", "RingN_%02d" % i, "Walls", x, y, FLOOR_TOP, 0.0)

        for j in range(lo + 1, hi):
            x, y = tile_xy(lo, j)
            self.place("VB_StoneWall_Straight", "RingW_%02d" % j, "Walls", x, y, FLOOR_TOP, 90.0)
            x, y = tile_xy(hi, j)
            self.place("VB_StoneWall_Straight", "RingE_%02d" % j, "Walls", x, y, FLOOR_TOP, 90.0)

    def build_corridor(self):
        """An L of wall running east from the plaza, then north.

        Somewhere to stand out of line of sight that is not the plaza ring.
        """
        for i in range(17, 22):
            x, y = tile_xy(i, 11)
            self.place("VB_StoneWall_Straight", "CorridorX_%02d" % i, "Walls", x, y, FLOOR_TOP, 0.0)

        x, y = tile_xy(22, 11)
        self.place("VB_StoneWall_Corner", "CorridorCorner", "Walls", x, y, FLOOR_TOP, 0.0)

        for j in range(12, 18):
            x, y = tile_xy(22, j)
            self.place("VB_StoneWall_Straight", "CorridorY_%02d" % j, "Walls", x, y, FLOOR_TOP, 90.0)

    # ── Props ───────────────────────────────────────────────────────────

    TREES = [(1, 1), (3, 0), (0, 6), (2, 10), (1, 15), (0, 20),
             (4, 22), (9, 23), (14, 22), (20, 23), (22, 20), (23, 3)]
    ROCKS = [(2, 5), (1, 18), (6, 21), (18, 1), (21, 6), (23, 15)]

    def build_props(self):
        for index, (i, j) in enumerate(self.TREES):
            x, y = tile_xy(i, j)
            seed = index * 7.0 + 1.0
            variant = "SM_Tree_A" if rnd(seed) < 0.6 else "SM_Tree_B"
            self.place(variant, "Tree_%02d" % index, "Props", x, y, FLOOR_TOP,
                       yaw=rnd(seed + 0.5) * 360.0, scale=0.85 + rnd(seed + 1.5) * 0.4)

        for index, (i, j) in enumerate(self.ROCKS):
            x, y = tile_xy(i, j)
            seed = index * 11.0 + 5.0
            self.place("SM_Rock_A", "Rock_%02d" % index, "Props", x, y, FLOOR_TOP,
                       yaw=rnd(seed) * 360.0, scale=0.9 + rnd(seed + 2.5) * 0.7)

    # ── Gameplay + lighting ─────────────────────────────────────────────

    def build_player_starts(self):
        """Four starts on the plaza.

        One would do for a single player, but ChoosePlayerStart hands the same
        start to every joiner, and two characters spawned inside one another is
        a distraction the two-client gate does not need.
        """
        for index, (i, j) in enumerate([(10, 10), (13, 10), (10, 13), (13, 13)]):
            x, y = tile_xy(i, j)
            self.place_class(unreal.PlayerStart, "PlayerStart_%d" % index, "Gameplay",
                             x, y, FLOOR_TOP + 100.0, yaw=45.0 + index * 90.0)

    def build_lighting(self):
        self.place_class(unreal.DirectionalLight, "Sun", "Lighting", 0.0, 0.0, 1200.0,
                         pitch=-50.0, yaw=-35.0)
        self.place_class(unreal.SkyLight, "SkyLight", "Lighting", 0.0, 0.0, 900.0)
        self.place_class(unreal.SkyAtmosphere, "SkyAtmosphere", "Lighting", 0.0, 0.0, 0.0)
        self.place_class(unreal.ExponentialHeightFog, "HeightFog", "Lighting", 0.0, 0.0, 200.0)


def build():
    builder = Builder()
    builder.build_floor()
    builder.build_ring()
    builder.build_corridor()
    builder.build_props()
    builder.build_player_starts()
    builder.build_lighting()

    summary = ", ".join("%s=%d" % kv for kv in sorted(builder.counts.items()))
    unreal.log("VALHALLA_GREYBOX built: %s" % summary)
    return builder.counts


build()
