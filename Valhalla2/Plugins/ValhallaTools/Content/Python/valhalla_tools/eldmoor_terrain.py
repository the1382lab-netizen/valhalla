"""B-06 1.4: Eldmoor Grasslands (`grasslands_v2`) terrain, generated from the layout.

Pure Python (no `unreal`, no numpy), so it runs the same in the editor and on
any machine: `python eldmoor_terrain.py <eldmoor_layout.json> <out_dir>`.

It samples the design (`Docs/Zones/Eldmoor/EldmoorDesign.md` sections 3, 4, 7
and 8; positions from `eldmoor_layout.json`, zone-local cm, +X east, +Y south)
on the Landscape's vertex grid and writes

* `eldmoor_height.png` - 16-bit greyscale heightmap, 316 x 316 (5 x 5
  components of 63 quads, 32 cm a quad), Landscape Z scale `Z_SCALE`;
* `eldmoor_height.r16` - the same, raw little-endian;
* `eldmoor_w_<layer>.png` - 8-bit weightmaps for the Landscape paint layers
  (Grass, Moss, Dirt, Rock) that sum to 255 at every vertex;
* `eldmoor_w_hole.png` - the undercroft Landscape hole (visibility mask);
* `eldmoor_preview.png` - a shaded colour preview (8-bit RGB) for review.

Heights are in cm above the river datum (world Z 0; the water surface sits at
`WATER_Z`). Layout, in design tiles (1 tile = 64 cm, the JSON's cm / 64):

* Southern Downs +1..+3 m rolling; Harrow's Rest knoll flat +4 m over the
  palisade (x 34-66, y 98-126) with a 12-tile smooth falloff (max ~22 deg).
* Mistwater Vale floor ~+0.6..+1.1 m within 12 tiles of the river; banks +0.5 m;
  the channel (3.36 m) is -0.7 m at the centre. Not fordable: bank collision is
  placed by the blockout, not by the heightfield.
* Thornwood +1..+2 m hummocks, no cliffs. Kingsbarrow: +3 m oval mound
  (centre (120,66), radii 16 x 14) with a -1 m crypt hollow at (130,58).
* Greyfell Highlands: +4 m ridge (x 26-50, y 0-28), a 1.2 m scarp at y 28 that
  eases into a ~12 deg ramp where the highland path crosses it, the +2.8 m shelf
  of Hask's Hold, the scree pocket +3 -> +1 m (x 0-26, y 26-50; no ridge west of x 26), and Greyfell
  Tor, +7 m, x 0-26 / y 0-26, with vertical S and E faces (one quad) and the
  cleft notch at x 5-7 on the south face.
* Ashvane Keep plateau +4 m flat (x 46-98, y 0-40), glacis to +1 m at y 49
  (~28 deg), a steep west face below the ridge line (continuing the scarp).
* Burnt Steadings +2.5 m in the north sloping to ~+1.6 m by the river.
* Undercroft: heights inside the hole (x 61-77, y 5-13) are dropped to the
  cellar floor (+1 m), so even without the visibility mask it is a pit.
* North of the tor, ridge and keep (x < 98) the terrain drops away beyond the
  zone edge (cliffs to nothing); elsewhere it continues under the tree line.
"""

import json
import math
import os
import struct
import sys
import zlib

TILE = 64.0
ZONE_CM = 9152.0
QUAD_CM = 32.0
COMPONENTS = 5
QUADS_PER_COMPONENT = 63
VERTS = COMPONENTS * QUADS_PER_COMPONENT + 1          # 316
ORIGIN_CM = -448.0                                     # zone-local cm of vertex (0, 0)
Z_SCALE = 10.0                                         # Landscape actor scale Z
CM_PER_UNIT = Z_SCALE / 128.0                          # 0.078125 cm per height unit
WATER_Z = 8.0
HALL_FLOOR_Z = 400.0
CELLAR_FLOOR_Z = HALL_FLOOR_Z - 300.0

HOLE_TILES = (61, 5, 77, 13)                          # x0, y0, x1, y1 (design section 7)
LAYERS = ("Grass", "Moss", "Dirt", "Rock")


# ── small maths ─────────────────────────────────────────────────────────────

def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def sstep(e0, e1, x):
    if e0 == e1:
        return 0.0 if x < e0 else 1.0
    t = clamp((x - e0) / (e1 - e0))
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def _hash(ix, iy, seed):
    n = (ix * 374761393 + iy * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0 * 2.0 - 1.0


def vnoise(x, y, seed):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = _hash(ix, iy, seed)
    b = _hash(ix + 1, iy, seed)
    c = _hash(ix, iy + 1, seed)
    d = _hash(ix + 1, iy + 1, seed)
    return lerp(lerp(a, b, ux), lerp(c, d, ux), uy)


def fbm(x, y, seed, octaves=3):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total += vnoise(x, y, seed + o * 17) * amp
        norm += amp
        amp *= 0.5
        x, y = x * 2.03, y * 2.03
    return total / norm


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else clamp(((px - ax) * dx + (py - ay) * dy) / l2)
    qx, qy = ax + t * dx, ay + t * dy
    return math.hypot(px - qx, py - qy)


def poly_dist(px, py, pts):
    best = 1e18
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        d = seg_dist(px, py, ax, ay, bx, by)
        if d < best:
            best = d
    return best


def soft_rect(tx, ty, x0, y0, x1, y1, b):
    return (sstep(x0 - b, x0 + b, tx) * (1.0 - sstep(x1 - b, x1 + b, tx)) *
            sstep(y0 - b, y0 + b, ty) * (1.0 - sstep(y1 - b, y1 + b, ty)))


def rect_dist(tx, ty, x0, y0, x1, y1):
    dx = max(x0 - tx, 0.0, tx - x1)
    dy = max(y0 - ty, 0.0, ty - y1)
    return math.hypot(dx, dy)


# ── the terrain ─────────────────────────────────────────────────────────────

class Terrain(object):
    def __init__(self, layout):
        self.layout = layout
        self.river = [tuple(p) for p in layout["river"]["centreline"]]
        # extend the river past both zone edges so the channel runs off the map
        (x0, y0), (x1, y1) = self.river[0], self.river[1]
        self.river.insert(0, (x0 - (x1 - x0), y0 - (y1 - y0)))
        (xa, ya), (xb, yb) = self.river[-2], self.river[-1]
        self.river.append((xb + (xb - xa), yb + (yb - ya)))
        self.roads = [(r["kind"], [tuple(p) for p in r["points"]]) for r in layout["roads"]]
        self.highland = next(pts for kind, pts in self.roads if kind == "track"
                             and pts[0] == (3584, 3904))
        self.camps = [tuple(c["centre"]) for c in layout["camps"]]

    # base heights per region, all cm
    def regions(self, tx, ty):
        n1 = fbm(tx / 18.0, ty / 18.0, 11)
        n2 = fbm(tx / 6.0, ty / 6.0, 23)
        big = 400.0
        rs = [
            # (x0, y0, x1, y1, height)
            (-big, 96, 92, big, 195.0 + 70.0 * n1 + 15.0 * n2),          # Southern Downs
            (-big, 48, 100, 96, 110.0 + 25.0 * n1),                       # Mistwater Vale
            (92, 84, big, big, 150.0 + 35.0 * n1 + 30.0 * n2),            # Thornwood
            (100, 48, big, 84, 140.0 + 20.0 * n1),                        # Kingsbarrow
            (-big, -big, 50, 48, 250.0),                                  # Highlands (shaped below)
            (50, -big, 94, 48, 250.0),                                    # Keep (shaped below)
            (94, -big, big, 48, 250.0 - 90.0 * clamp(ty / 44.0) + 25.0 * n1),  # Steadings
        ]
        wsum, hsum = 0.0, 0.0
        for x0, y0, x1, y1, h in rs:
            w = soft_rect(tx, ty, x0, y0, x1, y1, 4.0) + 1e-6
            wsum += w
            hsum += w * h
        return hsum / wsum, n1, n2

    def height(self, x, y):
        """Terrain height (cm) at zone-local (x, y) cm."""
        tx, ty = x / TILE, y / TILE
        h, n1, n2 = self.regions(tx, ty)

        # Greyfell Highlands (x 0-50, y 0-48): ridge, scarp, shelf, scree.
        d_path = poly_dist(x, y, self.highland) / TILE
        scarp_w = lerp(7.0, 0.4, sstep(2.0, 4.0, d_path))
        upper = 400.0
        shelf = lerp(280.0, 180.0, sstep(40.0, 52.0, ty))
        scree = lerp(300.0, 110.0, clamp((ty - 26.0) / 26.0))
        lower = lerp(scree, shelf, sstep(22.0, 30.0, tx))
        hi = lerp(upper, lower, sstep(28.0 - scarp_w / 2.0, 28.0 + scarp_w / 2.0, ty))
        # the ridge (and so the scarp) only exists east of the tor; west of x 26
        # the scree pocket runs straight up to the tor's south cliff foot (+3 m)
        hi = lerp(lower, hi, sstep(25.5, 26.5, tx))
        w_hi = soft_rect(tx, ty, -400, -400, 52, 50, 4.0)
        h = lerp(h, hi, w_hi)

        # Ashvane Keep plateau (x 46-98) and glacis; steep west face below y 28.
        kp = 400.0 if ty <= 40.0 else lerp(400.0, 100.0, clamp((ty - 40.0) / 9.0))
        west = sstep(45.8, 46.2, tx) if ty > 27.0 else sstep(40.0, 46.0, tx)
        wx = west * (1.0 - sstep(95.0, 105.0, tx))
        w_keep = wx * (1.0 - sstep(48.0, 52.0, ty))
        h = lerp(h, kp, w_keep)

        # Harrow's Rest knoll.
        d_knoll = rect_dist(tx, ty, 34, 98, 66, 126)
        h = lerp(h, 400.0, 1.0 - sstep(0.0, 12.0, d_knoll))

        # Kingsbarrow mound and crypt hollow.
        e = math.hypot((tx - 120.0) / 16.0, (ty - 66.0) / 14.0)
        h = lerp(h, 300.0 + 10.0 * n2, 1.0 - sstep(0.7, 1.15, e))
        e2 = math.hypot((tx - 130.0) / 5.0, (ty - 58.0) / 4.0)
        h -= 100.0 * (1.0 - sstep(0.2, 1.1, e2))

        # Greyfell Tor: +7 m, vertical S and E faces; the cleft notch.
        w_tor = (1.0 - sstep(25.8, 26.2, tx)) * (1.0 - sstep(25.8, 26.2, ty))
        h = lerp(h, 700.0 + 15.0 * n2, w_tor)
        # SM_CliffCleft (256 x 128) sits at x 4-8, its passage back to y 23.9:
        # keep the ground at cliff-foot height under its whole footprint.
        if 3.8 <= tx <= 8.2 and 23.5 <= ty <= 26.6:
            h = min(h, 300.0)

        # Mistwater: channel, bank lip, valley floor.
        d = poly_dist(x, y, self.river)
        if d < 168.0:
            h = -70.0 + 50.0 * (d / 168.0) ** 2
        elif d < 224.0:
            h = 50.0
        else:
            floor = 60.0 + 50.0 * sstep(224.0, 700.0, d)
            h = lerp(floor, h, sstep(300.0, 900.0, d))

        # Beyond the north edge of the tor, ridge and keep: cliffs to nothing.
        if ty < 0.0 and tx < 98.0:
            h = lerp(h, -400.0, sstep(-0.2, -1.2, ty) * (1.0 - sstep(96.0, 100.0, tx)))

        # Undercroft pit (the visibility hole is painted over it).
        x0, y0, x1, y1 = HOLE_TILES
        if x0 <= tx <= x1 and y0 <= ty <= y1:
            h = CELLAR_FLOOR_Z
        return h

    def weights(self, x, y, h, slope_deg):
        tx, ty = x / TILE, y / TILE
        dirt = 0.0
        for kind, pts in self.roads:
            d = poly_dist(x, y, pts)
            if kind == "road":
                dirt = max(dirt, 1.0 - sstep(30.0, 50.0, d))
            elif kind == "track":
                dirt = max(dirt, 0.85 * (1.0 - sstep(14.0, 30.0, d)))
            else:
                dirt = max(dirt, 0.55 * (1.0 - sstep(12.0, 26.0, d)))
        for cx, cy in self.camps:
            dirt = max(dirt, 0.6 * (1.0 - sstep(96.0, 190.0, math.hypot(x - cx, y - cy))))
        # packed ground inside the keep's baileys and the outpost
        dirt = max(dirt, 0.7 * soft_rect(tx, ty, 51, 12, 93, 33, 0.5))
        dirt = max(dirt, 0.45 * soft_rect(tx, ty, 37, 101, 63, 123, 0.5))
        dr = poly_dist(x, y, self.river)
        if dr < 200.0:
            dirt = max(dirt, 1.0 - sstep(150.0, 200.0, dr))

        rock = sstep(32.0, 42.0, slope_deg)
        rock = max(rock, (1.0 - sstep(25.5, 26.5, tx)) * (1.0 - sstep(25.5, 26.5, ty)))      # tor
        rock = max(rock, 0.65 * soft_rect(tx, ty, -10, 26, 26, 50, 2.0) *
                   clamp(0.75 + 0.5 * fbm(tx / 4.0, ty / 4.0, 5)))                          # scree
        rock = max(rock, 0.45 * soft_rect(tx, ty, 26, -10, 50, 28, 1.0))                    # ridge
        rock = max(rock, 0.25 * soft_rect(tx, ty, 26, 28, 46, 44, 1.5))                     # shelf
        moss = 0.75 * soft_rect(tx, ty, 92, 84, 200, 200, 2.0)                               # Thornwood
        moss = max(moss, 0.45 * (1.0 - sstep(250.0, 700.0, dr)))                            # vale
        moss = max(moss, 0.5 * (1.0 - sstep(0.8, 1.2, math.hypot((tx - 120.0) / 16.0, (ty - 66.0) / 14.0))))

        # weight blend: rock over dirt over moss over grass
        w_rock = rock
        rem = 1.0 - w_rock
        w_dirt = dirt * rem
        rem -= w_dirt
        w_moss = moss * rem
        w_grass = rem - w_moss
        return {"Grass": w_grass, "Moss": w_moss, "Dirt": w_dirt, "Rock": w_rock}


def in_hole(x, y):
    x0, y0, x1, y1 = HOLE_TILES
    return x0 * TILE <= x <= x1 * TILE and y0 * TILE <= y <= y1 * TILE


# ── PNG writing (no PIL needed) ─────────────────────────────────────────────

def _png(path, width, height, rows, bit_depth, colour_type):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, bit_depth, colour_type, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def generate(layout_path, out_dir, verts=VERTS):
    layout = json.load(open(layout_path, encoding="utf-8"))
    terrain = Terrain(layout)
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    n = verts
    xs = [ORIGIN_CM + i * QUAD_CM for i in range(n)]
    heights = [[terrain.height(xs[i], xs[j]) for i in range(n)] for j in range(n)]

    def slope(i, j):
        i0, i1 = max(i - 1, 0), min(i + 1, n - 1)
        j0, j1 = max(j - 1, 0), min(j + 1, n - 1)
        gx = (heights[j][i1] - heights[j][i0]) / ((i1 - i0) * QUAD_CM)
        gy = (heights[j1][i] - heights[j0][i]) / ((j1 - j0) * QUAD_CM)
        return math.degrees(math.atan(math.hypot(gx, gy)))

    to_u16 = lambda h: max(0, min(65535, int(round(32768 + h / CM_PER_UNIT))))
    rows16, raw = [], bytearray()
    layer_rows = {k: [] for k in LAYERS}
    hole_rows, prev_rows = [], []
    colours = {"Grass": (96, 128, 60), "Moss": (58, 86, 44), "Dirt": (128, 100, 66), "Rock": (120, 118, 112)}
    stats = {"min": 1e9, "max": -1e9}
    for j in range(n):
        r16 = bytearray()
        lrow = {k: bytearray() for k in LAYERS}
        hrow, prow = bytearray(), bytearray()
        for i in range(n):
            h = heights[j][i]
            stats["min"], stats["max"] = min(stats["min"], h), max(stats["max"], h)
            u = to_u16(h)
            r16 += struct.pack(">H", u)
            raw += struct.pack("<H", u)
            s = slope(i, j)
            w = terrain.weights(xs[i], xs[j], h, s)
            # quantise so the four sum to exactly 255
            q = {k: int(round(w[k] * 255)) for k in LAYERS}
            q["Grass"] = 255 - q["Moss"] - q["Dirt"] - q["Rock"]
            if q["Grass"] < 0:
                q["Moss"] += q["Grass"]
                q["Grass"] = 0
            for k in LAYERS:
                lrow[k].append(max(0, min(255, q[k])))
            hole = in_hole(xs[i], xs[j])
            hrow.append(255 if hole else 0)
            # preview: layer colour x hillshade, water blue, hole black
            c = [sum(colours[k][c_] * w[k] for k in LAYERS) for c_ in range(3)]
            ip, jp = min(i + 1, n - 1), min(j + 1, n - 1)
            shade = clamp(0.75 + (heights[j][i] - heights[jp][ip]) / 60.0, 0.35, 1.25)
            if h < WATER_Z:
                c = [40, 70, 90]
                shade = 1.0
            if hole:
                c, shade = [10, 10, 10], 1.0
            prow += bytes(int(clamp(v * shade, 0, 255)) for v in c)
        rows16.append(bytes(r16))
        for k in LAYERS:
            layer_rows[k].append(bytes(lrow[k]))
        hole_rows.append(bytes(hrow))
        prev_rows.append(bytes(prow))

    _png(os.path.join(out_dir, "eldmoor_height.png"), n, n, rows16, 16, 0)
    with open(os.path.join(out_dir, "eldmoor_height.r16"), "wb") as f:
        f.write(bytes(raw))
    for k in LAYERS:
        _png(os.path.join(out_dir, "eldmoor_w_{}.png".format(k.lower())), n, n, layer_rows[k], 8, 0)
    _png(os.path.join(out_dir, "eldmoor_w_hole.png"), n, n, hole_rows, 8, 0)
    _png(os.path.join(out_dir, "eldmoor_preview.png"), n, n, prev_rows, 8, 2)
    info = {
        "verts": n, "quadCm": QUAD_CM, "originZoneLocalCm": ORIGIN_CM,
        "components": COMPONENTS, "quadsPerSection": QUADS_PER_COMPONENT, "sectionsPerComponent": 1,
        "zScale": Z_SCALE, "cmPerUnit": CM_PER_UNIT, "waterZ": WATER_Z,
        "heightMinCm": round(stats["min"], 1), "heightMaxCm": round(stats["max"], 1),
        "layers": list(LAYERS), "holeTiles": list(HOLE_TILES),
    }
    json.dump(info, open(os.path.join(out_dir, "eldmoor_terrain.json"), "w"), indent=2)
    return info


if __name__ == "__main__":
    print(generate(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else VERTS))
