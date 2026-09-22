"""Composite layer sheets into finished character sheets (presets, NPCs, tests)."""
import json, os, sys
from PIL import Image

OUT = os.environ.get("VALHALLA_OUT") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "out")
M = json.load(open(os.path.join(OUT, "manifest.json")))
CELL = M["cell"]
DIRS = M["directions"]
ITEM = {i["id"]: i for i in M["items"]}
BODY = {b["id"]: b for b in M["bodies"]}
_cache = {}


def sheet(path):
    if path not in _cache:
        _cache[path] = Image.open(os.path.join(OUT, path)).convert("RGBA")
    return _cache[path]


def compose(body_id, slots):
    """slots: {slot: item_id}. Returns a full 20x8 composited sheet."""
    ref = sheet(BODY[body_id]["sheet"])
    out = Image.new("RGBA", ref.size, (0, 0, 0, 0))
    for r, d in enumerate(DIRS):
        for name in M["layerOrder"][d]:
            if name == "body":
                src = ref
            else:
                iid = slots.get(name)
                if not iid:
                    continue
                src = sheet(ITEM[iid]["sheet"])
            band = (0, r * CELL, ref.width, (r + 1) * CELL)
            out.alpha_composite(src.crop(band), (0, r * CELL))
    return out


def main():
    os.makedirs(os.path.join(OUT, "presets"), exist_ok=True)
    for p in M["presets"]:
        img = compose(p["body"], p["slots"])
        img.save(os.path.join(OUT, "presets", p["id"] + ".png"))
        print("composed", p["id"])


if __name__ == "__main__":
    main()
