#!/usr/bin/env python3
"""
Check a packaged client for content the cook left out (first public test, 2026-09-25).

The 0.1.1 build shipped without animations, the vision fog, NPC weapons, hair
and spell effects: all of them are loaded by name, so nothing pulled them into
the cook. This runs the packaged Valhalla2.exe on L_World with
`valhalla.CheckContent quit`, which loads every asset the game loads by name
(ValhallaContentCheck.h) and quits, then reads its log.

Fails (exit code 1, "CONTENT-CHECK=FAIL") when:
  * the log has "content check: MISSING", "Failed to find object '... /Game/...",
    "missing animation", "missing Niagara system", or "... is missing";
  * the check never reported ("content check: N checked, M missing" absent),
    e.g. the client crashed or timed out;
Prints "CONTENT-CHECK=OK" otherwise.

  python Tools/perf/check_packaged.py [--build-dir <pkg/Windows>] [--timeout 240]

Tools/perf/package_client.cmd runs it after packaging; Tools/publish refuses a
build that fails it (--skip-check to override).
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_BUILD = REPO / "Valhalla2" / "Saved" / "Perf" / "pkg" / "Windows"
LOG_NAME = "content_check.log"

BAD = [
    re.compile(r"content check: MISSING (.+)"),
    re.compile(r"Failed to find object '[^']*(/Game/[^']+)'"),
    re.compile(r"missing animation (/Game/\S+)"),
    re.compile(r"missing Niagara system (/Game/\S+)"),
    re.compile(r"(/Game/\S+) is missing"),
    re.compile(r"asset=(/Game/\S+) MISSING"),
]
SUMMARY = re.compile(r"content check: (\d+) checked, (\d+) missing")


def run(build: Path, timeout: int) -> int:
    exe = build / "Valhalla2.exe"
    if not exe.is_file():
        print(f"No Valhalla2.exe in {build}")
        print("CONTENT-CHECK=FAIL")
        return 1
    log = build.parent / LOG_NAME
    if log.exists():
        log.unlink()

    args = [str(exe), "/Game/Valhalla/Maps/L_World", "-windowed", "-resx=640", "-resy=360", "-nosound",
            "-unattended", "-nosplash", f"-abslog={log}", "-ExecCmds=valhalla.CheckContent quit"]
    print(f"Running the packaged client's content check ({exe}) ...", flush=True)
    started = time.time()
    proc = subprocess.Popen(args, cwd=build)
    try:
        code = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        code = None
        print(f"The client did not quit within {timeout} s; stopped it.")
    print(f"Client exited with {code} after {time.time() - started:.0f} s. Log: {log}")

    if not log.is_file():
        print("No log was written.")
        print("CONTENT-CHECK=FAIL")
        return 1

    text = log.read_text(encoding="utf-8", errors="replace")
    missing: dict[str, int] = {}
    for line in text.splitlines():
        for pattern in BAD:
            m = pattern.search(line)
            if m:
                path = m.group(1).strip()
                missing[path] = missing.get(path, 0) + 1
                break

    summary = SUMMARY.search(text)
    if summary:
        print(f"In-game check: {summary.group(1)} checked, {summary.group(2)} missing.")
    else:
        print("The in-game check never reported (crashed, timed out, or valhalla.CheckContent is not in this build).")

    if missing:
        print(f"{len(missing)} asset(s) missing from the packaged build:")
        for path in sorted(missing):
            print(f"  {path}")
        print("Add their folder to DirectoriesToAlwaysCook in Valhalla2/Config/DefaultGame.ini (or restore the asset).")
    ok = bool(summary) and summary.group(2) == "0" and not missing
    print("CONTENT-CHECK=" + ("OK" if ok else "FAIL"))
    return 0 if ok else 1


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    ap.add_argument("--timeout", type=int, default=240)
    args = ap.parse_args()
    sys.exit(run(args.build_dir.resolve(), args.timeout))


if __name__ == "__main__":
    main()
