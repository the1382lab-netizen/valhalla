"""Render a top-down orthogonal PNG of a MapCanvas so layout can be eyeballed.

This is a *debug* view, not what the game draws: the game draws the same grid
isometrically. Ortho keeps the geometry honest while iterating on the layout.
"""
from PIL import Image, ImageDraw
from tiles import GID
from canvas import GROUND, WALLS, DECO, LAYER_ORDER

COLOR = {
    "grass_light":  (108, 160, 74),
    "grass_dark":   (78, 128, 58),
    "dirt":         (150, 118, 78),
    "water":        (58, 108, 180),
    "stone_wall":   (128, 128, 136),
    "wood_fence":   (150, 110, 66),
    "tree":         (36, 92, 46),
    "stone_floor":  (168, 166, 158),
    "portal":       (186, 96, 220),
    "void":         (20, 20, 24),
    "road_cobble":  (140, 132, 118),
    "plaster_wall": (226, 214, 190),
    "timber_wall":  (122, 84, 52),
    "roof_thatch":  (196, 156, 78),
    "roof_tile":    (170, 74, 62),
    "door_wood":    (96, 58, 30),
    "window_lit":   (250, 222, 130),
    "wood_floor":   (176, 138, 92),
    "signpost":     (110, 80, 48),
    "well":         (96, 100, 110),
    "market_stall": (200, 96, 96),
    "crop_field":   (198, 176, 78),
    "bridge_wood":  (158, 118, 70),
    "cave_mouth":   (40, 34, 44),
    "cave_floor":   (86, 80, 76),
    "cave_wall":    (58, 54, 58),
    "rock":         (118, 114, 110),
    "flowers":      (216, 126, 176),
    "hedge":        (52, 106, 62),
    "campfire":     (240, 142, 60),
}
BY_GID = {GID[n]: c for n, c in COLOR.items()}


def render(cv, path, scale=5, labels=()):
    img = Image.new("RGB", (cv.w * scale, cv.h * scale), (0, 0, 0))
    px = img.load()
    for y in range(cv.h):
        for x in range(cv.w):
            c = (0, 0, 0)
            for layer in LAYER_ORDER:
                g = cv.get(layer, x, y)
                if g:
                    c = BY_GID.get(g, (255, 0, 255))
            for dy in range(scale):
                for dx in range(scale):
                    px[x * scale + dx, y * scale + dy] = c
    d = ImageDraw.Draw(img)
    for (lx, ly, text) in labels:
        d.text((lx * scale, ly * scale), text, fill=(255, 255, 255))
    img.save(path)
    return path
