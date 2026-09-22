"""Isometric preview — approximates what the client actually draws.

The ortho preview keeps the layout honest, but the game projects the same grid
2:1, and shapes that look fine on a grid can read very differently once they are
diamonds. This renders diamonds in the same painter order the client uses
(layer, then x+y), and gives the solid tiles a little height so walls, trees and
roofs stand up instead of lying flat.
"""
from PIL import Image, ImageDraw
from tiles import GID, SOLID_GIDS
from canvas import LAYER_ORDER
from preview import COLOR, BY_GID

TW, TH = 16, 8                      # preview tile size (game uses 128x64)

# How tall each tile stands, in preview pixels.
HEIGHT = {
    "stone_wall": 14, "plaster_wall": 14, "timber_wall": 14, "cave_wall": 16,
    "roof_thatch": 18, "roof_tile": 18, "window_lit": 14, "door_wood": 12,
    "tree": 20, "hedge": 8, "rock": 6, "market_stall": 10, "well": 8,
    "signpost": 10, "campfire": 6, "cave_mouth": 12,
}
BY_GID_HEIGHT = {GID[n]: h for n, h in HEIGHT.items()}


def shade(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def render(cv, path):
    w = (cv.w + cv.h) * TW // 2 + TW
    h = (cv.w + cv.h) * TH // 2 + TH + 24
    ox = cv.h * TW // 2
    oy = 24
    img = Image.new("RGB", (w, h), (14, 16, 20))
    d = ImageDraw.Draw(img)

    for layer in LAYER_ORDER:
        for s in range(cv.w + cv.h - 1):          # x + y, back to front
            for x in range(max(0, s - cv.h + 1), min(cv.w, s + 1)):
                y = s - x
                gid = cv.get(layer, x, y)
                if not gid:
                    continue
                col = BY_GID.get(gid, (255, 0, 255))
                cx = ox + (x - y) * TW // 2
                cy = oy + (x + y) * TH // 2
                lift = BY_GID_HEIGHT.get(gid, 0)
                top = [(cx, cy - lift), (cx + TW // 2, cy + TH // 2 - lift),
                       (cx, cy + TH - lift), (cx - TW // 2, cy + TH // 2 - lift)]
                if lift:
                    # Two side faces, so the block reads as a solid object.
                    d.polygon([top[3], top[2], (cx, cy + TH), (cx - TW // 2, cy + TH // 2)],
                              fill=shade(col, 0.55))
                    d.polygon([top[2], top[1], (cx + TW // 2, cy + TH // 2), (cx, cy + TH)],
                              fill=shade(col, 0.75))
                d.polygon(top, fill=col)

    img.save(path)
    return path


if __name__ == "__main__":
    import sys, importlib
    mod = importlib.import_module(sys.argv[1])
    render(mod.build(), sys.argv[2])
    print("wrote", sys.argv[2])
