"""
Render one sprite sheet per base body and per equipment item.

How an item layer is isolated
-----------------------------
For each frame we draw the WHOLE character -- base body plus the one item --
into the colour buffer and, in lockstep, an id into the tag buffer. The item's
layer is then the pixels whose *final* tag is the item's. Pixels where a body
part ended up on top are excluded, so those parts of the item are already
occluded in the sheet. That means the runtime can composite with a fixed
body -> item order and still get correct depth: a sword swung behind the torso
simply has no pixels there, and the body layer underneath shows through.
"""
import json, math, os, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image

from rig import (Canvas, View, DIRECTIONS, CELL, SS, OUTLINE, TAG_BODY, TAG_ITEM,
                 lighten, darken)
import parts as PT
from poses import all_frames, ANIMS, FRAMES_PER_DIR
from items import BODIES, ITEMS, PRESETS, SLOTS, BODY_BY_ID, ITEM_BY_ID

# Output cell size. The scene is drawn on a CELL*SS = 192px supersampled canvas,
# so any integer divisor of 192 is a clean, artefact-free output size:
#   192/4 = 48 · 192/3 = 64 · 192/2 = 96
# 64 matches the LPC sheets already in the game.
OUT_CELL = int(os.environ.get("VALHALLA_CELL", "64"))
assert (CELL * SS) % OUT_CELL == 0, "OUT_CELL must divide the supersampled canvas"

OUT = os.environ.get("VALHALLA_OUT") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "out")
ICON_OUT = os.environ.get("VALHALLA_ICON_OUT") or os.path.join(OUT, "icons")
ICON_SIZE = 32

REF_BODY = BODY_BY_ID["body_fair"]      # silhouette items are fitted to


# ------------------------------------------------------------------ scene
def build_scene(cv, body, slot, item, vw, p):
    """Draw base body + (optionally) one equipped item, tagged."""
    def add(z, tag, fn):
        draw.append((z, tag, fn))
    draw = []

    hr, hl = p["handR"], p["handL"]

    if slot == "back":
        add(vw.z(0, -3.2), TAG_ITEM, lambda: PT.back_draw(cv, vw, p, item))

    # A robe is a single object at one depth, but each leg's depth swings with
    # the stride and the facing, so a striding leg out-depths the robe and cuts
    # a hole in its layer. The robe's silhouette covers the legs from every
    # angle anyway, so omit them: the layer then paints over the bare legs the
    # body sheet drew, which is what "wearing a robe" means.
    robe = slot == "legs" and item["style"] == "robe"

    for side in (1, -1):
        z = PT.leg_z(vw, p, side)
        if not robe:
            add(z, TAG_BODY, (lambda s=side: PT.body_leg(cv, vw, p, body, s)))
        if slot == "legs" and item["style"] == "pants":
            add(z + 0.001, TAG_ITEM, (lambda s=side: PT.legs_leg(cv, vw, p, item, s)))
        if slot == "boots":
            add(z + 0.002, TAG_ITEM, (lambda s=side: PT.boots_foot(cv, vw, p, item, s)))

    if slot == "legs" and item["style"] == "robe":
        # after the bare legs so it hides them, before the torso
        add(0.004, TAG_ITEM, lambda: PT.legs_robe(cv, vw, p, item))

    add(0.0, TAG_BODY, lambda: PT.body_torso(cv, vw, p, body))
    if slot == "chest":
        add(0.001, TAG_ITEM, lambda: PT.chest_torso(cv, vw, p, item))

    for side in (1, -1):
        z = PT.arm_z(vw, p, side) + 0.02
        add(z, TAG_BODY, (lambda s=side: PT.body_arm(cv, vw, p, body, s)))
        if slot == "chest":
            add(z + 0.001, TAG_ITEM, (lambda s=side: PT.chest_arm(cv, vw, p, item, s)))
        if slot == "gloves":
            add(z + 0.002, TAG_ITEM, (lambda s=side: PT.gloves_hand(cv, vw, p, item, s)))

    if slot == "offhand":
        add(vw.z(hl[0], hl[1] + 1.6) + 0.05, TAG_ITEM,
            lambda: PT.offhand_draw(cv, vw, p, item))

    weapon_front = False
    if slot == "mainhand":
        anchor = hl if item["style"] == "bow" else hr
        wz = vw.z(anchor[0], anchor[1] + 2.0)
        weapon_front = wz > -0.15
        if not weapon_front:
            add(wz, TAG_ITEM, lambda: PT.mainhand_draw(cv, vw, p, item))

    draw.sort(key=lambda t: t[0])
    for _, tag, fn in draw:
        cv.tag = tag
        fn()

    cv.tag = TAG_BODY
    PT.body_head(cv, vw, p, body)
    if slot == "helm":
        cv.tag = TAG_ITEM
        PT.helm_draw(cv, vw, p, item, body)

    if slot == "mainhand":
        cv.tag = TAG_ITEM
        if weapon_front:
            PT.mainhand_draw(cv, vw, p, item)
        if item["style"] in ("sword", "greatsword", "mace"):
            PT.draw_slash(cv, vw, p, item.get("glow") or (250, 246, 230))
        elif item["style"] == "staff":
            PT.draw_cast_fx(cv, vw, p, item.get("glow"))


# ------------------------------------------------------------------ downsample
def crunch(rgba):
    """Box-downsample a supersampled frame, then snap back to its exact palette."""
    a = rgba.astype(np.float32)
    alpha = a[..., 3:4] / 255.0
    pm = np.concatenate([a[..., :3] * alpha, a[..., 3:4]], axis=2)
    n = OUT_CELL
    blk = (CELL * SS) // n
    pm = pm.reshape(n, blk, n, blk, 4).mean(axis=(1, 3))
    al = pm[..., 3]
    out = np.zeros((n, n, 4), np.uint8)
    keep = al >= 100
    if not keep.any():
        return out

    rgb = np.zeros((n, n, 3), np.float32)
    m = al > 1.0
    rgb[m] = pm[..., :3][m] / (al[m][:, None] / 255.0)

    opaque = rgba[..., 3] > 0
    pal = np.unique(rgba[..., :3][opaque].reshape(-1, 3), axis=0).astype(np.float32)
    flat = rgb[keep]
    d = ((flat[:, None, :] - pal[None, :, :]) ** 2).sum(2)
    out[..., :3][keep] = pal[d.argmin(1)].astype(np.uint8)
    out[..., 3][keep] = 255
    return out


def layer_frame(body, slot, item, vw, p, want_tag):
    cv = Canvas()
    build_scene(cv, body, slot, item, vw, p)
    src = np.asarray(cv.img)
    tags = np.asarray(cv.tag_img)
    masked = np.where((tags == want_tag)[..., None], src, 0).astype(np.uint8)
    return crunch(masked)


# ------------------------------------------------------------------ sheets
def build_sheet(kind, spec):
    """kind: 'body' | 'item'."""
    if kind == "body":
        body, slot, item, tag = spec, None, None, TAG_BODY
    else:
        body, slot, item, tag = REF_BODY, spec["slot"], spec, TAG_ITEM

    cols, rows = FRAMES_PER_DIR, len(DIRECTIONS)
    sheet = Image.new("RGBA", (cols * OUT_CELL, rows * OUT_CELL), (0, 0, 0, 0))
    frames = {}
    for r, (dname, fx, fy) in enumerate(DIRECTIONS):
        vw = View(fx, fy)
        for c, (anim, idx, pose) in enumerate(all_frames()):
            arr = layer_frame(body, slot, item, vw, pose, tag)
            sheet.paste(Image.fromarray(arr, "RGBA"), (c * OUT_CELL, r * OUT_CELL))
            frames[f"{anim}_{dname}_{idx:02d}"] = dict(
                frame=dict(x=c * OUT_CELL, y=r * OUT_CELL, w=OUT_CELL, h=OUT_CELL),
                rotated=False, trimmed=False,
                spriteSourceSize=dict(x=0, y=0, w=OUT_CELL, h=OUT_CELL),
                sourceSize=dict(w=OUT_CELL, h=OUT_CELL))
    return sheet, frames


def quantize(png_path):
    im = Image.open(png_path).convert("RGBA")
    a = np.asarray(im)
    flat = a.reshape(-1, 4).copy()
    flat[flat[:, 3] < 128] = [0, 0, 0, 0]
    cols, inv = np.unique(flat, axis=0, return_inverse=True)
    if len(cols) > 256:
        im.save(png_path, optimize=True)
        return
    ti = int(np.where((cols == [0, 0, 0, 0]).all(1))[0][0])
    order = np.concatenate([[ti], np.delete(np.arange(len(cols)), ti)])
    remap = np.zeros(len(cols), np.uint8)
    remap[order] = np.arange(len(cols), dtype=np.uint8)
    out = Image.fromarray(remap[inv].reshape(a.shape[:2]), "P")
    pal = cols[order][:, :3].astype(np.uint8).flatten().tolist()
    out.putpalette(pal + [0] * (3 * (256 - len(cols))))
    out.save(png_path, optimize=True, transparency=0)


def write_icon(sheet, item_id):
    """32x32 inventory icon: the idle frame in which the item shows the most
    pixels, cropped to its bounding box and centred. No scaling unless it has
    to, so the pixels stay exactly as drawn."""
    a = np.asarray(sheet.convert("RGBA"))
    best, best_n = None, 0
    for row in range(len(DIRECTIONS)):
        for col in (0, 1):                      # the two idle frames
            sub = a[row * OUT_CELL:(row + 1) * OUT_CELL,
                    col * OUT_CELL:(col + 1) * OUT_CELL]
            n = int((sub[..., 3] > 0).sum())
            if n > best_n:
                best, best_n = sub, n
    if best is None or best_n == 0:
        return None
    ys, xs = np.nonzero(best[..., 3])
    crop = Image.fromarray(best[ys.min():ys.max() + 1, xs.min():xs.max() + 1], "RGBA")
    if crop.width > ICON_SIZE or crop.height > ICON_SIZE:
        f = min(ICON_SIZE / crop.width, ICON_SIZE / crop.height)
        crop = crop.resize((max(1, int(crop.width * f)), max(1, int(crop.height * f))),
                           Image.NEAREST)
    else:
        # integer upscale so small items (gloves, boots, rings) fill the icon
        f = min(ICON_SIZE // crop.width, ICON_SIZE // crop.height, 3)
        if f > 1:
            crop = crop.resize((crop.width * f, crop.height * f), Image.NEAREST)
    icon = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), (0, 0, 0, 0))
    icon.paste(crop, ((ICON_SIZE - crop.width) // 2, (ICON_SIZE - crop.height) // 2))
    path = os.path.join(ICON_OUT, item_id + ".png")
    icon.save(path)
    quantize(path)
    return item_id + ".png"


def one(job):
    kind, spec = job
    sheet, frames = build_sheet(kind, spec)
    sub = "bodies" if kind == "body" else "items"
    png = os.path.join(OUT, sub, spec["id"] + ".png")
    if kind == "item":
        write_icon(sheet, spec["id"])
    sheet.save(png)
    quantize(png)
    atlas = dict(frames=frames,
                 meta=dict(app="valhalla-spritegen", version="2.0",
                           image=spec["id"] + ".png", format="RGBA8888",
                           size=dict(w=sheet.width, h=sheet.height), scale="1"))
    with open(os.path.join(OUT, sub, spec["id"] + ".json"), "w") as f:
        json.dump(atlas, f, separators=(",", ":"))
    return spec["id"], os.path.getsize(png)


# ------------------------------------------------------------------ layer order
def layer_order():
    """Per-direction slot draw order (back moves in front when facing away)."""
    order = {}
    for dname, fx, fy in DIRECTIONS:
        if fy >= 0.1:
            o = ["back", "body", "legs", "boots", "chest", "gloves", "helm",
                 "offhand", "mainhand"]
        else:
            o = ["body", "legs", "boots", "chest", "gloves", "back", "helm",
                 "offhand", "mainhand"]
        order[dname] = o
    return order


def main():
    for sub in ("bodies", "items", "presets"):
        os.makedirs(os.path.join(OUT, sub), exist_ok=True)
    os.makedirs(ICON_OUT, exist_ok=True)

    only = sys.argv[1] if len(sys.argv) > 1 else None
    jobs = [("body", b) for b in BODIES if not only or only in b["id"]]
    jobs += [("item", i) for i in ITEMS if not only or only in i["id"]]
    print(f"{len(jobs)} layers x {FRAMES_PER_DIR * len(DIRECTIONS)} frames")

    with Pool(os.cpu_count()) as pool:
        for n, (sid, size) in enumerate(pool.imap_unordered(one, jobs), 1):
            print(f"  [{n}/{len(jobs)}] {sid}  {size/1024:.0f}KB", flush=True)

    manifest = dict(
        version=2,
        cell=OUT_CELL,
        # feet sit at GROUND_Y in the authoring space; the client uses this as
        # the sprite origin so the character stands on the tile it occupies
        originX=0.5,
        originY=round(44.0 / 48.0, 4),
        directions=[d[0] for d in DIRECTIONS],
        animations=[dict(name=a, start=sum(n for _, n in ANIMS[:i]), frames=n)
                    for i, (a, n) in enumerate(ANIMS)],
        slots=SLOTS,
        layerOrder=layer_order(),
        bodies=[dict(id=b["id"], label=b["label"], sheet=f"bodies/{b['id']}.png",
                     atlas=f"bodies/{b['id']}.json") for b in BODIES],
        items=[dict(id=i["id"], slot=i["slot"], label=i["label"],
                    style=i.get("style"), sheet=f"items/{i['id']}.png",
                    atlas=f"items/{i['id']}.json", icon=f"{i['id']}.png")
               for i in ITEMS],
        presets=[dict(id=p["id"], label=p["label"], cls=p["cls"], tier=p["tier"],
                      body=p["body"], slots=p["slots"]) for p in PRESETS],
    )
    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("manifest.json written")


if __name__ == "__main__":
    main()
