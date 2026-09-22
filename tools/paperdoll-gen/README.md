# Paperdoll sprite generator

Generates the layered character sheets in `public/assets/sprites/paperdoll/`
and the inventory icons in `public/assets/sprites/icons/`.

Requires Python 3 with `pillow` and `numpy`.

    cd tools/paperdoll-gen
    ./generate.sh              # everything (~40s)
    ./generate.sh chest_       # only ids matching a substring

Key knobs:

* `items.py`   — the catalogue. A new item is one dict: `id`, `slot`, `label`,
                 colours (`main`, `alt`, `cloth`, `trim`, `belt`, `metal`,
                 `wood`, `glow`) and, for some slots, a `style`.
* `parts.py`   — the shape of each slot. Add a helm `style` here and every helm
                 item can use it.
* `poses.py`   — the five animation cycles. A new cycle is one pose function
                 plus an entry in `ANIMS`; every layer grows the same columns.
* `rig.py`     — 3D->2D projection. `DEPTH = 0.50` matches the game's
                 `orthoToIso()` exactly; do not change it without changing that.
* `render.py`  — scene assembly, depth order, and the tag buffer that isolates
                 each item layer with occlusion pre-baked against the body.

`VALHALLA_CELL` sets the output cell size. It must divide the 192px supersampled
canvas: 48, 64 (default) or 96. Changing it means re-checking the sprite origin
and the UI offsets anchored to it.

## Invariants worth knowing

**Robes omit the body legs.** A `legs` item with `style: "robe"` is one object at
a single depth, but each leg's depth swings with the stride and the facing. A
striding leg out-depths the robe, wins the tag contest, and cuts a hole in the
robe's layer — bare legs then show through the skirt in-game. So the robe scene
draws no body legs at all; the robe's silhouette covers them from every angle,
and the layer simply paints over what the body sheet drew. Pants are unaffected:
each armoured leg is drawn just above *its own* body leg, so their relative
order is preserved.

**Feet deliberately fall below the hem.** The robe polygon stops at `FOOT_V+0.5`
while the foot sits at `FOOT_V`, so boots read below the skirt.
