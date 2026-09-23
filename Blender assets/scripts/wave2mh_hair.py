"""B-15 Wave 2 (MetaHuman rework): hair (A-002 SK_Hair_Blonde, A-003
SK_Hair_Brown_Short) re-fitted to the MetaHuman head.

The clump designs are ``wave2_hair.py``'s, unchanged: this module runs its
``brown_short`` / ``blonde_long`` with the cranium ellipsoid, hairline and
clump sizes swapped for the MetaHuman's (``HEAD_C``, ``HEAD_R``,
``_hairline_z``; widths and lifts x ``K``), then pushes anything under the
skin out over ``MH_Fit`` and skins it with the MetaHuman's weights (head,
plus neck/spine for the long strands).

A-004 SK_Hood_Bald_Cap is not rebuilt: it was the scalp a hood sat on over the
Fable body's hair-less skull mesh; the MetaHuman head is its own scalp.

Run in Blender after ``wave2mh_base.prep()``: exec, then ``build_all()``.
Output: ``Import/Characters/MetaHuman/Hair/SK_<name>.fbx``.
"""

import importlib.util
import math
import os
import sys

import bpy
import mathutils

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\music\game-project\Valhalla2.0\Blender assets\scripts"


def _load(name, fresh=False):
    if name in sys.modules and not fresh:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, os.path.join(_here, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if not fresh:
        sys.modules[name] = mod
    return mod


mb = _load("wave2mh_base")
wh = _load("wave2_hair", fresh=True)        # a private copy: its globals are re-pointed below

# MetaHuman cranium (measured on MH_Face: width +-0.095 at 1.70, forehead
# -0.128, back +0.073, crown 1.802).
wh.HEAD_C = mathutils.Vector((0.0, -0.027, 1.690))
wh.HEAD_R = mathutils.Vector((0.097, 0.103, 0.114))
K = 0.52                                    # clump size, Fable head -> MetaHuman head


def _hairline_z(phi):
    """Forehead 1.762, above the ears 1.700 (dips to 1.68), nape 1.605."""
    f = (math.sin(phi) + 1.0) / 2.0
    side = 1.0 - abs(math.cos(phi))
    z = 1.762 * (1 - f) + 1.605 * f
    return z - 0.022 * (1 - side) * (1 - f)


wh._hairline_z = _hairline_z


class _Builder(wh.HairBuilder):
    def clump(self, phi0, theta0, length_deg, width, thick=0.55, sweep=0.0, lift=0.006, tip_lift=0.012,
              drop_to=None, segs=6, rings=6, shade=1.0):
        if drop_to is not None:
            drop_to = mb.Z(drop_to)
        return super().clump(phi0, theta0, length_deg, width * K, thick=thick, sweep=sweep, lift=lift * K,
                             tip_lift=tip_lift * K, drop_to=drop_to, segs=segs, rings=rings, shade=shade)

    def cap(self, lift=0.004, margin_deg=4.0, segs=36, rings=14, shade=0.85, z_min=None):
        return super().cap(lift=lift * K, margin_deg=margin_deg, segs=segs, rings=rings, shade=shade,
                           z_min=None if z_min is None else mb.Z(z_min))


wh.HairBuilder = _Builder


def fit_out(ob, offset=0.003):
    """Push anything the ellipsoid left under the skin out to ``offset``;
    keep how high clumps were meant to sit above the ideal cranium."""
    fit = bpy.data.objects[mb.FIT]
    bvh = mathutils.bvhtree.BVHTree.FromPolygons([v.co for v in fit.data.vertices],
                                                 [p.vertices for p in fit.data.polygons])
    C, R = wh.HEAD_C, wh.HEAD_R
    for v in ob.data.vertices:
        loc, nrm, _, _ = bvh.find_nearest(v.co)
        if loc is None:
            continue
        rel = v.co - C
        rho = math.sqrt((rel.x / R.x) ** 2 + (rel.y / R.y) ** 2 + (rel.z / R.z) ** 2)
        want = max(offset, (rho - 1.0) * 0.1) if v.co.z > mb.Z(0.80) else offset
        if (v.co - loc).dot(nrm) < want:
            v.co = loc + nrm * want
    ob.data.update()
    return ob


def build(names=("SK_Hair_Brown_Short", "SK_Hair_Blonde")):
    out = []
    makers = {"SK_Hair_Brown_Short": wh.brown_short, "SK_Hair_Blonde": wh.blonde_long}
    for n in names:
        if n in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[n])
        ob = makers[n]()
        fit_out(ob)
        mb.transfer_weights(ob, smooth=2 if n == "SK_Hair_Blonde" else 0)
        out.append(ob)
    return out


def build_all(do_export=True, save_blend=True):
    obs = build()
    if do_export:
        for ob in obs:
            mb.export_fbx(ob, mb.HAIR_OUT, colors="LINEAR")
    if save_blend:
        mb.save()
    return [dict(name=o.name, tris=sum(len(p.vertices) - 2 for p in o.data.polygons), groups=len(o.vertex_groups))
            for o in obs]
