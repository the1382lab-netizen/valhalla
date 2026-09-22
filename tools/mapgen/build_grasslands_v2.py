"""
Grasslands v2 — 128x128 isometric surface map.

Twice the linear size of the original grasslands (64x64), so four times the
area. Regions, roughly:

    NW  walled town of Eldmoor  — two inns, smithy, market square, shops
    NE  farmland                — hedged crop fields, farmhouse, barn
    C   the Elk river + a lake  — road crosses on two wooden bridges
    SW  Thornwood forest        — dense trees, a clearing with a campfire
    SE  the Greyfell hills      — rocky, with the mouth of Greyfell Cave
    E   desert road             — portal tiles at the eastern edge

Everything is written to maps/grasslands_v2.json. Nothing existing is touched.

Spawn/portal wiring lives in maps/overlays/grasslands_v2-overlay.json, not in
this file's objectgroup: the server replaces zoneConnections wholesale with the
overlay's portals when an overlay exists, and the editor only ever writes
overlays. Keeping both in one place avoids duplicate NPCs.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from canvas import MapCanvas, GROUND, WALLS, DECO
import preview

W = H = 128

# ---------------------------------------------------------------- landmarks
# Tile coordinates. World pixels = tile * 64 (ORTHO_TILE_SIZE).
TOWN = (14, 8, 54, 44)              # x0, y0, x1, y1 — wall footprint
TOWN_GATE_S = (33, 44)
TOWN_GATE_E = (54, 25)
TOWN_GATE_N = (33, 8)
MARKET = (26, 18, 42, 32)
STREET_Y = 25                        # main east-west street occupies y, y+1
STREET_X = 33                        # main north-south street occupies x, x+1

RIVER = [(100, 4), (97, 20), (92, 34), (84, 46), (74, 58), (67, 68), (60, 78)]
LAKE = (56, 88, 15, 11)              # cx, cy, rx, ry

ROAD_S = [(34, 44), (34, 56), (48, 62), (67, 70), (84, 78), (98, 88)]
ROAD_E = [(54, 26), (72, 26), (88, 27), (104, 30), (114, 42), (118, 60)]
ROAD_N = [(34, 8), (34, 3)]          # the old road, out of the north gate
OLD_ROAD_PORTAL = (31, 2, 36, 5)
BRIDGE_S = (67, 70)
BRIDGE_E = (96, 28)

CAVE_MOUTH = (100, 90)
DESERT_PORTAL = (118, 58, 124, 66)


def terrain(cv):
    """Base ground: grass with darker patches, then the water features."""
    # Mottled grass — big soft patches rather than per-tile noise, so the
    # ground reads as terrain instead of static.
    for _ in range(90):
        cx = cv.rng.randrange(W)
        cy = cv.rng.randrange(H)
        cv.ellipse(GROUND, cx, cy, cv.rng.randint(3, 9), cv.rng.randint(3, 8),
                   "grass_dark", jitter=0.35)

    cv.river(RIVER, width=3, bank="dirt")
    cx, cy, rx, ry = LAKE
    cv.ellipse(GROUND, cx, cy, rx + 2, ry + 2, "dirt", jitter=0.18)
    cv.ellipse(GROUND, cx, cy, rx, ry, "water", jitter=0.15)


def roads(cv):
    cv.path(GROUND, ROAD_S, "dirt", 4)
    cv.path(GROUND, ROAD_S, "road_cobble", 2)
    cv.path(GROUND, ROAD_E, "dirt", 4)
    cv.path(GROUND, ROAD_E, "road_cobble", 2)
    cv.path(GROUND, ROAD_N, "dirt", 4)
    cv.path(GROUND, ROAD_N, "road_cobble", 2)

    # Bridges: plank the crossing, and clear any wall/deco standing on it.
    for (bx, by), horiz in ((BRIDGE_S, False), (BRIDGE_E, True)):
        # The deck has to be at least as wide as the road's dirt shoulder,
        # otherwise the road's own dirt shows through where it cut the river.
        if horiz:
            cv.rect(GROUND, bx - 4, by - 2, bx + 4, by + 1, "bridge_wood")
            cv.rect(WALLS, bx - 4, by - 2, bx + 4, by + 1, None)
        else:
            cv.rect(GROUND, bx - 2, by - 4, bx + 3, by + 4, "bridge_wood")
            cv.rect(WALLS, bx - 2, by - 4, bx + 3, by + 4, None)

    cv.set(DECO, 36, 47, "signpost")
    cv.set(DECO, 58, 24, "signpost")
    cv.set(DECO, 95, 86, "signpost")


def town(cv):
    x0, y0, x1, y1 = TOWN

    # Ground inside the walls: trodden dirt, paved where the streets run.
    cv.rect(GROUND, x0, y0, x1, y1, "dirt")
    cv.rect(GROUND, MARKET[0], MARKET[1], MARKET[2], MARKET[3], "stone_floor")
    # Streets stop one tile short of the wall on every side; the gates below
    # are the only places the cobble is allowed to touch it.
    cv.rect(GROUND, x0 + 1, STREET_Y, x1 - 1, STREET_Y + 1, "road_cobble")
    cv.rect(GROUND, STREET_X, y0 + 1, STREET_X + 1, y1 - 1, "road_cobble")

    # Curtain wall with two gates.
    cv.outline(WALLS, x0, y0, x1, y1, "stone_wall")
    for gx in (TOWN_GATE_S[0], TOWN_GATE_S[0] + 1):
        cv.set(WALLS, gx, y1, None)
        cv.set(GROUND, gx, y1, "road_cobble")
    for gy in (TOWN_GATE_E[1], TOWN_GATE_E[1] + 1):
        cv.set(WALLS, x1, gy, None)
        cv.set(GROUND, x1, gy, "road_cobble")
    for gx in (TOWN_GATE_N[0], TOWN_GATE_N[0] + 1):
        cv.set(WALLS, gx, y0, None)
        cv.set(GROUND, gx, y0, "road_cobble")

    doors = []
    # --- north-west quarter -------------------------------------------------
    doors.append(("The Gilded Stag", cv.building(
        15, 10, 24, 18, roof="roof_thatch", door=(20, 18), enterable=True)))
    doors.append(("Cooper's house", cv.building(
        15, 20, 21, 24, roof="roof_thatch", door=(18, 24))))
    # --- north-east quarter -------------------------------------------------
    doors.append(("Eldmoor Smithy", cv.building(
        44, 10, 53, 17, wall="timber_wall", roof="roof_tile",
        door=(48, 17), enterable=True)))
    doors.append(("Miller's house", cv.building(
        44, 19, 50, 24, roof="roof_thatch", door=(47, 24))))
    # --- south-west quarter -------------------------------------------------
    doors.append(("General Store", cv.building(
        15, 33, 23, 40, roof="roof_tile", door=(19, 33), enterable=True)))
    doors.append(("Weaver's house", cv.building(
        25, 35, 31, 41, roof="roof_thatch", door=(28, 35))))
    # --- south-east quarter -------------------------------------------------
    doors.append(("The Wandering Boar", cv.building(
        43, 32, 53, 42, roof="roof_thatch", door=(48, 32), enterable=True)))
    doors.append(("Chapel", cv.building(
        36, 35, 41, 41, wall="stone_wall", roof="roof_tile", door=(38, 35))))

    # A cobbled spur from every door back to the nearest street, so no
    # doorway opens onto nothing.
    for _, (dx, dy) in doors:
        ty = STREET_Y if dy < STREET_Y else STREET_Y + 1
        step = 1 if dy < ty else -1
        for y in range(dy, ty + step, step):
            if not cv.get(WALLS, dx, y):
                cv.set(GROUND, dx, y, "road_cobble")

    # Market square furniture, kept off the two streets.
    cv.set(WALLS, 28, 21, "well")
    for sx, sy in ((29, 19), (38, 19), (29, 30), (38, 30), (40, 22), (27, 28)):
        cv.set(WALLS, sx, sy, "market_stall")
    cv.set(DECO, 34, 33, "signpost")

    # Garden strips against the inside of the north wall.
    cv.scatter(DECO, x0 + 2, y0 + 1, x1 - 2, y0 + 2, "flowers", 0.35)


def farmland(cv):
    """Hedged fields either side of the river, with a farmstead."""
    fields = [
        (74, 13, 83, 21), (74, 25, 83, 33), (74, 37, 82, 44),
        (86, 36, 93, 44), (103, 13, 112, 21), (99, 36, 107, 44),
    ]
    for fx0, fy0, fx1, fy1 in fields:
        cv.rect(GROUND, fx0, fy0, fx1, fy1, "crop_field")
        cv.outline(WALLS, fx0 - 1, fy0 - 1, fx1 + 1, fy1 + 1, "hedge")
        # A gap in the hedge so the field is enterable.
        cv.set(WALLS, (fx0 + fx1) // 2, fy1 + 1, None)

    cv.building(87, 14, 95, 21, roof="roof_thatch", door=(91, 21), enterable=True)
    cv.building(87, 24, 94, 30, wall="timber_wall", roof="roof_thatch", door=(90, 30))
    cv.rect(GROUND, 86, 22, 96, 23, "dirt")
    cv.scatter(DECO, 84, 12, 100, 34, "flowers", 0.02)


def forest(cv):
    """Thornwood: dense in the south-west, thinning toward the road."""
    for y in range(60, H):
        for x in range(0, 44):
            if not cv.inside(x, y) or cv.get(WALLS, x, y):
                continue
            if cv.get(GROUND, x, y) in (4,):     # never plant in water
                continue
            # Density ramps up away from the north-east corner of the wood.
            d = min(1.0, ((y - 58) / 40.0) * 0.6 + ((42 - x) / 42.0) * 0.6)
            if cv.rng.random() < d * 0.55:
                cv.set(WALLS, x, y, "tree")

    # A clearing with a campfire — somewhere for a bandit camp to live.
    cv.ellipse(WALLS, 22, 92, 7, 6, None)
    cv.ellipse(GROUND, 22, 92, 7, 6, "dirt", jitter=0.25)
    cv.set(WALLS, 22, 92, "campfire")
    cv.scatter(DECO, 16, 87, 28, 97, "flowers", 0.06)
    # A track from the south road down into the clearing.
    track = [(40, 66), (38, 70), (30, 80), (22, 88)]
    cv.path(WALLS, track, None, 4)      # fell the trees first, then lay the track
    cv.path(GROUND, track, "dirt", 2)

    # Scattered outliers north of the treeline so the edge isn't a straight cut.
    cv.scatter(WALLS, 4, 50, 40, 60, "tree", 0.12)


def hills(cv):
    """Greyfell: rocky ground rising to a cliff face with the cave mouth."""
    cv.ellipse(GROUND, 104, 98, 26, 22, "dirt", jitter=0.3)
    for y in range(74, H):
        for x in range(82, W):
            if not cv.inside(x, y) or cv.get(WALLS, x, y):
                continue
            d = min(1.0, ((x - 80) / 46.0) * 0.7 + ((y - 72) / 54.0) * 0.7)
            r = cv.rng.random()
            if r < d * 0.30:
                cv.set(WALLS, x, y, "rock")
            elif r < d * 0.34:
                cv.set(GROUND, x, y, "stone_floor")

    # Cliff face: a band of cave wall with the mouth punched through it.
    mx, my = CAVE_MOUTH
    cv.rect(WALLS, mx - 9, my - 5, mx + 9, my - 2, "cave_wall")
    cv.rect(WALLS, mx - 2, my - 5, mx + 2, my - 2, None)
    cv.rect(GROUND, mx - 2, my - 5, mx + 2, my + 1, "cave_floor")
    cv.rect(WALLS, mx - 1, my - 1, mx + 1, my, "cave_mouth")
    cv.rect(GROUND, mx - 4, my, mx + 4, my + 4, "dirt")
    cv.rect(WALLS, mx - 4, my + 1, mx + 4, my + 4, None)
    cv.set(DECO, mx + 4, my + 3, "campfire")


def gateway(cv, box, sign=None):
    """Paint a portal apron: clear ground, portal tiles, optional signpost. The
    portal *trigger* is defined in the overlay — these tiles only say where."""
    x0, y0, x1, y1 = box
    cv.rect(WALLS, x0 - 1, y0 - 1, x1 + 1, y1 + 1, None)
    cv.rect(DECO, x0 - 1, y0 - 1, x1 + 1, y1 + 1, None)
    cv.rect(GROUND, x0 - 1, y0 - 1, x1 + 1, y1 + 1, "dirt")
    cv.rect(GROUND, x0, y0, x1, y1, "portal")
    if sign:
        cv.set(DECO, sign[0], sign[1], "signpost")


def desert_road(cv):
    gateway(cv, DESERT_PORTAL, sign=(DESERT_PORTAL[0] - 2, DESERT_PORTAL[1] - 1))
    gateway(cv, OLD_ROAD_PORTAL, sign=(OLD_ROAD_PORTAL[0] - 2, OLD_ROAD_PORTAL[3]))


GRASS_GIDS = {1, 2}     # grass_light, grass_dark


def open_grass(cv, x, y):
    """True only on untouched grass — never on a road, field, floor or water,
    and never where something has already been built."""
    return (cv.inside(x, y)
            and cv.get(GROUND, x, y) in GRASS_GIDS
            and not cv.get(WALLS, x, y)
            and not cv.get(DECO, x, y))


def wildlife(cv):
    """Copses, boulders and flowers over the open ground between the regions,
    so the map has texture where nothing else is happening."""
    # Copses: small tree clusters, placed only if the whole footprint is clear.
    for _ in range(70):
        cx = cv.rng.randrange(4, W - 4)
        cy = cv.rng.randrange(4, H - 4)
        rx, ry = cv.rng.randint(2, 5), cv.rng.randint(2, 4)
        cells = [(x, y)
                 for y in range(cy - ry, cy + ry + 1)
                 for x in range(cx - rx, cx + rx + 1)]
        if not all(open_grass(cv, x, y) for x, y in cells):
            continue
        for x, y in cells:
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0 and cv.rng.random() < 0.75:
                cv.set(WALLS, x, y, "tree")

    # Lone trees and boulders, thinly.
    for _ in range(700):
        x, y = cv.rng.randrange(W), cv.rng.randrange(H)
        if not open_grass(cv, x, y):
            continue
        cv.set(WALLS, x, y, "tree" if cv.rng.random() < 0.7 else "rock")

    # Flowers grow in beds, not as confetti.
    for _ in range(55):
        cx, cy = cv.rng.randrange(W), cv.rng.randrange(H)
        for _ in range(cv.rng.randint(4, 14)):
            x = cx + cv.rng.randint(-3, 3)
            y = cy + cv.rng.randint(-2, 2)
            if open_grass(cv, x, y):
                cv.set(DECO, x, y, "flowers")

    # Two ponds in the open north, fed by nothing in particular.
    for px, py, prx, pry in ((66, 12, 6, 4), (52, 56, 5, 4)):
        cv.ellipse(WALLS, px, py, prx + 2, pry + 2, None)
        cv.ellipse(DECO, px, py, prx + 2, pry + 2, None)
        cv.ellipse(GROUND, px, py, prx + 1, pry + 1, "dirt", jitter=0.2)
        cv.ellipse(GROUND, px, py, prx, pry, "water", jitter=0.2)


def clear_roads(cv):
    """Nothing may stand on a road, a bridge or a paved square. Run last —
    it is the guarantee that every region stays connected to every other."""
    walkways = {21, 33, 8, 26, 28, 35, 34, 9}   # cobble, bridge, stone floor,
    cleared = 0                                  # door, planks, cave, portal
    for y in range(H):
        for x in range(W):
            if cv.get(GROUND, x, y) in walkways:
                for layer in (WALLS, DECO):
                    if cv.get(layer, x, y) and cv.get(layer, x, y) not in (
                            26, 27, 30, 31, 29, 34, 40):   # keep town furniture
                        cv.set(layer, x, y, None)
                        cleared += 1
    return cleared


def borders(cv):
    """A hard rim so nobody walks off the edge of the world."""
    cv.outline(WALLS, 0, 0, W - 1, H - 1, "rock")
    cv.outline(WALLS, 1, 1, W - 2, H - 2, "rock")


def build():
    cv = MapCanvas(W, H, seed=20260831, base="grass_light")
    terrain(cv)
    town(cv)
    farmland(cv)
    roads(cv)
    forest(cv)
    hills(cv)
    desert_road(cv)
    wildlife(cv)
    borders(cv)
    clear_roads(cv)
    return cv


SPAWN = (34, 28)                 # on the main street, just south of the market

POI = [
    ("town market square",   (34, 22)),
    ("The Gilded Stag",      (20, 14)),
    ("The Wandering Boar",   (48, 37)),
    ("Eldmoor Smithy",       (48, 13)),
    ("General Store",        (19, 36)),
    ("south gate",           (34, 45)),
    ("east gate",            (55, 26)),
    ("river bridge (south)", BRIDGE_S),
    ("river bridge (east)",  BRIDGE_E),
    ("farmhouse",            (91, 18)),
    ("crop field (east)",    (103, 40)),
    ("lakeshore",            (56, 76)),
    ("forest clearing",      (22, 90)),
    ("cave mouth",           CAVE_MOUTH),
    ("desert portal",        (121, 62)),
    ("old road portal",      (34, 3)),
]

if __name__ == "__main__":
    import validate
    cv = build()
    out = sys.argv[1] if len(sys.argv) > 1 else "grasslands_v2.json"
    cv.save(out)
    preview.render(cv, os.path.splitext(out)[0] + "_preview.png", scale=6)
    solid = sum(1 for g in cv.collision() if g)
    print(f"{out}: {W}x{H}, {solid} solid tiles "
          f"({solid / (W * H):.0%} of the map)")
    bad = validate.check(cv, SPAWN, POI)
    if bad:
        raise SystemExit("unreachable: " + ", ".join(bad))
