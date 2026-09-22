"""Tile-grid canvas + Tiled JSON emitter."""
import json, math, random
from tiles import GID, SOLID_GIDS

GROUND, WALLS, DECO = "ground", "walls", "decoration"
LAYER_ORDER = [GROUND, WALLS, DECO]


class MapCanvas:
    """A tile grid that writes Tiled-format isometric JSON.

    Layer order in the emitted file *is* the render order — the client draws
    layers in array order, it never matches them by name (except `collision`,
    which it strips). Collision is derived from every painted layer, so a solid
    tile blocks wherever it was placed.
    """

    def __init__(self, width, height, seed=0, base="grass_light"):
        self.w, self.h = width, height
        self.rng = random.Random(seed)
        self.layers = {
            GROUND: [GID[base]] * (width * height),
            WALLS:  [0] * (width * height),
            DECO:   [0] * (width * height),
        }
        self.objects = []
        self._next_obj = 1

    # ---------------------------------------------------------------- helpers
    def inside(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def set(self, layer, x, y, tile):
        if self.inside(x, y):
            self.layers[layer][y * self.w + x] = GID[tile] if tile else 0

    def get(self, layer, x, y):
        return self.layers[layer][y * self.w + x] if self.inside(x, y) else 0

    def rect(self, layer, x0, y0, x1, y1, tile):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(layer, x, y, tile)

    def outline(self, layer, x0, y0, x1, y1, tile):
        for x in range(x0, x1 + 1):
            self.set(layer, x, y0, tile); self.set(layer, x, y1, tile)
        for y in range(y0, y1 + 1):
            self.set(layer, x0, y, tile); self.set(layer, x1, y, tile)

    def ellipse(self, layer, cx, cy, rx, ry, tile, jitter=0.0):
        for y in range(int(cy - ry - 2), int(cy + ry + 3)):
            for x in range(int(cx - rx - 2), int(cx + rx + 3)):
                if not self.inside(x, y):
                    continue
                d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
                if d <= 1.0 + (self.rng.uniform(-jitter, jitter) if jitter else 0):
                    self.set(layer, x, y, tile)

    def scatter(self, layer, x0, y0, x1, y1, tile, chance, avoid_layers=(WALLS,)):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if not self.inside(x, y) or self.rng.random() > chance:
                    continue
                if any(self.get(l, x, y) for l in avoid_layers):
                    continue
                self.set(layer, x, y, tile)

    def path(self, layer, points, tile, width=2):
        """Paint a polyline of tiles between waypoints."""
        half = width // 2
        for (ax, ay), (bx, by) in zip(points, points[1:]):
            steps = max(abs(bx - ax), abs(by - ay)) * 2 + 1
            for i in range(steps + 1):
                t = i / steps
                cx, cy = round(ax + (bx - ax) * t), round(ay + (by - ay) * t)
                for dy in range(-half, width - half):
                    for dx in range(-half, width - half):
                        self.set(layer, cx + dx, cy + dy, tile)

    def river(self, points, width=3, bank="dirt"):
        self.path(GROUND, points, bank, width + 2)
        self.path(GROUND, points, "water", width)

    # ---------------------------------------------------------------- buildings
    def building(self, x0, y0, x1, y1, *, wall="plaster_wall", roof="roof_thatch",
                 door=None, enterable=False, windows=True, floor="wood_floor"):
        """Stamp a building footprint.

        enterable=True leaves the interior open (plank floor, no roof) so players
        can walk in through the door. enterable=False fills it with roof, making
        it a solid obstacle with a door that reads visually but goes nowhere.
        """
        if enterable:
            self.rect(GROUND, x0 + 1, y0 + 1, x1 - 1, y1 - 1, floor)
        else:
            self.rect(GROUND, x0, y0, x1, y1, "dirt")
        self.outline(WALLS, x0, y0, x1, y1, wall)
        if not enterable:
            self.rect(WALLS, x0 + 1, y0 + 1, x1 - 1, y1 - 1, roof)

        if windows:
            for x in range(x0 + 2, x1 - 1, 3):
                self.set(WALLS, x, y0, "window_lit")
            for y in range(y0 + 2, y1 - 1, 3):
                self.set(WALLS, x0, y, "window_lit")

        dx, dy = door if door else ((x0 + x1) // 2, y1)
        self.set(WALLS, dx, dy, "door_wood")
        self.set(GROUND, dx, dy, "road_cobble")
        return (dx, dy)

    # ---------------------------------------------------------------- objects
    def add_object(self, name, otype, x, y, w=0, h=0, props=None):
        """Tiled objectgroup entry. `otype` must be one of the parser's literal
        types (enemy_spawn / npc_spawn / player_spawn / item_spawn / portal) —
        the shipped maps use "spawn", which the parser silently discards."""
        obj = {
            "id": self._next_obj, "name": name, "type": otype,
            "x": x, "y": y, "width": w, "height": h,
            "rotation": 0, "visible": True,
            "properties": [{"name": k, "type": "string", "value": str(v)}
                           for k, v in (props or {}).items()],
        }
        self._next_obj += 1
        self.objects.append(obj)
        return obj

    # ---------------------------------------------------------------- emit
    def collision(self):
        grid = [0] * (self.w * self.h)
        for layer in LAYER_ORDER:
            data = self.layers[layer]
            for i, gid in enumerate(data):
                if gid in SOLID_GIDS:
                    grid[i] = GID["void"]
        return grid

    def to_tiled(self, tileset_name="valhalla_tileset"):
        def tile_layer(lid, name, data):
            return {
                "id": lid, "name": name, "type": "tilelayer",
                "x": 0, "y": 0, "width": self.w, "height": self.h,
                "opacity": 1, "visible": True,
                "offsetx": 0, "offsety": 0, "parallaxx": 1, "parallaxy": 1,
                "properties": [], "data": data,
            }

        layers = [tile_layer(i + 1, n, self.layers[n]) for i, n in enumerate(LAYER_ORDER)]
        layers.append(tile_layer(len(LAYER_ORDER) + 1, "collision", self.collision()))
        layers.append({
            "id": len(LAYER_ORDER) + 2, "name": "spawns", "type": "objectgroup",
            "draworder": "topdown", "opacity": 1, "visible": True,
            "x": 0, "y": 0, "properties": [], "objects": self.objects,
        })
        names = sorted(GID, key=lambda n: GID[n])
        return {
            "type": "map", "version": "1.10", "tiledversion": "1.10.2",
            "orientation": "isometric", "renderorder": "right-down",
            "infinite": False, "compressionlevel": -1,
            "width": self.w, "height": self.h,
            "tilewidth": 128, "tileheight": 64,
            "nextlayerid": len(layers) + 1,
            "nextobjectid": self._next_obj,
            "layers": layers,
            "tilesets": [{
                "firstgid": 1, "name": tileset_name,
                "tilewidth": 128, "tileheight": 64,
                "tilecount": 40, "columns": 40,
                "image": f"{tileset_name}.png", "imagewidth": 40 * 128, "imageheight": 64,
                "tiles": [{"id": GID[n] - 1, "type": n} for n in names],
            }],
        }

    def save(self, path):
        with open(path, "w") as f:
            json.dump(self.to_tiled(), f, separators=(",", ":"))
        return path
