"""B-15 Wave 3: the cactus rib texture (T_Cactus_*), drawn procedurally.

The cactus meshes (wave3_desert.py) map one texture repeat across each rib
(u, groove to groove) and one repeat per game metre up the stem (v), so the
texture is tall and thin: 128 x 2048. The rib crest runs down the middle with
an areole (a felted pad with a few spines) every 3 cm; the skin darkens and
gets glossier into the grooves. Tileable in both directions.

    python wave3_cactus_texture.py [<Import/Textures>]
"""

import math
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

OUT_ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Import", "Textures")
W, H = 128, 2048
PITCH = 64                                   # px between areoles (~3 cm)
rs = np.random.RandomState(12)

yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
u = xx / W                                   # 0..1 across the rib
crest = 0.5 + 0.5 * np.cos((u - 0.5) * 2 * math.pi)      # 1 on the crest, 0 in the grooves

# Skin: waxy green, lighter and a touch yellower on the crest, faint vertical
# striations, a slow blotchy variation along the stem.
streak = np.asarray(Image.fromarray((rs.rand(H // 8, W) * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC),
                    np.float32) / 255
blotch = np.asarray(Image.fromarray((rs.rand(H // 256, 2) * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC),
                    np.float32) / 255
g0 = np.array([0.13, 0.22, 0.10], np.float32)
g1 = np.array([0.26, 0.36, 0.16], np.float32)
col = g0 + (g1 - g0) * (0.35 + 0.65 * crest)[..., None]
col *= (0.92 + 0.10 * streak + 0.08 * blotch)[..., None]
height = 0.15 * crest + 0.02 * streak
rough = 0.62 - 0.12 * crest

# Areoles: felted pads on the crest, alternating slightly left/right.
pad = np.zeros((H, W), np.float32)
spine = np.zeros((H, W), np.float32)
for k in range(H // PITCH):
    cy = k * PITCH + PITCH / 2 + rs.uniform(-4, 4)
    cx = W / 2 + rs.uniform(-3, 3)
    for dy in (-H, 0, H):                    # tile vertically
        d = np.hypot(xx - cx, (yy - cy - dy) * 1.0)
        pad = np.maximum(pad, np.clip(1.0 - d / 7.0, 0, 1))
        # 6-8 spines radiating from the pad, mostly downward and sideways.
        for s in range(rs.randint(6, 9)):
            a = rs.uniform(-0.2, 1.2) * math.pi if s else math.pi / 2
            ln = rs.uniform(14, 30)
            ex, ey = cx + math.cos(a) * ln, cy + dy + math.sin(a) * ln
            px, py = xx - cx, yy - (cy + dy)
            vx, vy = ex - cx, ey - (cy + dy)
            t = np.clip((px * vx + py * vy) / (vx * vx + vy * vy), 0, 1)
            dist = np.hypot(px - t * vx, py - t * vy)
            spine = np.maximum(spine, np.clip(1.2 - dist, 0, 1) * (1.0 - 0.6 * t))
padcol = np.array([0.72, 0.66, 0.52], np.float32)
spinecol = np.array([0.80, 0.74, 0.58], np.float32)
col = col * (1 - pad[..., None] * 0.9) + padcol * pad[..., None] * 0.9
col = col * (1 - spine[..., None]) + spinecol * spine[..., None]
height = height + 0.25 * pad + 0.15 * spine
rough = np.clip(rough + 0.3 * pad + 0.2 * spine, 0, 1)
ao = np.clip(0.55 + 0.45 * crest, 0, 1) * (1 - 0.25 * np.clip(pad - 0.6, 0, 1))

hs = np.asarray(Image.fromarray((np.clip(height, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1)),
                np.float32) / 255
gy, gx = np.gradient(hs)
nx, ny, nz = -gx * 5.0, gy * 5.0, np.ones_like(gx)
ln = np.sqrt(nx * nx + ny * ny + nz * nz)


def img(a, mode):
    return Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8), mode)


folder = os.path.join(OUT_ROOT, "Cactus")
os.makedirs(folder, exist_ok=True)
img(col, "RGB").save(os.path.join(folder, "T_Cactus_BC.png"))
img(np.dstack([(nx / ln + 1) / 2, (ny / ln + 1) / 2, (nz / ln + 1) / 2]), "RGB").save(os.path.join(folder, "T_Cactus_N.png"))
img(np.dstack([ao, rough, np.zeros_like(rough)]), "RGB").save(os.path.join(folder, "T_Cactus_ORM.png"))
print("ok", folder)
