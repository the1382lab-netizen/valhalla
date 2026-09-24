"""B-15 Wave 4: finish the icon studio's raw renders (wave4_icons.py).

    python wave4_icon_finish.py [name ...]

Each ``Valhalla2/Saved/IconStudio/<name>_raw.png`` (256 px, transparent) becomes
``Import/UI/Icons/<name>.png``: 128 px, a 1 px dark outline and a soft shadow
down and to the right, so the icon reads on any slot background.
"""

import glob
import os
import sys

from PIL import Image, ImageFilter

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WORK = os.path.join(REPO, "Valhalla2", "Saved", "IconStudio")
OUT = os.path.join(REPO, "Import", "UI", "Icons")
PX = 128


def finish(raw, out):
    im = Image.open(raw).convert("RGBA").resize((PX, PX), Image.LANCZOS)
    a = im.split()[3]
    outline = a.filter(ImageFilter.MaxFilter(3))
    shadow = a.filter(ImageFilter.GaussianBlur(3)).point(lambda v: int(v * 0.55))
    base = Image.new("RGBA", im.size, (0, 0, 0, 0))
    base.paste(Image.new("RGBA", im.size, (0, 0, 0, 255)), (2, 3), shadow)
    base.paste(Image.new("RGBA", im.size, (12, 10, 8, 255)), (0, 0), outline)
    base.alpha_composite(im)
    base.save(out)


if __name__ == "__main__":
    names = sys.argv[1:] or [os.path.basename(p)[:-8] for p in glob.glob(os.path.join(WORK, "*_raw.png"))]
    for n in names:
        finish(os.path.join(WORK, n + "_raw.png"), os.path.join(OUT, n + ".png"))
    print("finished", len(names), sorted(names))
