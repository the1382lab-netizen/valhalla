"""B-15 Wave 5: foliage card textures, drawn procedurally (A-073, A-068).

All on transparent, BC alpha = opacity mask for M_ValhallaFoliage, DirectX
normals, ORM. 512 x 512, the base of the plant at the bottom of the image.

  T_GrassTuft   a tuft of arching grass blades (river banks, meadow clumps)
  T_Reeds       tall reeds with seed heads (river banks)
  T_Fern        a single pinnate fern frond (fern clumps)
  T_FlowersA    a meadow clump: green leaves, yellow and white flowers
  T_FlowersB    the same with violet and blue flowers

    python wave5_foliage_textures.py [<Import/Textures>]
"""

import math
import os
import random
import sys

import numpy as np
from PIL import Image, ImageFilter

OUT_ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Import", "Textures")
S = 512
SS = 2
W = H = S * SS


class Canvas:
    def __init__(self):
        self.col = np.zeros((H, W, 3), np.float32)
        self.a = np.zeros((H, W), np.float32)
        self.h = np.zeros((H, W), np.float32)
        self.yy, self.xx = np.mgrid[0:H, 0:W].astype(np.float32)

    def blade(self, x0, y0, x1, y1, bend, w0, w1, color, tip_color=None):
        """A curved, tapering blade from (x0, y0) (base) to (x1, y1) (tip), in
        0..1 image units, y up. ``bend`` pushes the middle sideways."""
        n = 24
        pts = []
        for i in range(n + 1):
            t = i / n
            x = x0 + (x1 - x0) * t + bend * math.sin(math.pi * t) * 0.5 + bend * t * t * 0.5
            y = y0 + (y1 - y0) * t
            pts.append((x * W, (1 - y) * H, (w0 + (w1 - w0) * t) * W, t))
        for (ax, ay, aw, at), (bx, by, bw, bt) in zip(pts, pts[1:]):
            self._seg(ax, ay, bx, by, max(aw, 0.6), max(bw, 0.6), color, tip_color, at, bt)

    def _seg(self, ax, ay, bx, by, aw, bw, color, tip_color, at, bt):
        pad = max(aw, bw) + 3
        x0, x1 = int(max(0, min(ax, bx) - pad)), int(min(W, max(ax, bx) + pad))
        y0, y1 = int(max(0, min(ay, by) - pad)), int(min(H, max(ay, by) + pad))
        if x1 <= x0 or y1 <= y0:
            return
        X, Y = self.xx[y0:y1, x0:x1], self.yy[y0:y1, x0:x1]
        vx, vy = bx - ax, by - ay
        L2 = vx * vx + vy * vy + 1e-6
        t = np.clip(((X - ax) * vx + (Y - ay) * vy) / L2, 0, 1)
        d = np.hypot(X - ax - t * vx, Y - ay - t * vy)
        wdt = aw + (bw - aw) * t
        k = np.clip(1 - d / np.maximum(wdt, 0.5), 0, 1)
        a = np.clip(k * 3, 0, 1)
        sl = (slice(y0, y1), slice(x0, x1))
        newer = a > self.a[sl] * 0.95
        c = np.array(color, np.float32)
        if tip_color is not None:
            tt = at + (bt - at) * t
            c = c[None, None, :] * (1 - tt[..., None] ** 3) + np.array(tip_color, np.float32) * tt[..., None] ** 3
        else:
            c = np.broadcast_to(c, (y1 - y0, x1 - x0, 3))
        shade = (0.8 + 0.2 * k)[..., None]
        self.col[sl] = np.where(newer[..., None], c * shade, self.col[sl])
        self.a[sl] = np.maximum(self.a[sl], a)
        self.h[sl] = np.where(newer, np.maximum(self.h[sl], k), self.h[sl])

    def disc(self, cx, cy, r, color, height=1.0):
        X, Y = self.xx, self.yy
        px, py, pr = cx * W, (1 - cy) * H, r * W
        x0, x1 = int(max(0, px - pr - 2)), int(min(W, px + pr + 2))
        y0, y1 = int(max(0, py - pr - 2)), int(min(H, py + pr + 2))
        if x1 <= x0 or y1 <= y0:
            return
        sl = (slice(y0, y1), slice(x0, x1))
        d = np.hypot(X[sl] - px, Y[sl] - py)
        k = np.clip(1 - d / pr, 0, 1)
        a = np.clip(k * 4, 0, 1)
        self.col[sl] = np.where((a > 0.5)[..., None], np.array(color, np.float32) * (0.8 + 0.3 * k)[..., None], self.col[sl])
        self.a[sl] = np.maximum(self.a[sl], a)
        self.h[sl] = np.maximum(self.h[sl], k * height)

    def flower(self, cx, cy, r, petal, centre, n=5, rng=None):
        rng = rng or random
        rot = rng.uniform(0, 2 * math.pi)
        for k in range(n):
            a = rot + 2 * math.pi * k / n
            self.disc(cx + math.cos(a) * r * 0.55, cy + math.sin(a) * r * 0.55, r * 0.5, petal, 0.8)
        self.disc(cx, cy, r * 0.3, centre, 1.0)

    def save(self, name):
        def down(arr, mode):
            im = Image.fromarray(np.clip(arr * 255 + 0.5, 0, 255).astype(np.uint8), mode)
            return im.resize((S, S), Image.LANCZOS)
        hs = np.asarray(Image.fromarray((self.h * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(SS)), np.float32) / 255
        gy, gx = np.gradient(hs)
        nx, ny, nz = -gx * 5, gy * 5, np.ones_like(gx)
        ln = np.sqrt(nx * nx + ny * ny + nz * nz)
        cim = down(self.col, "RGB")
        am = down(self.a, "L")
        mask = am.point(lambda p: 255 if p > 8 else 0)
        cim = Image.composite(cim, cim.filter(ImageFilter.GaussianBlur(8)), mask)
        folder = os.path.join(OUT_ROOT, name)
        os.makedirs(folder, exist_ok=True)
        Image.merge("RGBA", (*cim.split(), am)).save(os.path.join(folder, "T_%s_BC.png" % name))
        down(np.dstack([(nx / ln + 1) / 2, (ny / ln + 1) / 2, (nz / ln + 1) / 2]), "RGB").save(os.path.join(folder, "T_%s_N.png" % name))
        rough = np.clip(0.65 - 0.1 * self.h, 0, 1)
        ao = np.clip(0.55 + 0.45 * hs, 0, 1)
        down(np.dstack([ao, rough, np.zeros_like(ao)]), "RGB").save(os.path.join(folder, "T_%s_ORM.png" % name))


def green(rng, base=(0.20, 0.32, 0.10), var=0.04):
    g = rng.uniform(-var, var)
    return (base[0] + g, base[1] + g * 1.3, base[2] + g * 0.5)


def grass_tuft(seed=1):
    rng = random.Random(seed)
    c = Canvas()
    for i in range(70):
        x0 = 0.5 + rng.gauss(0, 0.08)
        h = rng.uniform(0.45, 0.95)
        lean = rng.gauss(0, 0.22)
        c.blade(x0, 0.0, x0 + lean * 0.6, h, lean * 0.4, 0.012, 0.001, green(rng), (0.55, 0.52, 0.28))
    c.save("GrassTuft")


def reeds(seed=2):
    rng = random.Random(seed)
    c = Canvas()
    for i in range(34):
        x0 = 0.5 + rng.gauss(0, 0.13)
        h = rng.uniform(0.7, 0.98)
        lean = rng.gauss(0, 0.06)
        c.blade(x0, 0.0, x0 + lean, h, lean * 0.3, 0.008, 0.004, green(rng, (0.28, 0.34, 0.14)), (0.45, 0.42, 0.25))
        if rng.random() < 0.45:
            # A cattail seed head near the top.
            c.blade(x0 + lean * (h - 0.12) / h, h - 0.16, x0 + lean, h - 0.02, 0.0, 0.016, 0.014, (0.30, 0.18, 0.09))
    for i in range(30):
        x0 = 0.5 + rng.gauss(0, 0.15)
        c.blade(x0, 0.0, x0 + rng.gauss(0, 0.2), rng.uniform(0.3, 0.6), rng.gauss(0, 0.1), 0.014, 0.001, green(rng))
    c.save("Reeds")


def fern(seed=3):
    rng = random.Random(seed)
    c = Canvas()
    c.blade(0.5, 0.02, 0.5, 0.97, 0.03, 0.008, 0.002, (0.20, 0.30, 0.10))
    n = 17
    for i in range(n):
        t = 0.06 + 0.9 * i / n
        y = 0.02 + 0.95 * t
        L = 0.36 * math.sin(math.pi * min(1.0, t * 1.05)) ** 0.8 * (1 - 0.35 * t)
        for side in (-1, 1):
            col = green(rng, (0.16, 0.30, 0.08), 0.03)
            c.blade(0.5 + 0.015 * math.sin(t * 3), y, 0.5 + side * L, y + L * 0.5, side * 0.03, 0.024 * (1 - 0.5 * t), 0.003, col)
    c.save("Fern")


def flowers(name, petals, seed):
    rng = random.Random(seed)
    c = Canvas()
    for i in range(40):
        x0 = 0.5 + rng.gauss(0, 0.12)
        c.blade(x0, 0.0, x0 + rng.gauss(0, 0.18), rng.uniform(0.25, 0.55), rng.gauss(0, 0.08), 0.016, 0.002, green(rng))
    for i in range(22):
        x0 = 0.5 + rng.gauss(0, 0.14)
        h = rng.uniform(0.45, 0.85)
        top = x0 + rng.gauss(0, 0.07)
        c.blade(x0, 0.0, top, h, rng.gauss(0, 0.03), 0.004, 0.003, (0.22, 0.34, 0.12))
        p = petals[rng.randrange(len(petals))]
        c.flower(top, h, rng.uniform(0.035, 0.055), p, (0.85, 0.7, 0.15), n=rng.choice((5, 6)), rng=rng)
    c.save(name)


if __name__ == "__main__":
    grass_tuft()
    reeds()
    fern()
    flowers("FlowersA", [(0.95, 0.80, 0.15), (0.92, 0.90, 0.85), (0.95, 0.72, 0.10)], 4)
    flowers("FlowersB", [(0.55, 0.30, 0.75), (0.30, 0.40, 0.85), (0.75, 0.45, 0.85)], 5)
    print("ok")
