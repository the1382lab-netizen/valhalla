"""B-15 shared helpers for building kit assets in Blender (Blender 5.x).

Conventions (Docs/ArtBible.md):
- 1 Blender unit = 1 m; the game is in cm, the glTF importer converts.
- Blender Z up. Unreal/glTF: glTF x = Blender x, glTF y = Blender z,
  glTF z = -Blender y. A first-pass glTF bound (gx, gy, gz) is Blender
  (gx, -gz, gy).
- Pivot at ground level, centred on the footprint.
- Material slot names are the Unreal material instance names (``MI_<Set>``);
  ``valhalla_tools.import_kit`` binds slots to them by name after import.
"""

import math
import os

import bmesh
import bpy

REPO = os.environ.get("VALHALLA_REPO", r"C:\Users\music\game-project\Valhalla2.0")
IMPORT = os.path.join(REPO, "Import")

#: Preview colours for materials in Blender only (Unreal uses the MI_ sets).
PREVIEW = {
    "MI_GrassGround": (0.25, 0.35, 0.12), "MI_DirtPath": (0.35, 0.27, 0.18),
    "MI_CobbleFloor": (0.35, 0.34, 0.32), "MI_StoneFloor": (0.4, 0.38, 0.35),
    "MI_WoodPlanks": (0.35, 0.24, 0.15),
}


def reset_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)


def material(name):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            c = PREVIEW.get(name, (0.5, 0.5, 0.5))
            bsdf.inputs["Base Color"].default_value = (c[0], c[1], c[2], 1.0)
    return mat


def mesh_object(name, bm, materials):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    for m in materials:
        me.materials.append(material(m))
    return obj


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def dist_to_segment(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    cx, cy = ax + t * dx, ay + t * dy
    return math.hypot(px - cx, py - cy)


def ground_tile(name, mat_name, top=0.06, size=0.64, res=16, mask=None):
    """A flat ground slab: ``size`` square, ``top`` high, pivot bottom centre.

    The top is a ``res`` x ``res`` grid so ``mask(x, y) -> 0..1`` can paint a
    smooth vertex-colour blend (red channel) for M_ValhallaGroundBlend.
    UVs are planar over the tile (the materials are world-aligned anyway).
    """
    bm = bmesh.new()
    h = size / 2.0
    grid = []
    for j in range(res + 1):
        row = []
        for i in range(res + 1):
            x = -h + size * i / res
            y = -h + size * j / res
            row.append(bm.verts.new((x, y, top)))
        grid.append(row)
    for j in range(res):
        for i in range(res):
            bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
    # Skirt down to z=0 on all four sides (visible at the edge of the world).
    ring = [grid[0][i] for i in range(res + 1)] + [grid[j][res] for j in range(1, res + 1)] + \
           [grid[res][i] for i in range(res - 1, -1, -1)] + [grid[j][0] for j in range(res - 1, 0, -1)]
    low = [bm.verts.new((v.co.x, v.co.y, 0.0)) for v in ring]
    n = len(ring)
    for k in range(n):
        a, b = ring[k], ring[(k + 1) % n]
        la, lb = low[k], low[(k + 1) % n]
        bm.faces.new((b, a, la, lb))
    bm.faces.new(list(reversed(low)))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    uv = bm.loops.layers.uv.new("UVMap")
    col = bm.loops.layers.color.new("Col")
    for f in bm.faces:
        side = abs(f.normal.z) < 0.5
        for loop in f.loops:
            x, y, z = loop.vert.co.x, loop.vert.co.y, loop.vert.co.z
            # Sides get their own UVs (along the edge, and up), or MikkTSpace
            # sees zero-area UV triangles and warns about degenerate tangents.
            loop[uv].uv = ((x + y) / size + 0.5, z / size) if side else (x / size + 0.5, y / size + 0.5)
            r = mask(x, y) if mask else 0.0
            loop[col] = (r, 0.0, 0.0, 1.0)
    return mesh_object(name, bm, [mat_name])


def export_glb(obj, path):
    """Export one object as .glb (Y-up, with vertex colours and materials)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    kwargs = dict(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                  export_apply=True, export_materials="EXPORT", export_normals=True,
                  export_texcoords=True)
    try:
        bpy.ops.export_scene.gltf(export_vertex_color="ACTIVE", **kwargs)
    except TypeError:
        bpy.ops.export_scene.gltf(export_colors=True, **kwargs)
    return path


def tri_count(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


# ── Box-built meshes ─────────────────────────────────────────────────────────

class MeshBuilder:
    """Collects boxes and prisms into one bmesh with per-part material slots.

    Coordinates are Blender metres (Z up). ``finish()`` makes the object and
    gives every face a box-projected UV where 1 UV unit = 1 game metre, so a
    material instance's ``UVScale`` = 1 / (texture repeat in game metres).
    """

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.mats = []
        self.uv_override = {}      # face index -> callable(co) -> (u, v)

    def _mat(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def _tag(self, before, m):
        idx = self._mat(m)
        new = [f for f in self.bm.faces if f not in before]
        for f in new:
            f.material_index = idx
        return new

    def box(self, mn, mx, m, bevel=0.0):
        before = set(self.bm.faces)
        geom = bmesh.ops.create_cube(self.bm, size=1.0)
        verts = geom["verts"]
        c = [(a + b) / 2.0 for a, b in zip(mn, mx)]
        s = [b - a for a, b in zip(mn, mx)]
        for v in verts:
            v.co.x = c[0] + v.co.x * s[0]
            v.co.y = c[1] + v.co.y * s[1]
            v.co.z = c[2] + v.co.z * s[2]
        if bevel > 0.0:
            edges = list({e for v in verts for e in v.link_edges})
            bmesh.ops.bevel(self.bm, geom=edges, offset=bevel, segments=1, affect="EDGES",
                            profile=0.5, clamp_overlap=True)
        return self._tag(before, m)

    def prism_xz(self, pts, y0, y1, m):
        """Polygon ``pts`` [(x, z), ...] (counter-clockwise seen from -Y),
        extruded from y0 to y1. For braces, gables and roof profiles."""
        return self._prism([(p[0], y0, p[1]) for p in pts], (0.0, y1 - y0, 0.0), m)

    def prism_yz(self, pts, x0, x1, m):
        """Polygon ``pts`` [(y, z), ...] extruded along X from x0 to x1."""
        return self._prism([(x0, p[0], p[1]) for p in pts], (x1 - x0, 0.0, 0.0), m)

    def _prism(self, base, offset, m):
        before = set(self.bm.faces)
        vs = [self.bm.verts.new(p) for p in base]
        f = self.bm.faces.new(vs)
        res = bmesh.ops.extrude_face_region(self.bm, geom=[f])
        moved = [e for e in res["geom"] if isinstance(e, bmesh.types.BMVert)]
        bmesh.ops.translate(self.bm, verts=moved, vec=offset)
        return self._tag(before, m)

    def finish(self, uv_fn=None):
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        uv = self.bm.loops.layers.uv.new("UVMap")
        for f in self.bm.faces:
            n = f.normal
            ax = max(range(3), key=lambda i: abs(n[i]))
            for loop in f.loops:
                co = loop.vert.co
                custom = uv_fn(f, co) if uv_fn else None
                if custom is not None:
                    loop[uv].uv = custom
                elif ax == 2:
                    loop[uv].uv = (co.x, co.y)
                elif ax == 0:
                    loop[uv].uv = (co.y, co.z)
                else:
                    loop[uv].uv = (co.x, co.z)
        return mesh_object(self.name, self.bm, self.mats)


def _lathe(self, profile, segs, m, cx=0.0, cy=0.0, cap_bottom=True, cap_top=True, uv_scale=1.0):
    """Revolve ``profile`` [(r, z), ...] (bottom to top) around a vertical axis
    at (cx, cy). UVs: u = arc length at that radius, v = height (game metres)."""
    import math as _m
    before = set(self.bm.faces)
    rings = []
    for r, z in profile:
        ring = []
        for k in range(segs):
            a = 2.0 * _m.pi * k / segs
            ring.append(self.bm.verts.new((cx + r * _m.cos(a), cy + r * _m.sin(a), z)))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(segs):
            k2 = (k + 1) % segs
            self.bm.faces.new((rings[i][k], rings[i][k2], rings[i + 1][k2], rings[i + 1][k]))
    if cap_bottom and profile[0][0] > 1e-4:
        self.bm.faces.new(list(reversed(rings[0])))
    if cap_top and profile[-1][0] > 1e-4:
        self.bm.faces.new(rings[-1])
    new = self._tag(before, m)
    for f in new:
        self.lathe_faces.add(f)
        self.lathe_axis[f] = (cx, cy)
    return new


def _finish_with_lathe(self, uv_fn=None):
    import math as _m
    lathe_faces, axes = self.lathe_faces, self.lathe_axis

    def uv(face, co):
        if face in lathe_faces and abs(face.normal.z) < 0.7:
            cx, cy = axes[face]
            c = face.calc_center_median()
            ac = _m.atan2(c.y - cy, c.x - cx)
            a = _m.atan2(co.y - cy, co.x - cx)
            a = ac + ((a - ac + _m.pi) % (2.0 * _m.pi) - _m.pi)   # no wrap seam inside a face
            r = _m.hypot(co.x - cx, co.y - cy)
            return (a * max(r, 0.05), co.z)
        return uv_fn(face, co) if uv_fn else None
    return MeshBuilder._finish_plain(self, uv_fn=uv)


def _init(self, name):
    MeshBuilder._init_plain(self, name)
    self.lathe_faces = set()
    self.lathe_axis = {}


MeshBuilder._init_plain = MeshBuilder.__init__
MeshBuilder.__init__ = _init
MeshBuilder._finish_plain = MeshBuilder.finish
MeshBuilder.finish = _finish_with_lathe
MeshBuilder.lathe = _lathe


def _prism_xy(self, pts, z0, z1, m):
    """Polygon ``pts`` [(x, y), ...] extruded along Z from z0 to z1 (shields)."""
    return self._prism([(p[0], p[1], z0) for p in pts], (0.0, 0.0, z1 - z0), m)


def _loft(self, sections, m, cap_bottom=True, tip=None):
    """Connect rings of equal point count: ``sections`` = [(z, [(x, y), ...]), ...]
    bottom to top. ``tip`` = (x, y, z) closes the top in a point (blades)."""
    before = set(self.bm.faces)
    rings = [[self.bm.verts.new((x, y, z)) for x, y in pts] for z, pts in sections]
    n = len(rings[0])
    for a, b in zip(rings, rings[1:]):
        for k in range(n):
            k2 = (k + 1) % n
            self.bm.faces.new((a[k], a[k2], b[k2], b[k]))
    if cap_bottom:
        self.bm.faces.new(list(reversed(rings[0])))
    if tip is not None:
        t = self.bm.verts.new(tip)
        top = rings[-1]
        for k in range(n):
            self.bm.faces.new((top[k], top[(k + 1) % n], t))
    else:
        self.bm.faces.new(rings[-1])
    return self._tag(before, m)


def rotate_up_to_y(obj):
    """Built standing up along +Z, turned so +Z becomes +Y (how the first-pass
    weapons point: blade/head towards Blender +Y, grip at the origin)."""
    import mathutils
    obj.data.transform(mathutils.Matrix.Rotation(-math.pi / 2.0, 4, "X"))
    obj.data.update()
    return obj


MeshBuilder.prism_xy = _prism_xy
MeshBuilder.loft = _loft
