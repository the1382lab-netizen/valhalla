"""B-08a: import the front end's fonts as font faces.

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/import_ui_fonts.py"

``Import/UI/Fonts/<Family>-<Style>.ttf`` -> ``/Game/Valhalla/UI/Fonts/FF_<Family>_<Style>``
(a UFontFace each). ``ValhallaUIArt::DisplayFont`` / ``BodyFont`` build their
composite fonts from these at runtime, so no UFont asset has to be authored
(Python cannot fill a UFont's composite font). Before this runs they read the
TTFs from disk, which only works beside the repo. Cinzel and EB Garamond are
SIL OFL 1.1 (Docs/branding/fonts).

A TTF is imported when its asset is missing or the TTF is newer than the asset.
"""

import os

import unreal

DEST = "/Game/Valhalla/UI/Fonts"


def _paths():
    project = os.path.normpath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    repo = os.path.normpath(os.path.join(project, ".."))
    return repo, project


def run():
    repo, project = _paths()
    source = os.path.join(repo, "Import", "UI", "Fonts")
    ttfs = sorted(f for f in os.listdir(source) if f.lower().endswith((".ttf", ".otf"))) if os.path.isdir(source) else []
    tasks = []
    for name in ttfs:
        base = "FF_" + os.path.splitext(name)[0].replace("-", "_")
        asset_path = "{}/{}".format(DEST, base)
        pkg = os.path.join(project, "Content", "Valhalla", "UI", "Fonts", base + ".uasset")
        src = os.path.join(source, name)
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path) and os.path.exists(pkg) \
                and os.path.getmtime(pkg) >= os.path.getmtime(src):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", DEST)
        task.set_editor_property("destination_name", base)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    report = []
    for task in tasks:
        base = task.get_editor_property("destination_name")
        asset_path = "{}/{}".format(DEST, base)
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        kind = type(asset).__name__ if asset else "missing"
        if asset:
            unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
        report.append("{} ({})".format(base, kind))
    unreal.log("import_ui_fonts: {} ttf(s); imported: {}".format(len(ttfs), ", ".join(report) or "none"))
    return report


run()
