"""B-15 Wave 1+: re-import a remade kit onto the existing asset paths.

The swap-in-place rule (Docs/ArtBible.md, section 8): a replacement keeps the
asset name, path, pivot and footprint of what it replaces, so every placed copy
and every tile field picks up the new art with no level rebuild.

``reimport_meshes(entries)`` takes ``[(glb_path, dest_folder), ...]`` where
``dest_folder`` is the content folder the first pass imported into (for example
``/Game/Valhalla/Environment/Town``). Interchange writes
``<dest>/<Name>/StaticMeshes/<Name>`` exactly as before. Then, per mesh:

- **Nanite** back on (the first pass had it on everywhere);
- **collision profile**: ``VisionBlocker`` for ``VB_`` meshes, else ``BlockAll``;
- **materials** by slot name: a glTF material called ``MI_GrassGround`` binds to
  ``/Game/Valhalla/Materials/Sets/MI_GrassGround`` (or a first-pass
  ``/Game/Valhalla/Environment/Materials/M_*`` of that name). The per-mesh
  copies Interchange makes are deleted afterwards;
- a report of the new bounds next to the old ones, so a footprint change on a
  wall is caught before it reaches line of sight.
"""

import json
import os

import unreal

SETS = "/Game/Valhalla/Materials/Sets"
FIRST_PASS_MATS = ("/Game/Valhalla/Environment/Materials", "/Game/Valhalla/Characters/Materials",
                   "/Game/Valhalla/Materials")
TAG = "LogValhallaImport:"
EAL = unreal.EditorAssetLibrary


def _log(msg):
    unreal.log("{} kit: {}".format(TAG, msg))


def _find_material(slot_name):
    for folder in (SETS,) + FIRST_PASS_MATS:
        path = "{}/{}".format(folder, slot_name)
        if EAL.does_asset_exist(path):
            return EAL.load_asset(path)
    return None


def _bounds(sm):
    b = sm.get_bounding_box()
    return [round(b.min.x, 1), round(b.min.y, 1), round(b.min.z, 1)], \
           [round(b.max.x, 1), round(b.max.y, 1), round(b.max.z, 1)]


def _set_profile(sm, profile):
    bs = sm.get_editor_property("body_setup")
    if bs is None:
        return False
    inst = bs.get_editor_property("default_instance")
    inst.set_editor_property("collision_profile_name", profile)
    bs.set_editor_property("default_instance", inst)
    return True


def reimport_meshes(entries):
    report = []
    old_bounds = {}
    old_profiles = {}
    for glb, dest in entries:
        name = os.path.splitext(os.path.basename(glb))[0]
        path = "{}/{}/StaticMeshes/{}".format(dest, name, name)
        if EAL.does_asset_exist(path):
            old = EAL.load_asset(path)
            old_bounds[path] = _bounds(old)
            bs = old.get_editor_property("body_setup")
            if bs is not None:
                old_profiles[path] = str(bs.get_editor_property("default_instance")
                                         .get_editor_property("collision_profile_name"))

    tasks = []
    for glb, dest in entries:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", glb)
        task.set_editor_property("destination_path", dest)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    imported = []
    for task in tasks:
        imported.extend(task.get_editor_property("imported_object_paths") or [])

    strays = [p for p in imported if "/Materials/" in p or "/Textures/" in p]
    for glb, dest in entries:
        name = os.path.splitext(os.path.basename(glb))[0]
        path = "{}/{}/StaticMeshes/{}".format(dest, name, name)
        sm = EAL.load_asset(path)
        if not isinstance(sm, unreal.StaticMesh):
            report.append({"mesh": path, "error": "not imported"})
            continue

        ns = sm.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", True)
        sm.set_editor_property("nanite_settings", ns)

        # Keep whatever profile the mesh already had (loot bags, portals...);
        # a new mesh gets VisionBlocker for VB_ and BlockAll otherwise.
        profile = old_profiles.get(path) or ("VisionBlocker" if name.startswith("VB_") else "BlockAll")
        _set_profile(sm, profile)

        slots = []
        missing = []
        mats = sm.get_editor_property("static_materials")
        for i, m in enumerate(mats):
            slot = str(m.material_slot_name)
            target = _find_material(slot)
            if target is None:
                missing.append(slot)
                continue
            sm.set_material(i, target)
            slots.append(slot)

        # A re-import keeps the old mesh's extra slots even when the new mesh
        # has fewer sections. Drop trailing slots no section uses.
        sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        used = set()
        for lod in range(sm.get_num_lods()):
            for sec in range(sm.get_num_sections(lod)):
                used.add(sme.get_lod_material_slot(sm, lod, sec))
        mats = list(sm.get_editor_property("static_materials"))
        keep = max(used) + 1 if used else len(mats)
        if keep < len(mats):
            sm.set_editor_property("static_materials", mats[:keep])
            slots = [s for s in slots][:keep]

        EAL.save_loaded_asset(sm)
        entry = {"mesh": path, "slots": slots, "tris": sm.get_num_triangles(0),
                 "bounds": _bounds(sm), "profile": profile}
        if path in old_bounds:
            entry["oldBounds"] = old_bounds[path]
        if missing:
            entry["missingMaterials"] = missing
        report.append(entry)

    deleted = []
    for p in strays:
        asset_path = p.split(".")[0]
        if EAL.does_asset_exist(asset_path) and EAL.delete_asset(asset_path):
            deleted.append(asset_path)
    _log("reimported {} mesh(es); removed {} Interchange material copies".format(len(report), len(deleted)))
    return {"meshes": report, "deletedCopies": deleted}


def blend_instance(name, set_a, set_b, size_a, size_b, sharpness=4.0, noise=0.3,
                   noise_scale=60.0, macro=0.12, tint_a=None, tint_b=None):
    """MI on M_ValhallaGroundBlend: layer A where vertex red is 0, B where 1."""
    MEL = unreal.MaterialEditingLibrary
    path = "{}/{}".format(SETS, name)
    mi = EAL.load_asset(path)
    if mi is None:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, SETS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, EAL.load_asset("/Game/Valhalla/Materials/M_ValhallaGroundBlend"))
    for layer, set_name, size in (("A", set_a, size_a), ("B", set_b, size_b)):
        tex = "/Game/Valhalla/Textures/{0}/T_{0}".format(set_name)
        MEL.set_material_instance_texture_parameter_value(mi, "BaseColorMap" + layer, EAL.load_asset(tex + "_BC"))
        MEL.set_material_instance_texture_parameter_value(mi, "NormalMap" + layer, EAL.load_asset(tex + "_N"))
        MEL.set_material_instance_texture_parameter_value(mi, "ORMMap" + layer, EAL.load_asset(tex + "_ORM"))
        MEL.set_material_instance_scalar_parameter_value(mi, "TextureWorldSize" + layer, size)
    MEL.set_material_instance_vector_parameter_value(mi, "TintA", tint_a or unreal.LinearColor(1, 1, 1, 1))
    MEL.set_material_instance_vector_parameter_value(mi, "TintB", tint_b or unreal.LinearColor(1, 1, 1, 1))
    MEL.set_material_instance_scalar_parameter_value(mi, "BlendSharpness", sharpness)
    MEL.set_material_instance_scalar_parameter_value(mi, "BlendNoise", noise)
    MEL.set_material_instance_scalar_parameter_value(mi, "BlendNoiseScale", noise_scale)
    MEL.set_material_instance_scalar_parameter_value(mi, "MacroVariation", macro)
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


def write_report(report, path):
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)


def flat_instance(name, color, roughness=0.7, metallic=0.0, specular=0.5, emissive=None):
    """An untextured M_ValhallaPBR instance (glass, paint, small metal parts)."""
    MEL = unreal.MaterialEditingLibrary
    path = "{}/{}".format(SETS, name)
    mi = EAL.load_asset(path)
    if mi is None:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, SETS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, EAL.load_asset("/Game/Valhalla/Materials/M_ValhallaPBR"))
    MEL.set_material_instance_vector_parameter_value(mi, "BaseColor", unreal.LinearColor(*color, 1.0))
    MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    MEL.set_material_instance_scalar_parameter_value(mi, "Specular", specular)
    if emissive:
        MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", unreal.LinearColor(*emissive[0], 1.0))
        MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", emissive[1])
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


def tune(name, **scalars):
    """Set scalar/vector parameters on an existing MI_ set instance."""
    MEL = unreal.MaterialEditingLibrary
    mi = EAL.load_asset("{}/{}".format(SETS, name))
    for k, v in scalars.items():
        if isinstance(v, (tuple, list)):
            MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v))
        else:
            MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


def reimport_weapons(glbs):
    """Weapons and shields go through the character pipeline (static, no Nanite,
    flat in /Game/Valhalla/Characters/Weapons, as character_import.import_equipment
    placed them), then get their materials bound by slot name like the kits."""
    from valhalla_tools import character_import as ci
    ci._ensure_dirs()
    ci._configure(skeleton=None, skeletal=False, static=True, animations=False)
    report = []
    for glb in glbs:
        name = os.path.splitext(os.path.basename(glb))[0]
        before = ci._list(ci.WEAPON_DIR)
        ci._import(glb, ci.WEAPON_DIR)
        created = ci._list(ci.WEAPON_DIR) - before
        path = "{}/{}".format(ci.WEAPON_DIR, name)
        sm = EAL.load_asset(path)
        if not isinstance(sm, unreal.StaticMesh):
            report.append({"mesh": path, "error": "not imported", "created": sorted(created)})
            continue
        slots, missing = [], []
        for i, m in enumerate(sm.get_editor_property("static_materials")):
            slot = str(m.material_slot_name)
            target = _find_material(slot)
            if target is None:
                missing.append(slot)
                continue
            sm.set_material(i, target)
            slots.append(slot)
        EAL.save_loaded_asset(sm)
        stray = [p for p in created if p.split(".")[0] != path]
        for p in stray:
            ap = p.split(".")[0]
            asset = EAL.load_asset(ap)
            if asset is not None and not isinstance(asset, unreal.StaticMesh):
                EAL.delete_asset(ap)
        report.append({"mesh": path, "slots": slots, "missingMaterials": missing,
                       "tris": sm.get_num_triangles(0), "bounds": _bounds(sm)})
    return report


def master_instance(name, parent, textures=None, scalars=None, vectors=None):
    """An MI_ in the Sets folder under any master (foliage, water): texture
    params by set/asset path, scalars, and (r, g, b[, a]) vectors."""
    MEL = unreal.MaterialEditingLibrary
    path = "{}/{}".format(SETS, name)
    mi = EAL.load_asset(path)
    if mi is None:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, SETS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, EAL.load_asset(parent))
    for k, tex_path in (textures or {}).items():
        MEL.set_material_instance_texture_parameter_value(mi, k, EAL.load_asset(tex_path))
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in (vectors or {}).items():
        c = list(v) + [1.0] * (4 - len(v))
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*c))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi
