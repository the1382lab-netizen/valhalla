"""Build `/Game/Valhalla/Maps/Zones/L_Desert` from scratch, and its overlay — RETIRED.

**B-19: this no longer runs against `L_Desert`.** The zone is hand-edited now;
`build()` refuses its own target level unconditionally (no `force`) and only
lays the historical layout out under a *new* zone id, into a new empty level.
New zones: `ValhallaLevelTools.scaffold_zone`. Recovering the old `L_Desert`:
git history for the `.umap`, or `Valhalla2/Saved/LevelBackups/<timestamp>/`.
Kept on disk as the record of how the zone was first built.

## What this is a port of

1.0's desert is `maps/desert.json`, a 64 x 64 isometric Tiled map, and
`maps/overlays/desert-overlay.json`, which puts five things in it: one player
spawn, one enemy spawn and *two* portals back to the grasslands plus one
`zone_entry` from it. Unlike the grasslands there is no procedural reference —
`FallbackMapGenerator` only ever generates the grassland theme — so the theme
here comes from the kit rather than from a generator, and the *structure* is
deliberately the grasslands' mirror image:

* the grasslands' portal is on its east edge, so the desert's is on its west;
* the grasslands' fort-equivalent (the ruin) is north-east, so the desert's
  fort is due north;
* the grasslands has a lake north-west, so the desert has its oasis east of
  centre, which is as far from the arrival road as the zone allows.

That mirroring is not decoration. The gate has to walk a player east through
one portal and straight back west through the other, and a zone whose entry and
exit were in unrelated places would make that a hunt.

## The one broken perimeter

The desert gets a perimeter wall the theme does not ask for, because a zone
without one has an edge a player can walk off: the floor stops at 4096 cm and
there is nothing beyond it. Rather than ring it in unbroken sandstone — which
would read as a walled garden, not a desert — the ring is *ruined*: about a
fifth of it is missing, every run is capped with `VB_SandstoneWall_End`, and the
gaps are wide enough to walk through. So it contains the zone in practice
(there is no straight line out that does not cross 40 m of open ground the
camera can see) while reading as the "sandstone wall ruins" the theme calls
for. The one deliberate gap is on the west, where the portal stands.

## Layout, in tile indices

    j=63  ╴ ╴ ╴ ╴ ruined sandstone ring ╴ ╴ ╴ ╴
          ╷      ┌───── FORT ─────┐          ╷
    j=48  ╵      │                │          ╵
          ╷      └──── gate ──────┘          ╷
    j=32  ╵                        ~oasis~   ╷   dunes rise ->
    j=14 portal ══ entry ══ cactus ══        ╵
          ╷    cracked earth       cactus    ╷
    j= 0  ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴ ╴
          i=0                              i=63
"""

import unreal

from valhalla_tools.build_zone import (
    N, ZoneBuilder, ring_tiles, rnd, run_builder, tile_xy,
)

LEVEL_PATH = "/Game/Valhalla/Maps/Zones/L_Desert"

#: The same first template the grasslands uses. One template across both zones
#: is what makes the gate's "desert NPCs are now relevant" line mean something:
#: the only thing distinguishing the two sets is where they are.
FIRST_TEMPLATE = "npc_1771431708366"

RING = (0, 0, N - 1, N - 1)

#: The arrival road, west to east. Two tiles wide, like the grasslands' roads,
#: but cracked earth rather than dirt: a desert has no maintained roads, and
#: the cracked-earth tile is what the kit offers for "ground people walk on".
ROAD_ROWS = (13, 14)

#: The oasis: water, ringed by palms.
OASIS_CENTRE = (44.0, 31.0)
OASIS_RADII = (2.8, 2.4)

#: The abandoned fort, due north.
FORT = (22, 44, 40, 58)

#: Cracked-earth patches: (i, j, radius).
CRACKED = [
    (18, 8, 5), (30, 20, 6), (9, 30, 4), (52, 10, 5),
    (24, 34, 4), (40, 6, 4), (14, 52, 4), (56, 40, 4),
]

#: Dune ridges: (start i, start j, length, step i, step j). `SM_Dune` rises
#: toward +X, so a ridge is a run of them placed along one axis; the yaw turns
#: the rise to face the direction the ridge is meant to climb.
DUNE_RIDGES = [
    (46, 2, 9, 1, 1, 0.0),
    (50, 18, 8, 1, 1, 0.0),
    (54, 46, 7, 1, 1, 0.0),
    (36, 26, 6, 1, 0, 90.0),
    (6, 40, 7, 1, 1, 180.0),
    (10, 18, 6, 1, -1, 180.0),
]

#: Cactus fields: (i, j, radius, density).
CACTUS_FIELDS = [
    (20, 18, 5, 0.30), (34, 10, 5, 0.28), (48, 24, 4, 0.26),
    (12, 24, 4, 0.24), (30, 38, 4, 0.22), (52, 56, 4, 0.24),
]

#: Loose boulders.
BOULDERS = [
    (7, 6), (15, 14), (26, 6), (33, 28), (41, 18), (45, 42),
    (50, 34), (58, 14), (9, 46), (19, 40), (28, 52), (60, 52),
    (16, 60), (38, 62), (4, 20), (55, 6),
]

#: Broken wall ruins away from the fort: (i, j, length, yaw).
WALL_RUINS = [
    (8, 34, 5, 0.0),
    (15, 22, 4, 90.0),
    (44, 8, 5, 0.0),
    (29, 30, 3, 90.0),
    (52, 30, 4, 0.0),
]


class Desert(ZoneBuilder):
    ZONE_ID = "desert"
    DISPLAY_NAME = "Scorched Desert"
    KITS = [
        "/Game/Valhalla/Environment/Desert/{0}/StaticMeshes/{0}",
        "/Game/Valhalla/Environment/Grassland/{0}/StaticMeshes/{0}",
        "/Game/Valhalla/Props/{0}/StaticMeshes/{0}",
    ]

    SAND = ["SM_Sand_A", "SM_Sand_B", "SM_Sand_C"]

    def build(self):
        self.mark_road()
        self.mark_cracked()
        self.mark_oasis()
        self.mark_fort()

        self.build_floor()
        self.flush_floors()

        self.build_perimeter()
        self.build_fort()
        self.build_wall_ruins()
        self.build_oasis_palms()
        self.build_dunes()
        self.build_cactus()
        self.build_boulders()

        self.build_gameplay()
        return self.summary()

    # ── Marking ─────────────────────────────────────────────────────────

    def mark_road(self):
        for i in range(1, N - 1):
            for j in ROAD_ROWS:
                self.claim(i, j, "road")

    def mark_cracked(self):
        for index, (ci, cj, radius) in enumerate(CRACKED):
            for i in range(ci - radius, ci + radius + 1):
                for j in range(cj - radius, cj + radius + 1):
                    if not (1 <= i < N - 1 and 1 <= j < N - 1):
                        continue
                    if not self.free(i, j):
                        continue
                    distance = ((i - ci) ** 2 + (j - cj) ** 2) ** 0.5
                    # Threshold wobble per tile, so a patch has a ragged edge
                    # rather than a circular one. Same trick the grasslands'
                    # lake shore uses, and for the same reason.
                    wobble = radius * (0.75 + rnd(i * 41.0 + j * 83.0 + index) * 0.45)
                    if distance < wobble:
                        self.claim(i, j, "cracked")

    def mark_oasis(self):
        cx, cy = OASIS_CENTRE
        rx, ry = OASIS_RADII
        for i in range(int(cx - rx) - 3, int(cx + rx) + 4):
            for j in range(int(cy - ry) - 3, int(cy + ry) + 4):
                if not (1 <= i < N - 1 and 1 <= j < N - 1):
                    continue
                dx = (i - cx) / rx
                dy = (j - cy) / ry
                distance = dx * dx + dy * dy
                threshold = 1.0 + (rnd(i * 29.0 + j * 67.0) - 0.5) * 0.5
                if distance < threshold:
                    self.claim(i, j, "water")
                elif distance < threshold + 1.4:
                    # The damp ring. Cracked earth stands in for it, which is
                    # what makes the oasis read as an oasis from above: water,
                    # then a dark rim, then sand.
                    if self.free(i, j):
                        self.claim(i, j, "oasis_rim")

    def mark_fort(self):
        i0, j0, i1, j1 = FORT
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                on_edge = i in (i0, i1) or j in (j0, j1)
                self.claim(i, j, "fortwall" if on_edge else "fort")

    # ── Floor ───────────────────────────────────────────────────────────

    def build_floor(self):
        for i in range(N):
            for j in range(N):
                what = self.claimed(i, j)
                seed = i * 127.0 + j * 19.0
                yaw = 90.0 * int(rnd(seed + 3.0) * 4.0)

                if what == "water":
                    self.floor("SM_Water", i, j, yaw)
                elif what in ("cracked", "road", "oasis_rim"):
                    self.floor("SM_CrackedEarth", i, j, yaw)
                elif what in ("fort", "fortwall"):
                    # The fort's courtyard is flagged stone, so it reads as
                    # built rather than as a square drawn on the sand.
                    self.floor("SM_Stone_Floor", i, j, yaw)
                else:
                    roll = rnd(seed)
                    variant = self.SAND[0] if roll < 0.55 else (
                        self.SAND[1] if roll < 0.85 else self.SAND[2])
                    self.floor(variant, i, j, yaw)

    # ── Perimeter ───────────────────────────────────────────────────────

    def build_perimeter(self):
        """A deliberately ruined ring. See the module docstring.

        The gaps are chosen by the same seeded hash the rest of the zone uses,
        so the ruin is in the same places on every rebuild — which matters
        because the top-down capture and the overlay are generated from this
        level and would otherwise describe a different one.
        """
        gaps = set()

        # The west gap, where the portal stands. Deliberate, not random.
        for j in ROAD_ROWS:
            gaps.add((0, j))

        # And roughly a fifth of the rest, in runs of two or three so a gap is
        # wide enough to read as collapsed rather than as one missing brick.
        for i, j, side in self._ring_iter():
            if (i, j) in gaps:
                continue
            seed = i * 17.0 + j * 31.0
            if rnd(seed) < 0.09:
                run = 2 + int(rnd(seed + 1.0) * 2.0)
                for step in range(run):
                    if side in ("S", "N"):
                        gaps.add((min(i + step, N - 2), j))
                    else:
                        gaps.add((i, min(j + step, N - 2)))

        placed = self.wall_ring(
            "VB_SandstoneWall_Straight", "VB_SandstoneWall_Corner",
            RING[0], RING[1], RING[2], RING[3],
            gaps=gaps, prefix="Ring", end="VB_SandstoneWall_End")

        self.notes["perimeterWalls"] = placed
        self.notes["perimeterGapTiles"] = len(gaps)

    @staticmethod
    def _ring_iter():
        return ring_tiles(RING[0], RING[1], RING[2], RING[3])

    # ── Fort ────────────────────────────────────────────────────────────

    def build_fort(self):
        """An abandoned sandstone fort: a wall ring, a gate, and an inner keep.

        Abandoned, so the ring is broken in two places on its far side. The
        gate is on the *south* wall, facing the road the player arrives on —
        the whole reason the fort is due north is that it should be the obvious
        thing to walk toward from the arrival point.
        """
        i0, j0, i1, j1 = FORT
        gate_i = (i0 + i1) // 2

        gaps = {(gate_i, j0), (gate_i + 1, j0)}
        # Two collapses on the north wall.
        for i in range(i0 + 3, i0 + 6):
            gaps.add((i, j1))
        for i in range(i1 - 4, i1 - 2):
            gaps.add((i, j1))

        outer = self.wall_ring(
            "VB_SandstoneWall_Straight", "VB_SandstoneWall_Corner",
            i0, j0, i1, j1, gaps=gaps, folder="Fort", prefix="Fort",
            end="VB_SandstoneWall_End")

        # The inner keep: a smaller square, offset so the courtyard is not
        # symmetrical and there is somewhere to stand out of sight of the gate.
        k0, l0 = i0 + 3, j0 + 4
        k1, l1 = k0 + 7, l0 + 6
        keep = self.wall_ring(
            "VB_SandstoneWall_Straight", "VB_SandstoneWall_Corner",
            k0, l0, k1, l1, gaps={(k0 + 3, l0), (k0 + 4, l0)},
            folder="Fort", prefix="Keep", end="VB_SandstoneWall_End")

        # Rubble in the courtyard.
        rubble = 0
        for index, (di, dj) in enumerate([(2, 2), (14, 3), (16, 10), (3, 12), (12, 13)]):
            i, j = i0 + di, j0 + dj
            if self.claimed(i, j) in ("wall", "prop"):
                continue
            self.place_tile("SM_Boulder_A", "FortRubble_%d" % index, "Fort", i, j,
                            yaw=rnd(index * 19.0) * 360.0,
                            scale=0.5 + rnd(index * 23.0) * 0.35)
            self.claim(i, j, "prop")
            rubble += 1

        self.notes["fortWalls"] = outer + keep
        self.notes["fortGate"] = list(tile_xy(gate_i, j0))

    def build_wall_ruins(self):
        """Isolated stubs of wall out in the sand.

        Gameplay, not scenery: each one is a piece of `VisionBlocker` geometry
        in the open, which is the only cover a desert offers and therefore the
        only thing that makes the fog interesting between the fort and the
        oasis.
        """
        placed = 0
        for index, (i0, j0, length, yaw) in enumerate(WALL_RUINS):
            for step in range(length):
                i = i0 + (step if yaw == 0.0 else 0)
                j = j0 + (0 if yaw == 0.0 else step)
                if not (1 <= i < N - 1 and 1 <= j < N - 1) or not self.free(i, j):
                    continue
                mesh = ("VB_SandstoneWall_End" if step in (0, length - 1)
                        else "VB_SandstoneWall_Straight")
                self.place_tile(mesh, "Ruin%d_%02d" % (index, step), "Ruins", i, j, yaw=yaw)
                self.claim(i, j, "wall")
                placed += 1
        self.notes["wallRuins"] = placed

    # ── Oasis ───────────────────────────────────────────────────────────

    def build_oasis_palms(self):
        """Palms on the damp rim, which is the only place they could grow."""
        planted = 0
        for (i, j), what in sorted(self._claimed.items()):
            if what != "oasis_rim":
                continue
            seed = i * 71.0 + j * 13.0
            if rnd(seed) > 0.45:
                continue
            self.place_tile("SM_Palm", "Palm_%02d_%02d" % (i, j), "Oasis", i, j,
                            yaw=rnd(seed + 1.0) * 360.0,
                            scale=0.9 + rnd(seed + 2.0) * 0.35)
            self.claim(i, j, "palm")
            planted += 1
        self.notes["palms"] = planted

    # ── Dunes ───────────────────────────────────────────────────────────

    def build_dunes(self):
        """Ridges of `SM_Dune`.

        Individual actors rather than an instanced field even though a dune
        blocks no sight, because there are only forty of them and each one has
        its own yaw and scale — an instanced field would be the same number of
        transforms with none of the per-actor readability in the outliner.
        """
        placed = 0
        for index, (i0, j0, length, di, dj, yaw) in enumerate(DUNE_RIDGES):
            for step in range(length):
                i, j = i0 + di * step, j0 + dj * step
                if not (0 <= i < N and 0 <= j < N) or not self.free(i, j):
                    continue
                seed = index * 31.0 + step * 7.0
                self.place_tile("SM_Dune", "Dune%d_%02d" % (index, step), "Dunes", i, j,
                                yaw=yaw + (rnd(seed) - 0.5) * 20.0, z=0.0,
                                scale=1.0 + rnd(seed + 1.0) * 0.6)
                self.claim(i, j, "dune")
                placed += 1
        self.notes["dunes"] = placed

    # ── Scatter ─────────────────────────────────────────────────────────

    def build_cactus(self):
        planted = 0
        for index, (ci, cj, radius, density) in enumerate(CACTUS_FIELDS):
            for i in range(ci - radius, ci + radius + 1):
                for j in range(cj - radius, cj + radius + 1):
                    if not (1 <= i < N - 1 and 1 <= j < N - 1) or not self.free(i, j):
                        continue
                    distance = ((i - ci) ** 2 + (j - cj) ** 2) ** 0.5
                    if distance > radius:
                        continue
                    seed = i * 59.0 + j * 23.0 + index
                    if rnd(seed) > density * (1.0 - distance / (radius + 1.0)) * 2.0:
                        continue
                    variant = "SM_Cactus_A" if rnd(seed + 0.5) < 0.65 else "SM_Cactus_B"
                    self.place_tile(variant, "Cactus_%02d_%02d" % (i, j), "Props", i, j,
                                    yaw=rnd(seed + 1.5) * 360.0,
                                    scale=0.85 + rnd(seed + 2.5) * 0.4)
                    self.claim(i, j, "cactus")
                    planted += 1
        self.notes["cactus"] = planted

    def build_boulders(self):
        placed = 0
        for index, (i, j) in enumerate(BOULDERS):
            if not self.free(i, j):
                continue
            seed = index * 11.0 + 3.0
            self.place_tile("SM_Boulder_A", "Boulder_%02d" % index, "Props", i, j,
                            yaw=rnd(seed) * 360.0, scale=0.8 + rnd(seed + 2.0) * 0.8)
            self.claim(i, j, "boulder")
            placed += 1
        self.notes["boulders"] = placed

    # ── Gameplay ────────────────────────────────────────────────────────

    def build_gameplay(self):
        # The default spawn is on the arrival road looking east, which is where
        # `zones.json` put the desert's too — (160, 3680) in 1.0's pixel space,
        # i.e. hard against one edge on the road. A player who logs straight
        # into the desert (Phase 7's character select will allow it) sees the
        # same view as one who walked in.
        self.zone_volume(8, ROAD_ROWS[0], spawn_yaw=0.0)

        self.player_starts([(9, 12), (9, 15), (12, 12), (12, 15)], yaw_base=0.0)

        # The portal home, in the west gap of the ring.
        self.portal(1, ROAD_ROWS[0], "grasslands", "entry_from_desert",
                    "Grasslands", yaw=90.0)

        # And the arrival point from the grasslands, six tiles east of it —
        # clear of the portal's 96 cm trigger by a wide margin, so the 1 s
        # cooldown is the second line of defence rather than the only one.
        self.zone_entry(7, ROAD_ROWS[0], "entry_from_grasslands", "grasslands",
                        "From Grasslands", yaw=0.0)

        groups = [
            ("enemy_road_east",   26, 12, 3, 320, "Road east"),
            ("enemy_cactus_west", 20, 20, 3, 300, "West cactus field"),
            ("enemy_oasis",       44, 26, 3, 280, "Oasis approach"),
            ("enemy_fort_yard",   31, 51, 4, 340, "Fort courtyard"),
            ("enemy_fort_gate",   31, 41, 2, 220, "Fort gate"),
            ("enemy_dunes_east",  52, 20, 2, 300, "East dunes"),
            ("enemy_south_sand",  38,  5, 2, 280, "South sand"),
            ("enemy_north_west",  12, 50, 2, 300, "North-west waste"),
        ]
        for spawn_id, i, j, count, radius, label in groups:
            self.enemy_spawn(spawn_id, i, j, FIRST_TEMPLATE, count, radius, label)

        self.write_overlay()


#: The builder class (history; `build_world.build_zone_level` no longer rebuilds existing zones).
BUILDER = Desert


def build(zone_id=None):
    """RETIRED (B-19): never rebuilds ``L_Desert``; builds this layout only as a NEW zone.

    Refuses unconditionally — places nothing, writes nothing, returns
    ``{"ok": False, "refused": ...}`` — when the open level is ``L_Desert`` or
    any level in ``maps/handedited.json``, and when ``zone_id`` is missing,
    ``"desert"``, or an overlay that already exists. There is no ``force``.

    With a new ``zone_id`` and a new, empty level open (create it by hand), it
    lays out the historical desert there and writes
    ``overlays-2.0/<zone_id>.json``; see ``build_zone.run_builder``. For a new
    zone use ``ValhallaLevelTools.scaffold_zone`` instead. To get the
    hand-edited ``L_Desert`` back as it was, use git history for the ``.umap`` or
    ``Valhalla2/Saved/LevelBackups/<timestamp>/``.
    """
    result = run_builder(Desert, LEVEL_PATH, zone_id=zone_id)
    unreal.log("VALHALLA_DESERT " + str(result))
    return result
