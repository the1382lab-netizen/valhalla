"""B-15: import a CC0 texture set and make an M_ValhallaPBR instance from it.

A texture set is the folder ``Import/Textures/<SetName>/`` written by the
Blender helper ``Blender assets/scripts/valhalla_textures.py``:

    T_<SetName>_BC.png    base colour (sRGB)
    T_<SetName>_N.png     normal, DirectX convention
    T_<SetName>_ORM.png   R = AO, G = roughness, B = metallic (linear)

``import_set("CobbleFloor")`` imports them to ``/Game/Valhalla/Textures/<SetName>``
with the right compression (normal map, masks), and ``make_instance`` builds
``/Game/Valhalla/Materials/Sets/MI_<SetName>`` with ``UseTextures`` on.
Re-running either replaces what is there, so a set can be re-exported and
re-imported without touching any mesh or level that uses it.
"""

import os

import unreal

TEX_ROOT = "/Game/Valhalla/Textures"
MI_ROOT = "/Game/Valhalla/Materials/Sets"
PBR = "/Game/Valhalla/Materials/M_ValhallaPBR"

MEL = unreal.MaterialEditingLibrary
TAG = "LogValhallaImport:"


def _log(msg):
    unreal.log("{} texture sets: {}".format(TAG, msg))


def import_root():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(project, "..", "Import", "Textures"))


def import_set(set_name):
    folder = os.path.join(import_root(), set_name)
    if not os.path.isdir(folder):
        raise RuntimeError("no texture set folder: " + folder)
    dest = "{}/{}".format(TEX_ROOT, set_name)

    tasks = []
    for file_name in sorted(os.listdir(folder)):
        if not file_name.lower().endswith(".png"):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(folder, file_name))
        task.set_editor_property("destination_path", dest)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    imported = {}
    for task in tasks:
        for path in task.get_editor_property("imported_object_paths"):
            tex = unreal.EditorAssetLibrary.load_asset(path)
            name = tex.get_name()
            if name.endswith("_N"):
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("srgb", False)
                imported["N"] = tex
            elif name.endswith("_ORM"):
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
                tex.set_editor_property("srgb", False)
                imported["ORM"] = tex
            elif name.endswith("_BC"):
                tex.set_editor_property("srgb", True)
                imported["BC"] = tex
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
            unreal.EditorAssetLibrary.save_loaded_asset(tex)
    _log("imported {} -> {}: {}".format(set_name, dest, sorted(imported)))
    return imported


def make_instance(set_name, world_aligned=False, texture_world_size=160.0, uv_scale=1.0,
                  macro_variation=0.1, tint=None, instance_name=None):
    """Create or update MI_<SetName> on M_ValhallaPBR using the set's textures."""
    name = instance_name or "MI_{}".format(set_name)
    path = "{}/{}".format(MI_ROOT, name)
    mi = unreal.EditorAssetLibrary.load_asset(path)
    if mi is None:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, MI_ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, unreal.EditorAssetLibrary.load_asset(PBR))

    tex_dir = "{}/{}".format(TEX_ROOT, set_name)
    maps = {
        "BaseColorMap": "{}/T_{}_BC".format(tex_dir, set_name),
        "NormalMap": "{}/T_{}_N".format(tex_dir, set_name),
        "ORMMap": "{}/T_{}_ORM".format(tex_dir, set_name),
    }
    for param, tex_path in maps.items():
        tex = unreal.EditorAssetLibrary.load_asset(tex_path)
        if tex is None:
            raise RuntimeError("missing texture " + tex_path)
        MEL.set_material_instance_texture_parameter_value(mi, param, tex)

    MEL.set_material_instance_static_switch_parameter_value(mi, "UseTextures", True)
    MEL.set_material_instance_static_switch_parameter_value(mi, "WorldAlignedUV", bool(world_aligned))
    MEL.set_material_instance_scalar_parameter_value(mi, "TextureWorldSize", texture_world_size)
    MEL.set_material_instance_scalar_parameter_value(mi, "UVScale", uv_scale)
    MEL.set_material_instance_scalar_parameter_value(mi, "MacroVariation", macro_variation)
    MEL.set_material_instance_vector_parameter_value(
        mi, "BaseColor", tint or unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    MEL.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    _log("instance {} ({} aligned, {} cm)".format(path, "world" if world_aligned else "uv", texture_world_size))
    return mi
