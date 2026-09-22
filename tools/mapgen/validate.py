"""Walkability checks for a generated map.

The generator can happily paint a beautiful map where the cave is sealed behind
its own cliff. A flood fill from the player spawn is the only thing that proves
otherwise, so every map is checked before it ships.
"""
from collections import deque
from canvas import LAYER_ORDER, GROUND, WALLS, DECO


def reachable(cv, start):
    solid = cv.collision()
    seen = [False] * (cv.w * cv.h)
    sx, sy = start
    if solid[sy * cv.w + sx]:
        raise SystemExit(f"start {start} is itself solid")
    q = deque([(sx, sy)])
    seen[sy * cv.w + sx] = True
    n = 1
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if not cv.inside(nx, ny):
                continue
            i = ny * cv.w + nx
            if seen[i] or solid[i]:
                continue
            seen[i] = True
            n += 1
            q.append((nx, ny))
    return seen, n


def check_regions(cv, start, regions):
    """Every free tile inside each named box must be reachable. Stronger than a
    single probe point: it catches a chamber whose far end is walled off by its
    own rubble, which a centre-tile check sails straight past."""
    seen, _ = reachable(cv, start)
    solid = cv.collision()
    bad = []
    for name, x0, y0, x1, y1 in regions:
        free = stranded = 0
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                i = y * cv.w + x
                if solid[i]:
                    continue
                free += 1
                if not seen[i]:
                    stranded += 1
        ok = free > 0 and stranded == 0
        print(f"  {'ok  ' if ok else 'FAIL'}  {name:<20} {free:>4} free"
              + (f", {stranded} stranded" if stranded else ""))
        if not ok:
            bad.append(name)
    return bad


def check(cv, start, targets):
    seen, n = reachable(cv, start)
    free = sum(1 for g in cv.collision() if not g)
    print(f"reachable {n}/{free} walkable tiles ({n / free:.0%}) from {start}")
    bad = []
    for label, (tx, ty) in targets:
        ok = seen[ty * cv.w + tx]
        print(f"  {'ok  ' if ok else 'FAIL'}  {label:<28} {(tx, ty)}")
        if not ok:
            bad.append(label)
    return bad
