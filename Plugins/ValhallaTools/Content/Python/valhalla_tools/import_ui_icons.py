"""Phase 8b: import the 1.0 item icons as UI textures.

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/import_ui_icons.py"

Every PNG in ``valhalla/client/dist/assets/sprites/icons`` becomes
``/Game/Valhalla/UI/Icons/Items/<basename>``: nearest filtering (they are
pixel art), UserInterface2D compression, the UI texture group, no mips.

Check-before-create: an icon that already exists is left alone (its settings
are re-applied), so the script never raises the editor's Overwrite dialog and
is safe to re-run. ``UValhallaGameHUDWidget::FindItemIcon`` loads the asset
first and falls back to the PNG on disk, so an icon added to 1.0 later still
shows before anyone re-runs this.
"""

import os

import unreal

DEST = "/Game/Valhalla/UI/Icons/Items"


def _source_dir():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(project, "..", "..", "valhalla", "client", "dist", "assets", "sprites", "icons"))


def _configure(texture):
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("srgb", True)


def run():
    source = _source_dir()
    pngs = sorted(f for f in os.listdir(source) if f.lower().endswith(".png")) if os.path.isdir(source) else []
    tasks, skipped = [], []
    for name in pngs:
        base = os.path.splitext(name)[0]
        asset_path = "{}/{}".format(DEST, base)
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            skipped.append(asset_path)
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(source, name))
        task.set_editor_property("destination_path", DEST)
        task.set_editor_property("destination_name", base)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", False)
        task.set_editor_property("save", False)
        tasks.append(task)

    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    configured = 0
    for name in pngs:
        asset_path = "{}/{}".format(DEST, os.path.splitext(name)[0])
        texture = unreal.EditorAssetLibrary.load_asset(asset_path)
        if isinstance(texture, unreal.Texture2D):
            _configure(texture)
            unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
            configured += 1

    unreal.log("import_ui_icons: {} png(s) in {}; {} imported, {} already present, {} configured and saved.".format(
        len(pngs), source, len(tasks), len(skipped), configured))


run()
