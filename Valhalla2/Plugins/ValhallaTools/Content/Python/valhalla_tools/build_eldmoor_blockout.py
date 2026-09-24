"""B-06 1.5: the Eldmoor Grasslands (`grasslands_v2`) blockout, from the layout.

`build()` places the whole zone's blockout into `L_GrasslandsV2` - which must
already hold the sculpted Landscape (`eldmoor_terrain.py` heights, imported
through Landscape mode) - reading every position from
`Docs/Zones/Eldmoor/eldmoor_layout.json` (zone-local cm, min corner (0,0),
+X east, +Y south). It works inside `L_World` with the level streamed at its
offset: the offset is read from the streaming level, so nothing here knows the
number 80000.

In the spirit of B-19 it **refuses** to run on a level that already has a
blockout (any actor tagged `EldmoorBlockout`): it is for building the zone
once, not for regenerating hand edits. There is no force.

What it builds (outliner folders `Eldmoor/<Region>/...`):

* River: water surface (MI_Water planes), bank strips and reeds (instanced),
  pawn-only invisible bank walls so the Mistwater is fordable only at the stone
  bridge and the plank footbridge; both bridges.
* Harrow's Rest: palisade (VB_Palisade*), both gates (SM_PalisadeGate), the
  Broken Spur (tavern kit), trader's house and smithy lean-to (town kit, forge
  and anvil), stall, tents, props, cobbled street, bind stone (SM_BindStone),
  lamp posts and gate braziers.
* Ashvane Keep: curtain (keep kit), towers, gatehouse, inner wall and gate,
  postern, great hall / barracks / chapel / tower-room partitions, bailey and
  courtyard dressing, the undercroft (room walls, floor, SM_StoneRamp,
  SM_KeepParapetLow, cell bars, torches) under the hall floor.
* Greyfell: tor cliff faces, SM_CliffCleft with an unmarked trigger
  placeholder (no portal, no glow), scarp, the north-edge cliffs, boulders.
* Kingsbarrow ruins, Burnt Steadings (VB_ScorchedWall farmsteads, cart,
  fences), camps C1-C10 (camp kit; fires only at beacons), thickets TH1-TH6
  (VB_Thicket_A/B), trees and scatter (instanced), mist-pocket markers
  (`PH_Mist_*`), guard posts (`GuardPost_*`), zone-edge walls.
* Gameplay: both portals and both zone entries, the four starts moved to the
  Grasslands-side arrival, the zone volume's default spawn at Harrow's Rest,
  fog bounds and a NavMeshBoundsVolume over the zone.

`add_neighbour_portals()` adds the Grasslands and Desert ends (portal + entry)
to `L_Grasslands_Gameplay` / `L_Desert_Gameplay` (idempotent: it skips what is
there). Lights: `light_fires()` gives this level's fires, braziers, forges and
torches their `fire_lights` point lights and its lamp posts theirs, touching
nothing in other levels.
"""

import json
import math
import os
import struct

import unreal

from valhalla_tools import build_zone

TAG = "EldmoorBlockout"
LEVEL = "/Game/Valhalla/Maps/Zones/L_GrasslandsV2"
LEVEL_NAME = "L_GrasslandsV2"
FOLDER = "Eldmoor"
WATER_Z = 8.0
HALL_Z = 400.0          # keep plateau / great hall floor (landscape)
FLOOR_T = 6.0           # floor tile thickness
CELLAR_Z = 100.0

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

KITS = {
    "Grassland": ["SM_Rock_A", "SM_Tree_A", "SM_Tree_B", "SM_Stone_Floor", "SM_Water"],
    "Town": ["SM_Barrel", "SM_Cobble_A", "SM_Cobble_B", "SM_Crate", "SM_Fence", "SM_LampPost", "SM_MarketStall",
             "SM_Roof_Thatch", "SM_Signpost", "SM_Well", "SM_WoodFloor", "VB_HouseWall", "VB_HouseWall_Door",
             "VB_HouseWall_Window"],
    "Camp": ["SM_Bedroll", "SM_Campfire", "SM_CampfireCooking", "SM_LogSeat", "SM_SupplyPile", "SM_TentA",
             "SM_TentB", "SM_WeaponRack"],
    "Keep": ["SM_KeepBanner", "SM_KeepGate", "VB_KeepCorner", "VB_KeepTower", "VB_KeepWall", "VB_KeepWall_Slit"],
    "Ruins": ["SM_RubblePile", "SM_RuinArch", "SM_RuinColumn", "SM_RuinColumnFallen", "SM_RuinStatue",
              "SM_RuinWall_Low", "VB_RuinCorner", "VB_RuinWall", "VB_RuinWall_Broken"],
    "River": ["SM_BridgeStone", "SM_BridgeWood", "SM_Reeds", "SM_RiverBank_Corner", "SM_RiverBank_Edge"],
    "Temple": ["SM_Brazier", "SM_Statue", "SM_TempleAltar", "SM_TempleColumn", "SM_TempleFloor",
               "SM_TempleSteps", "VB_TempleWall"],
    "Tavern": ["SM_TavernChimney", "SM_TavernFloor", "SM_TavernRoof", "SM_TavernSign", "VB_TavernCorner",
               "VB_TavernWall", "VB_TavernWall_Door", "VB_TavernWall_Window"],
    "Interior": ["SM_BarCounter", "SM_Bed", "SM_Bench", "SM_Books", "SM_Candle", "SM_Chair", "SM_Chest",
                 "SM_Fireplace", "SM_Keg", "SM_Shelf", "SM_Table", "SM_TableClutter"],
    "Foliage": ["SM_BushA", "SM_BushB", "SM_DeadTree", "SM_Fern", "SM_FlowersA", "SM_FlowersB", "SM_GrassTuft",
                "SM_LogFallen", "SM_Stump"],
    "Eldmoor": ["SM_Anvil", "SM_BindStone", "SM_Cart", "SM_CellBars", "SM_CliffCleft", "SM_Forge", "SM_HayBale",
                "SM_KeepParapetLow", "SM_PalisadeGate", "SM_ScorchedBeams", "SM_ScorchedPost", "SM_StoneRamp",
                "SM_StrawPile", "SM_TorchSconce", "SM_TrainingPost", "VB_BoulderLarge_A", "VB_BoulderLarge_B",
                "VB_CliffCorner", "VB_CliffFace_Low", "VB_CliffFace_Mid", "VB_CliffFace_Tall",
                "VB_PalisadeCorner", "VB_PalisadeWall", "VB_PalisadeWall_Long", "VB_RockOutcrop",
                "VB_ScorchedWall", "VB_Thicket_A", "VB_Thicket_B"],
}
MESH_PATH = {m: "/Game/Valhalla/Environment/{0}/{1}/StaticMeshes/{1}".format(k, m) for k, ms in KITS.items() for m in ms}
MESH_PATH["SM_PortalMarker"] = "/Game/Valhalla/Props/SM_PortalMarker/StaticMeshes/SM_PortalMarker"
MESH_PATH["Plane"] = "/Engine/BasicShapes/Plane"
MESH_PATH["Cube"] = "/Engine/BasicShapes/Cube"

T = 64.0  # design tile, cm

#: Zone entries are found by id across the whole world (AValhallaZoneEntry::Find),
#: so the layout's ids ("entry_from_grasslands" is also the Desert's entry from
#: the Grasslands, "entry_from_desert" the Grasslands' from the Desert) would
#: collide. Every Eldmoor-related entry is prefixed with the zone it stands in.
ENTRY_IDS = {"entry_from_grasslands": "eldmoor_from_grasslands", "entry_from_desert": "eldmoor_from_desert"}
PORTAL_TARGETS = {"portal_to_grasslands": "grasslands_from_eldmoor", "portal_to_desert": "desert_from_eldmoor"}


def _log(msg):
    unreal.log("VALHALLA_ELDMOOR " + msg)


def rnd(seed):
    v = math.sin(seed * 12.9898 + 78.233) * 43758.5453
    return v - math.floor(v)


def seg_dist(px, py, a, b):
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def poly_dist(px, py, pts):
    return min(seg_dist(px, py, a, b) for a, b in zip(pts, pts[1:]))


class Heights(object):
    """Bilinear lookup in the heightmap the Landscape was imported from."""

    def __init__(self, repo):
        tdir = os.path.join(repo, "Docs", "Zones", "Eldmoor", "terrain")
        info = json.load(open(os.path.join(tdir, "eldmoor_terrain.json")))
        self.n = info["verts"]
        self.q = info["quadCm"]
        self.o = info["originZoneLocalCm"]
        self.k = info["cmPerUnit"]
        raw = open(os.path.join(tdir, "eldmoor_height.r16"), "rb").read()
        self.h = struct.unpack("<%dH" % (self.n * self.n), raw)

    def at(self, x, y):
        fx = (x - self.o) / self.q
        fy = (y - self.o) / self.q
        i = max(0, min(self.n - 2, int(math.floor(fx))))
        j = max(0, min(self.n - 2, int(math.floor(fy))))
        tx, ty = fx - i, fy - j
        g = lambda a, b: (self.h[b * self.n + a] - 32768) * self.k
        return ((g(i, j) * (1 - tx) + g(i + 1, j) * tx) * (1 - ty) +
                (g(i, j + 1) * (1 - tx) + g(i + 1, j + 1) * tx) * ty)


def _streaming(name):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    return world, unreal.GameplayStatics.get_streaming_level(world, name)


class Builder(object):
    def __init__(self):
        self.repo = build_zone.repo_root()
        self.layout = json.load(open(os.path.join(self.repo, "Docs", "Zones", "Eldmoor", "eldmoor_layout.json"),
                                     encoding="utf-8"))
        self.hm = Heights(self.repo)
        world, sl = _streaming(LEVEL_NAME)
        if sl is None:
            raise RuntimeError("{} is not a streaming level of the open world".format(LEVEL_NAME))
        self.world = world
        self.off = sl.get_editor_property("level_transform").translation
        unreal.EditorLevelUtils.make_level_current(sl)
        self.level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).get_current_level()
        if self.level.get_outer().get_name() != LEVEL_NAME:
            raise RuntimeError("could not make {} current".format(LEVEL_NAME))
        self.meshes = {}
        self.inst = {}
        self.counts = {}
        self.notes = {}
        self.water_mi = unreal.EditorAssetLibrary.load_asset("/Game/Valhalla/Materials/Sets/MI_Water")

    # ── helpers ──────────────────────────────────────────────────────────
    def mesh(self, name):
        if name not in self.meshes:
            m = unreal.EditorAssetLibrary.load_asset(MESH_PATH[name])
            if m is None:
                raise RuntimeError("missing mesh " + MESH_PATH[name])
            self.meshes[name] = m
        return self.meshes[name]

    def gz(self, x, y):
        return self.hm.at(x, y)

    def W(self, x, y, z):
        return unreal.Vector(self.off.x + x, self.off.y + y, self.off.z + z)

    def _finish(self, actor, label, folder):
        actor.set_actor_label(label)
        actor.set_folder_path("{}/{}".format(FOLDER, folder) if folder else FOLDER)
        actor.tags = [TAG]
        key = folder.split("/")[0] if folder else "-"
        self.counts[key] = self.counts.get(key, 0) + 1
        return actor

    def place(self, mesh, label, folder, x, y, z=None, yaw=0.0, scale=None, dz=0.0, pitch=0.0, roll=0.0):
        if z is None:
            z = self.gz(x, y)
        a = EAS.spawn_actor_from_object(self.mesh(mesh), self.W(x, y, z + dz), unreal.Rotator(roll, pitch, yaw))
        if scale is not None:
            s = scale if isinstance(scale, (tuple, list)) else (scale, scale, scale)
            a.set_actor_scale3d(unreal.Vector(*s))
        return self._finish(a, label, folder)

    def place_t(self, mesh, label, folder, tx, ty, **kw):
        return self.place(mesh, label, folder, tx * T, ty * T, **kw)

    def cls(self, klass, label, folder, x, y, z=None, yaw=0.0, dz=0.0):
        if z is None:
            z = self.gz(x, y)
        a = EAS.spawn_actor_from_class(klass, self.W(x, y, z + dz), unreal.Rotator(0.0, 0.0, yaw))
        return self._finish(a, label, folder)

    def instance(self, mesh, x, y, z=None, yaw=0.0, scale=1.0, folder="Scatter"):
        if z is None:
            z = self.gz(x, y)
        s = scale if isinstance(scale, (tuple, list)) else (scale, scale, scale)
        t = unreal.Transform(location=unreal.Vector(x, y, z), rotation=unreal.Rotator(0.0, 0.0, yaw),
                             scale=unreal.Vector(*s))
        self.inst.setdefault((mesh, folder), []).append(t)

    def flush_instances(self):
        for (mesh, folder), ts in sorted(self.inst.items()):
            f = EAS.spawn_actor_from_class(unreal.ValhallaTileField, self.W(0, 0, 0), unreal.Rotator())
            f.set_tile_mesh(self.mesh(mesh))
            f.clear_tiles()
            f.add_tiles(ts)
            self._finish(f, "Inst_{}_{}".format(folder.replace("/", "_"), mesh), folder)
        self.notes["instances"] = {"{}:{}".format(f, m): len(ts) for (m, f), ts in self.inst.items()}
        self.inst = {}

    def invisible_wall(self, label, folder, x0, y0, x1, y1, z0, z1, thick=30.0):
        """Pawn-only blocker (InvisibleWall profile: no sight, no cursor) between two points."""
        cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        length = math.hypot(x1 - x0, y1 - y0)
        yaw = math.degrees(math.atan2(y1 - y0, x1 - x0))
        a = self.place("Cube", label, folder, cx, cy, z=(z0 + z1) / 2.0, yaw=yaw,
                       scale=(length / 100.0, thick / 100.0, (z1 - z0) / 100.0))
        c = a.static_mesh_component
        c.set_collision_profile_name("InvisibleWall")
        c.set_editor_property("cast_shadow", False)
        a.set_actor_hidden_in_game(True)
        return a

    def marker(self, label, folder, x, y, z=None, dz=100.0, tags=()):
        a = self.cls(unreal.TargetPoint, label, folder, x, y, z=z, dz=dz)
        a.tags = [TAG] + list(tags)
        return a

    def run(self, straight, length, label, folder, x0, y0, x1, y1, yaw, z=None, scale=None, gaps=()):
        """Fill a straight line with pieces `length` long; skip pieces whose centre lies in a gap (x,y,r)."""
        d = math.hypot(x1 - x0, y1 - y0)
        n = max(1, int(round(d / length)))
        placed = []
        for k in range(n):
            t = (k + 0.5) / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            if any(math.hypot(x - gx, y - gy) < gr for gx, gy, gr in gaps):
                continue
            sc = scale
            if sc is None and abs(d / n - length) > 1.0:
                sc = (d / n / length, 1.0, 1.0)
            placed.append(self.place(straight, "{}_{:02d}".format(label, k), folder, x, y, z=z, yaw=yaw, scale=sc))
        return placed

    # ── pieces of the zone ───────────────────────────────────────────────
    def river(self):
        L = self.layout
        pts = [tuple(p) for p in L["river"]["centreline"]]
        (x0, y0), (x1, y1) = pts[0], pts[1]
        pts.insert(0, (x0 - (x1 - x0) * 0.6, y0 - (y1 - y0) * 0.6))
        (xa, ya), (xb, yb) = pts[-2], pts[-1]
        pts.append((xb + (xb - xa) * 0.6, yb + (yb - ya) * 0.6))
        bridges = [(tuple(c["centre"]), c["id"]) for c in L["river"]["crossings"]]
        bridge_gaps = [(tuple(c["centre"]), 70.0 if c["id"] == "stone_bridge" else 57.0) for c in L["river"]["crossings"]]
        for i, (a, b) in enumerate(zip(pts, pts[1:])):
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            yaw = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
            w = self.place("Plane", "Water_{:02d}".format(i), "River/Water", (a[0] + b[0]) / 2, (a[1] + b[1]) / 2,
                           z=WATER_Z, yaw=yaw, scale=((length + 380.0) / 100.0, 3.7, 1.0))
            c = w.static_mesh_component
            c.set_material(0, self.water_mi)
            c.set_collision_profile_name("NoCollision")
            c.set_editor_property("cast_shadow", False)
            ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
            nx, ny = -uy, ux
            for side, sname in ((1, "S"), (-1, "N")):
                # bank strips, land on the strip's local +Y
                n_strip = int(length // 64)
                yaw_s = math.degrees(math.atan2(-(nx * side), (ny * side)))
                for k in range(n_strip):
                    t = (k + 0.5) * length / n_strip
                    px, py = a[0] + ux * t + nx * side * 168.0, a[1] + uy * t + ny * side * 168.0
                    if any(abs(px - bx) < 90 and abs(py - by) < 300 for (bx, by), _ in bridges):
                        continue
                    self.instance("SM_RiverBank_Edge", px, py, z=WATER_Z - 2.0, yaw=yaw_s, folder="River/Banks",
                                  scale=(1.02, 1.0, 1.0))
                    if rnd(i * 97 + k * 13 + side) < 0.22:
                        rr = 150.0 + 60.0 * rnd(k * 7 + i)
                        rx, ry = a[0] + ux * t + nx * side * rr, a[1] + uy * t + ny * side * rr
                        self.instance("SM_Reeds", rx, ry, z=WATER_Z + 20.0 if rr < 168 else None,
                                      yaw=360 * rnd(k + i * 3), folder="River/Banks")
                # pawn walls along the bank lip, broken at the bridges (gap = bridge deck + 12 cm a side)
                n_wall = max(1, int(math.ceil(length / 256.0)))
                for k in range(n_wall):
                    t0, t1 = k * length / n_wall, (k + 1) * length / n_wall
                    near = [(bx, by, hw) for (bx, by), hw in bridge_gaps
                            if abs(a[0] + ux * (t0 + t1) / 2 + nx * side * 200.0 - bx) < 300
                            and abs(a[1] + uy * (t0 + t1) / 2 + ny * side * 200.0 - by) < 400]
                    subs = [(t0, t1)] if not near else [(t0 + (t1 - t0) * q / 8.0, t0 + (t1 - t0) * (q + 1) / 8.0)
                                                        for q in range(8)]
                    for q, (s0, s1) in enumerate(subs):
                        wx0, wy0 = a[0] + ux * s0 + nx * side * 200.0, a[1] + uy * s0 + ny * side * 200.0
                        wx1, wy1 = a[0] + ux * s1 + nx * side * 200.0, a[1] + uy * s1 + ny * side * 200.0
                        mx = (wx0 + wx1) / 2
                        if any(abs(mx - bx) < hw for bx, by, hw in near):
                            continue
                        self.invisible_wall("BankWall_{}_{:02d}_{}_{}".format(sname, i, k, q), "River/BankWalls",
                                            wx0, wy0, wx1, wy1, -100.0, 260.0)
        # bridges (N-S): stone 384 long, footbridge scaled to span 384
        stone = [c for c in L["river"]["crossings"] if c["id"] == "stone_bridge"][0]
        self.place("SM_BridgeStone", "Bridge_Stone", "River", stone["centre"][0], stone["centre"][1], z=46.0, yaw=90.0)
        foot = [c for c in L["river"]["crossings"] if c["id"] == "plank_footbridge"][0]
        self.place("SM_BridgeWood", "Bridge_Footbridge", "River", foot["centre"][0], foot["centre"][1], z=46.0,
                   yaw=90.0, scale=(1.55, 1.0, 1.0))
        self.place_t("SM_Signpost", "Signpost_BridgeSouth", "River", 57.5, 69.0, yaw=180.0)

    def outpost(self):
        f = "Outpost"
        z = HALL_Z
        x0, y0, x1, y1 = 36 * T, 100 * T, 64 * T, 124 * T
        long, short = "VB_PalisadeWall_Long", "VB_PalisadeWall"

        def palisade(ax, ay, bx, by, yaw, label):
            d = math.hypot(bx - ax, by - ay)
            nl = int(d // 256)
            rest = d - nl * 256
            ns = int(round(rest / 64))
            ux, uy = (bx - ax) / d, (by - ay) / d
            pos = 0.0
            for k in range(nl):
                self.place(long, "{}_L{:02d}".format(label, k), f + "/Palisade", ax + ux * (pos + 128), ay + uy * (pos + 128), z=z, yaw=yaw)
                pos += 256
            for k in range(ns):
                self.place(short, "{}_S{:02d}".format(label, k), f + "/Palisade", ax + ux * (pos + 32), ay + uy * (pos + 32), z=z, yaw=yaw)
                pos += 64

        # north wall y=100, gate x 47-53 (SM_PalisadeGate centred (50,100), yaw 180)
        palisade(x0, y0, 47 * T, y0, 180.0, "Palisade_N_W")
        palisade(53 * T, y0, x1, y0, 180.0, "Palisade_N_E")
        palisade(x0, y1, x1, y1, 0.0, "Palisade_S")
        palisade(x0, y0, x0, y1, 90.0, "Palisade_W")
        palisade(x1, y0, x1, 109 * T, -90.0, "Palisade_E_N")
        palisade(x1, 115 * T, x1, y1, -90.0, "Palisade_E_S")
        for cx, cy, n in ((x0, y0, "NW"), (x1, y0, "NE"), (x0, y1, "SW"), (x1, y1, "SE")):
            self.place("VB_PalisadeCorner", "Palisade_Corner_" + n, f + "/Palisade", cx, cy, z=z)
        self.place("SM_PalisadeGate", "Palisade_Gate_North", f + "/Palisade", 50 * T, y0, z=z, yaw=180.0)
        self.place("SM_PalisadeGate", "Palisade_Gate_East", f + "/Palisade", x1, 112 * T, z=z, yaw=-90.0)
        self.place("SM_KeepBanner", "Banner_Harrow_EastGate", f + "/Palisade", x1 + 20, 112 * T, z=z + 300, yaw=-90.0)

        # the Broken Spur (tavern kit) x 38-47, y 102-109; door south at x 42-44
        self.building("Tavern", 38, 102, 47, 109, "VB_TavernWall", "VB_TavernWall_Door", "VB_TavernWall_Window",
                      "VB_TavernCorner", door=("S", 43.0), windows={("N", 40), ("N", 44), ("W", 104), ("E", 106), ("S", 40)},
                      floor="SM_TavernFloor", roof="SM_TavernRoof", wall_h=360.0, folder=f + "/Tavern")
        self.place_t("SM_TavernChimney", "Tavern_Chimney", f + "/Tavern", 39.0, 102.5, z=z + 360)
        self.place_t("SM_TavernSign", "Tavern_Sign", f + "/Tavern", 45.0, 109.3, z=z + 250, yaw=0.0)
        self.place_t("SM_BarCounter", "Tavern_Bar", f + "/Tavern", 42.0, 104.0, z=z + FLOOR_T, yaw=0.0)
        for k, (tx, ty) in enumerate(((39.5, 106.5), (44.5, 106.5), (45.2, 103.8))):
            self.place_t("SM_Table", "Tavern_Table_%d" % k, f + "/Tavern", tx, ty, z=z + FLOOR_T)
        self.place_t("SM_Keg", "Tavern_Keg", f + "/Tavern", 38.8, 103.0, z=z + FLOOR_T)
        # trader's house (town kit) x 52-60, y 102-108, door south
        self.building("Trader", 52, 102, 60, 108, "VB_HouseWall", "VB_HouseWall_Door", "VB_HouseWall_Window", None,
                      door=("S", 56.0), windows={("N", 54), ("N", 58), ("E", 105)}, floor="SM_WoodFloor",
                      roof="SM_Roof_Thatch", wall_h=180.0, folder=f + "/Trader")
        # smithy lean-to x 38-45, y 114-120, open south
        self.building("Smithy", 38, 114, 45, 120, "VB_HouseWall", None, None, None, door=None, windows=set(),
                      floor=None, roof="SM_Roof_Thatch", wall_h=180.0, folder=f + "/Smithy", open_side="S")
        self.place_t("SM_Forge", "Smithy_Forge", f + "/Smithy", 41.5, 115.3, z=z)
        self.place_t("SM_Anvil", "Smithy_Anvil", f + "/Smithy", 43.8, 117.2, z=z, yaw=20.0)
        self.place_t("SM_MarketStall", "Provisioner_Stall", f + "/Props", 50.0, 117.0, z=z)
        self.place_t("SM_TentB", "Captain_Tent", f + "/Props", 57.5, 112.5, z=z)
        self.place_t("SM_TentA", "Guard_Tent_A", f + "/Props", 58.0, 118.0, z=z, yaw=90.0)
        self.place_t("SM_TentA", "Guard_Tent_B", f + "/Props", 61.0, 118.0, z=z, yaw=90.0)
        kinds = {"well": "SM_Well", "campfire": "SM_Campfire", "log_seat": "SM_LogSeat", "supply_pile": "SM_SupplyPile",
                 "weapon_rack": "SM_WeaponRack", "lamp_post": "SM_LampPost", "signpost": "SM_Signpost",
                 "brazier": "SM_Brazier", "bind_stone": "SM_BindStone", "barrel": "SM_Barrel", "crate": "SM_Crate"}
        for k, p in enumerate(self.layout["outpost"]["props"]):
            x, y = p["cm"]
            if p["kind"] == "picket_fence":
                for s in range(6):
                    self.place("SM_Fence", "Tavern_Yard_Fence_%d" % s, f + "/Props", x + 32 + s * 64, y, z=z)
                continue
            mesh = kinds[p["kind"]]
            label = "BindStone_HarrowsRest" if p["kind"] == "bind_stone" else "{}_{:02d}".format(mesh[3:], k)
            zz = None if x >= 4096 or y < 6400 else z
            self.place(mesh, label, f + "/Props", x, y, z=zz, yaw=-90.0 if p["kind"] == "signpost" else 0.0)
        self.place_t("SM_WeaponRack", "GatePost_North_Rack", f + "/Props", 53.5, 102.0, z=z, yaw=180.0)
        self.place_t("SM_WeaponRack", "CommandPost_Rack", f + "/Props", 53.5, 113.0, z=z, yaw=90.0)
        for k in range(3):
            self.place_t("SM_Barrel", "Trader_Barrel_%d" % k, f + "/Props", 55.0 + k, 108.6, z=z)
        # cobbled street: east gate -> well -> north gate, two tiles wide
        for tx in range(50, 64):
            for ty in (111, 112):
                self.instance("SM_Cobble_A" if rnd(tx * 3 + ty) < 0.6 else "SM_Cobble_B", tx * T + 32, ty * T + 32,
                              z=z, yaw=90 * int(rnd(tx + ty * 7) * 4), folder=f + "/Street")
        for ty in range(100, 111):
            for tx in (49, 50):
                self.instance("SM_Cobble_A" if rnd(tx * 5 + ty) < 0.6 else "SM_Cobble_B", tx * T + 32, ty * T + 32,
                              z=z, yaw=90 * int(rnd(tx * 11 + ty) * 4), folder=f + "/Street")
        # guard posts (decision 9): markers for the later guards-fight feature
        for g in self.layout["guardPosts"]:
            x, y = g["post"]
            self.marker("GuardPost_" + g["npc"].replace("npc_harrow_", ""), f + "/GuardPosts", x, y, z=z,
                        tags=["GuardPost", g["kind"]])
            if g["path"]:
                for n, (px, py) in zip("AB", g["path"]):
                    self.marker("GuardPatrol_{}_{}".format(g["npc"].replace("npc_harrow_", ""), n), f + "/GuardPosts",
                                px, py, z=z, tags=["GuardPatrol"])
        self.place_t("SM_Signpost", "Signpost_Portal", f + "/Signs", 81.2, 136.0, yaw=-90.0)
        self.place_t("SM_Signpost", "Signpost_Fork", f + "/Signs", 81.5, 127.0, yaw=-90.0)

    def building(self, name, i0, j0, i1, j1, wall, door_mesh, window_mesh, corner, door=None, windows=(),
                 floor=None, roof=None, wall_h=180.0, folder="Buildings", open_side=None, z=HALL_Z):
        """Walls on the footprint's edges (tile lines), a 128 cm door, windows, floor and roof."""
        sides = {"N": ((i0, j0), (i1, j0), 180.0), "S": ((i0, j1), (i1, j1), 0.0),
                 "W": ((i0, j0), (i0, j1), 90.0), "E": ((i1, j0), (i1, j1), -90.0)}
        for s, ((a0, b0), (a1, b1), yaw) in sides.items():
            if s == open_side:
                continue
            horiz = b0 == b1
            n = (a1 - a0) if horiz else (b1 - b0)
            k = 0
            while k < n:
                t = (a0 if horiz else b0) + k
                if door and door[0] == s and door_mesh and abs(t + 1 - door[1]) < 0.01:
                    cx, cy = ((t + 1) * T, b0 * T) if horiz else (a0 * T, (t + 1) * T)
                    self.place(door_mesh, "{}_Door".format(name), folder, cx, cy, z=z, yaw=yaw)
                    k += 2
                    continue
                cx, cy = ((t + 0.5) * T, b0 * T) if horiz else (a0 * T, (t + 0.5) * T)
                mesh = window_mesh if (window_mesh and (s, t) in windows) else wall
                self.place(mesh, "{}_Wall_{}_{:02d}".format(name, s, k), folder, cx, cy, z=z, yaw=yaw)
                k += 1
        if corner:
            for cx, cy in ((i0, j0), (i1, j0), (i0, j1), (i1, j1)):
                self.place(corner, "{}_Corner_{}_{}".format(name, cx, cy), folder, cx * T, cy * T, z=z)
        w, h = (i1 - i0) * T, (j1 - j0) * T
        nx, ny = max(1, int(round(w / 128.0))), max(1, int(round(h / 128.0)))
        sx, sy = w / nx / 128.0, h / ny / 128.0
        for a in range(nx):
            for b in range(ny):
                cx, cy = i0 * T + (a + 0.5) * w / nx, j0 * T + (b + 0.5) * h / ny
                if roof:
                    self.place(roof, "{}_Roof_{}_{}".format(name, a, b), folder, cx, cy, z=z + wall_h,
                               scale=(sx, sy * (128.0 / 140.0 if roof == "SM_Roof_Thatch" else 128.0 / 152.0), 1.0))
                if floor == "SM_TavernFloor":
                    self.place(floor, "{}_Floor_{}_{}".format(name, a, b), folder, cx, cy, z=z, scale=(sx, sy, 1.0))
        if floor == "SM_WoodFloor":
            for a in range(i1 - i0):
                for b in range(j1 - j0):
                    self.instance(floor, (i0 + a + 0.5) * T, (j0 + b + 0.5) * T, z=z, folder=folder + "/Floor")

    def keep(self):
        f = "Keep"
        z = HALL_Z
        C = self.layout["castle"]
        wall = "VB_KeepWall"
        # curtain: 22 modules N/S, 15 E/W (128 cm); gate modules skipped on the south wall
        for k in range(22):
            x = 3200 + 64 + 128 * k
            self.place(wall if k % 4 else "VB_KeepWall_Slit", "Curtain_N_%02d" % k, f + "/Curtain", x, 256, z=z, yaw=180.0)
            if k not in (10, 11):
                self.place(wall if k % 4 else "VB_KeepWall_Slit", "Curtain_S_%02d" % k, f + "/Curtain", x, 2176, z=z, yaw=0.0)
            if k not in (10, 11):
                self.place(wall, "InnerWall_%02d" % k, f + "/InnerWall", x, 1536, z=z, yaw=0.0)
        for k in range(15):
            y = 256 + 64 + 128 * k
            if k != 7:   # postern at y 1152-1280 (tiles 18-20)
                self.place(wall if k % 3 else "VB_KeepWall_Slit", "Curtain_W_%02d" % k, f + "/Curtain", 3200, y, z=z, yaw=90.0)
            self.place(wall if k % 3 else "VB_KeepWall_Slit", "Curtain_E_%02d" % k, f + "/Curtain", 6016, y, z=z, yaw=-90.0)
        self.place("SM_KeepGate", "Gatehouse", f + "/Curtain", 4608, 2176, z=z)
        self.place("SM_KeepGate", "InnerGate", f + "/InnerWall", 4608, 1536, z=z)
        self.place("SM_KeepGate", "Postern", f + "/Curtain", 3200, 1216, z=z, yaw=90.0, scale=(0.5, 1.0, 0.62))
        for t in C["towers"]:
            x, y = t["cm"]
            if t["id"] == "T_W":
                y = 1024   # moved 4 tiles north so the postern door (tiles 18-20) can open beside it
            self.place("VB_KeepTower", "Tower_" + t["id"], f + "/Towers", x, y, z=z)
        self.notes["posternTowerMovedTo"] = [3200, 1024]
        for k, (x, y, yaw) in enumerate(((4608, 2176 + 45, 0.0), (4608, 1536 + 45, 0.0), (3200 - 45, 1216, 90.0))):
            self.place("SM_KeepBanner", "Banner_Ashvane_%d" % k, f + "/Curtain", x, y, z=z + 380, yaw=yaw)

        # interior partitions (tavern-kit stone, 360 cm; chapel temple kit)
        def line(label, mesh, a, b, fixed, horiz, yaw, doors=(), folder=f + "/Rooms"):
            k, t = 0, a
            while t < b - 0.01:
                if any(abs(t + 1 - d) < 0.01 for d in doors) and t + 2 <= b + 0.01:
                    dm = mesh + "_Door" if mesh + "_Door" in MESH_PATH else None
                    cx, cy = ((t + 1) * T, fixed * T) if horiz else (fixed * T, (t + 1) * T)
                    if dm:
                        self.place(dm, "{}_Door_{}".format(label, k), folder, cx, cy, z=z, yaw=yaw)
                    t += 2
                    k += 1
                    continue
                cx, cy = ((t + 0.5) * T, fixed * T) if horiz else (fixed * T, (t + 0.5) * T)
                self.place(mesh, "{}_{:02d}".format(label, k), folder, cx, cy, z=z, yaw=yaw)
                t += 1
                k += 1

        tw = "VB_TavernWall"
        line("Wall_BarracksEast", tw, 4, 17, 62, False, -90.0, doors=(14,))
        line("Wall_HallSouth", tw, 62, 90, 12, True, 0.0, doors=(72,))
        line("Wall_ChapelNorth", "VB_TempleWall", 90, 94, 12, True, 0.0, doors=(92,))
        line("Wall_HallEast", tw, 4, 12, 90, False, -90.0)
        line("Wall_ChapelWest", "VB_TempleWall", 12, 24, 82, False, 90.0, doors=(18,))
        line("Wall_BarracksSouth", tw, 50, 62, 17, True, 0.0)
        line("Wall_ArmouryEast", tw, 4, 8, 54, False, -90.0, doors=(7,))
        line("Wall_ArmourySouth", tw, 50, 54, 8, True, 0.0)
        line("Wall_LanternSouth", tw, 90, 94, 8, True, 0.0, doors=(92,))
        line("Wall_PosternNorth", tw, 50, 54, 18, True, 180.0)
        line("Wall_PosternSouth", tw, 50, 54, 21, True, 0.0)
        line("Wall_PosternEast", tw, 18, 21, 54, False, -90.0, doors=(19,))

        # great hall floor over the undercroft pit (x 61-77, y 5-13), slot x 66-76 / y 5-7 left open
        for a in range(16):
            for b in range(8):
                x, y = 3904 + 32 + 64 * a, 320 + 32 + 64 * b
                if 4224 < x < 4864 and y < 448:
                    continue
                self.instance("SM_Stone_Floor", x, y, z=z, folder=f + "/GreatHall/Floor")
        self.place("SM_StoneRamp", "Undercroft_Ramp", f + "/Undercroft", 4544, 384, z=z + FLOOR_T, yaw=0.0)
        for k in range(5):
            self.place("SM_KeepParapetLow", "Undercroft_Parapet_%d" % k, f + "/Undercroft", 4288 + 128 * k, 448 + 10,
                       z=z + FLOOR_T)
        # undercroft room walls (keep curtain modules, 300 cm tall, BlockAll not VB: they sit under the hall)
        uw = []
        for k in range(8):
            uw.append(("S", 3968 + 64 + 128 * k, 800, 0.0))
        for k in range(4):
            uw.append(("W", 3936, 352 + 64 + 128 * k - 32, 90.0))
            uw.append(("E", 4896, 352 + 64 + 128 * k - 32, -90.0))
        for k, (s, x, y, yaw) in enumerate(uw):
            a = self.place(wall, "Undercroft_Wall_{}_{:02d}".format(s, k), f + "/Undercroft", x, y, z=CELLAR_Z, yaw=yaw,
                           scale=(1.0, 1.0, 300.0 / 425.0))
            a.static_mesh_component.set_collision_profile_name("BlockAll")
        for a in range(7):
            for b in range(4):
                self.instance("SM_Stone_Floor", 3968 + 32 + 128 * a + (64 if a else 0) * 0, 352 + 32 + 128 * b, z=CELLAR_Z,
                              scale=(2.0, 2.0, 1.0), folder=f + "/Undercroft/Floor")
        for k, y in enumerate((448, 576, 704)):
            self.place("SM_CellBars", "Undercroft_CellBars_%d" % k, f + "/Undercroft", 4096, y, z=CELLAR_Z, yaw=90.0)
        for k, (tx, ty) in enumerate(((70.5, 5.6), (72.0, 5.6), (73.5, 5.6), (75.0, 5.6), (71.3, 6.4), (74.2, 6.4))):
            self.place_t("SM_Keg" if k % 2 == 0 else "SM_Crate", "Undercroft_UnderRamp_%d" % k, f + "/Undercroft", tx, ty,
                         z=CELLAR_Z, yaw=90.0 * k)
        self.place_t("SM_Chest", "Undercroft_CastellanChest", f + "/Undercroft", 75.3, 11.6, z=CELLAR_Z, yaw=180.0)
        for k, x in enumerate((4160, 4480, 4800)):
            self.place("SM_TorchSconce", "Undercroft_Torch_%d" % k, f + "/Undercroft", x, 800 - 36, z=CELLAR_Z + 150,
                       yaw=180.0)
        self.place("SM_TorchSconce", "Undercroft_Torch_W", f + "/Undercroft", 3936 + 36, 700, z=CELLAR_Z + 150, yaw=-90.0)

        # dressing
        P = lambda m, lbl, tx, ty, sub, **kw: self.place_t(m, lbl, f + "/" + sub, tx, ty, z=z, **kw)
        P("SM_Fireplace", "Hall_Fireplace", 80.0, 5.05, "GreatHall")
        for k in range(4):
            P("SM_Table", "Hall_TableA_%d" % k, 67.5 + 1.5 * k + 0.0, 9.0, "GreatHall", dz=FLOOR_T)
            P("SM_Table", "Hall_TableB_%d" % k, 78.0 + 1.5 * k, 9.5, "GreatHall")
            P("SM_Bench", "Hall_BenchA_%d" % k, 67.5 + 1.5 * k, 10.0, "GreatHall", dz=FLOOR_T)
            P("SM_Bench", "Hall_BenchB_%d" % k, 78.0 + 1.5 * k, 10.5, "GreatHall")
        P("SM_Chair", "Hall_CastellanChair", 86.5, 8.0, "GreatHall", yaw=90.0)
        P("SM_KeepBanner", "Hall_Banner", 88.0, 4.6, "GreatHall", dz=330)
        for k, ty in enumerate((9.0, 10.3, 11.6, 12.9, 14.2, 15.5)):
            P("SM_Bed", "Barracks_Bed_%d" % k, 52.6, ty, "Barracks", yaw=0.0)
            P("SM_Chest", "Barracks_Chest_%d" % k, 54.2, ty, "Barracks", yaw=90.0)
        P("SM_Table", "Barracks_Table", 58.5, 11.0, "Barracks", yaw=90.0)
        P("SM_Bench", "Barracks_Bench_A", 57.6, 11.0, "Barracks", yaw=90.0)
        P("SM_Bench", "Barracks_Bench_B", 59.4, 11.0, "Barracks", yaw=90.0)
        P("SM_Candle", "Barracks_Candle", 58.5, 11.0, "Barracks", dz=52)
        P("SM_Fireplace", "Barracks_Fireplace", 58.0, 5.05, "Barracks")
        P("SM_Keg", "Barracks_Keg_A", 60.8, 6.0, "Barracks")
        P("SM_Keg", "Barracks_Keg_B", 61.2, 6.8, "Barracks")
        P("SM_WeaponRack", "Barracks_Rack", 60.5, 15.5, "Barracks", yaw=-90.0)
        P("SM_WeaponRack", "Armoury_Rack_A", 51.2, 6.5, "TowerRooms", yaw=90.0)
        P("SM_Chest", "Armoury_Chest", 52.8, 5.4, "TowerRooms")
        P("SM_Shelf", "Lantern_Shelf", 92.5, 5.3, "TowerRooms")
        P("SM_Brazier", "LanternTower_Brazier", 94.0, 4.0, "Towers", dz=482.0)
        for k, (tx, ty) in enumerate(((84, 14), (84, 17), (84, 20), (91, 14), (91, 17), (91, 20))):
            P("SM_TempleColumn", "Chapel_Column_%d" % k, tx, ty, "Chapel")
        P("SM_TempleAltar", "Chapel_Altar", 87.5, 13.2, "Chapel", yaw=0.0)
        P("SM_TempleSteps", "Chapel_Steps", 87.5, 14.3, "Chapel")
        P("SM_RuinStatue", "Chapel_DefacedSaint", 87.5, 12.6, "Chapel")
        for k, tx in enumerate((86.6, 88.4)):
            P("SM_Candle", "Chapel_Candle_%d" % k, tx, 13.2, "Chapel", dz=106)
        P("SM_Books", "Chapel_Books", 86.0, 22.5, "Chapel")
        for a in range(6):
            for b in range(6):
                self.instance("SM_TempleFloor", 82 * T + 64 + a * 128 - 16, 12 * T + 64 + b * 128, z=z, folder=f + "/Chapel/Floor",
                              scale=(0.94, 1.0, 1.0))
        for k, (tx, ty) in enumerate(((66, 16), (68, 18), (78, 16))):
            P("SM_TrainingPost", "Yard_TrainingPost_%d" % k, tx, ty, "Courtyard", yaw=30.0 * k)
        for k, tx in enumerate((64.5, 67.0, 77.5, 80.0)):
            P("SM_WeaponRack", "Yard_Rack_%d" % k, tx, 23.2, "Courtyard", yaw=180.0)
        P("SM_SupplyPile", "Yard_SupplyPile", 80.0, 22.0, "Courtyard")
        P("SM_Campfire", "Yard_Campfire", 76.0, 22.0, "Courtyard")
        P("SM_Brazier", "InnerGate_Brazier_W", 69.5, 23.0, "Courtyard")
        P("SM_Brazier", "InnerGate_Brazier_E", 74.5, 23.0, "Courtyard")
        # outer bailey: stables west, campfire, well, smithy corner east
        for k, (tx, ty) in enumerate(((51.8, 26.0), (52.5, 26.0), (51.8, 26.7), (53.3, 26.2))):
            P("SM_HayBale", "Bailey_Hay_%d" % k, tx, ty, "Bailey", yaw=90.0 * (k % 2))
        P("SM_StrawPile", "Bailey_Straw", 52.5, 28.5, "Bailey")
        P("SM_Cart", "Bailey_Cart", 55.0, 31.5, "Bailey", yaw=15.0)
        P("SM_SupplyPile", "Bailey_Supplies", 51.8, 31.8, "Bailey")
        P("SM_Barrel", "Bailey_Barrel_A", 53.5, 32.8, "Bailey")
        P("SM_Crate", "Bailey_Crate_A", 54.3, 32.9, "Bailey")
        P("SM_Campfire", "Bailey_Campfire", 60.0, 29.0, "Bailey")
        P("SM_LogSeat", "Bailey_LogSeat_A", 59.0, 30.2, "Bailey")
        P("SM_LogSeat", "Bailey_LogSeat_B", 61.0, 30.2, "Bailey")
        P("SM_Well", "Bailey_Well", 84.0, 29.0, "Bailey")
        P("SM_Forge", "Bailey_Forge", 90.5, 25.4, "Bailey")
        P("SM_Anvil", "Bailey_Anvil", 88.5, 27.0, "Bailey")
        P("SM_WeaponRack", "Bailey_Rack", 92.8, 29.5, "Bailey", yaw=-90.0)
        for k in range(3):
            P("SM_KeepBanner", "InnerWall_Banner_%d" % k, 58.0 + 12.0 * k + (8 if k == 2 else 0), 24.6, "InnerWall", dz=330)
        # gate braziers outside on the glacis, and the approach signpost
        self.place_t("SM_Brazier", "KeepGate_Brazier_W", f + "/Approach", 69.5, 35.6)
        self.place_t("SM_Brazier", "KeepGate_Brazier_E", f + "/Approach", 74.5, 35.6)

    def greyfell(self):
        f = "Highlands"
        # Tor south face (y 26), 6.5 modules x 0-26; the cleft at x = 6 tiles
        for k in range(7):
            x = 128 + 256 * k
            if x > 1664:
                break
            if k == 1:
                self.place("SM_CliffCleft", "Tor_Cleft", f + "/Tor", 384, 1600, z=300.0, yaw=0.0)
                continue
            self.place("VB_CliffFace_Mid", "Tor_FaceS_%d" % k, f + "/Tor", min(x, 1536), 1600, z=300.0, yaw=0.0)
        for k in range(7):
            y = 128 + 256 * k
            if y > 1536:
                break
            self.place("VB_CliffFace_Mid", "Tor_FaceE_%d" % k, f + "/Tor", 1600, y, z=400.0, yaw=-90.0,
                       scale=(1.0, 1.0, 0.78))
        self.place("VB_CliffCorner", "Tor_Corner_SE", f + "/Tor", 1600, 1600, z=300.0, scale=(1.0, 1.0, 0.6))
        # the ridge's west end steps down to the scree at x 26, y 26-28
        self.place("VB_CliffFace_Low", "Tor_RidgeEndStep", f + "/Tor", 1712, 1728, z=280.0, yaw=90.0)
        # tor's north face over the zone edge (to nothing): two rows
        for k in range(7):
            x = 128 + 256 * k
            self.place("VB_CliffFace_Tall", "Tor_FaceN_%d" % k, f + "/Tor", x, -64, z=0.0, yaw=180.0)
        # unmarked cleft trigger placeholder (Phase 2 hidden portal -> cave_dungeon / entry_from_eldmoor)
        trig = self.cls(unreal.TriggerBox, "PH_Portal_GreyfellCleft", f + "/Tor", 384, 1610, z=360.0)
        trig.tags = [TAG, "PH_HiddenPortal", "target:cave_dungeon", "entry:entry_from_eldmoor"]
        trig.set_actor_scale3d(unreal.Vector(1.2, 0.8, 2.0))
        # the screen (design section 8)
        scr = self.layout["hiddenEntrance"]["screen"]
        m = {"boulder (large, art gap: rock outcrop)": "VB_BoulderLarge_A", "boulder (large)": "VB_BoulderLarge_A",
             "dead tree": "SM_DeadTree", "fallen log": "SM_LogFallen", "bush": "SM_BushA", "fern clumps": "SM_Fern",
             "stump": "SM_Stump", "rock (small) x6 scree": "SM_Rock_A"}
        for k, s in enumerate(scr):
            x, y = s["cm"]
            mesh = m[s["prop"]]
            if mesh == "SM_Rock_A":
                for r in range(6):
                    self.place(mesh, "Cleft_Scree_%d" % r, f + "/CleftScreen", x + (rnd(r) - 0.5) * 300,
                               y + (rnd(r + 9) - 0.5) * 220, yaw=360 * rnd(r + 3), scale=0.7 + 0.5 * rnd(r + 5))
            else:
                self.place(mesh, "Cleft_{}_{}".format(mesh[3:], k), f + "/CleftScreen", x, y, yaw=37.0 * k)
        # ridge north cliff and the keep plateau's north edge (to nothing), x 26-98, two rows
        for k in range(28):
            x = 1664 + 128 + 256 * k
            if x > 6272:
                break
            for r, zz in enumerate((0.0, -400.0)):
                self.place("VB_CliffFace_Mid", "NorthCliff_%d_%d" % (k, r), f + "/NorthCliff", x, -64, z=zz, yaw=180.0)
        for k in range(8):
            self.place("VB_CliffFace_Tall", "Tor_FaceN_Low_%d" % k, f + "/Tor", 128 + 256 * k, -64, z=-700.0, yaw=180.0)
        # scarp at y 28 above Hask's Hold (x 26-46), open where the highland path crosses (x ~36)
        for k in range(5):
            x = 1664 + 128 + 256 * k
            if abs(x - 36 * T) < 200:
                continue
            self.place("VB_CliffFace_Low", "Scarp_%d" % k, f + "/Scarp", x, 1792 - 48, z=250.0, yaw=0.0)
        for k in range(3):
            self.place("VB_CliffFace_Low", "PlateauWest_%d" % k, f + "/Scarp", 2944 + 48, 1792 + 128 + 256 * k, z=250.0,
                       yaw=90.0)
        # Hask's Hold rocks and the ridge cairn
        self.place_t("VB_RockOutcrop", "Hask_CrossbowRock", f + "/HasksHold", 25, 34, yaw=20.0)
        self.place_t("VB_BoulderLarge_B", "Hask_SightRock", f + "/HasksHold", 31, 42, yaw=-15.0)
        for r in range(7):
            a = r * 0.9
            self.place("SM_Rock_A", "Cairn_Rock_%d" % r, f + "/RidgeCairn", 42 * T + math.cos(a) * 40, 24 * T + math.sin(a) * 40,
                       dz=(0 if r < 5 else 35), yaw=51.0 * r, scale=0.9)
        for k, (tx, ty) in enumerate(((20, 44), (16, 52), (28, 50), (10, 46), (36, 12), (44, 8), (30, 18), (14, 38))):
            self.place_t("SM_DeadTree", "Highland_DeadTree_%d" % k, f + "/Dressing", tx, ty, yaw=47.0 * k)
        for k in range(18):
            tx, ty = 2 + 44 * rnd(k * 3.1), 28 + 22 * rnd(k * 5.7)
            if abs(tx - 6) < 5 and ty < 36:
                continue
            self.instance("SM_Rock_A", tx * T, ty * T, yaw=360 * rnd(k), scale=0.6 + 0.8 * rnd(k + 2), folder=f + "/Scree")

    def kingsbarrow(self):
        f = "Kingsbarrow"
        W = lambda m, lbl, tx, ty, yaw=0.0, **kw: self.place_t(m, lbl, f + "/Ruins", tx, ty, yaw=yaw, **kw)
        # broken rectangle x 112-124, y 60-72: the half-arch at (114,72) is the pull chokepoint
        for k, tx in enumerate((116, 118, 120, 122)):
            W("VB_RuinWall_Broken" if k % 2 else "VB_RuinWall", "Wall_S_%d" % k, tx, 72.0)
        W("SM_RuinArch", "HalfArch", 114.0, 72.0, yaw=0.0)
        for k, ty in enumerate((61, 63, 67, 69)):
            W("VB_RuinWall_Broken" if k % 2 else "VB_RuinWall", "Wall_W_%d" % k, 112.0, ty + 0.5, yaw=90.0)
        for k, ty in enumerate((61, 65, 69)):
            W("VB_RuinWall_Broken", "Wall_E_%d" % k, 124.0, ty + 0.5, yaw=90.0)
        for k, tx in enumerate((113, 117, 121)):
            W("SM_RuinWall_Low", "Wall_N_%d" % k, tx + 0.5, 60.0)
        W("VB_RuinCorner", "Corner_NW", 112.0, 60.0)
        for k, (tx, ty) in enumerate(((115, 63), (121, 63), (115, 69), (121, 69))):
            W("SM_RuinColumn" if k != 2 else "SM_RuinColumnFallen", "Column_%d" % k, tx, ty, yaw=30.0 * k)
        W("SM_RuinStatue", "HeadlessKing", 116.0, 72.8, yaw=0.0, scale=1.6)
        W("SM_TempleAltar", "AltarStone", 118.0, 65.0)
        W("SM_Brazier", "Altar_Brazier_Cold", 119.4, 65.0)
        for k in range(3):
            W("SM_Candle", "Altar_Candle_%d" % k, 117.4 + 0.6 * k, 65.0, dz=106)
        blue = self.cls(unreal.PointLight, "Kingsbarrow_CultLight", f + "/Ruins", 119.4 * T, 65.0 * T, dz=140.0)
        lc = blue.get_editor_property("point_light_component")
        lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        lc.set_editor_property("intensity", 40.0)
        lc.set_editor_property("light_color", unreal.Color(r=150, g=190, b=255, a=255))
        lc.set_editor_property("attenuation_radius", 600.0)
        lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        blue.tags = [TAG, "ValhallaBeacon"]
        for k, (tx, ty) in enumerate(((108, 58), (128, 62), (126, 74), (106, 70), (132, 66))):
            W("SM_RubblePile", "Rubble_%d" % k, tx, ty, yaw=70.0 * k)
        # P2 crypt hollow
        W("SM_RuinStatue", "Crypt_BrokenStatue", 130.0, 57.0, yaw=200.0, pitch=0.0)
        W("SM_RuinColumnFallen", "Crypt_Column", 128.5, 59.5, yaw=120.0)
        for k in range(3):
            W("SM_RubblePile", "Crypt_Rubble_%d" % k, 129.0 + 1.5 * k, 58.5 + (k % 2), yaw=90.0 * k)

    def steadings(self):
        f = "Steadings"

        def farm(name, i0, j0, i1, j1, gaps=()):
            for s, (a0, b0, a1, b1, yaw) in {"N": (i0, j0, i1, j0, 180.0), "S": (i0, j1, i1, j1, 0.0),
                                              "W": (i0, j0, i0, j1, 90.0), "E": (i1, j0, i1, j1, -90.0)}.items():
                horiz = b0 == b1
                n = int(((a1 - a0) if horiz else (b1 - b0)) // 2)
                for k in range(n):
                    t = (a0 if horiz else b0) + 1 + 2 * k
                    cx, cy = (t, b0) if horiz else (a0, t)
                    if (s, k) in gaps:
                        self.place_t("SM_ScorchedPost", "{}_Post_{}{}".format(name, s, k), f + "/" + name, cx, cy, yaw=yaw)
                        continue
                    self.place_t("VB_ScorchedWall", "{}_Wall_{}{}".format(name, s, k), f + "/" + name, cx, cy, yaw=yaw)
            self.place_t("SM_ScorchedBeams", name + "_Beams", f + "/" + name, (i0 + i1) / 2.0, (j0 + j1) / 2.0, yaw=25.0)

        farm("MillersStead", 130, 1, 136, 6, gaps={("S", 1), ("W", 1), ("E", 0)})
        farm("Farmstead_West", 102, 3, 108, 7, gaps={("S", 1), ("E", 1)})
        farm("Farmstead_East", 122, 12, 128, 16, gaps={("N", 1), ("S", 2)})
        # C10 foragers' yard: fence ring with a gate gap on the south at (113,14)
        x0, y0, x1, y1 = 106, 5, 118, 14
        for tx in range(x0, x1):
            if tx not in (112, 113):
                self.place_t("SM_Fence", "Yard_Fence_S_%d" % tx, f + "/Foragers", tx + 0.5, y1)
            self.place_t("SM_Fence", "Yard_Fence_N_%d" % tx, f + "/Foragers", tx + 0.5, y0, yaw=180.0)
        for ty in range(y0, y1):
            self.place_t("SM_Fence", "Yard_Fence_W_%d" % ty, f + "/Foragers", x0, ty + 0.5, yaw=90.0)
            self.place_t("SM_Fence", "Yard_Fence_E_%d" % ty, f + "/Foragers", x1, ty + 0.5, yaw=-90.0)
        self.place_t("SM_Cart", "Foragers_Cart", f + "/Foragers", 114.5, 9.0, yaw=-20.0)
        self.place_t("SM_Well", "Foragers_Well", f + "/Foragers", 108.0, 7.0)
        self.place_t("SM_Signpost", "Signpost_Landing", f + "/Signs", 133.0, 12.0, yaw=180.0)

    def camps(self):
        fire = {"C1": "SM_Campfire", "C3": "SM_Campfire", "C4": "SM_CampfireCooking", "C5": "SM_Campfire",
                "C10": "SM_CampfireCooking"}
        for c in self.layout["camps"]:
            cid = c["id"]
            x, y = c["centre"]
            f = "Camps/{}_{}".format(cid, c["name"].split(" (")[0].replace("'", "").replace(" ", ""))
            if cid in fire:
                self.place(fire[cid], "{}_Fire".format(cid), f, x, y, scale=1.4 if cid == "C5" else 1.0)
            if cid == "C7":
                continue   # the vigil is the Kingsbarrow ruin (altar + cult light)
            s = int(cid[1:])
            props = [("SM_TentA", 150, 20), ("SM_Bedroll", 110, 140), ("SM_Bedroll", 110, 200), ("SM_SupplyPile", 150, 260),
                     ("SM_WeaponRack", 170, 320), ("SM_LogSeat", 70, 80)]
            if cid in ("C8", "C9"):
                props = [("SM_Bedroll", 110, 140), ("SM_SupplyPile", 150, 260)]
            if cid == "C6":
                props = [("SM_Bedroll", 90, 100), ("SM_Bedroll", 90, 160), ("SM_Bedroll", 110, 220), ("SM_SupplyPile", 150, 300)]
            for k, (mesh, r, ang) in enumerate(props):
                a = math.radians(ang + 40 * rnd(s))
                self.place(mesh, "{}_{}_{}".format(cid, mesh[3:], k), f, x + math.cos(a) * r, y + math.sin(a) * r,
                           yaw=math.degrees(a) + 90)
            if cid == "C2":   # ruined shepherd's hut + the archer's wall + the pull boulder
                for k, (tx, ty, yaw) in enumerate(((6, 112, 0), (8, 112, 0), (5, 113.5, 90), (5, 115.5, 90), (10, 113, 90))):
                    self.place_t("VB_RuinWall_Broken" if k % 2 else "VB_RuinWall", "C2_Hut_%d" % k, f, tx, ty, yaw=yaw)
                self.place_t("VB_BoulderLarge_B", "C2_PullBoulder", f, 14, 118, yaw=30.0)
            if cid == "C5":
                self.place_t("SM_Stump", "C5_BowmanStump", f, 123, 105)
                self.place_t("SM_WeaponRack", "C5_SkinningRack", f, 120, 105.2, yaw=180.0)
            if cid == "C6":
                self.place_t("SM_DeadTree", "C6_FallenGiant", f, 131, 134, yaw=80.0, scale=1.6, pitch=0.0)
                for k in range(3):
                    self.place_t("SM_LogFallen", "C6_Log_%d" % k, f, 128 + 2.5 * k, 133.5 + (k % 2), yaw=15.0 * k, scale=1.3)
            if cid == "C4":
                self.place_t("SM_WeaponRack", "C4_DryingRack", f, 97, 52.4, yaw=180.0)
        for p in self.layout["placeholderCamps"]:
            x, y = p["centre"]
            self.marker("PH_CreatureCamp_" + p["id"], "Camps/Placeholders", x, y, tags=["PH_CreatureCamp", p["id"]])
        self.place_t("SM_RubblePile", "P1_DenBones", "Camps/Placeholders", 100, 100)

    def thickets(self):
        f = "Thornwood/Thickets"
        for t in self.layout["thickets"]:
            tid = t["id"]
            if t["kind"] == "ring":
                cx, cy = t["centre"]
                rx, ry = t["rxCm"], t["ryCm"]
                gx, gy = t["gap"]["centre"]
                perim = 2 * math.pi * math.sqrt((rx * rx + ry * ry) / 2)
                n = int(perim // 118)
                for k in range(n):
                    a = 2 * math.pi * k / n
                    x, y = cx + rx * math.cos(a), cy + ry * math.sin(a)
                    if math.hypot(x - gx, y - gy) < 96 + 64 + 20:
                        continue
                    if x > 9152 - 40 or y > 9152 - 40:
                        continue
                    yaw = math.degrees(math.atan2(ry * math.cos(a), -rx * math.sin(a)))
                    self.place("VB_Thicket_A", "{}_{:02d}".format(tid, k), f + "/" + tid, x, y, yaw=yaw)
            elif t["kind"] == "belt":
                (x0, y0), (x1, y1) = t["points"]
                gaps = [(g["centre"][0], g["centre"][1], g["widthCm"] / 2 + 128) for g in t["gaps"]]
                yaw = math.degrees(math.atan2(y1 - y0, x1 - x0))
                self.run("VB_Thicket_B", 256.0, tid, f + "/" + tid, x0, y0, x1, y1, yaw, gaps=gaps)
            else:   # TH6 corridors along both game trails
                for r in self.layout["roads"]:
                    if r["id"] not in ("trail_thornwood_a", "trail_thornwood_b"):
                        continue
                    pts = r["points"]
                    k = 0
                    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
                        d = math.hypot(bx - ax, by - ay)
                        ux, uy = (bx - ax) / d, (by - ay) / d
                        steps = int(d // 150)
                        for s in range(1, steps):
                            k += 1
                            if k % 5 == 0:
                                continue   # breaks: ferns and logs
                            px, py = ax + ux * s * 150, ay + uy * s * 150
                            for side in (1, -1):
                                x, y = px - uy * side * 176, py + ux * side * 176
                                if min(poly_dist(x, y, p) for p in self.roads_only) < 200:
                                    continue
                                self.place("VB_Thicket_A", "TH6_{}_{:02d}_{}".format(r["id"][-1], k, "L" if side > 0 else "R"),
                                           f + "/TH6", x, y, yaw=math.degrees(math.atan2(uy, ux)))
                            if k % 5 == 4:
                                self.instance("SM_LogFallen", px - uy * 150, py + ux * 150, yaw=math.degrees(math.atan2(uy, ux)),
                                              folder="Thornwood/Scatter")

    def vegetation(self):
        L = self.layout
        roads = [r["points"] for r in L["roads"]]
        river = L["river"]["centreline"]
        camps = [c["centre"] for c in L["camps"]] + [p["centre"] for p in L["placeholderCamps"]]
        rings = [(t["centre"], t["rxCm"] + 60, t["ryCm"] + 60) for t in L["thickets"] if t["kind"] == "ring"]
        keepout = [(34, 98, 66, 126), (44, 0, 100, 40), (0, 0, 26, 26), (128, 0, 143, 16), (75, 132, 86, 143),
                   (100, 0, 138, 18), (104, 52, 136, 80)]

        def free(x, y, road_m=110.0):
            tx, ty = x / T, y / T
            if any(a <= tx <= c and b <= ty <= d for a, b, c, d in keepout):
                return False
            if min(poly_dist(x, y, p) for p in roads) < road_m:
                return False
            if poly_dist(x, y, river) < 320:
                return False
            if any(math.hypot(x - cx, y - cy) < 300 for cx, cy in camps):
                return False
            if any(((x - c[0]) / rx) ** 2 + ((y - c[1]) / ry) ** 2 < 1.0 for c, rx, ry in rings):
                return False
            return True

        n = 0
        # tree line on the south, west, east and north-east edges
        edges = [((0, 9152 - 150), (9152, 9152 - 150)), ((150, 3200), (150, 9152)), ((9152 - 150, 2800), (9152 - 150, 9152)),
                 ((6300, 150), (8100, 150))]
        for (ax, ay), (bx, by) in edges:
            d = math.hypot(bx - ax, by - ay)
            for k in range(int(d // 170)):
                t = (k + 0.5) * 170 / d
                x = ax + (bx - ax) * t + (rnd(k * 3 + ax) - 0.5) * 120
                y = ay + (by - ay) * t + (rnd(k * 7 + ay) - 0.5) * 120
                if not free(x, y, 160):
                    continue
                self.instance("SM_Tree_A" if rnd(k + ax) < 0.55 else "SM_Tree_B", x, y, yaw=360 * rnd(k),
                              scale=0.9 + 0.4 * rnd(k + 1), folder="Edges/TreeLine")
                n += 1
        # Thornwood: dense trees, ferns, logs, stumps
        for i in range(92, 143, 3):
            for j in range(84, 143, 3):
                x = (i + 1.5 + (rnd(i * 31 + j) - 0.5) * 2.2) * T
                y = (j + 1.5 + (rnd(i * 17 + j * 3) - 0.5) * 2.2) * T
                if not free(x, y, 150):
                    continue
                r = rnd(i * 7 + j * 13)
                if r < 0.6:
                    self.instance("SM_Tree_A" if r < 0.35 else "SM_Tree_B", x, y, yaw=360 * rnd(i + j), scale=0.9 + 0.5 * rnd(i),
                                  folder="Thornwood/Trees")
                elif r < 0.8:
                    self.instance("SM_Fern", x, y, yaw=360 * rnd(i * j), folder="Thornwood/Scatter")
                elif r < 0.9:
                    self.instance("SM_Stump", x, y, yaw=360 * rnd(i * j + 1), folder="Thornwood/Scatter")
                else:
                    self.instance("SM_BushB", x, y, yaw=360 * rnd(i * j + 2), folder="Thornwood/Scatter")
        # Downs and vale: single trees, rocks, flowers, tufts
        for k in range(160):
            x, y = 200 + 8700 * rnd(k * 1.37), 3100 + 5900 * rnd(k * 2.11)
            if x / T > 92 and y / T > 84:
                continue
            if not free(x, y):
                continue
            r = rnd(k * 5.3)
            mesh = ("SM_Tree_A" if r < 0.12 else "SM_Rock_A" if r < 0.3 else "SM_BushA" if r < 0.42 else
                    "SM_FlowersA" if r < 0.6 else "SM_FlowersB" if r < 0.72 else "SM_GrassTuft")
            self.instance(mesh, x, y, yaw=360 * rnd(k), scale=0.8 + 0.5 * rnd(k + 3), folder="Downs/Scatter")
        self.notes["treeLine"] = n

    def edges(self):
        S = 9152.0
        for lbl, (x0, y0, x1, y1) in {"N": (0, -16, S, -16), "S": (0, S + 16, S, S + 16),
                                      "W": (-16, 0, -16, S), "E": (S + 16, 0, S + 16, S)}.items():
            self.invisible_wall("ZoneEdge_" + lbl, "Edges", x0, y0, x1, y1, -600.0, 1400.0, thick=32.0)

    def markers(self):
        for m in self.layout["mistPockets"]:
            x, y = m["centre"]
            a = self.marker("PH_Mist_{}_{}".format(m["id"], m["density"]), "Markers/Mist", x, y, dz=50.0,
                            tags=["PH_Mist", m["density"], "rx:%d" % m["radiusXCm"], "ry:%d" % m["radiusYCm"]])
            a.set_actor_scale3d(unreal.Vector(m["radiusXCm"] / 100.0, m["radiusYCm"] / 100.0, 1.0))

    def gameplay(self):
        """Portals, entries, starts, default spawn, fog bounds, nav bounds."""
        f = "Gameplay"
        pe = {p["id"]: p for p in self.layout["portalsAndEntries"] if not p.get("phase")}
        yaws = {"portal_to_grasslands": 0.0, "portal_to_desert": 90.0,
                "entry_from_grasslands": -90.0, "entry_from_desert": 180.0}
        for pid, p in pe.items():
            x, y = p["x"], p["y"]
            if p["type"] == "portal":
                a = self.cls(unreal.ValhallaPortal, "Portal_to_" + p["targetZone"], f, x, y, yaw=yaws[pid])
                a.set_editor_property("target_zone_id", p["targetZone"])
                a.set_editor_property("target_entry_id", PORTAL_TARGETS[pid])
                mk = a.get_editor_property("marker")
                if mk.get_editor_property("static_mesh") is None:
                    mk.set_editor_property("static_mesh", self.mesh("SM_PortalMarker"))
            else:
                a = self.cls(unreal.ValhallaZoneEntry, "Entry_" + ENTRY_IDS[pid], f, x, y, yaw=yaws[pid])
                a.set_editor_property("entry_id", ENTRY_IDS[pid])
                a.set_editor_property("from_zone_id", p["fromZone"])
        # the scaffold's four starts -> the Grasslands-side arrival
        starts = sorted([a for a in EAS.get_all_level_actors()
                         if isinstance(a, unreal.PlayerStart) and a.get_level() == self.level],
                        key=lambda a: a.get_actor_label())
        for a, s in zip(starts, self.layout["playerStarts"]):
            a.set_actor_location(self.W(s["x"], s["y"], self.gz(s["x"], s["y"]) + 100.0), False, False)
            a.set_actor_rotation(unreal.Rotator(0.0, 0.0, -90.0), False)
        self.notes["starts"] = [[s["x"], s["y"]] for s in self.layout["playerStarts"]]
        # zone volume: default spawn at Harrow's Rest (bind), 3200,7168
        vol = [a for a in EAS.get_all_level_actors() if isinstance(a, unreal.ValhallaZoneVolume) and a.get_level() == self.level][0]
        box_c = vol.get_actor_location()
        bx, by = 3200.0, 7168.0
        arrow = vol.get_editor_property("default_spawn")
        arrow.set_relative_location_and_rotation(
            unreal.Vector(self.off.x + bx - box_c.x, self.off.y + by - box_c.y, self.off.z + self.gz(bx, by) - box_c.z),
            unreal.Rotator(0.0, 0.0, -90.0), False, False)
        self.notes["defaultSpawn"] = [bx, by, self.gz(bx, by)]
        # fog bounds: the whole zone, tall enough for the tor and cleft
        from valhalla_tools import build_fog
        build_fog.place_fog_bounds(centre=(self.off.x + 4576.0, self.off.y + 4576.0, 350.0),
                                   extent=(4576.0, 4576.0, 750.0), label="FogBounds_grasslands_v2")
        # nav bounds for B-16 (not built here)
        nav = self.cls(unreal.NavMeshBoundsVolume, "NavMeshBounds_grasslands_v2", f, 4576.0, 4576.0, z=300.0)
        nav.set_actor_scale3d(unreal.Vector(9152.0 / 200.0, 9152.0 / 200.0, 1600.0 / 200.0))

    # ── run ──────────────────────────────────────────────────────────────
    def build(self):
        self.roads_only = [r["points"] for r in self.layout["roads"] if r["kind"] == "road"]
        steps = [self.river, self.outpost, self.keep, self.greyfell, self.kingsbarrow, self.steadings,
                 self.camps, self.thickets, self.vegetation, self.edges, self.markers, self.gameplay]
        for s in steps:
            s()
            _log("{} done".format(s.__name__))
        self.flush_instances()
        return {"ok": True, "counts": self.counts, "notes": self.notes}


def refusal():
    world, sl = _streaming(LEVEL_NAME)
    if sl is None:
        return "{} is not streamed into the open world (open L_World)".format(LEVEL_NAME)
    for a in EAS.get_all_level_actors():
        if a.get_level().get_outer().get_name() == LEVEL_NAME:
            if TAG in [str(t) for t in a.tags]:
                return "{} already has a blockout (actor {} is tagged {}); it is hand-edited from here on".format(
                    LEVEL_NAME, a.get_actor_label(), TAG)
    if not any(isinstance(a, unreal.Landscape) and a.get_level().get_outer().get_name() == LEVEL_NAME
               for a in EAS.get_all_level_actors()):
        return "{} has no Landscape yet (import eldmoor_height.png first)".format(LEVEL_NAME)
    return ""


def build():
    why = refusal()
    if why:
        _log("refused: " + why)
        return {"ok": False, "refused": why}
    return Builder().build()


# ── the other zones' ends, and lights ───────────────────────────────────────

NEIGHBOURS = {
    "L_Grasslands_Gameplay": [
        ("portal", "Portal_to_grasslands_v2", 2048.0, 96.0, 0.0, ("grasslands_v2", "eldmoor_from_grasslands")),
        ("entry", "Entry_grasslands_from_eldmoor", 2048.0, 380.0, 90.0, ("grasslands_from_eldmoor", "grasslands_v2")),
    ],
    "L_Desert_Gameplay": [
        ("portal", "Portal_to_grasslands_v2", 3968.0, 864.0, 90.0, ("grasslands_v2", "eldmoor_from_desert")),
        ("entry", "Entry_desert_from_eldmoor", 3616.0, 864.0, 180.0, ("desert_from_eldmoor", "grasslands_v2")),
    ],
}


def add_neighbour_portals():
    out = {}
    marker = unreal.EditorAssetLibrary.load_asset(MESH_PATH["SM_PortalMarker"])
    for level_name, items in NEIGHBOURS.items():
        world, sl = _streaming(level_name)
        off = sl.get_editor_property("level_transform").translation
        unreal.EditorLevelUtils.make_level_current(sl)
        existing = {a.get_actor_label() for a in EAS.get_all_level_actors()
                    if a.get_level().get_outer().get_name() == level_name}
        done = []
        for kind, label, x, y, yaw, (p1, p2) in items:
            if label in existing:
                done.append(label + " (exists)")
                continue
            loc = unreal.Vector(off.x + x, off.y + y, off.z + build_zone.FLOOR_TOP)
            if kind == "portal":
                a = EAS.spawn_actor_from_class(unreal.ValhallaPortal, loc, unreal.Rotator(0.0, 0.0, yaw))
                a.set_editor_property("target_zone_id", p1)
                a.set_editor_property("target_entry_id", p2)
                mk = a.get_editor_property("marker")
                if mk.get_editor_property("static_mesh") is None:
                    mk.set_editor_property("static_mesh", marker)
            else:
                a = EAS.spawn_actor_from_class(unreal.ValhallaZoneEntry, loc, unreal.Rotator(0.0, 0.0, yaw))
                a.set_editor_property("entry_id", p1)
                a.set_editor_property("from_zone_id", p2)
            a.set_actor_label(label)
            a.set_folder_path("Gameplay/Eldmoor")
            done.append(label)
        out[level_name] = done
    world, sl = _streaming(LEVEL_NAME)
    unreal.EditorLevelUtils.make_level_current(sl)
    return out


def light_fires():
    """fire_lights / lamp_lights for this level only (the shared helpers walk every level)."""
    from valhalla_tools import fire_lights, lamp_lights
    added = 0
    for actor in EAS.get_all_level_actors():
        if actor.get_level().get_outer().get_name() != LEVEL_NAME or not isinstance(actor, unreal.StaticMeshActor):
            continue
        mesh = actor.static_mesh_component.get_editor_property("static_mesh")
        name = mesh.get_name() if mesh else ""
        if name in fire_lights.FIRES:
            if fire_lights._existing(actor) is None:
                light = EAS.spawn_actor_from_class(unreal.PointLight, actor.get_actor_location(), unreal.Rotator(0, 0, 0))
                light.tags = [fire_lights.TAG, TAG]
                light.set_actor_label("{}_Light".format(actor.get_actor_label()))
                fire_lights.configure(light, actor, name)
                light.attach_to_actor(actor, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                                      unreal.AttachmentRule.KEEP_WORLD, False)
                light.set_folder_path(actor.get_folder_path())
                added += 1
        elif name == lamp_lights.LAMP_MESH and lamp_lights._has_light(actor) is None:
            light = EAS.spawn_actor_from_class(unreal.PointLight, actor.get_actor_location(), unreal.Rotator(0, 0, 0))
            light.tags = [lamp_lights.TAG, TAG]
            light.set_actor_label("{}_Light".format(actor.get_actor_label()))
            lc = light.get_editor_property("point_light_component")
            lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            lc.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
            lc.set_editor_property("intensity", lamp_lights.LOOK["intensity_cd"])
            lc.set_editor_property("light_color", lamp_lights.LOOK["color"])
            lc.set_editor_property("attenuation_radius", lamp_lights.LOOK["radius"])
            lc.set_editor_property("source_radius", lamp_lights.LOOK["source_radius"])
            lc.set_editor_property("cast_shadows", False)
            loc = actor.get_actor_location()
            light.set_actor_location(unreal.Vector(loc.x, loc.y, loc.z + lamp_lights.LANTERN_Z), False, False)
            light.attach_to_actor(actor, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                                  unreal.AttachmentRule.KEEP_WORLD, False)
            light.set_folder_path(actor.get_folder_path())
            added += 1
    return added
