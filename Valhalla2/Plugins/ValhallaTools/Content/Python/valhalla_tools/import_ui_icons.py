"""Import the HUD's 2D art as UI textures.

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/import_ui_icons.py"

Three folders beside the Unreal project, each into its own content folder:

- ``Import/UI/Icons``        -> ``/Game/Valhalla/UI/Icons/Items``   item icons
- ``Import/UI/Icons/Skills`` -> ``/Game/Valhalla/UI/Icons/Skills``  skill icons (B-15 Wave 4)
- ``Import/UI/Frames``       -> ``/Game/Valhalla/UI/Frames``        HUD frame art (B-15 Wave 4)

Settings by kind: the 1.0 item icons are 32 px pixel art (nearest filtering,
no mips); the Wave 4 icons are 128 px renders shown at 36-48 px (bilinear, with
mips so they shrink cleanly); frame art is drawn near 1:1 (bilinear, no mips).
All UserInterface2D compression in the UI texture group.

A PNG is imported when its asset is missing or the PNG is newer than the
asset's package on disk, so re-running only picks up what changed.
``UValhallaGameHUDWidget`` loads the asset first and falls back to the PNG on
disk, so new art shows in the editor before this runs.
"""

import os

import unreal

SETS = [
    (os.path.join("Import", "UI", "Icons"), "/Game/Valhalla/UI/Icons/Items", "icon"),
    (os.path.join("Import", "UI", "Icons", "Skills"), "/Game/Valhalla/UI/Icons/Skills", "icon"),
    (os.path.join("Import", "UI", "Frames"), "/Game/Valhalla/UI/Frames", "frame"),
]


def _repo():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(project, "..")), os.path.normpath(project)


def _package_file(project, asset_path):
    rel = asset_path[len("/Game/"):]
    return os.path.join(project, "Content", rel.replace("/", os.sep) + ".uasset")


def _configure(texture, kind):
    pixel_art = kind == "icon" and texture.blueprint_get_size_x() <= 32
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST if pixel_art else unreal.TextureFilter.TF_BILINEAR)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    mips = unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP if (kind == "icon" and not pixel_art) \
        else unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
    texture.set_editor_property("mip_gen_settings", mips)
    texture.set_editor_property("srgb", True)


def run():
    repo, project = _repo()
    report = []
    for rel, dest, kind in SETS:
        source = os.path.join(repo, rel)
        pngs = sorted(f for f in os.listdir(source) if f.lower().endswith(".png")) if os.path.isdir(source) else []
        tasks = []
        for name in pngs:
            base = os.path.splitext(name)[0]
            asset_path = "{}/{}".format(dest, base)
            pkg = _package_file(project, asset_path)
            src = os.path.join(source, name)
            if unreal.EditorAssetLibrary.does_asset_exist(asset_path) and os.path.exists(pkg) \
                    and os.path.getmtime(pkg) >= os.path.getmtime(src):
                continue
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", src)
            task.set_editor_property("destination_path", dest)
            task.set_editor_property("destination_name", base)
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("save", False)
            tasks.append(task)
        if tasks:
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
        configured = 0
        for task in tasks:
            base = task.get_editor_property("destination_name")
            asset_path = "{}/{}".format(dest, base)
            texture = unreal.EditorAssetLibrary.load_asset(asset_path)
            if isinstance(texture, unreal.Texture2D):
                _configure(texture, kind)
                unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
                configured += 1
        report.append("{}: {} png(s), {} imported".format(dest, len(pngs), configured))
    unreal.log("import_ui_icons: " + "; ".join(report))
    return report


run()
