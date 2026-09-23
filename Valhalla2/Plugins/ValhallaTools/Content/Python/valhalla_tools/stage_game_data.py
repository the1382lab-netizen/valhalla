"""B-03: copy the game data into Content/Data so a packaged build carries it.

    py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/stage_game_data.py"

Run before packaging a client. ``DefaultGame.ini`` stages ``Content/Data`` as
loose files (DirectoriesToAlwaysStageAsNonUFS), and
``UValhallaDataSubsystem::ChooseDataSource`` seeds the client's ``Saved/Data``
from it until the first download from the backend. ``Content/Data`` is
git-ignored: ``shared/data`` stays the one source.
"""

import os
import shutil

import unreal

FILES = [
    "classes.json", "items.json", "skills.json", "npc-templates.json",
    "loot-tables.json", "zones.json", "ui-config.json",
]


def run():
    settings = unreal.get_default_object(unreal.ValhallaDataSettings)
    source = str(settings.get_resolved_data_root())
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    dest = os.path.join(project, "Data")
    os.makedirs(dest, exist_ok=True)
    copied = []
    for name in FILES:
        src = os.path.join(source, name)
        if not os.path.isfile(src):
            raise RuntimeError("missing {} in {}".format(name, source))
        shutil.copy2(src, os.path.join(dest, name))
        copied.append(name)
    unreal.log("VALHALLA_STAGE_DATA copied {} files from {} to {}".format(len(copied), source, dest))
    return {"ok": True, "source": source, "dest": dest, "files": copied}


if __name__ == "__main__":
    run()
