"""B-08a: the front end's "Gilded Hall" art.

Writes four textures to Import/UI/Frames (where ValhallaUIArt::LoadUiTexture
finds them on disk; Plugins/ValhallaTools' import_ui_icons.py imports them to
/Game/Valhalla/UI/Frames for a packaged build):

  T_UI_Logo.png              the coin logo (Docs/branding/valhalla_logo.svg)
                             without its square background, 512 px
  T_UI_FrontEndBackdrop.png  the warm glow over the dark page, 1920 x 1080,
                             alpha only where the glow is
  T_UI_PreviewMask.png       fades the character preview's edges into the page
                             (transparent middle, page colour at the edges), 2:3
  T_UI_FloorGlow.png         the pool of light the preview character stands in

Needs Pillow, NumPy and CairoSVG. Run from anywhere:

    python Tools/ui/make_frontend_art.py
"""

import io
import os
import re

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "Import", "UI", "Frames")
LOGO_SVG = os.path.join(ROOT, "Docs", "branding", "valhalla_logo.svg")

# ValhallaUIArt's palette (sRGB).
BACKGROUND = (0x0C, 0x0A, 0x07)
BACKGROUND_GLOW = (0x2A, 0x21, 0x12)
GOLD_LIGHT = (0xD9, 0xB6, 0x5E)

RNG = np.random.default_rng(8)


def save(rgba, name):
    """rgba: float array H x W x 4 in 0..1. Dithered to 8 bits so gradients do not band."""
    noise = (RNG.random(rgba.shape) - 0.5) / 255.0
    data = np.clip((rgba + noise) * 255.0 + 0.5, 0, 255).astype(np.uint8)
    path = os.path.join(OUT, name)
    Image.fromarray(data, "RGBA").save(path)
    print("wrote", os.path.relpath(path, ROOT))


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def solid(h, w, rgb, alpha):
    out = np.zeros((h, w, 4))
    out[..., 0] = rgb[0] / 255.0
    out[..., 1] = rgb[1] / 255.0
    out[..., 2] = rgb[2] / 255.0
    out[..., 3] = alpha
    return out


def logo():
    import cairosvg

    svg = open(LOGO_SVG, encoding="utf-8").read()
    svg = re.sub(r"<metadata>.*?</metadata>", "", svg, flags=re.S)
    svg = svg.replace('<rect width="1024" height="1024" fill="url(#bg)"/>', "")
    png = cairosvg.svg2png(bytestring=svg.encode("utf-8"), output_width=512, output_height=512)
    path = os.path.join(OUT, "T_UI_Logo.png")
    Image.open(io.BytesIO(png)).convert("RGBA").save(path)
    print("wrote", os.path.relpath(path, ROOT))


def backdrop():
    # A soft warm glow, centred a little above the middle, over the solid page.
    h, w = 1080, 1920
    y, x = np.mgrid[0:h, 0:w].astype(float)
    d = np.sqrt(((x - w * 0.5) / (w * 0.62)) ** 2 + ((y - h * 0.38) / (h * 0.78)) ** 2)
    alpha = (1.0 - smoothstep(0.0, 1.0, d)) * 0.85
    save(solid(h, w, BACKGROUND_GLOW, alpha), "T_UI_FrontEndBackdrop.png")


def preview_mask():
    # 2:3 like the preview's render target. The capture's empty space is black,
    # a shade off the page; this blends the rectangle's edges away.
    h, w = 600, 400
    y, x = np.mgrid[0:h, 0:w].astype(float)
    u = np.minimum(x, w - 1 - x) / w   # 0 at the side edges, 0.5 in the middle
    v = np.minimum(y, h - 1 - y) / h
    alpha = 1.0 - smoothstep(0.0, 0.16, u) * smoothstep(0.0, 0.10, v)
    save(solid(h, w, BACKGROUND, alpha), "T_UI_PreviewMask.png")


def floor_glow():
    h, w = 110, 620
    y, x = np.mgrid[0:h, 0:w].astype(float)
    d = np.sqrt(((x - w * 0.5) / (w * 0.5)) ** 2 + ((y - h * 0.5) / (h * 0.5)) ** 2)
    alpha = (1.0 - smoothstep(0.0, 1.0, d)) ** 1.6 * 0.30
    save(solid(h, w, GOLD_LIGHT, alpha), "T_UI_FloorGlow.png")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    logo()
    backdrop()
    preview_mask()
    floor_glow()
