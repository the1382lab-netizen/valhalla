"""B-15: turn a downloaded Poly Haven texture material into an Unreal-ready set.

Run inside Blender (the Blender MCP connector does this). After
``download_polyhaven_asset(<id>, "textures", "2k")`` has imported the material,

    export_texture_set("cobblestone_floor_001", "CobbleFloor")

writes three PNGs into ``Import/Textures/<SetName>/``:

- ``T_<SetName>_BC.png``   base colour, sRGB.
- ``T_<SetName>_N.png``    normal map, **DirectX** convention (green flipped
                           from Poly Haven's OpenGL ``nor_gl``), which is what
                           Unreal expects.
- ``T_<SetName>_ORM.png``  linear: R = ambient occlusion, G = roughness,
                           B = metallic. That is M_ValhallaPBR's ORMMap layout.
                           AO comes from Poly Haven's ``AO`` map when the
                           download has one, otherwise it is derived from the
                           displacement map (recessed = darker). Metallic is 0
                           unless ``metallic`` is given.

The Unreal side (import, compression settings, material instance) is
``valhalla_tools.import_texture_sets`` in the ValhallaTools plugin.
"""

import os

import bpy
import numpy as np

REPO = os.environ.get("VALHALLA_REPO", r"C:\Users\music\game-project\Valhalla2.0")
OUT_ROOT = os.path.join(REPO, "Import", "Textures")


def _find_image(asset_id, *suffixes):
    for img in bpy.data.images:
        name = img.name.lower()
        if not name.startswith(asset_id.lower()):
            continue
        for suffix in suffixes:
            if name.endswith("_" + suffix.lower()):
                return img
    return None


def _pixels(img):
    w, h = img.size
    arr = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(arr)
    return arr.reshape(h, w, 4), w, h


def _save(arr, w, h, path, colorspace):
    name = os.path.splitext(os.path.basename(path))[0]
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    out = bpy.data.images.new(name, width=w, height=h, alpha=False, float_buffer=False)
    out.colorspace_settings.name = colorspace
    out.pixels.foreach_set(arr.astype(np.float32).ravel())
    out.filepath_raw = path
    out.file_format = "PNG"
    out.save()
    bpy.data.images.remove(out)


def _box_blur(channel, radius):
    """Cheap separable box blur, for deriving cavity AO from displacement."""
    if radius < 1:
        return channel
    k = 2 * radius + 1
    padded = np.pad(channel, radius, mode="wrap")
    c = np.cumsum(padded, axis=0)
    c = np.vstack([np.zeros((1, c.shape[1])), c])
    v = (c[k:] - c[:-k]) / k
    c = np.cumsum(v, axis=1)
    c = np.hstack([np.zeros((c.shape[0], 1)), c])
    return (c[:, k:] - c[:, :-k]) / k


def export_texture_set(asset_id, set_name, metallic=0.0, ao_strength=0.6):
    base = _find_image(asset_id, "diffuse", "diff", "col", "color")
    normal = _find_image(asset_id, "nor_gl", "normal")
    rough = _find_image(asset_id, "rough", "roughness")
    ao = _find_image(asset_id, "ao")
    disp = _find_image(asset_id, "displacement", "disp", "height")
    arm = _find_image(asset_id, "arm")
    if base is None or normal is None or (rough is None and arm is None):
        raise RuntimeError("{}: need diffuse, normal and roughness images (have {})".format(
            asset_id, [i.name for i in bpy.data.images if i.name.lower().startswith(asset_id.lower())]))

    folder = os.path.join(OUT_ROOT, set_name)
    os.makedirs(folder, exist_ok=True)

    bc, w, h = _pixels(base)
    _save(bc, w, h, os.path.join(folder, "T_{}_BC.png".format(set_name)), "sRGB")

    n, nw, nh = _pixels(normal)
    n = n.copy()
    n[..., 1] = 1.0 - n[..., 1]          # OpenGL -> DirectX
    _save(n, nw, nh, os.path.join(folder, "T_{}_N.png".format(set_name)), "Non-Color")

    if arm is not None:
        orm, ow, oh = _pixels(arm)
        orm = orm.copy()
        orm[..., 2] = metallic
    else:
        r, ow, oh = _pixels(rough)
        orm = np.ones((oh, ow, 4), dtype=np.float32)
        orm[..., 1] = r[..., 0]
        orm[..., 2] = metallic
        if ao is not None:
            a, _, _ = _pixels(ao)
            orm[..., 0] = a[..., 0]
        elif disp is not None:
            d, dw, dh = _pixels(disp)
            height = d[..., 0]
            # Cavity: how far below its neighbourhood each texel sits.
            cavity = np.clip((_box_blur(height, max(2, dw // 128)) - height) * 4.0, 0.0, 1.0)
            orm[..., 0] = 1.0 - cavity * ao_strength
    _save(orm, ow, oh, os.path.join(folder, "T_{}_ORM.png".format(set_name)), "Non-Color")

    written = sorted(os.listdir(folder))
    print("export_texture_set {} -> {}: {}".format(asset_id, folder, written))
    return folder, written


MANIFEST = os.path.join(OUT_ROOT, "texture_sets.json")


def record_manifest(set_name, asset_id, resolution, real_size_m, note=""):
    """Remember where a set came from, so the PNGs never need to be committed:
    ``Import/Textures/*/*.png`` is git-ignored and any set can be rebuilt by
    re-downloading ``asset_id`` at ``resolution`` and re-running the export."""
    import json
    data = {}
    if os.path.exists(MANIFEST):
        with open(MANIFEST, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    data[set_name] = {"source": "polyhaven", "id": asset_id, "resolution": resolution,
                      "real_size_m": real_size_m, "url": "https://polyhaven.com/a/" + asset_id,
                      "license": "CC0", "note": note}
    os.makedirs(OUT_ROOT, exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
