"""B-05 / B-19: keep every tool off the levels Kevin edits by hand.

`L_World`, `L_Grasslands`, `L_Desert` and every zone made since are
hand-edited. B-05 put a refusal and backups in front of the from-scratch
builders; **B-19 retired those builders altogether**: no MCP tool regenerates an
existing zone any more, and `build_world.build_all` / `build_grasslands.build` /
`build_desert.build` refuse their target levels unconditionally (there is no
`force`). New zones are made with `ValhallaLevelTools.scaffold_zone`
(`valhalla_tools/scaffold_zone.py`), which registers each one here the moment
it exists. This module is the guard every writer consults.

## The marker

`<repo>/maps/handedited.json`:

    {
      "version": 1,
      "levels":   ["/Game/Valhalla/Maps/L_World", ...],   # package paths
      "note": "..."
    }

(The zone overlay JSON, `maps/overlays-2.0`, is retired: portals, zone
entries and player starts live only in the levels. An `overlays` key left in
an old marker is ignored.)

**The rule.** A listed level is never regenerated. Writers skip or
refuse it and name it in their result (`refusal_message`). `register()` adds
entries (what `scaffold_zone` calls); nothing in the tools removes one — to
release a level, Kevin edits the file by hand.

## Backups

`backup_files` / `backup_targets` copy a level's `.umap` / `_BuiltData.uasset`
to `Valhalla2/Saved/LevelBackups/<YYYYMMDD-HHMMSS>/`,
paths kept relative to the repo root. Since B-19 no tool in the plugin calls
them (only the retired forced rebuild did); they stay for any future in-place
editing script that wants a copy before it writes, and `LevelBackups/` still
holds the B-05-era copies for recovering an old layout.

Fail-safe by design: a marker that exists but does not parse raises rather
than being read as "nothing protected". Nothing in here imports `unreal` at
module level, so the pure parts can be tested outside the editor
(`self_check()`).
"""

import datetime
import json
import os
import shutil

MARKER_NAME = "handedited.json"
BACKUP_DIR_NAME = "LevelBackups"
STAMP_FORMAT = "%Y%m%d-%H%M%S"


class ProtectedError(RuntimeError):
    """Raised by a writer asked to overwrite a marked level."""


def _log(message):
    try:
        import unreal
        unreal.log("VALHALLA_PROTECT {}".format(message))
    except ImportError:
        print("VALHALLA_PROTECT {}".format(message))


def _norm(path):
    return os.path.normpath(path).replace("\\", "/")


# ── Where things are ────────────────────────────────────────────────────


def project_dir():
    """`Valhalla2/` (the UE project), absolute."""
    import unreal
    return _norm(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


def repo_root():
    """The monorepo root: `build_zone.repo_root()`, or `Valhalla2/..`."""
    try:
        from valhalla_tools.build_zone import repo_root as zone_repo_root
        root = zone_repo_root()
        if root and os.path.isdir(os.path.join(root, "maps")):
            return _norm(root)
    except Exception:  # noqa: BLE001
        pass
    return _norm(os.path.dirname(project_dir()))


def marker_path():
    """`<repo>/maps/handedited.json`.

    Looks under `build_zone.repo_root()` first and then at `Valhalla2/../maps`,
    so a `DataRoot` that cannot be read does not silently disable protection.
    """
    candidates = []
    try:
        from valhalla_tools.build_zone import repo_root as zone_repo_root
        candidates.append(os.path.join(zone_repo_root(), "maps", MARKER_NAME))
    except Exception:  # noqa: BLE001
        pass
    try:
        candidates.append(os.path.join(os.path.dirname(project_dir()), "maps", MARKER_NAME))
    except Exception:  # noqa: BLE001
        pass
    for candidate in candidates:
        if os.path.isfile(candidate):
            return _norm(candidate)
    return _norm(candidates[-1]) if candidates else MARKER_NAME


def level_files(level_path, content_dir=None):
    """On-disk files of a `/Game/...` level: the `.umap` and `_BuiltData.uasset`.

    Only files that exist are returned.
    """
    if content_dir is None:
        content_dir = os.path.join(project_dir(), "Content")
    if not level_path.startswith("/Game/"):
        raise ValueError("not a /Game/ package path: {}".format(level_path))
    stem = os.path.join(content_dir, level_path[len("/Game/"):])
    files = [stem + ".umap", stem + "_BuiltData.uasset"]
    return [_norm(f) for f in files if os.path.isfile(f)]


# ── The marker ──────────────────────────────────────────────────────────


def load_marker(path=None):
    """`{"levels": set, "path": str}`. Missing file: nothing listed.

    A file that exists but is not valid JSON, or has the wrong shape, raises:
    protection must never fail open.
    """
    path = path or marker_path()
    if not os.path.isfile(path):
        return {"levels": set(), "path": path, "exists": False}
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    levels = document.get("levels", [])
    if not isinstance(levels, list):
        raise ValueError("{}: 'levels' must be a list".format(path))
    return {
        "levels": {str(p).split(".")[0].rstrip("/") for p in levels},
        "path": path,
        "exists": True,
    }


def is_level_protected(level_path, marker=None):
    marker = marker if marker is not None else load_marker()
    return level_path.split(".")[0].rstrip("/") in marker["levels"]


def refusal_message(levels=(), marker=None):
    where = marker["path"] if marker else "maps/" + MARKER_NAME
    parts = ["level(s) " + ", ".join(levels)] if levels else []
    one = len(levels) == 1
    return ("refused: {} {} hand-edited (listed in {}) and nothing regenerates {} "
            "(B-19). Edit in place in the editor; make a new zone with "
            "ValhallaLevelTools.scaffold_zone; recover an old layout from git history "
            "or Saved/{}/.").format(" and ".join(parts), "is" if one else "are",
                                    where, "it" if one else "them", BACKUP_DIR_NAME)


def register(levels=(), path=None):
    """Add levels to the marker, keeping everything already listed.

    What `scaffold_zone` calls the moment it has created a zone, so the new
    zone is protected from then on. Creates the marker if it is missing.
    Never removes anything: releasing a level is a hand edit of the file.

    Returns:
        ``{"path": str, "added": {"levels": [...]}}``.
    """
    path = path or marker_path()
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as handle:
            document = json.load(handle)
    else:
        document = {"version": 1, "levels": []}
    listed_levels = document.setdefault("levels", [])
    if not isinstance(listed_levels, list):
        raise ValueError("{}: 'levels' must be a list".format(path))

    added = {"levels": []}
    for level_path in levels:
        level_path = str(level_path).split(".")[0].rstrip("/")
        if level_path not in listed_levels:
            listed_levels.append(level_path)
            added["levels"].append(level_path)

    if added["levels"]:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(document, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
        _log("registered {} in {}".format(added, path))
    return {"path": _norm(path), "added": added}


# ── Backups ─────────────────────────────────────────────────────────────


def default_backup_root():
    return _norm(os.path.join(project_dir(), "Saved", BACKUP_DIR_NAME))


def _relative(path, bases):
    path = _norm(os.path.abspath(path))
    for base in bases:
        base = _norm(os.path.abspath(base))
        if path == base or path.startswith(base + "/"):
            return os.path.relpath(path, base).replace("\\", "/")
    return os.path.basename(path)


def backup_files(paths, backup_root=None, bases=None, stamp=None):
    """Copy `paths` into `<backup_root>/<YYYYMMDD-HHMMSS>/`, keeping relative paths.

    Args:
        paths: files to copy; ones that do not exist are skipped.
        backup_root: defaults to `Valhalla2/Saved/LevelBackups`.
        bases: directories to express each path relative to, first match wins;
            defaults to the repo root then the project dir. A path under none
            of them keeps only its file name.
        stamp: folder name; defaults to the local time. A folder that already
            exists gets a `-2`, `-3`... suffix, so two runs in one second never
            share one.

    Returns:
        `{"folder": abs path, "copied": [relative paths]}`; `folder` is None
        when there was nothing to copy.
    """
    existing = [p for p in paths if p and os.path.isfile(p)]
    if not existing:
        return {"folder": None, "copied": []}
    if backup_root is None:
        backup_root = default_backup_root()
    if bases is None:
        bases = [repo_root(), project_dir()]
    stamp = stamp or datetime.datetime.now().strftime(STAMP_FORMAT)

    folder = os.path.join(backup_root, stamp)
    suffix = 2
    while os.path.exists(folder):
        folder = os.path.join(backup_root, "{}-{}".format(stamp, suffix))
        suffix += 1

    copied = []
    for source in existing:
        relative = _relative(source, bases)
        target = os.path.join(folder, relative)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(source, target)
        copied.append(relative)

    folder = _norm(folder)
    _log("backed up {} file(s) to {}".format(len(copied), folder))
    return {"folder": folder, "copied": copied}


def backup_targets(level_paths=(), **kwargs):
    """Back up the files of these levels (existing ones only)."""
    files = []
    for level_path in level_paths:
        files.extend(level_files(level_path))
    return backup_files(files, **kwargs)


# ── Self-check (no test harness in the plugin; run from the console) ───


def self_check(scratch=None):
    """Exercise the marker and backup logic on scratch files. Returns a report.

    From the editor: `py -c "from valhalla_tools import level_protection as p; print(p.self_check())"`
    or outside it: `python level_protection.py`. `scratch` defaults to
    `Valhalla2/Saved/b05_scratch` in the editor and a temp dir outside it.
    Touches nothing outside `scratch`.
    """
    import tempfile

    if scratch is None:
        try:
            scratch = os.path.join(project_dir(), "Saved", "b05_scratch")
        except ImportError:
            scratch = tempfile.mkdtemp(prefix="b05_scratch_")
    scratch = _norm(os.path.abspath(scratch))
    checks = []

    def check(name, condition):
        checks.append((name, bool(condition)))

    # A fake repo: <scratch>/repo/{maps/..., Valhalla2/Content/...}
    repo = os.path.join(scratch, "repo")
    content = os.path.join(repo, "Valhalla2", "Content")
    for directory in (os.path.join(content, "Valhalla", "Maps", "Zones"), os.path.join(repo, "maps")):
        os.makedirs(directory, exist_ok=True)
    dummies = {
        os.path.join(content, "Valhalla", "Maps", "L_World.umap"): "world",
        os.path.join(content, "Valhalla", "Maps", "L_World_BuiltData.uasset"): "built",
        os.path.join(content, "Valhalla", "Maps", "Zones", "L_Desert.umap"): "desert",
    }
    for path, text in dummies.items():
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)

    marker_file = os.path.join(repo, "maps", MARKER_NAME)
    with open(marker_file, "w", encoding="utf-8") as handle:
        # A leftover "overlays" key (pre-retirement marker) must be ignored.
        json.dump({"version": 1, "levels": ["/Game/Valhalla/Maps/L_World"],
                   "overlays": ["desert"]}, handle)

    marker = load_marker(marker_file)
    check("marker lists L_World", is_level_protected("/Game/Valhalla/Maps/L_World", marker))
    check("object path form matches",
          is_level_protected("/Game/Valhalla/Maps/L_World.L_World", marker))
    check("unlisted level is free", not is_level_protected("/Game/Valhalla/Maps/Zones/L_Desert", marker))
    check("an old 'overlays' key is ignored", set(marker) == {"levels", "path", "exists"})
    check("missing marker protects nothing",
          not load_marker(os.path.join(scratch, "nope.json"))["levels"])

    bad = os.path.join(scratch, "bad.json")
    with open(bad, "w", encoding="utf-8") as handle:
        handle.write("{ not json")
    try:
        load_marker(bad)
        check("malformed marker raises", False)
    except ValueError:
        check("malformed marker raises", True)

    world_files = level_files("/Game/Valhalla/Maps/L_World", content_dir=content)
    check("level_files finds umap + BuiltData", len(world_files) == 2)

    root = os.path.join(scratch, BACKUP_DIR_NAME)
    first = backup_files(world_files, backup_root=root, bases=[repo])
    second = backup_files(world_files, backup_root=root, bases=[repo],
                          stamp=os.path.basename(first["folder"]))
    folder = first["folder"] or ""
    name = os.path.basename(folder)
    check("folder is <root>/YYYYMMDD-HHMMSS",
          os.path.dirname(folder) == _norm(root)
          and len(name) == 15 and name[8] == "-" and name.replace("-", "").isdigit())
    check("relative paths kept", sorted(first["copied"]) == sorted([
        "Valhalla2/Content/Valhalla/Maps/L_World.umap",
        "Valhalla2/Content/Valhalla/Maps/L_World_BuiltData.uasset"]))
    check("copies are byte-identical", all(
        open(os.path.join(folder, rel), encoding="utf-8").read()
        == open(os.path.join(repo, rel), encoding="utf-8").read() for rel in first["copied"]))
    check("same-second run gets its own folder", second["folder"] == folder + "-2")
    check("nothing to copy -> no folder",
          backup_files([os.path.join(scratch, "missing.umap")], backup_root=root)["folder"] is None)

    registered = register(levels=["/Game/Valhalla/Maps/Zones/L_Tundra",
                                  "/Game/Valhalla/Maps/L_World"], path=marker_file)
    after = load_marker(marker_file)
    check("register adds new entries only",
          registered["added"] == {"levels": ["/Game/Valhalla/Maps/Zones/L_Tundra"]})
    check("register keeps existing entries",
          is_level_protected("/Game/Valhalla/Maps/L_World", after)
          and is_level_protected("/Game/Valhalla/Maps/Zones/L_Tundra", after))

    failed = [n for n, ok in checks if not ok]
    report = {"ok": not failed, "passed": len(checks) - len(failed), "failed": failed,
              "scratch": scratch, "backupFolder": folder, "copied": first["copied"]}
    _log("self_check " + json.dumps(report))
    return report


if __name__ == "__main__":
    print(json.dumps(self_check(), indent=2))
