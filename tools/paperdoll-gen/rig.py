"""
Valhalla — 8-directional pixel-art character rig.

Coordinate system
-----------------
Body space is 3D: (L, D, V)
  L = lateral  (+ = character's RIGHT side)
  D = sagittal (+ = the direction the character faces)
  V = vertical (+ = up from the ground plane)

Facing is a unit ground vector f = (fx, fy) where
  fx = +1 -> facing screen right
  fy = +1 -> facing "south" (toward the camera / down the screen)

Projection to the 48x48 cell:
  x = CX + (-fy * L + fx * D)
  y = GROUND_Y + DEPTH * (fx * L + fy * D) - V

Depth key (larger = nearer the camera, drawn later):
  z = DEPTH * (fx * L + fy * D)

Layer tagging
-------------
Canvas paints two buffers in lockstep: the colour image and an 8-bit `tag`
image.  Setting `canvas.tag = n` before a group of primitives stamps that id
into every pixel they cover.  After the whole scene is drawn, the pixels whose
final tag == n are exactly the pixels that group *wins* -- i.e. the group as it
appears with occlusion by everything drawn over it already resolved.  That is
what lets one equipment item be extracted as a standalone sprite layer that
composites correctly over the base body.
"""
import math
from PIL import Image, ImageDraw

SS = 4              # supersample factor
CELL = 48           # final cell size in px
DEPTH = 0.50        # ground-plane foreshortening; matches Valhalla's
                    # orthoToIso() exactly: screenY = (worldX + worldY) / 2
CX = 24.0
GROUND_Y = 44.0

OUTLINE = (26, 22, 32)

TAG_BODY = 1
TAG_ITEM = 2

# ---------------------------------------------------------------- colour utils

def lighten(c, f=0.30):
    return tuple(min(255, int(round(v + (255 - v) * f))) for v in c[:3])

def darken(c, f=0.34):
    return tuple(max(0, int(round(v * (1.0 - f)))) for v in c[:3])

def mix(a, b, t):
    return tuple(int(round(a[i] * (1 - t) + b[i] * t)) for i in range(3))

# ---------------------------------------------------------------- canvas

class Canvas:
    def __init__(self):
        self.img = Image.new("RGBA", (CELL * SS, CELL * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)
        self.tag_img = Image.new("L", (CELL * SS, CELL * SS), 0)
        self.td = ImageDraw.Draw(self.tag_img)
        self.tag = TAG_BODY

    def _s(self, v):
        return v * SS

    def ellipse(self, cx, cy, rx, ry, fill, outline=True, ow=1.0):
        box = [self._s(cx - rx), self._s(cy - ry), self._s(cx + rx), self._s(cy + ry)]
        w = max(1, int(ow * SS))
        self.d.ellipse(box, fill=fill, outline=OUTLINE if outline else None, width=w)
        self.td.ellipse(box, fill=self.tag, outline=self.tag if outline else None, width=w)

    def polygon(self, pts, fill, outline=True):
        p = [(self._s(x), self._s(y)) for x, y in pts]
        self.d.polygon(p, fill=fill, outline=OUTLINE if outline else None, width=SS)
        self.td.polygon(p, fill=self.tag, outline=self.tag if outline else None, width=SS)

    def line(self, p0, p1, width, fill, outline=True):
        a = (self._s(p0[0]), self._s(p0[1]))
        b = (self._s(p1[0]), self._s(p1[1]))
        for d, col, ocol in ((self.d, fill, OUTLINE), (self.td, self.tag, self.tag)):
            if outline:
                ww = max(1, int((width + 2) * SS))
                d.line([a, b], fill=ocol, width=ww)
                for q in (a, b):
                    d.ellipse([q[0] - ww / 2, q[1] - ww / 2, q[0] + ww / 2, q[1] + ww / 2], fill=ocol)
            w = max(1, int(width * SS))
            d.line([a, b], fill=col, width=w)
            for q in (a, b):
                d.ellipse([q[0] - w / 2, q[1] - w / 2, q[0] + w / 2, q[1] + w / 2], fill=col)

    def dot(self, cx, cy, r, fill):
        box = [self._s(cx - r), self._s(cy - r), self._s(cx + r), self._s(cy + r)]
        self.d.ellipse(box, fill=fill)
        self.td.ellipse(box, fill=self.tag)


# ---------------------------------------------------------------- projection

class View:
    def __init__(self, fx, fy):
        n = math.hypot(fx, fy) or 1.0
        self.fx, self.fy = fx / n, fy / n

    def p(self, L, D, V):
        x = CX + (-self.fy * L + self.fx * D)
        y = GROUND_Y + DEPTH * (self.fx * L + self.fy * D) - V
        return (x, y)

    def z(self, L, D):
        return DEPTH * (self.fx * L + self.fy * D)

    @property
    def facing_camera(self):
        return self.fy          # +1 fully toward camera, -1 fully away

    def width(self, lat, sag):
        """screen half-width of an ellipsoid with lateral/sagittal half-axes"""
        return math.hypot(lat * self.fy, sag * self.fx)


DIRECTIONS = [
    ("s",  0.0,  1.0),
    ("se", 0.7071,  0.7071),
    ("e",  1.0,  0.0),
    ("ne", 0.7071, -0.7071),
    ("n",  0.0, -1.0),
    ("nw", -0.7071, -0.7071),
    ("w",  -1.0, 0.0),
    ("sw", -0.7071, 0.7071),
]
