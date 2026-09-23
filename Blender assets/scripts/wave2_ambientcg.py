"""B-15 Wave 2: fetch an ambientCG (CC0) material and pack it as a Valhalla
texture set — plain Python (urllib, numpy, Pillow), no Blender needed, so it
can run wherever the download works (the cloud shell; the PC if it has net).

    python wave2_ambientcg.py <AssetId> <SetName> [--res 2K] [--out <Import/Textures>]

writes ``<out>/<SetName>/T_<SetName>_BC.png`` (Color, sRGB),
``T_<SetName>_N.png`` (NormalDX — ambientCG ships DirectX normals, no flip)
and ``T_<SetName>_ORM.png`` (R = AmbientOcclusion or 1, G = Roughness,
B = Metalness or 0), the layout ``valhalla_tools.import_texture_sets`` and
M_ValhallaPBR expect, and prints a manifest entry for
``Import/Textures/texture_sets.json``. Stage B (armour) sets:

    Leather028 -> Leather     brown leather, 1 m
    Chainmail004 -> Chainmail riveted mail, 1 m
    Fabric032 -> Wool        white wool weave, tinted per instance, 1 m
    Metal009 -> IronPlate    brushed, scratched steel, 1 m
"""

import argparse
import io
import json
import os
import sys
import urllib.request
import zipfile

import numpy as np
from PIL import Image


def fetch(asset_id, res="2K", tries=4):
    """The zip, retried: the download proxy resets the odd connection."""
    url = "https://ambientcg.com/get?file={}_{}-PNG.zip".format(asset_id, res)
    req = urllib.request.Request(url, headers={"User-Agent": "valhalla-b15/1.0"})
    last = None
    for _ in range(tries):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                return zipfile.ZipFile(io.BytesIO(r.read())), url
        except (urllib.error.URLError, ConnectionError, OSError) as exc:
            last = exc
    raise SystemExit("{}: download failed: {}".format(url, last))


def _find(zf, suffix):
    for name in zf.namelist():
        if name.lower().endswith("_" + suffix.lower() + ".png"):
            return Image.open(io.BytesIO(zf.read(name)))
    return None


def pack(asset_id, set_name, out_root, res="2K", metallic=None):
    zf, url = fetch(asset_id, res)
    color = _find(zf, "Color")
    normal = _find(zf, "NormalDX")
    rough = _find(zf, "Roughness")
    ao = _find(zf, "AmbientOcclusion")
    metal = _find(zf, "Metalness")
    if color is None or normal is None or rough is None:
        raise SystemExit("{}: zip lacks Color/NormalDX/Roughness: {}".format(asset_id, zf.namelist()))
    folder = os.path.join(out_root, set_name)
    os.makedirs(folder, exist_ok=True)
    color.convert("RGB").save(os.path.join(folder, "T_{}_BC.png".format(set_name)))
    normal.convert("RGB").save(os.path.join(folder, "T_{}_N.png".format(set_name)))
    w, h = rough.size
    r = np.asarray(rough.convert("L"), dtype=np.uint8)
    a = np.asarray(ao.convert("L").resize((w, h)), dtype=np.uint8) if ao is not None else np.full((h, w), 255, np.uint8)
    if metallic is not None:
        m = np.full((h, w), int(round(metallic * 255)), np.uint8)
    elif metal is not None:
        m = np.asarray(metal.convert("L").resize((w, h)), dtype=np.uint8)
    else:
        m = np.zeros((h, w), np.uint8)
    Image.fromarray(np.stack([a, r, m], axis=-1), "RGB").save(os.path.join(folder, "T_{}_ORM.png".format(set_name)))
    maps = "Color, NormalDX, ORM packed from {}+Roughness+{}".format(
        "AmbientOcclusion" if ao is not None else "AO=1",
        "Metalness" if (metal is not None and metallic is None) else "metallic={}".format(metallic or 0))
    entry = {"source": "ambientcg", "id": asset_id, "resolution": res + "-PNG", "real_size_m": 1.0,
             "url": "https://ambientcg.com/view?id=" + asset_id, "license": "CC0", "note": "Maps: " + maps + "."}
    return folder, entry


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("asset_id")
    ap.add_argument("set_name")
    ap.add_argument("--res", default="2K")
    ap.add_argument("--out", default=os.path.join("Import", "Textures"))
    ap.add_argument("--metallic", type=float, default=None)
    a = ap.parse_args()
    folder, entry = pack(a.asset_id, a.set_name, a.out, a.res, a.metallic)
    print(folder)
    print(json.dumps({a.set_name: entry}, indent=2))
