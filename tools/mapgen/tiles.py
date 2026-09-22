"""
Tile registry — the single source of truth shared by every generated map.

`solid` drives the `collision` layer, so a wall can never be forgotten: the
generator derives collision from what was actually painted rather than asking
the author to maintain a parallel grid by hand.

GIDs must match `GameScene.GID_TEXTURE_MAP` in the client. 1-20 are the original
grass/desert sets; 21-40 are the town/farm/cave set.
"""

# name -> (gid, solid)
TILES = {
    # ---- base grass tileset (client GIDs 1-10) ----
    "grass_light":  (1,  False),
    "grass_dark":   (2,  False),
    "dirt":         (3,  False),
    "water":        (4,  True),
    "stone_wall":   (5,  True),
    "wood_fence":   (6,  True),
    "tree":         (7,  True),
    "stone_floor":  (8,  False),
    "portal":       (9,  False),
    "void":         (10, True),

    # ---- town / farm / cave set (client GIDs 21-40) ----
    "road_cobble":  (21, False),
    "plaster_wall": (22, True),
    "timber_wall":  (23, True),
    "roof_thatch":  (24, True),
    "roof_tile":    (25, True),
    "door_wood":    (26, False),   # walkable: this is how you get inside
    "window_lit":   (27, True),
    "wood_floor":   (28, False),
    "signpost":     (29, True),
    "well":         (30, True),
    "market_stall": (31, True),
    "crop_field":   (32, False),
    "bridge_wood":  (33, False),
    "cave_mouth":   (34, False),   # walkable: the entrance itself
    "cave_floor":   (35, False),
    "cave_wall":    (36, True),
    "rock":         (37, True),
    "flowers":      (38, False),
    "hedge":        (39, True),
    "campfire":     (40, True),
}

GID = {name: gid for name, (gid, _) in TILES.items()}
SOLID_GIDS = {gid for gid, solid in TILES.values() if solid}
