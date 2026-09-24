#!/usr/bin/env python3
"""
Packaged-client secret audit (B-04).

Scans what ships to testers for anything that would let a client act as the
game server:

  * the literal dev secret            dev-server-secret
  * the server-to-server header name  X-Server-Secret
  * the real VALHALLA_SERVER_SECRET and JWT_SECRET values from
    <repo root>/secrets.local.env, read at runtime and NEVER printed (a hit
    names the key, the file and the line or byte offset, not the value)

Usage (from anywhere):
  python Tools/audit_client_secrets.py                 # default targets
  python Tools/audit_client_secrets.py <folder> ...    # e.g. a packaged Windows/ folder

Default targets: Valhalla2/Config (every ini there is packaged),
Valhalla2/Content/Data (game data staged by stage_game_data.py) or, when it has
not been staged, its source shared/data, and Valhalla2/Saved/StagedBuilds when
a package has been made.

Text files report path:line. Binary files (.pak/.utoc/.ucas/.exe/.dll/...)
are searched as bytes, in UTF-8 and UTF-16LE, and report path@offset. In an
executable or .dll the two literal strings are code (the game module sets the
header on the *server* path and compares against the dev default), so they are
reported as INFO there, not as findings; a real secret value is a finding
everywhere. Note that a compressed or encrypted .pak can hide an ini's text
from a byte search: audit the staged folder (Saved/StagedBuilds/Windows)
before it is paked, or package with compression off for the audit.

Exit code: 0 clean, 1 findings, 2 usage error.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PROJECT = REPO / "Valhalla2"
SECRETS_FILE = Path(os.environ.get("VALHALLA_SECRETS_FILE", REPO / "secrets.local.env"))

LITERALS = ["dev-server-secret", "X-Server-Secret"]
SECRET_KEYS = ["VALHALLA_SERVER_SECRET", "JWT_SECRET"]
TEXT_SUFFIXES = {".ini", ".json", ".txt", ".cfg", ".csv", ".xml", ".md", ".env", ".bat", ".cmd", ".ps1", ".py", ".js", ".ts", ".log", ".uproject", ".uplugin"}
CODE_SUFFIXES = {".exe", ".dll", ".pdb", ".so", ".dylib", ".modules", ".target", ".version"}
CHUNK = 8 * 1024 * 1024


def read_secret_values() -> dict[str, str]:
    """KEY -> value from secrets.local.env (same dotenv subset as server/src/loadEnv.ts)."""
    values: dict[str, str] = {}
    if not SECRETS_FILE.is_file():
        return values
    for raw in SECRETS_FILE.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key, value = key.strip(), value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        if key in SECRET_KEYS and value:
            values[key] = value
    return values


def needles(secrets: dict[str, str]) -> list[tuple[str, str, bool]]:
    """(label, text, is_secret). The label is what gets printed; the text never is."""
    out = [(f"'{lit}'", lit, False) for lit in LITERALS]
    for key, value in secrets.items():
        if value in LITERALS:
            continue  # already covered, and printing the literal is harmless
        out.append((f"the {key} value from secrets.local.env", value, True))
    return out


def scan_text(path: Path, items, findings, infos) -> None:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        infos.append(f"unreadable: {path} ({e})")
        return
    for lineno, line in enumerate(text.splitlines(), 1):
        for label, needle, is_secret in items:
            # Literals case-insensitively (an ini key or header in any case);
            # secret values exactly.
            hit = (needle in line) if is_secret else (needle.lower() in line.lower())
            if hit:
                findings.append(f"{path}:{lineno}: {label}")


def scan_binary(path: Path, items, findings, infos) -> None:
    patterns = []
    for label, needle, is_secret in items:
        for enc in ("utf-8", "utf-16-le"):
            patterns.append((label, needle.encode(enc), is_secret, enc))
    overlap = max(len(p[1]) for p in patterns)
    is_code = path.suffix.lower() in CODE_SUFFIXES
    try:
        with path.open("rb") as f:
            offset = 0
            tail = b""
            while True:
                block = f.read(CHUNK)
                if not block:
                    break
                data = tail + block
                base = offset - len(tail)
                for label, pat, is_secret, enc in patterns:
                    start = 0
                    while (hit := data.find(pat, start)) != -1:
                        # Skip hits wholly inside the carried-over tail: already reported.
                        if hit + len(pat) > len(tail):
                            where = f"{path}@{base + hit} ({enc}): {label}"
                            if is_code and not is_secret:
                                infos.append(f"code literal (expected) {where}")
                            else:
                                findings.append(where)
                        start = hit + 1
                offset += len(block)
                tail = data[-overlap:]
    except OSError as e:
        infos.append(f"unreadable: {path} ({e})")


def default_targets() -> list[Path]:
    targets = [PROJECT / "Config"]
    staged_data = PROJECT / "Content" / "Data"
    targets.append(staged_data if staged_data.is_dir() else REPO / "shared" / "data")
    staged = PROJECT / "Saved" / "StagedBuilds"
    if staged.is_dir():
        targets.append(staged)
    return targets


def main(argv: list[str]) -> int:
    targets = [Path(a).resolve() for a in argv] or default_targets()
    missing = [t for t in targets if not t.exists()]
    if argv and missing:
        print(f"no such path: {', '.join(map(str, missing))}")
        return 2

    secrets = read_secret_values()
    items = needles(secrets)
    print(f"[audit] secrets.local.env: {'found, checking ' + ', '.join(sorted(secrets)) if secrets else 'not found (checking the literals only)'}")

    findings: list[str] = []
    infos: list[str] = []
    files = 0
    for target in targets:
        if not target.exists():
            print(f"[audit] (skipped, absent) {target}")
            continue
        paths = [target] if target.is_file() else sorted(p for p in target.rglob("*") if p.is_file())
        print(f"[audit] {target}: {len(paths)} file(s)")
        for p in paths:
            files += 1
            if p.suffix.lower() in TEXT_SUFFIXES:
                scan_text(p, items, findings, infos)
            else:
                scan_binary(p, items, findings, infos)

    for line in infos:
        print(f"[audit] INFO {line}")
    for line in findings:
        print(f"[audit] FOUND {line}")
    print(f"[audit] {files} file(s) scanned, {len(findings)} finding(s).")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
