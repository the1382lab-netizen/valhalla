"""B-15 Wave 3: fetch a Poly Haven (CC0) texture and pack it as a Valhalla
texture set, in plain Python (urllib, numpy, Pillow), no Blender needed.

    python wave3_polyhaven.py <asset_id> <SetName> [--res 2k] [--out Import/Textures]

Uses Poly Haven's own DirectX normal (``nor_dx``) and packed ``arm``
(R = AO, G = roughness, B = metallic), which is already M_ValhallaPBR's ORM
layout, so nothing is flipped or repacked. Writes ``T_<Set>_BC/N/ORM.png``
and records the source in ``Import/Textures/texture_sets.json`` (the PNGs are
gitignored; this manifest is how they are rebuilt).
"""

import argparse
import io
import json
import os
import urllib.request

from PIL import Image

API = "https://api.polyhaven.com"


def _get(url, tries=4):
    req = urllib.request.Request(url, headers={"User-Agent": "valhalla-b15/1.0"})
    last = None
    for _ in range(tries):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                return r.read()
        except OSError as exc:
            last = exc
    raise SystemExit("{}: download failed: {}".format(url, last))


def pack(asset_id, set_name, out_root, res="2k"):
    files = json.loads(_get("{}/files/{}".format(API, asset_id)))
    info = json.loads(_get("{}/info/{}".format(API, asset_id)))

    def img(key):
        return Image.open(io.BytesIO(_get(files[key][res]["png"]["url"])))

    folder = os.path.join(out_root, set_name)
    os.makedirs(folder, exist_ok=True)
    img("Diffuse").convert("RGB").save(os.path.join(folder, "T_{}_BC.png".format(set_name)))
    img("nor_dx").convert("RGB").save(os.path.join(folder, "T_{}_N.png".format(set_name)))
    img("arm").convert("RGB").save(os.path.join(folder, "T_{}_ORM.png".format(set_name)))
    size = round(max(info.get("dimensions", [1000, 1000])) / 1000.0, 3)
    entry = {"source": "polyhaven", "id": asset_id, "resolution": res, "real_size_m": size,
             "url": "https://polyhaven.com/a/" + asset_id, "license": "CC0",
             "note": "B-15 Wave 3 (wave3_polyhaven.py): nor_dx and arm as shipped."}
    manifest = os.path.join(out_root, "texture_sets.json")
    data = json.load(open(manifest, encoding="utf-8")) if os.path.exists(manifest) else {}
    data[set_name] = entry
    with open(manifest, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
    return folder, entry


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("asset_id")
    ap.add_argument("set_name")
    ap.add_argument("--res", default="2k")
    ap.add_argument("--out", default=os.path.join("Import", "Textures"))
    a = ap.parse_args()
    print(json.dumps(pack(a.asset_id, a.set_name, a.out, a.res)[1], indent=2))
