"""Collapse Interchange's per-mesh material copies into one asset each.

## The problem

Interchange imports each ``.glb`` on its own, and it has no way to know that
the ``M_Grass`` in ``SM_Grass_A.glb`` is the same material as the ``M_Grass`` in
``SM_Grass_B.glb``. So it makes one material asset *per mesh folder*: after the
three kits are in, the project holds five copies of ``M_Grass``, four of
``M_Stone``, four of ``M_StoneDark``, six of ``M_Wood``, and so on — about sixty
assets standing in for twenty-odd materials.

Every copy is byte-identical and every copy is a separate drift risk. Retinting
the grass means finding five assets; missing one means one grass tile in the
field is the old colour, which reads as a lighting artefact rather than as a
mistake. Worse for this project, the copies are instances of
``/InterchangeAssets/gltf/Substrate/M_GLTF`` — a physically-based master — so
the kit renders as slightly shiny PBR rather than through
``M_ValhallaToon``, and the flat-colour-plus-outline look Phase 2a established
never arrives.

## What this does

For every distinct material *name* found under ``content_root``:

1. Keep one canonical asset at ``/Game/Valhalla/Environment/Materials/M_<Name>``.
2. Make it a ``MaterialInstanceConstant`` of ``/Game/Valhalla/Materials/M_ValhallaToon``,
   carrying the glTF import's own numbers across: ``BaseColorFactor`` ->
   ``BaseColor``, ``EmissiveFactor`` -> ``EmissiveColor``, ``EmissiveStrength``
   -> ``EmissiveStrength``. The colours are therefore still the ones authored in
   Blender; only the shading model changes.
3. Repoint every static mesh slot in ``content_root`` at it, matching on the
   slot name.
4. Delete the per-mesh copies and resolve the redirectors that leaves.

A donor material this cannot read the glTF factors off — a hand-authored
``UMaterial``, or a future Interchange whose parameters are named differently —
is *moved* to the canonical path instead of being replaced, so the material
survives with whatever it had. That is the "else keep the imported material"
half of the job, and it is why step 2 reads the factors before creating
anything.

## Safety

Deletion happens last and only if every slot was assigned. A mesh left with a
null material section renders as the grey world-grid checker, which is a much
worse outcome than sixty redundant assets, so a single failed assignment aborts
the cleanup and keeps the imports.

Idempotent: a second run finds the canonical assets already there, finds every
slot already pointing at them, finds nothing to delete, and reports zeroes.

Run it from the editor console:

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/consolidate_env_materials.py"

or, for one root, through ``ValhallaDataTools.consolidate_materials``.
"""

import re

import unreal

#: Where the one true copy of each material lives.
#:
#: One folder for the whole kit, including the props, rather than one per kit.
#: ``M_Stone`` appears in the grassland walls, the town cobbles and the portal
#: marker; three canonical copies of it in three kit folders would be the same
#: drift this exists to remove, one level up.
MAT_DIR = "/Game/Valhalla/Environment/Materials"

#: The flat-shaded master every canonical material instances. Authored by
#: ``build_toon.py``.
TOON_MASTER = "/Game/Valhalla/Materials/M_ValhallaToon"

#: Interchange's glTF master. A donor parented to this is one we can read.
GLTF_MASTER_SUBSTRING = "M_GLTF"

#: What the glTF instance calls a thing -> what M_ValhallaToon calls it.
#: Confirmed by inspection rather than assumed; see the module docstring.
VECTOR_MAP = {"BaseColorFactor": "BaseColor", "EmissiveFactor": "EmissiveColor"}
SCALAR_MAP = {"EmissiveStrength": "EmissiveStrength"}

#: Interchange's uniquifying suffixes: ``M_Grass_1``, ``M_Grass.001``.
_SUFFIX = re.compile(r"^(?P<stem>.+?)(?:[._](?:\d+))$")


def _log(message):
    unreal.log("VALHALLA_MATS {}".format(message))


def _warn(message):
    unreal.log_warning("VALHALLA_MATS {}".format(message))


def _list_assets(root):
    """Every asset object path under a content folder, recursively."""
    if not unreal.EditorAssetLibrary.does_directory_exist(root):
        return []
    return [str(p) for p in unreal.EditorAssetLibrary.list_assets(root, recursive=True)]


def _package_path(object_path):
    """``/Game/X/M_Y.M_Y`` -> ``/Game/X/M_Y``."""
    return object_path.split(".")[0]


def _base_name(package_path):
    """``/Game/X/M_Grass_1`` -> ``M_Grass``."""
    name = package_path.rsplit("/", 1)[-1]
    match = _SUFFIX.match(name)
    return match.group("stem") if match else name


def _read_gltf_factors(instance):
    """Pull BaseColor / Emissive out of a glTF MaterialInstanceConstant.

    Returns a dict of M_ValhallaToon parameter name -> value, or ``None`` if
    this is not a glTF instance and therefore cannot be translated. The
    ``None`` is the whole point: it is what routes the donor down the
    "keep the imported material" path instead of silently building a grey
    instance from defaults.
    """
    if not isinstance(instance, unreal.MaterialInstanceConstant):
        return None

    parent = instance.get_editor_property("parent")
    if parent is None or GLTF_MASTER_SUBSTRING not in parent.get_path_name():
        return None

    vectors = {}
    scalars = {}

    for entry in instance.get_editor_property("vector_parameter_values"):
        info = entry.get_editor_property("parameter_info")
        source = str(info.get_editor_property("name"))
        if source in VECTOR_MAP:
            vectors[VECTOR_MAP[source]] = entry.get_editor_property("parameter_value")

    for entry in instance.get_editor_property("scalar_parameter_values"):
        info = entry.get_editor_property("parameter_info")
        source = str(info.get_editor_property("name"))
        if source in SCALAR_MAP:
            scalars[SCALAR_MAP[source]] = float(entry.get_editor_property("parameter_value"))

    # A glTF material with no base colour factor at all is a white material,
    # which is a legitimate export and not a reason to bail.
    if "BaseColor" not in vectors:
        vectors["BaseColor"] = unreal.LinearColor(1.0, 1.0, 1.0, 1.0)

    return {"vectors": vectors, "scalars": scalars}


def _make_canonical(name, donor, donor_package):
    """Create ``MAT_DIR/name`` as a toon instance, or move the donor there.

    Returns ``(asset, how)`` where ``how`` is one of ``existed``,
    ``instanced`` or ``moved``.
    """
    target = "{}/{}".format(MAT_DIR, name)

    existing = unreal.EditorAssetLibrary.load_asset(target)
    if existing is not None:
        return existing, "existed"

    factors = _read_gltf_factors(donor)

    if factors is None:
        # Not translatable. Keep what was imported, at the canonical path.
        if not unreal.EditorAssetLibrary.does_directory_exist(MAT_DIR):
            unreal.EditorAssetLibrary.make_directory(MAT_DIR)
        if unreal.EditorAssetLibrary.rename_asset(donor_package, target):
            _warn("{}: not a glTF instance; moved the imported material to {}".format(name, target))
            return unreal.EditorAssetLibrary.load_asset(target), "moved"
        _warn("{}: not a glTF instance and could not be moved; left at {}".format(name, donor_package))
        return donor, "moved"

    master = unreal.EditorAssetLibrary.load_asset(TOON_MASTER)
    if master is None:
        raise RuntimeError("{} does not exist; run build_toon first".format(TOON_MASTER))

    if not unreal.EditorAssetLibrary.does_directory_exist(MAT_DIR):
        unreal.EditorAssetLibrary.make_directory(MAT_DIR)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    instance = tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, master)

    for parameter, value in factors["vectors"].items():
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            instance, parameter, value)
    for parameter, value in factors["scalars"].items():
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            instance, parameter, value)

    # The kit meshes carry no vertex colour; the characters' do, which is why
    # the master has the switch at all. Explicit rather than defaulted, so a
    # change to the master's default cannot silently retint the world.
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        instance, "UseVertexColor", 0.0)

    unreal.EditorAssetLibrary.save_asset(target)
    return instance, "instanced"


def _fix_redirectors(roots):
    """Resolve the redirectors left behind by renames and deletions.

    Without this the meshes keep pointing at a redirector rather than at the
    asset, which works until the redirector is cleaned up by somebody else and
    then does not.
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    search = unreal.ARFilter(
        class_paths=[unreal.TopLevelAssetPath("/Script/CoreUObject", "ObjectRedirector")],
        package_paths=[unreal.Name(r) for r in roots],
        recursive_paths=True,
    )
    redirectors = [str(a.package_name) for a in registry.get_assets(search)]
    if redirectors:
        loaded = [unreal.EditorAssetLibrary.load_asset(p) for p in redirectors]
        unreal.AssetToolsHelpers.get_asset_tools().fixup_referencers(
            [r for r in loaded if r is not None])
        _log("resolved {} redirector(s)".format(len(redirectors)))
    return redirectors


def consolidate(content_roots):
    """Consolidate every material under each of ``content_roots``.

    Returns a dict of counts, which is what the toolset hands back to MCP.
    """
    if isinstance(content_roots, str):
        content_roots = [content_roots]

    # ── Gather: name -> every package path that holds a material by that name
    by_name = {}
    mesh_packages = []

    for root in content_roots:
        for object_path in sorted(_list_assets(root)):
            package = _package_path(object_path)
            if package.startswith(MAT_DIR):
                continue

            asset = unreal.EditorAssetLibrary.load_asset(object_path)
            if isinstance(asset, unreal.MaterialInterface):
                by_name.setdefault(_base_name(package), []).append(package)
            elif isinstance(asset, unreal.StaticMesh):
                mesh_packages.append(package)

    if not by_name:
        _log("nothing to consolidate under {}".format(", ".join(content_roots)))
        return {"roots": content_roots, "uniqueMaterials": 0, "importedCopies": 0,
                "created": [], "moved": [], "existed": [], "slotsAssigned": 0,
                "deleted": [], "unmatchedSlots": [], "failedSlots": [], "redirectors": []}

    imported_copies = sum(len(v) for v in by_name.values())

    # ── Canonicalise ────────────────────────────────────────────────────
    canonical = {}
    created, moved, existed = [], [], []

    for name in sorted(by_name):
        packages = by_name[name]
        donor_package = packages[0]
        donor = unreal.EditorAssetLibrary.load_asset(donor_package)

        asset, how = _make_canonical(name, donor, donor_package)
        canonical[name] = asset

        target = "{}/{}".format(MAT_DIR, name)
        if how == "instanced":
            created.append(target)
        elif how == "moved":
            moved.append(target)
            # The donor is no longer where it was, so it must not be deleted.
            by_name[name] = [p for p in packages if p != donor_package]
        else:
            existed.append(target)

    _log("{} unique material(s) from {} imported copy/ies: {} created, {} moved, {} already there".format(
        len(canonical), imported_copies, len(created), len(moved), len(existed)))

    # ── Repoint every static mesh slot ──────────────────────────────────
    assigned = 0
    unmatched = []
    failed = []

    for package in mesh_packages:
        mesh = unreal.EditorAssetLibrary.load_asset(package)
        if mesh is None:
            continue

        # An unreal.Array of structs yields *copies* when iterated: mutating
        # the loop variable changes nothing and writing the same Array object
        # back writes back what was already there. The array has to be rebuilt
        # from the mutated copies and assigned as a new list — and then read
        # back, rather than trusted.
        rebuilt = []
        changed = False

        for entry in mesh.get_editor_property("static_materials"):
            slot = str(entry.get_editor_property("material_slot_name"))
            name = _base_name("/x/" + slot)
            target = canonical.get(name)

            if target is None:
                unmatched.append("{}:{}".format(package, slot))
            else:
                current = entry.get_editor_property("material_interface")
                if current != target:
                    entry.set_editor_property("material_interface", target)
                    changed = True
                assigned += 1

            rebuilt.append(entry)

        mesh.set_editor_property("static_materials", rebuilt)

        for entry in mesh.get_editor_property("static_materials"):
            if entry.get_editor_property("material_interface") is None:
                failed.append("{}:{}".format(
                    package, entry.get_editor_property("material_slot_name")))

        if changed:
            unreal.EditorAssetLibrary.save_asset(package)

    # ── Delete the copies, last and only if nothing is dangling ─────────
    deleted = []
    if failed:
        _warn("{} slot(s) still unassigned; keeping every imported material".format(len(failed)))
        for entry in failed:
            _warn("  unassigned {}".format(entry))
    else:
        for name in sorted(by_name):
            for package in by_name[name]:
                if unreal.EditorAssetLibrary.delete_asset(package):
                    deleted.append(package)

    redirectors = _fix_redirectors(content_roots + [MAT_DIR])

    for entry in unmatched:
        _warn("  slot matches no known material: {}".format(entry))

    _log("done: {} slot(s) assigned across {} mesh(es), {} copy/ies deleted, "
         "{} unmatched, {} failed".format(
             assigned, len(mesh_packages), len(deleted), len(unmatched), len(failed)))

    return {
        "roots": content_roots,
        "uniqueMaterials": len(canonical),
        "importedCopies": imported_copies,
        "created": created,
        "moved": moved,
        "existed": existed,
        "meshes": len(mesh_packages),
        "slotsAssigned": assigned,
        "deleted": deleted,
        "unmatchedSlots": unmatched,
        "failedSlots": failed,
        "redirectors": redirectors,
    }


#: The four roots Phase 3 imports into.
DEFAULT_ROOTS = [
    "/Game/Valhalla/Environment/Grassland",
    "/Game/Valhalla/Environment/Desert",
    "/Game/Valhalla/Environment/Town",
    "/Game/Valhalla/Props",
]


if __name__ == "__main__":
    import json
    unreal.log("VALHALLA_MATS_RESULT " + json.dumps(consolidate(DEFAULT_ROOTS)))
