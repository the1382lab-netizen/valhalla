"""B-15 Wave 3: the palm frond texture (T_PalmFrond_*), drawn procedurally.

One pinnate frond on transparent, base at the bottom of the image (v = 0) and
tip at the top (v = 1), rachis down the middle. Leaflets leave the rachis at
about 40 degrees towards the tip, longest a third of the way up, and each one
is folded along its midrib (a V keel), which is where the normal map comes
from. Tips dry to yellow-brown. BC alpha is the opacity mask for
M_ValhallaFoliage; normals are DirectX (green down).

    python wave3_palm_texture.py [<Import/Textures>]
"""

import math
import os
import random
import sys

import numpy as np
from PIL import Image, ImageFilter

OUT_ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Import", "Textures")
W, H = 512, 1024
SS = 2                               # supersampling
rng = random.Random(31)

w, h = W * SS, H * SS
height = np.zeros((h, w), np.float32)     # leaflet keel height, 0..1
alpha = np.zeros((h, w), np.float32)
col = np.zeros((h, w, 3), np.float32)
dry = np.zeros((h, w), np.float32)        # 0 green .. 1 dry tip

yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
cx = w / 2.0


def v_to_y(v):
    return (1.0 - v) * h


def leaflet(x0, y0, ang, length, width, hue, tipdry):
    """Paint one leaflet from (x0, y0), direction ``ang`` (radians, 0 = up)."""
    dx, dy = math.sin(ang), -math.cos(ang)
    x1, y1 = x0 + dx * length, y0 + dy * length
    pad = width * 2 + 4
    bx0, bx1 = int(max(0, min(x0, x1) - pad)), int(min(w, max(x0, x1) + pad))
    by0, by1 = int(max(0, min(y0, y1) - pad)), int(min(h, max(y0, y1) + pad))
    if bx1 <= bx0 or by1 <= by0:
        return
    X, Y = xx[by0:by1, bx0:bx1], yy[by0:by1, bx0:bx1]
    px, py = X - x0, Y - y0
    t = (px * dx + py * dy) / length                 # 0 at the rachis .. 1 at the tip
    s = px * (-dy) + py * dx                         # across the leaflet
    # A slight droop: the leaflet curves back as it goes.
    s = s - 0.06 * length * t * t
    half = width * np.maximum(np.sin(np.clip(t, 0, 1) * math.pi), 0.0) ** 0.55 * (1.0 - 0.35 * t)
    inside = (t >= 0) & (t <= 1) & (np.abs(s) <= half)
    k = np.where(inside, 1.0 - np.abs(s) / np.maximum(half, 1e-3), 0.0)
    a = np.clip(k * 3.0, 0, 1)                       # soft edge
    sl = (slice(by0, by1), slice(bx0, bx1))
    newer = a > alpha[sl] * 0.98
    alpha[sl] = np.maximum(alpha[sl], a)
    height[sl] = np.where(newer, np.maximum(height[sl], k), height[sl])
    shade = 0.82 + 0.18 * k
    base = np.array(hue, np.float32)
    col[sl] = np.where(newer[..., None], base * shade[..., None], col[sl])
    d = np.clip((t - (1.0 - tipdry)) / max(tipdry, 1e-3), 0, 1)
    dry[sl] = np.where(newer, np.maximum(dry[sl] * 0, d), dry[sl])


# Leaflets, alternating sides, base (v=0.04) to tip (v=0.985).
n = 38
for i in range(n):
    for side in (-1, 1):
        v = 0.05 + 0.93 * (i + (0.5 if side > 0 else 0.0)) / n
        y0 = v_to_y(v)
        # Length profile: short at the base, longest at ~0.35, tapering to the tip.
        prof = math.sin(min(1.0, (v - 0.03) / 0.97) * math.pi) ** 0.7 * (1.0 - 0.35 * v)
        length = (0.46 * w) * (0.25 + 0.75 * prof) * rng.uniform(0.9, 1.05)
        ang = side * math.radians(rng.uniform(34, 46) + 10 * v)
        width = w * rng.uniform(0.015, 0.019) * (0.8 + 0.4 * prof)
        g = rng.uniform(-0.03, 0.03)
        hue = (0.20 + g, 0.33 + g * 1.4, 0.11 + g * 0.5)
        leaflet(cx + side * w * 0.008, y0, ang, length, width, hue, rng.uniform(0.08, 0.2))

# Rachis: a tapering yellow-green midrib, drawn over the leaflet roots.
rw = w * (0.022 - 0.016 * (1 - yy / h) * 0)       # constant here; tapered below
taper = 0.016 * (0.3 + 0.7 * (yy / h))           # yy/h: 0 at the tip .. 1 at the base
r_mask = (np.abs(xx - cx) < w * taper) & (yy > h * 0.012)
rk = np.clip(1.0 - np.abs(xx - cx) / (w * taper), 0, 1)
alpha = np.where(r_mask, np.maximum(alpha, np.clip(rk * 3, 0, 1)), alpha)
height = np.where(r_mask, np.maximum(height, 0.6 + 0.4 * rk), height)
col = np.where(r_mask[..., None], np.array([0.42, 0.40, 0.18], np.float32) * (0.85 + 0.15 * rk)[..., None], col)
dry = np.where(r_mask, 0.0, dry)

# Dry tips and a little mottling.
noise = np.asarray(Image.fromarray((np.random.RandomState(4).rand(h // 16, w // 16) * 255).astype(np.uint8))
                   .resize((w, h), Image.BICUBIC), np.float32) / 255
dry_col = np.array([0.55, 0.45, 0.22], np.float32)
col = col * (1 - dry[..., None] * 0.85) + dry_col * dry[..., None] * 0.85
col *= (0.9 + 0.2 * noise)[..., None]

# Normal from the keel height (DirectX: +Y is down the image).
hs = np.asarray(Image.fromarray((height * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(SS)), np.float32) / 255
gy, gx = np.gradient(hs)
strength = 6.0
nx, ny, nz = -gx * strength, gy * strength, np.ones_like(gx)
ln = np.sqrt(nx * nx + ny * ny + nz * nz)
N = np.dstack([nx / ln, ny / ln, nz / ln])

rough = np.clip(0.55 + 0.25 * dry - 0.1 * height, 0, 1)
ao = np.clip(0.65 + 0.35 * hs, 0, 1)


def down(a, mode):
    im = Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8), mode)
    return im.resize((W, H), Image.LANCZOS)


# Bleed colour into transparent texels so mips don't fringe dark.
cim = down(col, "RGB")
am = down(alpha, "L")
mask = am.point(lambda p: 255 if p > 8 else 0)
bled = cim.filter(ImageFilter.GaussianBlur(10))
cim = Image.composite(cim, bled, mask)
folder = os.path.join(OUT_ROOT, "PalmFrond")
os.makedirs(folder, exist_ok=True)
Image.merge("RGBA", (*cim.split(), am)).save(os.path.join(folder, "T_PalmFrond_BC.png"))
down((N + 1) / 2, "RGB").save(os.path.join(folder, "T_PalmFrond_N.png"))
down(np.dstack([ao, rough, np.zeros_like(rough)]), "RGB").save(os.path.join(folder, "T_PalmFrond_ORM.png"))
print("ok", folder)
