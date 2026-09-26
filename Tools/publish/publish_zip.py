#!/usr/bin/env python3
"""
Publish a packaged client for testers as a zip on Cloudflare R2 (B-17 L2, first slice).

Testers download the zip by hand for now. Everything here is laid out so the
B-17 launcher can build on it later: every build has a version (0.1.1, 0.1.2,
...), a manifest that lists every file with its size and SHA-256, and published
files are never overwritten. Only the small pointer files latest.json and
latest.txt move.

Steps:
  1. Package the client with Tools/perf/package_client.cmd (skip with --skip-package).
  2. Pick the version: --version, or one more than the highest published so far.
  3. Run Tools/audit_client_secrets.py on the build; stop on any finding.
  4. Write version.json next to Valhalla2.exe and manifest.json beside the zip.
  5. Zip the build as Valhalla-<version>.zip (without Saved/, logs or .pdb symbols;
     the .pdb is kept locally under Valhalla2/Saved/Publish/symbols/<version>/).
  6. Upload to the R2 bucket under downloads/: the zip, <zip>.sha256 and
     Valhalla-<version>.manifest.json. Refuses if that version already exists.
  7. Update downloads/latest.json and downloads/latest.txt (not cached).
  8. Delete older versions, keeping the newest 3 (--keep).
  9. Check the public link, then print a message to send to testers.

--dry-run does steps 1-5 and prints what it would upload; no credentials needed.

Credentials come from <repo>/secrets.local.env (never committed):
  R2_ACCOUNT_ID, R2_ACCESS_KEY_ID, R2_SECRET_ACCESS_KEY, R2_BUCKET, R2_PUBLIC_URL
The token only needs Object Read & Write on that one bucket.

Needs Python 3.10+; the upload also needs boto3 (python -m pip install boto3).
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROJECT = REPO / "Valhalla2"
DEFAULT_BUILD = PROJECT / "Saved" / "Perf" / "pkg" / "Windows"
PUBLISH_DIR = PROJECT / "Saved" / "Publish"
SECRETS_FILE = REPO / "secrets.local.env"
PACKAGE_CMD = REPO / "Tools" / "perf" / "package_client.cmd"
AUDIT_PY = REPO / "Tools" / "audit_client_secrets.py"

PREFIX = "downloads/"
VERSION_BASE = (0, 1)          # versions are 0.1.N
LAUNCH_EXE = "Valhalla2.exe"
CHANNEL = "test"

# Left out of the zip: the build's own runtime folder (logs can hold login tokens),
# debug symbols (kept locally instead) and UAT's bookkeeping files.
EXCLUDE_DIRS = {"Valhalla2/Saved"}
EXCLUDE_SUFFIXES = {".pdb"}
EXCLUDE_NAMES = {"Manifest_DebugFiles_Win64.txt", "Manifest_NonUFSFiles_Win64.txt", "Manifest_UFSFiles_Win64.txt"}

VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
ZIP_RE = re.compile(r"^downloads/Valhalla-(\d+\.\d+\.\d+)\.zip$")

# Cloudflare turns away Python's default User-Agent on r2.dev (error 1010), so
# every public-link request names itself.
USER_AGENT = "Valhalla-publish/1.0 (+Tools/publish)"


def open_url(url: str, method: str = "GET", timeout: int = 30):
    return urllib.request.urlopen(urllib.request.Request(url, method=method, headers={"User-Agent": USER_AGENT}), timeout=timeout)


# ---------------------------------------------------------------- helpers

def say(text: str) -> None:
    print(f"[{dt.datetime.now():%H:%M:%S}] {text}", flush=True)


def fail(text: str) -> "None":
    print(f"\nSTOPPED: {text}", flush=True)
    sys.exit(1)


def read_env_file(path: Path) -> dict[str, str]:
    """KEY=VALUE lines, '#' comments, optional matching quotes (same subset as server/src/loadEnv.ts)."""
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key.strip()] = value
    return values


def parse_version(text: str) -> tuple[int, int, int] | None:
    m = VERSION_RE.match(text.strip())
    return (int(m[1]), int(m[2]), int(m[3])) if m else None


def fmt_version(v: tuple[int, int, int]) -> str:
    return ".".join(str(x) for x in v)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def git(*args: str) -> str:
    try:
        return subprocess.run(["git", "--no-optional-locks", *args], cwd=REPO, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def human(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return str(n)


def included(rel: str) -> bool:
    if rel in EXCLUDE_NAMES:
        return False
    if any(rel == d or rel.startswith(d + "/") for d in EXCLUDE_DIRS):
        return False
    return Path(rel).suffix.lower() not in EXCLUDE_SUFFIXES


# ---------------------------------------------------------------- R2

class R2:
    """The few S3 calls this script needs, against Cloudflare R2."""

    def __init__(self, env: dict[str, str]):
        try:
            import boto3  # noqa: F401
            from boto3.s3.transfer import TransferConfig
        except ImportError:
            fail("boto3 is not installed. Run:  python -m pip install boto3")
        import boto3
        from boto3.s3.transfer import TransferConfig
        self.bucket = env["R2_BUCKET"]
        self.client = boto3.client(
            "s3",
            endpoint_url=f"https://{env['R2_ACCOUNT_ID']}.r2.cloudflarestorage.com",
            aws_access_key_id=env["R2_ACCESS_KEY_ID"],
            aws_secret_access_key=env["R2_SECRET_ACCESS_KEY"],
            region_name="auto",
        )
        self.transfer = TransferConfig(multipart_threshold=64 * 1024 * 1024, multipart_chunksize=64 * 1024 * 1024, max_concurrency=4)

    def exists(self, key: str) -> bool:
        try:
            self.client.head_object(Bucket=self.bucket, Key=key)
            return True
        except Exception as e:  # botocore ClientError 404
            code = getattr(e, "response", {}).get("Error", {}).get("Code", "")
            if code in ("404", "NoSuchKey", "NotFound"):
                return False
            raise

    def keys(self, prefix: str) -> list[str]:
        out: list[str] = []
        token = None
        while True:
            kw = {"Bucket": self.bucket, "Prefix": prefix}
            if token:
                kw["ContinuationToken"] = token
            page = self.client.list_objects_v2(**kw)
            out += [o["Key"] for o in page.get("Contents", [])]
            if not page.get("IsTruncated"):
                return out
            token = page.get("NextContinuationToken")

    def upload(self, path: Path, key: str, content_type: str, cache: str | None = None) -> None:
        extra = {"ContentType": content_type}
        if cache:
            extra["CacheControl"] = cache
        self.client.upload_file(str(path), self.bucket, key, ExtraArgs=extra, Config=self.transfer)

    def put_text(self, key: str, text: str, content_type: str, cache: str) -> None:
        self.client.put_object(Bucket=self.bucket, Key=key, Body=text.encode("utf-8"), ContentType=content_type, CacheControl=cache)

    def delete(self, keys: list[str]) -> None:
        for i in range(0, len(keys), 1000):
            self.client.delete_objects(Bucket=self.bucket, Delete={"Objects": [{"Key": k} for k in keys[i:i + 1000]]})


# ---------------------------------------------------------------- steps

def package() -> None:
    say("Packaging the client (Tools/perf/package_client.cmd, a few minutes)...")
    proc = subprocess.run(["cmd.exe", "/c", str(PACKAGE_CMD)], cwd=REPO, capture_output=True, text=True, errors="replace")
    tail = "\n".join(proc.stdout.strip().splitlines()[-15:])
    if "PACKAGE-RC=0" not in proc.stdout or "BUILD SUCCESSFUL" not in proc.stdout:
        print(tail)
        fail("packaging did not succeed (see the lines above; the full output is in Valhalla2/Saved/Logs).")
    say("Packaged.")


def published_versions(r2: R2 | None, public_url: str) -> set[tuple[int, int, int]]:
    found: set[tuple[int, int, int]] = set()
    if r2:
        for key in r2.keys(PREFIX):
            m = ZIP_RE.match(key)
            if m and (v := parse_version(m[1])):
                found.add(v)
    elif public_url:
        try:
            with open_url(f"{public_url}/{PREFIX}latest.json", timeout=15) as resp:
                v = parse_version(json.load(resp).get("version", ""))
                if v:
                    found.add(v)
        except (urllib.error.URLError, ValueError, OSError):
            pass
    history = PUBLISH_DIR / "published.jsonl"
    if history.is_file():
        for line in history.read_text(encoding="utf-8").splitlines():
            try:
                v = parse_version(json.loads(line).get("version", ""))
            except ValueError:
                v = None
            if v:
                found.add(v)
    return found


def next_version(existing: set[tuple[int, int, int]]) -> tuple[int, int, int]:
    same_line = [v for v in existing if v[:2] == VERSION_BASE]
    return (*VERSION_BASE, (max(same_line)[2] + 1) if same_line else 1)


def audit(build: Path) -> None:
    say("Checking the build for secrets (Tools/audit_client_secrets.py)...")
    proc = subprocess.run([sys.executable, str(AUDIT_PY), str(build)], cwd=REPO, capture_output=True, text=True, errors="replace")
    summary = [l for l in proc.stdout.splitlines() if "finding" in l.lower()]
    if proc.returncode != 0:
        print(proc.stdout[-3000:])
        fail("the secrets check found something in the build. Nothing was published.")
    say("Secrets check: " + (summary[-1].strip().rstrip(".") if summary else "clean") + ".")


def collect(build: Path) -> list[Path]:
    files = []
    for p in sorted(build.rglob("*")):
        if p.is_file() and included(p.relative_to(build).as_posix()):
            files.append(p)
    return files


def check_setup(env: dict[str, str], public_url: str) -> None:
    """Test the R2 setup without publishing: list, write, read back publicly, delete."""
    r2 = R2(env)
    say(f"Bucket {r2.bucket}: listing {PREFIX}...")
    try:
        keys = r2.keys(PREFIX)
    except Exception as e:
        fail(f"could not list the bucket ({e}). Check R2_ACCOUNT_ID, the token's keys and that it covers this bucket.")
    versions = sorted({fmt_version(v) for k in keys if (m := ZIP_RE.match(k)) and (v := parse_version(m[1]))})
    say(f"Listing works: {len(keys)} object(s) under {PREFIX}; published versions: {', '.join(versions) or 'none yet'}.")
    key = PREFIX + ".publish-check.txt"
    stamp = f"publish check {dt.datetime.now(dt.timezone.utc).isoformat()}"
    try:
        r2.put_text(key, stamp, "text/plain; charset=utf-8", "no-cache, max-age=0")
    except Exception as e:
        fail(f"could not write to the bucket ({e}). The token needs Object Read & Write on {r2.bucket}.")
    say("Writing works.")
    try:
        with open_url(f"{public_url}/{key}") as resp:
            body = resp.read().decode("utf-8", "replace")
        if body.strip() == stamp:
            say("Public link works: the test file downloads through R2_PUBLIC_URL.")
        else:
            say("WARNING: the public link answered, but with different content. Check R2_PUBLIC_URL.")
    except (urllib.error.URLError, OSError) as e:
        say(f"WARNING: the public link did not work ({e}). Turn on R2.dev public access for the bucket and check R2_PUBLIC_URL.")
    finally:
        try:
            r2.delete([key])
            say("Deleting works; test file removed.")
        except Exception as e:
            say(f"WARNING: could not delete the test file {key} ({e}).")
    say("R2 setup check finished.")


def main() -> None:
    ap = argparse.ArgumentParser(description="Zip a packaged client and publish it to R2 for testers.")
    ap.add_argument("--skip-package", action="store_true", help="publish the build already in the build folder")
    ap.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD, help=f"packaged Windows folder (default {DEFAULT_BUILD.relative_to(REPO)})")
    ap.add_argument("--version", help="version to publish, e.g. 0.1.4 (default: one more than the highest published)")
    ap.add_argument("--notes", default="", help="what changed, shown to testers")
    ap.add_argument("--keep", type=int, default=3, help="how many versions to keep in R2 (default 3)")
    ap.add_argument("--dry-run", action="store_true", help="do everything except upload and delete")
    ap.add_argument("--check", action="store_true", help="only test the R2 setup: credentials, bucket, upload, public link; publishes nothing")
    args = ap.parse_args()

    env = {**read_env_file(SECRETS_FILE), **{k: v for k, v in os.environ.items() if k.startswith("R2_")}}
    public_url = env.get("R2_PUBLIC_URL", "").rstrip("/")
    need = ["R2_ACCOUNT_ID", "R2_ACCESS_KEY_ID", "R2_SECRET_ACCESS_KEY", "R2_BUCKET", "R2_PUBLIC_URL"]
    missing = [k for k in need if not env.get(k)]
    if missing and not args.dry_run:
        fail("missing in secrets.local.env: " + ", ".join(missing) + " (see Tools/publish/README.md).")
    if args.keep < 1:
        fail("--keep must be at least 1.")
    if args.check:
        check_setup(env, public_url)
        return

    # Git state: the game server must run the same commit as this client.
    commit = git("rev-parse", "HEAD")
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    ahead = git("rev-list", "--count", f"origin/{branch}..HEAD") if branch else ""
    say(f"Commit {commit[:8]} on {branch}" + (" (uncommitted changes!)" if dirty else "") + (f" ({ahead} commit(s) not pushed)" if ahead not in ("", "0") else ""))
    if dirty:
        say("WARNING: tracked files have uncommitted changes, so this build does not match any commit exactly.")

    r2 = None if args.dry_run or missing else R2(env)

    if args.version:
        version = parse_version(args.version)
        if not version:
            fail(f"--version must look like 0.1.4, not {args.version!r}.")
    else:
        version = next_version(published_versions(r2, public_url))
    ver = fmt_version(version)
    zip_name = f"Valhalla-{ver}.zip"
    zip_key = PREFIX + zip_name
    say(f"Version {ver}")
    if r2 and r2.exists(zip_key):
        fail(f"{zip_key} is already in the bucket. Published versions are never overwritten; pick another --version.")

    if not args.skip_package:
        package()
    build = args.build_dir.resolve()
    if not (build / LAUNCH_EXE).is_file():
        fail(f"no {LAUNCH_EXE} in {build}. Package first, or point --build-dir at a packaged Windows folder.")

    audit(build)

    built_at = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    version_info = {"version": ver, "channel": CHANNEL, "commit": commit, "branch": branch, "dirty": dirty, "builtAt": built_at}
    (build / "version.json").write_text(json.dumps(version_info, indent=2) + "\n", encoding="utf-8")

    out = PUBLISH_DIR / ver
    out.mkdir(parents=True, exist_ok=True)

    # Keep the debug symbols locally for crash reports from this version.
    for pdb in build.rglob("*.pdb"):
        dest = PUBLISH_DIR / "symbols" / ver / pdb.relative_to(build)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(pdb, dest)

    files = collect(build)
    say(f"Hashing {len(files)} files...")
    entries = []
    for p in files:
        rel = p.relative_to(build).as_posix()
        entries.append({"path": rel, "sha256": sha256_file(p), "size": p.stat().st_size})
    total = sum(e["size"] for e in entries)
    manifest = {**version_info, "launchExe": LAUNCH_EXE, "notes": args.notes, "files": entries}
    manifest_path = out / f"Valhalla-{ver}.manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    zip_path = out / zip_name
    say(f"Zipping {human(total)} into {zip_path} (a few minutes)...")
    root = f"Valhalla-{ver}/"
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
        for p in files:
            z.write(p, root + p.relative_to(build).as_posix())
    zip_sha = sha256_file(zip_path)
    zip_size = zip_path.stat().st_size
    sha_path = out / (zip_name + ".sha256")
    sha_path.write_text(f"{zip_sha}  {zip_name}\n", encoding="utf-8")
    say(f"Zip: {human(zip_size)}, SHA-256 {zip_sha}")

    zip_url = f"{public_url}/{zip_key}" if public_url else f"<R2_PUBLIC_URL>/{zip_key}"
    latest = {"version": ver, "zip": zip_name, "url": zip_url, "size": zip_size, "sha256": zip_sha,
              "commit": commit, "builtAt": built_at, "notes": args.notes,
              "manifest": f"{public_url}/{PREFIX}Valhalla-{ver}.manifest.json" if public_url else ""}
    latest_txt = (f"Valhalla {ver}\nDownload: {zip_url}\nSize: {human(zip_size)}\nSHA-256: {zip_sha}\n"
                  f"Built: {built_at} from commit {commit[:8]}\n" + (f"Notes: {args.notes}\n" if args.notes else ""))

    if args.dry_run or r2 is None:
        say("Dry run: nothing uploaded. It would upload:")
        for k in (zip_key, zip_key + ".sha256", f"{PREFIX}Valhalla-{ver}.manifest.json", f"{PREFIX}latest.json", f"{PREFIX}latest.txt"):
            print(f"    {k}")
        print(f"  and keep the newest {args.keep} version(s) in {PREFIX}.")
        print(f"\nLocal files: {out}")
        return

    say(f"Uploading {zip_name} ({human(zip_size)}) to R2...")
    r2.upload(zip_path, zip_key, "application/zip")
    r2.upload(sha_path, zip_key + ".sha256", "text/plain; charset=utf-8")
    r2.upload(manifest_path, f"{PREFIX}Valhalla-{ver}.manifest.json", "application/json")
    no_cache = "no-cache, max-age=0"
    r2.put_text(f"{PREFIX}latest.json", json.dumps(latest, indent=2) + "\n", "application/json", no_cache)
    r2.put_text(f"{PREFIX}latest.txt", latest_txt, "text/plain; charset=utf-8", no_cache)
    say("Uploaded.")

    # Keep the newest N versions; delete every file of the older ones.
    by_version: dict[tuple[int, int, int], list[str]] = {}
    for key in r2.keys(PREFIX + "Valhalla-"):
        m = re.match(r"^downloads/Valhalla-(\d+\.\d+\.\d+)\.", key)
        if m and (v := parse_version(m[1])):
            by_version.setdefault(v, []).append(key)
    old = sorted(by_version)[:-args.keep] if len(by_version) > args.keep else []
    if old:
        r2.delete([k for v in old for k in by_version[v]])
        say("Removed old version(s): " + ", ".join(fmt_version(v) for v in old))

    # The public link must work before anyone is told about it.
    try:
        with open_url(zip_url, method="HEAD") as resp:
            length = int(resp.headers.get("Content-Length", "0"))
        say("Public link checked." if length == zip_size else f"WARNING: the public link reports {length} bytes, expected {zip_size}.")
    except (urllib.error.URLError, OSError) as e:
        say(f"WARNING: could not open the public link ({e}). Check that R2.dev public access is on for the bucket.")

    # Local copies follow the same rule: zips and symbols of versions no longer
    # in R2 are removed from Valhalla2/Saved/Publish.
    kept = set(sorted(by_version)[-args.keep:]) | {version}
    for parent in (PUBLISH_DIR, PUBLISH_DIR / "symbols"):
        if parent.is_dir():
            for d in parent.iterdir():
                v = parse_version(d.name) if d.is_dir() else None
                if v and v not in kept:
                    shutil.rmtree(d, ignore_errors=True)

    with (PUBLISH_DIR / "published.jsonl").open("a", encoding="utf-8") as h:
        h.write(json.dumps({"version": ver, "sha256": zip_sha, "size": zip_size, "commit": commit, "builtAt": built_at, "url": zip_url}) + "\n")

    print("\n" + "=" * 70)
    print("Message for testers:\n")
    print(f"Valhalla {ver} is ready." + (f" {args.notes}" if args.notes else ""))
    print(f"Download: {zip_url}")
    print(f"Size: {human(zip_size)}. Unzip it anywhere and run Valhalla2.exe.")
    print(f"SHA-256 (optional check): {zip_sha}")
    print("=" * 70)
    print(f"\nThe game server must run commit {commit[:8]} (the same code as this build).")


if __name__ == "__main__":
    main()
