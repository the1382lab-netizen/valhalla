# Publishing a tester build to Cloudflare R2

`publish.cmd` packages the client, zips it and uploads it to the R2 bucket, then
prints a message to send to testers. Testers download the zip by hand for now;
the layout is the first slice of B-17 L2, so the launcher can build on it later.

```
Tools\publish\publish.cmd --notes "Eldmoor patrol fixes"
```

| Option | What it does |
|---|---|
| `--notes "..."` | What changed; goes into the tester message and `latest.json`. |
| `--skip-package` | Publish the build already in `Valhalla2\Saved\Perf\pkg\Windows` instead of packaging again. |
| `--version 0.1.7` | Publish as this version instead of the next one. |
| `--keep 3` | How many versions stay in R2 (default 3). Older ones are deleted, here and locally. |
| `--dry-run` | Everything except the upload and the clean-up. Needs no credentials. |
| `--out-dir D:\Valhalla-Builds` | Where the local copies go (see below). Must be outside the repository. |
| `--where` | Print the builds folder (moving old copies into it) and stop. |
| `--check` | Only test the R2 setup (list, write, public link, delete). Publishes nothing. |

## What it does

1. Packages a Development client with `Tools\perf\package_client.cmd` (game data staged).
2. Picks the version: one more than the highest published, `0.1.1`, `0.1.2`, and so on.
3. Runs `Tools\audit_client_secrets.py` on the build and stops on any finding.
4. Writes `version.json` next to `Valhalla2.exe` (version, commit, build time) and a
   manifest listing every file with its size and SHA-256.
5. Zips the build as `Valhalla-<version>.zip`, leaving out the build's own `Saved\`
   folder (logs can contain login tokens) and the `.pdb` debug symbols, which are kept
   locally in the builds folder under `symbols\<version>\` for crash reports.
6. Uploads to `downloads/` in the bucket: the zip, `<zip>.sha256` and
   `Valhalla-<version>.manifest.json`. A version that is already there is never
   overwritten; the script stops instead.
7. Updates `downloads/latest.json` and `downloads/latest.txt` (sent with
   `Cache-Control: no-cache`, so a download link to them is always current).
8. Deletes versions older than the newest 3, in R2 and in the builds folder.
9. Checks the public link and prints the tester message: version, link, size, SHA-256.

It warns if the working tree has uncommitted or unpushed changes: the game server
must run the same commit as the client (the message prints which one).

## The builds folder (local copies)

Every published version is also kept on this PC, **outside the repository**, so
nothing a build produces can end up in a commit:

```
C:\Users\music\game-project\Valhalla-Builds\     (next to the Valhalla2.0 repo folder)
  0.1.3\Valhalla-0.1.3.zip, .zip.sha256, .manifest.json
  symbols\0.1.3\...\*.pdb                          (for crash reports from that version)
  published.jsonl                                 (every version published from this PC)
```

To keep them somewhere else, add `VALHALLA_BUILDS_DIR=D:\Valhalla-Builds` to
`secrets.local.env` (or pass `--out-dir`). A folder inside the repository is
refused. Builds that an older version of the script left in
`Valhalla2\Saved\Publish` are moved there on the next run.

The packaged client itself is still staged in `Valhalla2\Saved\Perf\pkg` by
`Tools\perf\package_client.cmd` (the benchmarks use it too); `Valhalla2\Saved\`
is gitignored and is only a working copy, overwritten by every package.

## One-time setup

1. **Cloudflare:** R2 → Create bucket `valhalla-builds`. In the bucket, Settings →
   Public access → **R2.dev subdomain → Allow**, and note the public URL
   (`https://pub-….r2.dev`).
2. **Token:** R2 → Manage API tokens → Create API token, permission **Object Read &
   Write**, applied to **valhalla-builds only**. Copy the Access Key ID and Secret
   Access Key (shown once) and your Account ID.
3. **secrets.local.env** (repo root, gitignored):
   ```
   R2_ACCOUNT_ID=...
   R2_ACCESS_KEY_ID=...
   R2_SECRET_ACCESS_KEY=...
   R2_BUCKET=valhalla-builds
   R2_PUBLIC_URL=https://pub-....r2.dev
   ```
4. **boto3:** `py -3 -m pip install boto3` (only the upload needs it).
5. **Test it:** `py -3 Tools\publish\publish_zip.py --check` lists the bucket, writes a
   test file, downloads it through the public link and deletes it again.

## Layout in the bucket

```
downloads/Valhalla-0.1.3.zip
downloads/Valhalla-0.1.3.zip.sha256
downloads/Valhalla-0.1.3.manifest.json
downloads/latest.json        {version, url, size, sha256, commit, builtAt, notes, manifest}
downloads/latest.txt         the same, for people
```

B-17 L2 adds `builds/<version>/files/...` (per-file, for patching) and
`channels/<channel>/latest.json` for the launcher next to `downloads/`; the zips
stay for manual installs. Database backups go to a separate, private bucket (B-17 L5),
never this public one.
