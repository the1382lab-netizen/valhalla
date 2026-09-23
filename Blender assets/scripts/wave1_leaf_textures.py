"""Leaf textures for the Wave 1 trees, composited from ambientCG LeafSet016 (CC0).

T_LeafCluster_*: one leafy clump on transparent (alpha = opacity mask) for cards.
T_LeafMass_*:    a tileable, fully covered leaf mass for the opaque canopy cores.
Normals are DirectX (green down), rotated with each leaf.
"""
import math, random, sys
import numpy as np
from PIL import Image

SRC = sys.argv[2] if len(sys.argv) > 2 else "LeafSet016_2K-PNG/LeafSet016_2K-PNG_"  # ambientCG LeafSet016 2K PNG, unzipped
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/leafgen/out"
random.seed(7); np.random.seed(7)

col = np.asarray(Image.open(SRC + "Color.png").convert("RGB"), np.float32) / 255
opa = np.asarray(Image.open(SRC + "Opacity.png").convert("L"), np.float32) / 255
nrm = np.asarray(Image.open(SRC + "NormalDX.png").convert("RGB"), np.float32) / 255
rgh = np.asarray(Image.open(SRC + "Roughness.png").convert("L"), np.float32) / 255

# Split the 3 x 2 atlas into leaves by the opacity bounding box of each cell.
leaves = []
H, W = opa.shape
for r in range(2):
    for c in range(3):
        y0, y1, x0, x1 = r * H // 2, (r + 1) * H // 2, c * W // 3, (c + 1) * W // 3
        m = opa[y0:y1, x0:x1] > 0.5
        ys, xs = np.where(m)
        by0, by1, bx0, bx1 = ys.min() + y0, ys.max() + y0 + 1, xs.min() + x0, xs.max() + x0 + 1
        rgba = np.dstack([col[by0:by1, bx0:bx1], opa[by0:by1, bx0:bx1]])
        leaves.append((rgba, nrm[by0:by1, bx0:bx1], rgh[by0:by1, bx0:bx1]))

def to_img(a, mode):
    return Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8), mode)

def leaf_images(i, scale_px, angle_deg):
    rgba, n, r = leaves[i]
    h = scale_px
    w = max(4, int(rgba.shape[1] * h / rgba.shape[0]))
    ci = to_img(rgba, "RGBA").resize((w, h), Image.LANCZOS).rotate(angle_deg, Image.BICUBIC, expand=True)
    # Rotate the normal's XY with the image: tangent-space vectors turn too.
    nv = n * 2 - 1
    ni = to_img(np.dstack([n, rgba[..., 3]]), "RGBA").resize((w, h), Image.LANCZOS).rotate(angle_deg, Image.BICUBIC, expand=True)
    ri = to_img(np.dstack([r, r, r, rgba[..., 3]]), "RGBA").resize((w, h), Image.LANCZOS).rotate(angle_deg, Image.BICUBIC, expand=True)
    na = np.asarray(ni, np.float32) / 255
    v = na[..., :3] * 2 - 1
    a = math.radians(angle_deg)
    # PIL rotates counter-clockwise on screen. DX normal: +X right, +Y down.
    ca, sa = math.cos(a), math.sin(a)
    x, y = v[..., 0], v[..., 1]
    xr = x * ca + y * sa
    yr = -x * sa + y * ca
    v = np.dstack([xr, yr, v[..., 2]])
    na = np.dstack([(v + 1) / 2, na[..., 3]])
    return np.asarray(ci, np.float32) / 255, na, np.asarray(ri, np.float32) / 255

def composite(size, items, tile, bg=None):
    C = np.zeros((size, size, 3), np.float32); A = np.zeros((size, size), np.float32)
    N = np.zeros((size, size, 3), np.float32); N[..., 2] = 1.0
    R = np.full((size, size), 0.6, np.float32); SH = np.ones((size, size), np.float32)
    if bg is not None:
        C[:] = bg; A[:] = 1.0
    for (cx, cy, px, ang, idx, shade, hue) in items:
        c, n, r = leaf_images(idx, px, ang)
        h, w = c.shape[:2]
        x0, y0 = int(cx - w / 2), int(cy - h / 2)
        offs = [(0, 0)]
        if tile:
            offs = [(dx, dy) for dx in (-size, 0, size) for dy in (-size, 0, size)]
        for dx, dy in offs:
            ax0, ay0 = x0 + dx, y0 + dy
            sx0, sy0 = max(0, -ax0), max(0, -ay0)
            ex, ey = min(w, size - ax0), min(h, size - ay0)
            if ex <= sx0 or ey <= sy0:
                continue
            a = c[sy0:ey, sx0:ex, 3]
            sl = (slice(ay0 + sy0, ay0 + ey), slice(ax0 + sx0, ax0 + ex))
            rgb = c[sy0:ey, sx0:ex, :3] * shade * np.array(hue, np.float32)
            C[sl] = C[sl] * (1 - a[..., None]) + rgb * a[..., None]
            # Everything under a new leaf is shadowed a little by it.
            SH[sl] = SH[sl] * (1 - 0.25 * a) + 0.25 * a * 1.0
            N[sl] = N[sl] * (1 - a[..., None]) + (n[sy0:ey, sx0:ex, :3] * 2 - 1) * a[..., None]
            R[sl] = R[sl] * (1 - a) + r[sy0:ey, sx0:ex, 0] * a
            A[sl] = np.maximum(A[sl], a)
    N /= np.linalg.norm(N, axis=2, keepdims=True) + 1e-6
    return C, A, N, R

def save(name, C, A, N, R, AO, alpha):
    bc = np.dstack([C, A]) if alpha else C
    to_img(bc, "RGBA" if alpha else "RGB").save("%s/T_%s_BC.png" % (OUT, name))
    to_img((N + 1) / 2, "RGB").save("%s/T_%s_N.png" % (OUT, name))
    to_img(np.dstack([AO, R, np.zeros_like(R)]), "RGB").save("%s/T_%s_ORM.png" % (OUT, name))

def hue():
    k = random.uniform(-1, 1)
    return (0.92 + 0.08 * k, 1.0, 0.85 + 0.1 * random.random())

# ── Cluster: leaves in layers, back (dark, inner) to front (light, outer) ───
S = 1024
items = []
for layer in range(4):
    n = [40, 45, 45, 35][layer]
    for _ in range(n):
        rad = random.random() ** 0.6 * (0.30 + 0.05 * layer) * S * (1.0 - 0.15 * layer / 3)
        th = random.uniform(0, 2 * math.pi)
        cx, cy = S / 2 + rad * math.cos(th), S / 2 + rad * math.sin(th) * 0.92
        # Leaves point outward from the clump centre (petiole in, tip out).
        ang = math.degrees(-th) - 90 + random.uniform(-35, 35)
        px = int(S * random.uniform(0.15, 0.21))
        shade = 0.55 + 0.15 * layer + random.uniform(-0.06, 0.06)
        items.append((cx, cy, px, ang, random.randrange(6), shade, hue()))
C, A, N, R = composite(S, items, tile=False)
yy, xx = np.mgrid[0:S, 0:S] / S
AO = np.clip(0.55 + 0.45 * np.hypot(xx - 0.5, yy - 0.42) / 0.45, 0, 1).astype(np.float32)
# Premultiplied-looking fringe fix: bleed colour into transparent pixels so the
# mask edge doesn't pick up black when mipped.
from PIL import ImageFilter
cim = to_img(C, "RGB"); m = to_img(A, "L").point(lambda v: 255 if v > 10 else 0)
bled = cim.filter(ImageFilter.GaussianBlur(12))
C = np.asarray(Image.composite(cim, bled, m), np.float32) / 255
save("LeafCluster", C, A, N, R, AO, True)

# ── Mass: tileable, fully covered ────────────────────────────────────────────
items = []
for layer in range(4):
    for _ in range(90):
        cx, cy = random.uniform(0, S), random.uniform(0, S)
        ang = random.uniform(0, 360)
        px = int(S * random.uniform(0.13, 0.18))
        shade = 0.5 + 0.16 * layer + random.uniform(-0.06, 0.06)
        items.append((cx, cy, px, ang, random.randrange(6), shade, hue()))
C, A, N, R = composite(S, items, tile=True, bg=np.array([0.10, 0.16, 0.05], np.float32))
AO = np.clip(0.6 + 0.4 * (C.mean(axis=2) / (C.mean() * 1.6)), 0, 1).astype(np.float32)
save("LeafMass", C, A, N, R, AO, False)
print("ok")
