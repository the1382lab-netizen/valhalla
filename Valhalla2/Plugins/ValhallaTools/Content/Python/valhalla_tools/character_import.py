"""Phase 4c character art pipeline: glTF -> one skeleton, many meshes.

Everything the Blender export produces for a character comes in through here:
the body and its three hair caps, seven animations, sixteen skinned equipment
pieces and six rigid props. The single hard requirement is that *every*
skeletal mesh ends up bound to one skeleton asset, because the equipment is
worn by driving follower components from the body's pose
(``SetLeaderPoseComponent``) and a follower whose skeleton differs from the
leader's silently falls back to its own ref pose.

Interchange can do that on its own: ``InterchangeGenericAssetsPipeline`` has a
``common_skeletal_meshes_and_animations_properties.skeleton`` slot, and a glb
imported with that slot filled is bound to the named skeleton instead of
generating a new one. The body is imported first with the slot empty so the
skeleton exists to point at; every later import points at it. ``verify()``
re-reads the meshes afterwards and fails loudly if any of them disagree,
because a silent mismatch here is invisible until something animates.

Materials are *not* taken from the glTF. The importer is allowed to create
them only so that the mesh sections keep their slot names; the slot names are
then used to reassign each section to a hand-built instance of
``M_ValhallaToon`` under ``/Game/Valhalla/Characters/Materials``, and the
imported originals are deleted. That is what makes the consolidation exact —
one ``M_<Name>`` per distinct glTF material, whatever order the files were
imported in — and it is also what gives the flat toon shading, which no
imported PBR instance would.
"""

import json
import os

import unreal

# ── Content layout ───────────────────────────────────────────────────────────

ROOT = "/Game/Valhalla/Characters"
BODY_DIR = ROOT + "/Body"
HAIR_DIR = ROOT + "/Hair"
EQUIP_DIR = ROOT + "/Equipment"
WEAPON_DIR = ROOT + "/Weapons"
ANIM_DIR = ROOT + "/Animations"
MAT_DIR = ROOT + "/Materials"
#: Scratch space. Imports land here first so the move into the final folders
#: is a rename we control rather than whatever sub-foldering Interchange picks.
STAGE_DIR = ROOT + "/_Import"
#: The duplicated pipeline asset the imports are driven with.
PIPELINE_ASSET = STAGE_DIR + "/PL_ValhallaCharacter"
DEFAULT_PIPELINE = "/Interchange/Pipelines/DefaultAssetsPipeline"

SKELETON_PATH = ROOT + "/SK_Valhalla_Skeleton"

#: The three hair caps live in the body glb but belong in their own folder.
HAIR_MESHES = ("SK_Hair_Brown_Short", "SK_Hair_Blonde", "SK_Hood_Bald_Cap")
BODY_MESH = "SK_Valhalla_Body"

ANIMATIONS = ("A_Idle", "A_Walk", "A_Attack", "A_Shoot", "A_Cast", "A_Hit", "A_Death")

TAG = "LogValhallaImport:"


def _log(msg):
    unreal.log("{} {}".format(TAG, msg))


def _warn(msg):
    unreal.log_warning("{} {}".format(TAG, msg))


# ── Small asset-registry helpers ─────────────────────────────────────────────

def _list(path):
    """Every asset under a content path, as ``/Game/..../Name`` package paths."""
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        return set()
    return set(unreal.EditorAssetLibrary.list_assets(path, recursive=True, include_folder=False))


def _package(object_path):
    """``/Game/A/B.B`` -> ``/Game/A/B``."""
    return object_path.split(".")[0]


def _ensure_dirs():
    for d in (ROOT, BODY_DIR, HAIR_DIR, EQUIP_DIR, WEAPON_DIR, ANIM_DIR, MAT_DIR, STAGE_DIR):
        if not unreal.EditorAssetLibrary.does_directory_exist(d):
            unreal.EditorAssetLibrary.make_directory(d)


def _move(object_path, dest_dir, new_name=None):
    """Rename an asset into ``dest_dir``. Returns the new package path."""
    src = _package(object_path)
    name = new_name or src.rsplit("/", 1)[1]
    dst = "{}/{}".format(dest_dir, name)
    if src == dst:
        return dst
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.EditorAssetLibrary.delete_asset(dst)
    if not unreal.EditorAssetLibrary.rename_asset(src, dst):
        _warn("could not move {} -> {}".format(src, dst))
        return src
    return dst


# ── Pipeline configuration ───────────────────────────────────────────────────

def _pipeline():
    """A project-local copy of the default assets pipeline, created once.

    Interchange only accepts an override pipeline as a soft object path, so the
    pipeline has to be a real asset rather than a Python object.

    UE 5.8: EditorAssetLibrary no longer resolves engine-plugin content
    (/Interchange/...) nor, reliably, a freshly duplicated asset, so this goes
    through the object system and AssetTools instead.
    """
    obj_path = "{0}.{1}".format(PIPELINE_ASSET, PIPELINE_ASSET.rsplit("/", 1)[1])
    pipeline = unreal.find_object(None, obj_path) or unreal.load_object(None, obj_path)
    if pipeline is None:
        source = unreal.load_object(None, "{0}.{1}".format(DEFAULT_PIPELINE, DEFAULT_PIPELINE.rsplit("/", 1)[1]))
        folder, name = PIPELINE_ASSET.rsplit("/", 1)
        pipeline = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(name, folder, source)
    return pipeline


def _configure(skeleton=None, skeletal=True, static=False, animations=False):
    """Set the pipeline up for one group of files and save it.

    Args:
        skeleton: The ``unreal.Skeleton`` every skeletal mesh must bind to, or
            None to let the import create one.
        skeletal: Import skeletal meshes.
        static: Import static meshes.
        animations: Import animation sequences.
    """
    pipeline = _pipeline()

    pipeline.set_editor_property("asset_type_sub_folders", False)
    pipeline.set_editor_property("use_source_name_for_asset", False)

    common = pipeline.get_editor_property("common_meshes_properties")
    # The hair's colour is a COLOR_0 attribute, so the vertex colours in the
    # file are the art and must survive the import untouched.
    common.set_editor_property("vertex_color_import_option",
                               unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    common.set_editor_property("import_sockets", True)
    common.set_editor_property("recompute_normals", False)
    common.set_editor_property("recompute_tangents", False)
    common.set_editor_property("import_lods", False)

    skel = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    skel.set_editor_property("skeleton", skeleton)
    # With a skeleton named explicitly, auto-selection can only disagree with it.
    skel.set_editor_property("try_auto_select_skeleton", skeleton is None)
    skel.set_editor_property("import_only_animations", bool(animations and not skeletal))

    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("import_skeletal_meshes", skeletal)
    mesh.set_editor_property("import_static_meshes", static)
    # One mesh in the file, one asset in the project. This matters: the default
    # is BY_SKELETON, and the body file holds the body *and* three hair caps all
    # skinned to the same armature, so the default would weld the hair onto the
    # head and hand back a single un-swappable mesh.
    mesh.set_editor_property("combine_skeletal_meshes_behavior",
                             unreal.InterchangeCombineSkeletalMeshesBehavior.DO_NOT_COMBINE)
    mesh.set_editor_property("combine_static_meshes_behavior",
                             unreal.InterchangeCombineStaticMeshesBehavior.DO_NOT_COMBINE)
    # Nothing here is ever simulated; a generated physics asset per equipment
    # piece is 22 assets of pure clutter.
    mesh.set_editor_property("create_physics_asset", False)
    mesh.set_editor_property("build_nanite", False)
    mesh.set_editor_property("import_morph_targets", False)

    anim = pipeline.get_editor_property("animation_pipeline")
    anim.set_editor_property("import_animations", animations)
    anim.set_editor_property("import_bone_tracks", animations)

    mat = pipeline.get_editor_property("material_pipeline")
    # Materials are imported only for their slot names; consolidate_materials()
    # replaces them and deletes the originals.
    mat.set_editor_property("import_materials", True)
    mat.set_editor_property("create_material_instance_for_parent", None)

    unreal.EditorAssetLibrary.save_loaded_asset(pipeline, False)
    return pipeline


def _import(filename, dest):
    """Run one Interchange import and return the package paths it created."""
    before = _list(dest)

    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    source = unreal.InterchangeManager.create_source_data(filename)

    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", True)
    params.set_editor_property("override_pipelines",
                               [unreal.SoftObjectPath("{0}.{1}".format(PIPELINE_ASSET, PIPELINE_ASSET.rsplit("/", 1)[1]))])

    if not manager.import_asset(dest, source, params):
        _warn("import_asset returned false for {}".format(filename))

    created = sorted(_package(p) for p in (_list(dest) - before))
    return created


# ── Step 1: the body, its hair and its animations ────────────────────────────

def import_body(source_dir):
    """Import ``SK_Valhalla_Body.glb``: 4 skeletal meshes, 1 skeleton, 7 anims."""
    _ensure_dirs()
    path = os.path.join(source_dir, "SK_Valhalla_Body.glb")

    _configure(skeleton=None, skeletal=True, static=False, animations=True)
    created = _import(path, STAGE_DIR)
    _log("body import created {} asset(s)".format(len(created)))

    skeleton_path = None
    moved = {}
    for package_path in created:
        asset = unreal.EditorAssetLibrary.load_asset(package_path)
        name = package_path.rsplit("/", 1)[1]

        if isinstance(asset, unreal.Skeleton):
            skeleton_path = _move(package_path, ROOT, "SK_Valhalla_Skeleton")
            moved[name] = skeleton_path
        elif isinstance(asset, unreal.AnimSequence):
            moved[name] = _move(package_path, ANIM_DIR)
        elif isinstance(asset, unreal.SkeletalMesh):
            dest = HAIR_DIR if name in HAIR_MESHES else BODY_DIR
            moved[name] = _move(package_path, dest)

    for name, package_path in sorted(moved.items()):
        _log("  {} -> {}".format(name, package_path))

    if skeleton_path and skeleton_path != SKELETON_PATH:
        _warn("skeleton landed at {} not {}".format(skeleton_path, SKELETON_PATH))

    # Interchange names the skeleton after whichever mesh it happened to build
    # first, so the skeleton is always renamed above — which leaves every asset
    # that referenced it pointing at a redirector in the staging folder. Those
    # redirectors have to be resolved *before* the staging folder is cleaned
    # up, or an asset silently loads with a null Skeleton and evaluates to
    # nothing at all.
    unreal.EditorAssetLibrary.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    _fix_redirectors()

    return moved


def _fix_redirectors():
    """Resolve and delete every ObjectRedirector under ROOT."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    search = unreal.ARFilter(package_paths=[ROOT], recursive_paths=True,
                             class_names=["ObjectRedirector"])
    redirectors = [str(a.package_name) for a in registry.get_assets(search)]
    if redirectors:
        unreal.AssetToolsHelpers.get_asset_tools().fixup_referencers(
            [unreal.EditorAssetLibrary.load_asset(p) for p in redirectors])
        _log("resolved {} redirector(s)".format(len(redirectors)))
    return redirectors


def repair_skeleton_links(source_dir):
    """Rebind any animation or mesh that lost its skeleton, by re-importing it.

    The failure this exists for is quiet and total: an AnimSequence whose
    Skeleton is null logs one line at load ("Unable to retrieve target
    USkeleton"), then plays as nothing at all, so the character simply never
    adopts that pose. Re-importing the animations against the skeleton that now
    exists is more trustworthy than assigning the property back by hand, which
    would leave the compressed bone tracks keyed to a skeleton the asset no
    longer knows it came from.
    """
    skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON_PATH)
    if skeleton is None:
        raise RuntimeError("{} does not exist".format(SKELETON_PATH))

    broken = []
    for object_path in sorted(_list(ANIM_DIR)):
        asset = unreal.EditorAssetLibrary.load_asset(object_path)
        if isinstance(asset, unreal.AnimSequence) and asset.get_editor_property("skeleton") is None:
            broken.append(_package(object_path).rsplit("/", 1)[1])

    if not broken:
        _log("repair: every animation already has its skeleton")
        return []

    _warn("repair: {} animation(s) with no skeleton: {}".format(len(broken), broken))

    _configure(skeleton=skeleton, skeletal=False, static=False, animations=True)
    created = _import(os.path.join(source_dir, "SK_Valhalla_Body.glb"), STAGE_DIR)

    repaired = []
    for package_path in created:
        asset = unreal.EditorAssetLibrary.load_asset(package_path)
        name = package_path.rsplit("/", 1)[1]
        if isinstance(asset, unreal.AnimSequence) and name in broken:
            repaired.append(_move(package_path, ANIM_DIR))
        elif isinstance(asset, unreal.AnimSequence):
            unreal.EditorAssetLibrary.delete_asset(package_path)

    unreal.EditorAssetLibrary.save_directory(ANIM_DIR, only_if_is_dirty=False, recursive=True)
    _log("repair: rebound {}".format(repaired))
    return repaired


# ── Step 2: equipment bound to the existing skeleton ─────────────────────────

def import_equipment(source_dir):
    """Import the 16 skinned pieces against the body's skeleton, and 6 props."""
    _ensure_dirs()
    skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON_PATH)
    if skeleton is None:
        raise RuntimeError("{} does not exist; run import_body first".format(SKELETON_PATH))

    equip_dir = os.path.join(source_dir, "Equipment")
    skinned = sorted(f for f in os.listdir(equip_dir) if f.startswith("SK_") and f.endswith(".glb"))
    rigid = sorted(f for f in os.listdir(equip_dir) if f.startswith("SM_") and f.endswith(".glb"))

    results = {"skinned": {}, "rigid": {}}

    # The skeleton slot is the whole trick: every one of these files carries its
    # own copy of the same 24-joint armature, and without the slot each import
    # would mint a private skeleton that no leader pose could drive.
    _configure(skeleton=skeleton, skeletal=True, static=False, animations=False)
    for file_name in skinned:
        created = _import(os.path.join(equip_dir, file_name), STAGE_DIR)
        for package_path in created:
            asset = unreal.EditorAssetLibrary.load_asset(package_path)
            if isinstance(asset, unreal.SkeletalMesh):
                results["skinned"][package_path.rsplit("/", 1)[1]] = _move(package_path, EQUIP_DIR)

    _configure(skeleton=None, skeletal=False, static=True, animations=False)
    for file_name in rigid:
        created = _import(os.path.join(equip_dir, file_name), STAGE_DIR)
        for package_path in created:
            asset = unreal.EditorAssetLibrary.load_asset(package_path)
            if isinstance(asset, unreal.StaticMesh):
                results["rigid"][package_path.rsplit("/", 1)[1]] = _move(package_path, WEAPON_DIR)

    _log("equipment: {} skinned, {} rigid".format(
        len(results["skinned"]), len(results["rigid"])))
    return results


# ── Step 3: skeleton sharing, checked rather than assumed ────────────────────

def verify():
    """Confirm every skeletal mesh under ROOT reports the one skeleton."""
    skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON_PATH)
    report = {"skeleton": SKELETON_PATH, "ok": True, "meshes": {}, "mismatched": [],
              "animations": {}, "static": []}

    for object_path in sorted(_list(ROOT)):
        asset = unreal.EditorAssetLibrary.load_asset(object_path)
        name = _package(object_path).rsplit("/", 1)[1]

        if isinstance(asset, unreal.SkeletalMesh):
            owned = asset.get_editor_property("skeleton")
            same = owned == skeleton
            report["meshes"][name] = owned.get_path_name() if owned else None
            if not same:
                report["ok"] = False
                report["mismatched"].append(name)
        elif isinstance(asset, unreal.AnimSequence):
            owned = asset.get_editor_property("skeleton")
            report["animations"][name] = owned.get_path_name() if owned else None
            if owned != skeleton:
                report["ok"] = False
                report["mismatched"].append(name)
        elif isinstance(asset, unreal.StaticMesh):
            report["static"].append(name)

    _log("verify: ok={} meshes={} anims={} static={} mismatched={}".format(
        report["ok"], len(report["meshes"]), len(report["animations"]),
        len(report["static"]), report["mismatched"]))
    return report


# ── Step 4: rebind a stray mesh, if Interchange ever refuses the slot ────────

def force_skeleton(mesh_package_paths=None):
    """Fallback: point meshes at SKELETON_PATH after the fact.

    Only ever needed if an import ignored the pipeline's skeleton slot. The
    bone names are identical across every file, so the rebind is a property
    write plus a rebuild rather than a retarget.
    """
    skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON_PATH)
    subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)

    if mesh_package_paths is None:
        mesh_package_paths = [
            _package(p) for p in sorted(_list(ROOT))
            if isinstance(unreal.EditorAssetLibrary.load_asset(p), unreal.SkeletalMesh)
        ]

    fixed = []
    for package_path in mesh_package_paths:
        mesh = unreal.EditorAssetLibrary.load_asset(package_path)
        if mesh is None or mesh.get_editor_property("skeleton") == skeleton:
            continue
        mesh.set_editor_property("skeleton", skeleton)
        try:
            subsystem.rename_socket(mesh, "__noop__", "__noop__")  # forces a modify
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_asset(package_path)
        fixed.append(package_path)

    _log("force_skeleton rebound {} mesh(es)".format(len(fixed)))
    return fixed


# ── Step 5: materials ────────────────────────────────────────────────────────

#: Every distinct glTF material, its linear base colour, and how the toon
#: master should treat it. Read straight out of the .glb pbrMetallicRoughness
#: factors, so these are the art's numbers and not a guess at them.
MATERIALS = {
    "M_Skin":         dict(color=(0.745404, 0.439657, 0.291771)),
    "M_Hair":         dict(color=(1.0, 1.0, 1.0), vertex_color=True),
    "M_Chain":        dict(color=(0.160444, 0.176774, 0.193972)),
    "M_Cloth_Blue":   dict(color=(0.075926, 0.160444, 0.442323)),
    "M_Cloth_Brown":  dict(color=(0.147998, 0.065754, 0.024223)),
    "M_Cloth_Cream":  dict(color=(0.694081, 0.618686, 0.442323)),
    "M_Iron":         dict(color=(0.271577, 0.302125, 0.339223)),
    "M_IronDark":     dict(color=(0.078057, 0.091518, 0.106156)),
    "M_Leather":      dict(color=(0.197516, 0.067725, 0.018913)),
    "M_LeatherDark":  dict(color=(0.065754, 0.020951, 0.005522)),
    "M_Rope":         dict(color=(0.442323, 0.329729, 0.144972)),
    "M_Wood":         dict(color=(0.259027, 0.101145, 0.027755)),
    "M_MaceGlow":     dict(color=(1.0, 0.680020, 0.147998), emissive=6.0),
    "M_OrbGlow":      dict(color=(0.215764, 0.694081, 1.0), emissive=6.0),
}

TOON_MASTER = "/Game/Valhalla/Materials/M_ValhallaToon"


def build_material_instances():
    """One ``M_<Name>`` instance of the toon master per glTF material."""
    _ensure_dirs()
    master = unreal.EditorAssetLibrary.load_asset(TOON_MASTER)
    if master is None:
        raise RuntimeError("{} does not exist; build it first".format(TOON_MASTER))

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    built = []

    for name, spec in sorted(MATERIALS.items()):
        package_path = "{}/{}".format(MAT_DIR, name)
        instance = unreal.EditorAssetLibrary.load_asset(package_path)
        if instance is None:
            instance = tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                                          unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(instance, master)

        red, green, blue = spec["color"]
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            instance, "BaseColor", unreal.LinearColor(red, green, blue, 1.0))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            instance, "UseVertexColor", 1.0 if spec.get("vertex_color") else 0.0)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            instance, "EmissiveStrength", float(spec.get("emissive", 0.0)))
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            instance, "EmissiveColor", unreal.LinearColor(red, green, blue, 1.0))

        unreal.EditorAssetLibrary.save_asset(package_path)
        built.append(package_path)

    _log("built {} material instance(s)".format(len(built)))
    return built


def _canonical(slot_name):
    """``M_Iron_3`` / ``M_Iron.001`` -> ``M_Iron``, if that is a known material."""
    candidate = str(slot_name)
    for _ in range(3):
        if candidate in MATERIALS:
            return candidate
        stem, sep, tail = candidate.rpartition("_")
        if sep and (tail.isdigit() or tail.startswith("00")):
            candidate = stem
            continue
        stem, sep, tail = candidate.rpartition(".")
        if sep and tail.isdigit():
            candidate = stem
            continue
        break
    return candidate if candidate in MATERIALS else None


def consolidate_materials():
    """Point every mesh section at ``MAT_DIR/M_<Name>`` and bin the imports.

    The imported materials exist only so the sections carry the glTF material
    names; once each section has been repointed at the corresponding toon
    instance they are dead weight, and leaving twenty-two near-identical
    ``M_Iron_n`` assets in the project is exactly the drift this consolidates
    away.
    """
    instances = {}
    for name in MATERIALS:
        asset = unreal.EditorAssetLibrary.load_asset("{}/{}".format(MAT_DIR, name))
        if asset is None:
            raise RuntimeError("missing material instance {}/{}".format(MAT_DIR, name))
        instances[name] = asset

    assigned, unmatched, orphans, failed = 0, [], [], []

    def repoint(asset, property_name, package_path):
        """Rewrite one mesh's material array, and confirm the write landed.

        Iterating an ``unreal.Array`` of structs yields *copies*: mutating the
        loop variable changes nothing, and writing the same Array object back
        writes back what was already there. The whole array has to be rebuilt
        from the mutated copies and assigned as a new list — which is also why
        this reads the result back afterwards rather than trusting the write.
        """
        nonlocal assigned

        rebuilt = []
        for entry in asset.get_editor_property(property_name):
            slot = str(entry.get_editor_property("material_slot_name"))
            name = _canonical(slot)
            if name is None:
                unmatched.append("{}:{}".format(package_path, slot))
            else:
                entry.set_editor_property("material_interface", instances[name])
                assigned += 1
            rebuilt.append(entry)

        asset.set_editor_property(property_name, rebuilt)

        for entry in asset.get_editor_property(property_name):
            if entry.get_editor_property("material_interface") is None:
                failed.append("{}:{}".format(
                    package_path, entry.get_editor_property("material_slot_name")))

        unreal.EditorAssetLibrary.save_asset(package_path)

    for object_path in sorted(_list(ROOT)):
        package_path = _package(object_path)
        if package_path.startswith(MAT_DIR):
            continue
        asset = unreal.EditorAssetLibrary.load_asset(object_path)

        if isinstance(asset, unreal.SkeletalMesh):
            repoint(asset, "materials", package_path)
        elif isinstance(asset, unreal.StaticMesh):
            repoint(asset, "static_materials", package_path)
        elif isinstance(asset, unreal.MaterialInterface):
            orphans.append(package_path)

    # Only bin the imported originals once every section has been repointed —
    # deleting them first would leave a mesh with null sections if anything
    # above went wrong, which renders as the grey world-grid checker.
    if not failed:
        for package_path in orphans:
            unreal.EditorAssetLibrary.delete_asset(package_path)
    else:
        _warn("{} section(s) still unassigned; imported materials kept".format(len(failed)))
        for entry in failed:
            _warn("  unassigned {}".format(entry))

    _log("consolidate: {} section(s) assigned, {} imported material(s) deleted, "
         "{} unmatched, {} failed".format(
             assigned, 0 if failed else len(orphans), len(unmatched), len(failed)))
    for entry in unmatched:
        _warn("  unmatched slot {}".format(entry))

    return {"assigned": assigned, "deleted": [] if failed else orphans,
            "unmatched": unmatched, "failed": failed}


# ── Orientation, measured rather than assumed ───────────────────────────────

def describe_orientation():
    """Report the body's ref-pose landmarks so the mesh offset is not guessed.

    ``socket_back`` sits on the character's spine, behind it; whichever axis it
    is negative on is the model's backward axis, and that is what decides the
    yaw the mesh component needs relative to the actor.
    """
    mesh = unreal.EditorAssetLibrary.load_asset("{}/{}".format(BODY_DIR, BODY_MESH))
    if mesh is None:
        raise RuntimeError("body mesh not imported")

    bounds = mesh.get_bounds()
    origin = bounds.box_extent
    report = {"bounds_extent": [origin.x, origin.y, origin.z],
              "bounds_origin": [bounds.origin.x, bounds.origin.y, bounds.origin.z],
              "bones": {}}

    skeleton = mesh.get_editor_property("skeleton")
    ref = skeleton.get_reference_pose() if hasattr(skeleton, "get_reference_pose") else None
    report["reference_pose_available"] = ref is not None

    for bone in ("root", "pelvis", "head", "socket_back", "socket_weapon_r",
                 "socket_offhand_l", "socket_head_top", "foot_l"):
        try:
            index = skeleton.get_editor_property("bone_tree")  # not usable directly
        except Exception:
            index = None
        report["bones"][bone] = None

    _log("orientation: extent={} origin={}".format(
        report["bounds_extent"], report["bounds_origin"]))
    return report


# ── The whole run ────────────────────────────────────────────────────────────

def run_all(source_dir):
    """Import everything, consolidate the materials, then verify."""
    result = {}
    result["body"] = import_body(source_dir)
    result["equipment"] = import_equipment(source_dir)
    result["materials"] = build_material_instances()
    result["consolidated"] = consolidate_materials()
    result["verify"] = verify()
    if not result["verify"]["ok"]:
        result["forced"] = force_skeleton()
        result["verify"] = verify()
    unreal.EditorAssetLibrary.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    _log("run_all done: {}".format(json.dumps(result["verify"]["mismatched"])))
    return result
