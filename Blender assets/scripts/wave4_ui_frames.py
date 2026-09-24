"""B-15 Wave 4 (A-061): HUD frame art, drawn procedurally.

Nine-slice textures for the HUD (``Import/UI/Frames``), in the art bible's
palette: dark leather panels trimmed in bevelled bronze (gold trim #B08D3C),
recessed slot wells, bronze buttons, iron bar frames and a glossy bar fill.
The HUD draws each as a Slate box brush with the margin noted below, so the
borders keep their size whatever the panel's.

    python wave4_ui_frames.py

  T_UI_Panel      256 x 256   margin 24 px   panels (inventory, skills, target...)
  T_UI_Slot        64 x 64    margin 10 px   inventory / action bar cells
  T_UI_Button      64 x 32    margin 10 px   buttons
  T_UI_BarFrame   128 x 24    margin  6 px   HP / mana / cast bars
  T_UI_BarFill     64 x 16    (stretched)    bar fill, tinted by the bar colour
"""

import os

import numpy as np
from PIL import Image, ImageFilter

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(REPO, "Import", "UI", "Frames")
LEATHER = os.path.join(REPO, "Import", "Textures", "Leather", "T_Leather_BC.png")
GOLD = np.array([0.69, 0.55, 0.24])
IRON = np.array([0.33, 0.35, 0.38])
SS = 4                              # supersample


def rect_sdf(w, h, inset, radius=0.0):
    """Signed distance (px) to a rounded rectangle inset from the image edge,
    negative inside, at SS x resolution."""
    yy, xx = np.mgrid[0:h * SS, 0:w * SS].astype(np.float32) / SS
    cx, cy = w / 2.0, h / 2.0
    hx, hy = w / 2.0 - inset - radius, h / 2.0 - inset - radius
    qx, qy = np.abs(xx + 0.5 / SS - cx) - hx, np.abs(yy + 0.5 / SS - cy) - hy
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - radius


def shade(height, strength=6.0, light=(-0.6, -0.65, 0.5)):
    """Lambert + a little specular from a height field (image y down)."""
    gy, gx = np.gradient(height)
    nx, ny, nz = -gx * strength, -gy * strength, np.ones_like(height)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    L = np.array(light, np.float32)
    L /= np.linalg.norm(L)
    lam = np.clip((nx * L[0] + ny * L[1] + nz * L[2]) / ln, 0, 1)
    hv = L + np.array([0, 0, 1.0])
    hv /= np.linalg.norm(hv)
    spec = np.clip((nx * hv[0] + ny * hv[1] + nz * hv[2]) / ln, 0, 1) ** 24
    return lam, spec


def band(d, outer, inner):
    """Height of a bevelled band between outer and inner distances (d<=0 inside)."""
    t = np.clip((-d - outer) / max(inner - outer, 1e-3), 0, 1)       # 0 at the outer edge .. 1 at the inner
    return np.clip(np.minimum(t, 1 - t) * 2.5, 0, 1) ** 0.7 * ((t > 0) & (t < 1))


def metal(color, lam, spec, noise):
    return color * (0.35 + 0.9 * lam)[..., None] * (0.9 + 0.2 * noise)[..., None] + spec[..., None] * 0.55


def noise(h, w, scale, seed):
    rs = np.random.RandomState(seed)
    n = rs.rand(max(2, h // scale), max(2, w // scale)).astype(np.float32)
    return np.asarray(Image.fromarray((n * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC), np.float32) / 255


def save(name, rgb, alpha, w, h):
    im = Image.fromarray((np.clip(np.dstack([rgb, alpha]), 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")
    im.resize((w, h), Image.LANCZOS).save(os.path.join(OUT, name + ".png"))


def panel(w=256, h=256):
    W, H = w * SS, h * SS
    d = rect_sdf(w, h, 1.0, 6.0)
    alpha = np.clip(0.5 - d * SS, 0, 1)
    # Interior: dark leather, darker towards the trim (an inner shadow).
    lea = np.asarray(Image.open(LEATHER).convert("RGB").resize((W, H), Image.BICUBIC), np.float32) / 255
    lum = lea.mean(axis=2, keepdims=True)
    inside = np.array([0.075, 0.062, 0.05]) * (0.7 + 0.8 * lum)
    shadow = np.clip((-d - 16) / 26, 0, 1)
    rgb = inside * (0.45 + 0.55 * shadow)[..., None]
    # Trim: outer bronze band (2..14 px) and a thin inner gold line (18..20 px).
    hband = band(d, 1.0, 14.0) + 0.6 * band(d, 17.0, 20.5)
    n = noise(H, W, 24, 3)
    lam, spec = shade(hband, 5.0)
    m = metal(GOLD, lam, spec, n)
    trim = np.clip((hband > 0.001).astype(np.float32), 0, 1)
    # Dark separating groove between the band and the leather.
    groove = ((-d > 14) & (-d < 17)).astype(np.float32)
    rgb = rgb * (1 - trim[..., None]) + m * trim[..., None]
    rgb = rgb * (1 - 0.7 * groove[..., None])
    # Corner studs.
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32) / SS
    for cx in (8.0, w - 8.0):
        for cy in (8.0, h - 8.0):
            r = np.hypot(xx - cx, yy - cy)
            hs = np.clip(1 - r / 5.0, 0, 1) ** 0.6
            ls, ss = shade(hs, 3.0)
            k = (r < 5.0).astype(np.float32)
            rgb = rgb * (1 - k[..., None]) + metal(GOLD * 1.1, ls, ss, n)[...] * k[..., None]
    save("T_UI_Panel", rgb, alpha, w, h)


def slot(w=64, h=64):
    W, H = w * SS, h * SS
    d = rect_sdf(w, h, 0.5, 3.0)
    alpha = np.clip(0.5 - d * SS, 0, 1)
    hband = band(d, 0.5, 5.0)
    lam, spec = shade(hband, 4.0)
    n = noise(H, W, 8, 5)
    rim = metal(GOLD * 0.9, lam, spec, n)
    # The well: dark, with a shadow cast from the top-left rim.
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32) / SS
    depth = np.clip((-d - 5.0) / 8.0, 0, 1)
    tl = np.clip(1 - (np.minimum(xx, yy) - 5.0) / 7.0, 0, 1)
    well = np.array([0.055, 0.05, 0.048]) * (0.6 + 0.5 * depth)[..., None] * (1 - 0.45 * tl)[..., None]
    well = well * (0.9 + 0.2 * n)[..., None]
    k = (hband > 0.001).astype(np.float32) + ((-d) < 1.0) * 1.0
    k = np.clip(k, 0, 1)
    rgb = well * (1 - k[..., None]) + rim * k[..., None]
    save("T_UI_Slot", rgb, alpha, w, h)


def button(w=64, h=32):
    W, H = w * SS, h * SS
    d = rect_sdf(w, h, 0.5, 5.0)
    alpha = np.clip(0.5 - d * SS, 0, 1)
    hgt = np.clip(-d / 5.0, 0, 1) ** 0.6
    lam, spec = shade(hgt, 5.0)
    n = noise(H, W, 6, 7)
    yy = np.mgrid[0:H, 0:W][0].astype(np.float32) / H
    rgb = metal(GOLD, lam, spec, n) * (1.1 - 0.35 * yy)[..., None]
    save("T_UI_Button", rgb, alpha, w, h)


def bar_frame(w=128, h=24):
    W, H = w * SS, h * SS
    d = rect_sdf(w, h, 0.5, 3.0)
    alpha = np.clip(0.5 - d * SS, 0, 1)
    hband = band(d, 0.5, 4.0)
    lam, spec = shade(hband, 4.0)
    n = noise(H, W, 6, 9)
    rim = metal(IRON, lam, spec, n)
    k = (hband > 0.001).astype(np.float32)
    inner = np.array([0.03, 0.03, 0.035])
    rgb = inner * (1 - k[..., None]) + rim * k[..., None]
    save("T_UI_BarFrame", rgb, alpha, w, h)


def bar_fill(w=64, h=16):
    yy = (np.mgrid[0:h * SS, 0:w * SS][0].astype(np.float32) + 0.5) / (h * SS)
    v = 1.0 - 0.45 * yy
    v = v + 0.25 * np.exp(-((yy - 0.22) / 0.1) ** 2)            # a gloss line near the top
    rgb = np.dstack([v, v, v]) / 1.2
    save("T_UI_BarFill", rgb, np.ones_like(v), w, h)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    panel()
    slot()
    button()
    bar_frame()
    bar_fill()
    print("ok", sorted(os.listdir(OUT)))
