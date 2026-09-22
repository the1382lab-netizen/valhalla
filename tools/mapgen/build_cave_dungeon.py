"""
Greyfell Cave — a 64x64 hand-authored dungeon interior.

Reached from the cave mouth in the Greyfell hills on grasslands_v2. Solid rock
everywhere by default; chambers and corridors are carved out of it, which is
the only way to guarantee a dungeon has no accidental holes in its walls.

Layout, entrance at the south:

        deep chamber (the drowned hall)
                 |
        cistern -- upper gallery -- collapsed gallery
                 |
      west warren -- hub -- east workings
                 |
           entrance hall  ->  back to grasslands_v2
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from canvas import MapCanvas, GROUND, WALLS, DECO
import preview

W = H = 64

ENTRANCE = (32, 58)

# label, x0, y0, x1, y1
ROOMS = [
    ("entrance hall",     27, 53, 37, 61),
    ("hub",               25, 40, 39, 49),
    ("west warren",        6, 36, 19, 48),
    ("east workings",     45, 38, 58, 50),
    ("upper gallery",     24, 24, 40, 33),
    ("cistern",            7, 20, 20, 31),
    ("collapsed gallery", 44, 20, 57, 30),
    ("drowned hall",      21, 4, 43, 16),
]
ROOM = {name: box for name, *box in ((r[0], *r[1:]) for r in ROOMS)}

CORRIDORS = [
    ((32, 53), (32, 49)),      # entrance -> hub
    ((25, 44), (19, 44)),      # hub -> west warren
    ((39, 44), (45, 44)),      # hub -> east workings
    ((32, 40), (32, 33)),      # hub -> upper gallery
    ((24, 28), (20, 28)),      # upper gallery -> cistern
    ((40, 27), (44, 25)),      # upper gallery -> collapsed gallery
    ((32, 24), (32, 16)),      # upper gallery -> drowned hall
    ((13, 36), (13, 31)),      # west warren -> cistern (the back way round)
    ((50, 38), (50, 30)),      # east workings -> collapsed gallery
]


def carve_rect(cv, x0, y0, x1, y1):
    cv.rect(WALLS, x0, y0, x1, y1, None)
    cv.rect(GROUND, x0, y0, x1, y1, "cave_floor")


def carve_corridor(cv, a, b, width=3):
    """L-shaped, horizontal leg first. Corridors are carved wide enough that
    two players can pass; a 1-tile passage in an isometric view reads as a
    seam rather than a route."""
    (ax, ay), (bx, by) = a, b
    half = width // 2
    for x in range(min(ax, bx), max(ax, bx) + 1):
        carve_rect(cv, x, ay - half, x, ay + half)
    for y in range(min(ay, by), max(ay, by) + 1):
        carve_rect(cv, bx - half, y, bx + half, y)


def rough_edges(cv):
    """Chew the chamber walls so rooms don't read as rectangles: any wall tile
    with three or more carved neighbours is likely a corner nub — knock it out."""
    for _ in range(3):
        doomed = []
        for y in range(2, H - 2):
            for x in range(2, W - 2):
                if not cv.get(WALLS, x, y):
                    continue
                open_n = sum(1 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                             if not cv.get(WALLS, x + dx, y + dy))
                if open_n >= 3 and cv.rng.random() < 0.8:
                    doomed.append((x, y))
        for x, y in doomed:
            carve_rect(cv, x, y, x, y)


def dress(cv):
    """Pillars, rubble, pools and firelight."""
    def pool(cx, cy, rx, ry):
        """Water only ever fills carved floor — flooding the surrounding rock
        would paint a lake inside the walls."""
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 > 1.0 + cv.rng.uniform(-0.25, 0.25):
                    continue
                if cv.get(GROUND, x, y) == 35 and not cv.get(WALLS, x, y):
                    cv.set(GROUND, x, y, "water")

    # A drowned pool in the cistern, and another in the deep hall.
    pool(13, 25, 5, 4)
    pool(32, 10, 8, 4)
    # A dry ledge across the deep pool so the hall stays crossable.
    cv.rect(GROUND, 30, 4, 34, 16, "cave_floor")

    # Freestanding pillars — only in the middle of a room, never in a doorway.
    for cx, cy in ((30, 44), (35, 44), (50, 43), (12, 42), (28, 29), (37, 29),
                   (26, 8), (38, 8), (50, 25)):
        if all(not cv.get(WALLS, cx + dx, cy + dy)
               for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2)):
            cv.set(WALLS, cx, cy, "cave_wall")

    for cx, cy in ((32, 56), (12, 44), (52, 45), (32, 29), (33, 13)):
        if not cv.get(WALLS, cx, cy):
            cv.set(DECO, cx, cy, "campfire")

    # Rubble strewn on the floor, but never on a campfire or a pillar.
    for _ in range(260):
        x, y = cv.rng.randrange(W), cv.rng.randrange(H)
        if not cv.get(WALLS, x, y) and not cv.get(DECO, x, y) \
                and cv.get(GROUND, x, y) == 35:
            cv.set(WALLS, x, y, "rock")


def build():
    cv = MapCanvas(W, H, seed=419, base="cave_wall")
    cv.rect(WALLS, 0, 0, W - 1, H - 1, "cave_wall")

    for name, x0, y0, x1, y1 in ROOMS:
        carve_rect(cv, x0, y0, x1, y1)
    for a, b in CORRIDORS:
        carve_corridor(cv, a, b)

    rough_edges(cv)
    dress(cv)

    # The way back out: a cave mouth in the entrance hall's south wall.
    ex, ey = ENTRANCE
    cv.rect(WALLS, ex - 1, ey, ex + 1, 61, None)
    cv.rect(GROUND, ex - 1, ey, ex + 1, 61, "cave_floor")
    cv.rect(DECO, ex - 1, 61, ex + 1, 61, "portal")

    # Seal the rim: rough_edges can reach the border, and a dungeon that leaks
    # into the void is worse than one that is a little boxy.
    cv.outline(WALLS, 0, 0, W - 1, H - 1, "cave_wall")
    cv.outline(WALLS, 1, 1, W - 2, H - 2, "cave_wall")

    # Rubble occasionally boxes in a lone floor tile. A pocket a player can see
    # but never stand in is just a rendering artefact — backfill it with rock.
    import validate
    seen, _ = validate.reachable(cv, SPAWN)
    solid = cv.collision()
    for y in range(H):
        for x in range(W):
            i = y * W + x
            if not solid[i] and not seen[i]:
                cv.set(WALLS, x, y, "cave_wall")
    return cv


SPAWN = (32, 59)

if __name__ == "__main__":
    import validate
    cv = build()
    out = sys.argv[1] if len(sys.argv) > 1 else "cave_dungeon.json"
    cv.save(out)
    preview.render(cv, os.path.splitext(out)[0] + "_preview.png", scale=8)
    solid = sum(1 for g in cv.collision() if g)
    print(f"{out}: {W}x{H}, {solid} solid tiles ({solid / (W * H):.0%})")
    seen, n = validate.reachable(cv, SPAWN)
    free = sum(1 for g in cv.collision() if not g)
    print(f"reachable {n}/{free} walkable tiles ({n / free:.0%}) from {SPAWN}")
    bad = validate.check_regions(cv, SPAWN, ROOMS)
    if bad:
        raise SystemExit("stranded: " + ", ".join(bad))
