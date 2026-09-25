"""Build `/Game/Valhalla/Maps/Zones/L_Grasslands` from scratch — RETIRED.

**B-19: this no longer runs against `L_Grasslands`.** The zone is hand-edited now;
`build()` refuses its own target level unconditionally (no `force`) and only
lays the historical layout out under a *new* zone id, into a new empty level.
New zones: `ValhallaLevelTools.scaffold_zone`. Recovering the old `L_Grasslands`:
git history for the `.umap`, or `Valhalla2/Saved/LevelBackups/<timestamp>/`.
Kept on disk as the record of how the zone was first built.

## What this is a port of

1.0's grasslands had two descriptions: `maps/grasslands.json`, a 64 x 64
isometric Tiled map, and `shared/src/FallbackMapGenerator.ts`, the procedural
map the server generates when the Tiled file will not load. The fallback is the
better reference and is what this follows, for two reasons: it is the *readable*
statement of what the zone is meant to contain, and every feature in it is
placed by a rule rather than by hand, so it can be reproduced rather than
traced.

The features are the fallback's, rearranged so they make a zone worth walking
across rather than a test grid — which is also what
`Claude outputs/eldmoor-iso-preview.png` shows:

| 1.0 (`FallbackMapGenerator.ts`)                    | here                                  |
|----------------------------------------------------|---------------------------------------|
| perimeter `STONE_WALL` ring, closed (`:53-60`)     | the same ring, with three gaps        |
| `GRASS_LIGHT` / `GRASS_DARK` at 30% (`:62-68`)     | `SM_Grass_A/B/C` noise                |
| two 2-tile dirt roads crossing (`:70-80`)          | the same, crossing at the town        |
| a walled town with 4 gates, NW (`:82-117`)         | a cobbled town, middle-south          |
| `TREE` at 35% over a 24 x 23 block (`:119-127`)    | tree clusters                         |
| a circular lake, r=8, SE (`:129-139`)              | an irregular lake, north-west         |
| `STONE_FLOOR` cave mouth, south (`:141-153`)       | a stone-floored ruin, north-east      |
| 15 scattered `WOOD_FENCE` (`:155-163`)             | fences around the town                |
| `cave_entrance` zone connection (`:233-245`)       | the desert portal, east edge          |

The closed perimeter is the one thing deliberately changed. 1.0's ring has no
gaps at all, which is fine for a tile map whose edge the camera never reaches
and wrong for a 3D zone: a player walking the boundary should be able to see
that the road goes somewhere. Three gaps — south, west and east — and the roads
run through them.

## Layout, in tile indices

    j=63  ┌──────────────── N wall ───────────────┐
          │  . . .        trees          ┌ruin┐   │
    j=48  │ ~~lake~~                     │    │   │
          │  ~~~~~                       └────┘   │
    j=44  │        ╔═══ dirt ══════════╗          │
          │        ║                              │
    j=14 ─┤══ dirt ══╦══ TOWN ══╦══ dirt ═════════├─ portal
          │          ║  cobble  ║                 │
    j= 6  │          ║  4 houses║                 │
          │          ║          ║                 │
    j= 0  └───────── S gap ──────────────────────┘
          i=0                i=31/32              i=63
"""

import unreal

from valhalla_tools.build_zone import (
    FLOOR_TOP, TILE, WALL_HEIGHT, N, ZoneBuilder, rnd, run_builder, tile_xy,
)

LEVEL_PATH = "/Game/Valhalla/Maps/Zones/L_Grasslands"

#: The first template in `npc-templates.json` — "Test Enemy", 150 hp, 100 xp,
#: aggressive. The only template with a loot table, which is what makes it the
#: right one for a gate that also has to show a loot bag drop.
FIRST_TEMPLATE = "npc_1771431708366"

#: The second template, for the one friendly. `npc-templates.json` has three.
MERCHANT_TEMPLATE = "npc_1771709765831"

# ── Feature extents, all in tile indices ────────────────────────────────

#: The outermost ring of tiles.
RING = (0, 0, N - 1, N - 1)

#: The two dirt roads. Each is two tiles wide, as 1.0's were.
ROAD_COLS = (31, 32)
ROAD_ROWS = (13, 14)

#: Where the north spur leaves the vertical road and turns east to the ruin.
SPUR_ROW = (44, 45)
SPUR_END = 52

#: The town's cobbled plaza.
TOWN = (23, 5, 41, 21)

#: The four houses, as (i0, j0) of a 4 x 3 block plus which side the door is on.
#: The door always faces the plaza centre, which is the whole point of naming
#: the side: a door opening onto its own back wall is the layout mistake this
#: zone is most likely to make.
HOUSES = [
    (25, 7, "N"),    # south-west of the square, door facing north into it
    (35, 7, "N"),    # south-east
    (25, 17, "S"),   # north-west, door facing south
    (35, 17, "S"),   # north-east
]

#: The lake. An ellipse, then bitten into with noise so the edge is not a curve.
LAKE_CENTRE = (12.0, 47.5)
LAKE_RADII = (4.6, 3.4)

#: The ruin: a broken square with an L of wall inside it.
RUIN = (46, 46, 58, 58)

#: Tree clusters: (i, j, radius in tiles, density).
TREE_CLUSTERS = [
    (20, 52, 5, 0.42), (30, 56, 4, 0.38), (42, 40, 4, 0.34),
    (8, 26, 4, 0.36), (52, 24, 5, 0.40), (17, 34, 3, 0.30),
    (6, 10, 3, 0.34), (55, 56, 3, 0.30),
]

#: Loose rocks, as tile coordinates. Hand-placed so they sit beside the roads
#: rather than in them.
ROCKS = [
    (5, 20), (9, 40), (14, 22), (19, 8), (22, 43), (27, 47),
    (36, 46), (39, 27), (44, 18), (47, 33), (50, 10), (54, 43),
    (58, 20), (60, 38), (3, 55), (44, 60),
]


class Grasslands(ZoneBuilder):
    ZONE_ID = "grasslands"
    DISPLAY_NAME = "Grasslands"
    KITS = [
        "/Game/Valhalla/Environment/Grassland/{0}/StaticMeshes/{0}",
        "/Game/Valhalla/Environment/Town/{0}/StaticMeshes/{0}",
        "/Game/Valhalla/Props/{0}/StaticMeshes/{0}",
    ]

    _door_mesh = "VB_HouseWall_Door"
    _window_mesh = "VB_HouseWall_Window"

    GRASS = ["SM_Grass_A", "SM_Grass_B", "SM_Grass_C"]

    # ── Passes, in order of authority ───────────────────────────────────
    #
    # Each pass claims the tiles it uses, and the scattering passes at the end
    # only touch what is still free. That ordering is what stops a tree growing
    # through a roof or a barrel floating in the lake, and it is why the
    # cosmetic passes come last rather than first.

    def build(self):
        self.mark_roads()
        self.mark_town()
        self.mark_houses()
        self.mark_lake()
        self.mark_ruin()

        self.build_floor()
        self.flush_floors()

        self.build_perimeter()
        self.build_town()
        self.build_ruin()
        self.build_trees()
        self.build_rocks()

        self.build_gameplay()
        return self.summary()

    # ── Marking: decide what every tile is before anything is placed ────

    def mark_roads(self):
        for i in range(1, N - 1):
            for j in ROAD_ROWS:
                self.claim(i, j, "road")
        for j in range(1, SPUR_ROW[1]):
            for i in ROAD_COLS:
                self.claim(i, j, "road")
        for i in range(ROAD_COLS[0], SPUR_END + 1):
            for j in SPUR_ROW:
                self.claim(i, j, "road")

    def mark_town(self):
        i0, j0, i1, j1 = TOWN
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                # The roads keep their dirt *through* the plaza on the two lanes
                # that cross it, so the crossing reads as a crossroads rather
                # than as a square the roads happen to touch.
                if self.claimed(i, j) == "road":
                    continue
                self.claim(i, j, "plaza")

    def mark_houses(self):
        """Claim the house blocks before the floor pass runs.

        This has to happen *before* `build_floor`, not during `build_town`:
        a house interior is wood and the plaza is cobble, and if the floor pass
        had already laid cobble under the house there would be two floor tiles
        occupying the same 64 cm — which does not look like a bug, it looks
        like z-fighting, and z-fighting on a floor is one of the hardest things
        to attribute to the script that caused it.
        """
        for i0, j0, _door_side in HOUSES:
            i1, j1 = i0 + 3, j0 + 2
            for i in range(i0, i1 + 1):
                for j in range(j0, j1 + 1):
                    on_edge = i in (i0, i1) or j in (j0, j1)
                    self.claim(i, j, "housewall" if on_edge else "house")

    def mark_lake(self):
        cx, cy = LAKE_CENTRE
        rx, ry = LAKE_RADII
        for i in range(int(cx - rx) - 2, int(cx + rx) + 3):
            for j in range(int(cy - ry) - 2, int(cy + ry) + 3):
                if not (1 <= i < N - 1 and 1 <= j < N - 1):
                    continue
                if self.claimed(i, j) in ("road", "plaza", "wall"):
                    continue
                dx = (i - cx) / rx
                dy = (j - cy) / ry
                # The ellipse, with the *threshold* wobbled per tile rather
                # than the radius. Wobbling the radius gives a smooth blob;
                # wobbling the threshold bites single tiles out of the edge,
                # which is what a lake shore looks like at 64 cm resolution.
                threshold = 1.0 + (rnd(i * 37.0 + j * 91.0) - 0.5) * 0.55
                if dx * dx + dy * dy < threshold:
                    self.claim(i, j, "water")

    def mark_ruin(self):
        i0, j0, i1, j1 = RUIN
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                if self.claimed(i, j) == "road":
                    continue
                self.claim(i, j, "ruin")

    # ── Floor ───────────────────────────────────────────────────────────

    def build_floor(self):
        for i in range(N):
            for j in range(N):
                what = self.claimed(i, j)
                seed = i * 131.0 + j * 17.0
                yaw = 90.0 * int(rnd(seed + 3.0) * 4.0)

                if what == "water":
                    self.floor("SM_Water", i, j, yaw)
                elif what == "road":
                    self.floor(self._road_mesh(i, j), i, j, self._road_yaw(i, j))
                elif what == "house":
                    self.floor("SM_WoodFloor", i, j, yaw)
                elif what in ("plaza", "housewall"):
                    # Cobble under the house walls too: a wall stands *on* the
                    # ground, and leaving its tile bare would show a 64 cm hole
                    # in the plaza at the foot of every wall.
                    variant = "SM_Cobble_A" if rnd(seed + 7.0) < 0.65 else "SM_Cobble_B"
                    self.floor(variant, i, j, yaw)
                elif what == "ruin":
                    self.floor("SM_Stone_Floor", i, j, yaw)
                else:
                    # 1.0 flipped 30% of tiles to GRASS_DARK off one seeded LCG
                    # (`:62-68`). Three variants rather than two because the kit
                    # has three, weighted so A dominates and C is the rare one.
                    roll = rnd(seed)
                    variant = self.GRASS[0] if roll < 0.62 else (
                        self.GRASS[1] if roll < 0.88 else self.GRASS[2])
                    self.floor(variant, i, j, yaw)

    def _road_mesh(self, i, j):
        """Corner pieces where the roads turn or cross, straights elsewhere."""
        on_col = i in ROAD_COLS
        on_row = j in ROAD_ROWS or j in SPUR_ROW
        if on_col and on_row:
            return "SM_Dirt_Corner"
        return "SM_Dirt_Straight"

    def _road_yaw(self, i, j):
        """Turn each dirt tile so its grass verges run *along* the road.

        `SM_Dirt_Straight` is not a plain brown square: it has two material
        slots, `M_Dirt` and `M_Grass`, because it models a path with a grass
        verge — and the verge is on the tile's local **Y** edges, so at yaw 0
        the path runs north-south.

        Getting this backwards is not subtle once you look at a top-down
        capture: the verges cross the road instead of edging it, and a
        64 cm-striped green ladder runs the whole length of both roads. It
        reads as missing geometry rather than as a path. So the east-west road
        is the one that needs 90 degrees, and the north-south road is the one
        that needs none.
        """
        north_south = i in ROAD_COLS and not (j in ROAD_ROWS or j in SPUR_ROW)
        return 0.0 if north_south else 90.0

    # ── Perimeter ───────────────────────────────────────────────────────

    def build_perimeter(self):
        """The ring, with the three gaps the roads run through.

        1.0's ring (`FallbackMapGenerator.ts:53-60`) is closed. These gaps are
        the one deliberate departure — see the module docstring — and they are
        exactly where the two roads meet the edge, so each one is a place the
        road visibly continues rather than a hole in a wall.
        """
        gaps = set()
        for j in ROAD_ROWS:
            gaps.add((0, j))            # west
            gaps.add((N - 1, j))        # east, where the portal stands
        for i in ROAD_COLS:
            gaps.add((i, 0))            # south

        placed = self.wall_ring(
            "VB_StoneWall_Straight", "VB_StoneWall_Corner",
            RING[0], RING[1], RING[2], RING[3],
            gaps=gaps, prefix="Ring", end="VB_StoneWall_End")

        self.notes["perimeterWalls"] = placed
        self.notes["perimeterGaps"] = sorted(gaps)

    # ── Town ────────────────────────────────────────────────────────────

    def build_town(self):
        centre_i, centre_j = 32, 13

        for index, (i0, j0, door_side) in enumerate(HOUSES):
            self._house(index, i0, j0, 4, 3, door_side)

        # The well goes on the crossroads itself, which is where 1.0's town had
        # its open square and where a player's eye lands when they arrive.
        self.place_tile("SM_Well", "Well", "Town", 29, 17)
        self.claim(29, 17, "prop")

        self.place_tile("SM_MarketStall", "MarketStall_A", "Town", 35, 12, yaw=180.0)
        self.place_tile("SM_MarketStall", "MarketStall_B", "Town", 28, 12, yaw=180.0)
        self.claim(35, 12, "prop")
        self.claim(28, 12, "prop")

        self.place_tile("SM_Signpost", "Signpost", "Town", 34, 15, yaw=225.0)
        self.claim(34, 15, "prop")

        for index, (i, j) in enumerate([(26, 11), (26, 12), (38, 11), (38, 16), (30, 20)]):
            self.place_tile("SM_Crate", "Crate_%d" % index, "Town", i, j,
                            yaw=rnd(index * 3.0 + 1.0) * 90.0)
            self.claim(i, j, "prop")

        for index, (i, j) in enumerate([(27, 11), (37, 12), (39, 16), (24, 15), (30, 6)]):
            self.place_tile("SM_Barrel", "Barrel_%d" % index, "Town", i, j,
                            yaw=rnd(index * 5.0 + 2.0) * 360.0)
            self.claim(i, j, "prop")

        # Lamp posts on the four corners of the crossroads. The lantern is
        # emissive, so these are what make the town read as a place at all
        # once the fog has dimmed the ground around it to 0.05.
        for index, (i, j) in enumerate([(30, 12), (30, 15), (33, 12), (33, 15)]):
            self.place_tile("SM_LampPost", "LampPost_%d" % index, "Town", i, j)
            self.claim(i, j, "prop")

        # Fences along the plaza's north and south edges, skipping the road
        # lanes so the roads are not fenced across.
        fenced = 0
        i0, j0, i1, j1 = TOWN
        for i in range(i0, i1 + 1):
            if i in ROAD_COLS:
                continue
            for j, yaw in ((j0, 0.0), (j1, 0.0)):
                if self.claimed(i, j) in ("wall", "prop", "road"):
                    continue
                self.place_tile("SM_Fence", "Fence_%02d_%02d" % (i, j), "Town", i, j, yaw=yaw)
                self.claim(i, j, "fence")
                fenced += 1

        self.notes["townFences"] = fenced
        self.notes["townCentre"] = list(tile_xy(centre_i, centre_j))

    def _house(self, index, i0, j0, width, depth, door_side):
        """One house: a wall ring on the block perimeter, floor inside, a roof on top.

        The block is `width x depth` tiles and the walls occupy its perimeter
        *tiles*, which is the convention `build_greybox.py` set and 1.0's own
        building used (`FallbackMapGenerator.ts:110-116`: a wall ring on the
        tiles, with one cleared for a door).
        """
        i1 = i0 + width - 1
        j1 = j0 + depth - 1

        # The door goes in the middle of the named side, and that side is always
        # the one facing the plaza centre — so the tile in front of a door is
        # always plaza, never another wall.
        door_i = i0 + width // 2
        if door_side == "N":
            door = (door_i, j1)
        elif door_side == "S":
            door = (door_i, j0)
        elif door_side == "W":
            door = (i0, j0 + depth // 2)
        else:
            door = (i1, j0 + depth // 2)

        # Windows on the two long sides, away from the door.
        windows = set()
        for i in (i0 + 1, i1 - 1):
            for j in (j0, j1):
                if (i, j) != door and i0 < i < i1:
                    windows.add((i, j))

        # `VB_HouseWall` is used for the corners too. The town kit has no corner
        # piece, and it does not need one: a plastered wall is 64 x 25 and two
        # of them meeting at a corner tile leave a 25 cm notch that the roof
        # overhangs. A stone corner from the other kit would be a different
        # material at the corner of every house, which is far more visible.
        self.wall_ring("VB_HouseWall", "VB_HouseWall", i0, j0, i1, j1,
                       folder="Buildings", prefix="House%d" % index,
                       doors={door}, windows=windows)

        # The interior floor was laid as wood by `build_floor`; `mark_houses`
        # is what told it to. Nothing to do here.

        # Roof. `SM_Roof_Thatch` is a 128 cm (2 tile) square footprint with its
        # pivot at its own base and its ridge along X, so it goes on at the top
        # of the walls. A 4 x 3 tile block is 256 x 192 cm: two pieces side by
        # side along X covers the 256 exactly, and each is stretched to 1.5 on Y
        # to cover the 192. Stretching Y widens the slope span, which is the
        # axis a ridge roof is meant to be stretched along.
        block_x0 = i0 * TILE
        block_y_centre = (j0 + depth / 2.0) * TILE
        roof_z = FLOOR_TOP + WALL_HEIGHT

        for piece in range(width // 2):
            self.place(
                "SM_Roof_Thatch", "House%d_Roof_%d" % (index, piece), "Buildings",
                block_x0 + 64.0 + piece * 128.0, block_y_centre, roof_z,
                scale3d=unreal.Vector(1.0, depth * TILE / 128.0, 1.0))

    # ── Ruin ────────────────────────────────────────────────────────────

    def build_ruin(self):
        """A broken square with an L of wall inside it.

        The point of the ruin is line of sight, not decoration: an L gives a
        corner to break contact around and the broken square gives a room with
        two ways in, which is the geometry Phase 5's fog is most obviously
        right or wrong about.
        """
        i0, j0, i1, j1 = RUIN

        # The broken outer square: the north-east quadrant of the ring is gone.
        gaps = set()
        for i in range(i0 + 5, i1 + 1):
            gaps.add((i, j1))
        for j in range(j0 + 7, j1 + 1):
            gaps.add((i1, j))
        gaps.add((i0 + 3, j0))
        gaps.add((i0 + 4, j0))

        outer = self.wall_ring(
            "VB_StoneWall_Straight", "VB_StoneWall_Corner", i0, j0, i1, j1,
            gaps=gaps, folder="Ruin", prefix="RuinOuter", end="VB_StoneWall_End")

        # The inner L.
        inner = 0
        for i in range(i0 + 3, i0 + 9):
            self.place_tile("VB_StoneWall_Straight", "RuinL_X_%02d" % i, "Ruin",
                            i, j0 + 5, yaw=0.0)
            self.claim(i, j0 + 5, "wall")
            inner += 1

        self.place_tile("VB_StoneWall_Corner", "RuinL_Corner", "Ruin",
                        i0 + 9, j0 + 5, yaw=0.0)
        self.claim(i0 + 9, j0 + 5, "wall")
        inner += 1

        for j in range(j0 + 6, j0 + 11):
            self.place_tile("VB_StoneWall_Straight", "RuinL_Y_%02d" % j, "Ruin",
                            i0 + 9, j, yaw=90.0)
            self.claim(i0 + 9, j, "wall")
            inner += 1

        # A few fallen blocks, so the floor is not a clean slab.
        for index, (di, dj) in enumerate([(2, 2), (6, 9), (10, 3), (4, 11), (11, 8)]):
            i, j = i0 + di, j0 + dj
            if self.claimed(i, j) == "wall":
                continue
            self.place_tile("SM_Rock_A", "RuinRubble_%d" % index, "Ruin", i, j,
                            yaw=rnd(index * 13.0) * 360.0, scale=0.6 + rnd(index * 7.0) * 0.3)
            self.claim(i, j, "prop")

        self.notes["ruinWalls"] = outer + inner

    # ── Scatter ─────────────────────────────────────────────────────────

    def build_trees(self):
        """Tree clusters, 1.0's 35% forest density expressed as blobs.

        1.0 filled a single 24 x 23 rectangle at 35% (`:119-127`), which reads
        as a hedge from above. Clusters with a falloff toward the edge read as
        woodland and leave clearings for enemies to be found in.
        """
        planted = 0
        for index, (ci, cj, radius, density) in enumerate(TREE_CLUSTERS):
            for i in range(ci - radius, ci + radius + 1):
                for j in range(cj - radius, cj + radius + 1):
                    if not (1 <= i < N - 1 and 1 <= j < N - 1) or not self.free(i, j):
                        continue

                    distance = ((i - ci) ** 2 + (j - cj) ** 2) ** 0.5
                    if distance > radius:
                        continue

                    seed = i * 53.0 + j * 29.0 + index
                    if rnd(seed) > density * (1.0 - distance / (radius + 1.0)) * 2.0:
                        continue

                    variant = "SM_Tree_A" if rnd(seed + 0.5) < 0.6 else "SM_Tree_B"
                    self.place_tile(variant, "Tree_%02d_%02d" % (i, j), "Props", i, j,
                                    yaw=rnd(seed + 1.5) * 360.0,
                                    scale=0.85 + rnd(seed + 2.5) * 0.45)
                    self.claim(i, j, "tree")
                    planted += 1

        self.notes["trees"] = planted

    def build_rocks(self):
        placed = 0
        for index, (i, j) in enumerate(ROCKS):
            if not self.free(i, j):
                continue
            seed = index * 11.0 + 5.0
            self.place_tile("SM_Rock_A", "Rock_%02d" % index, "Props", i, j,
                            yaw=rnd(seed) * 360.0, scale=0.9 + rnd(seed + 2.5) * 0.7)
            self.claim(i, j, "rock")
            placed += 1
        self.notes["rocks"] = placed

    # ── Gameplay ────────────────────────────────────────────────────────

    def build_gameplay(self):
        # The zone box and the default spawn: the town crossroads, facing north
        # up the road, so the first thing a new character sees is the town.
        self.zone_volume(32, 11, spawn_yaw=90.0)

        # Four starts around the crossroads, on the plaza rather than in it.
        self.player_starts([(30, 10), (33, 10), (30, 17), (33, 17)], yaw_base=45.0)

        # The portal stands in the east gap of the perimeter, so walking east
        # along the road takes you through the wall and out of the zone.
        self.portal(N - 2, ROAD_ROWS[0], "desert", "entry_from_grasslands",
                    "Scorched Desert", yaw=90.0)

        # And arrivals from the desert come out five tiles inside it, facing
        # west up the same road. Five tiles rather than one: an entry inside its
        # own return portal's trigger is 1.0's ping-pong bug, and the 1 s
        # cooldown should be the second line of defence, not the first.
        self.zone_entry(N - 7, ROAD_ROWS[0], "entry_from_desert", "desert",
                        "From Scorched Desert", yaw=180.0)

        # Seven enemy groups, spread so that no two are in sight of each other
        # and every one is somewhere a player has a reason to be. All on the
        # first template in npc-templates.json.
        groups = [
            ("enemy_field_west",  10, 22, 3, 320, "West field"),
            ("enemy_field_east",  50, 26, 3, 320, "East field"),
            ("enemy_lakeside",    17, 42, 2, 260, "Lakeside"),
            ("enemy_north_road",  32, 36, 2, 220, "North road"),
            ("enemy_ruin_yard",   52, 51, 3, 300, "Ruin courtyard"),
            ("enemy_woods",       21, 57, 2, 280, "North woods"),
            ("enemy_south_road",  32,  3, 2, 200, "South gate road"),
        ]
        for spawn_id, i, j, count, radius, label in groups:
            self.enemy_spawn(spawn_id, i, j, FIRST_TEMPLATE, count, radius, label)

        # One friendly, where 1.0 put "Bjorn the Trader" — beside the market.
        self.npc_spawn("npc_merchant", 34, 12, MERCHANT_TEMPLATE, "Bjorn the Trader")


#: The builder class (history; `build_world.build_zone_level` no longer rebuilds existing zones).
BUILDER = Grasslands


def build(zone_id=None):
    """RETIRED (B-19): never rebuilds ``L_Grasslands``; builds this layout only as a NEW zone.

    Refuses unconditionally — places nothing, writes nothing, returns
    ``{"ok": False, "refused": ...}`` — when the open level is ``L_Grasslands`` or
    any level in ``maps/handedited.json``, and when ``zone_id`` is missing,
    ``"grasslands"``, or a zone that already has a zone volume. There is no ``force``.

    With a new ``zone_id`` and a new, empty level open (create it by hand), it
    lays out the historical grasslands there; see ``build_zone.run_builder``. For a new
    zone use ``ValhallaLevelTools.scaffold_zone`` instead. To get the
    hand-edited ``L_Grasslands`` back as it was, use git history for the ``.umap`` or
    ``Valhalla2/Saved/LevelBackups/<timestamp>/``.
    """
    result = run_builder(Grasslands, LEVEL_PATH, zone_id=zone_id)
    unreal.log("VALHALLA_GRASSLANDS " + str(result))
    return result
