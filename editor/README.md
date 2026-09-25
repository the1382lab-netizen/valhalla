# Valhalla Editor (running against Valhalla 2.0)

React 18 + zustand + Vite editor for the game data in `shared/data` and live server
administration. It talks to a tiny Express file server
(`editor/src/server.ts`) that reads and writes the repo, and proxies `/api/admin/*` to the
running game server.

Since Valhalla 2.0 the world lives in Unreal: zones are UE levels, coordinates are
**zone-local centimetres**, and the Live Dashboard draws on top-down captures of the levels.
Portals, zone entries, player starts and NPC Spawn Points are placed in Unreal and nowhere
else; the dashboard reads them live from the game server's `GET /api/admin/state`.

## Running

```bash
npm run dev:editor          # Vite on :5180 + the file server on :5181
# or, individually
npx vite --config editor/vite.config.ts     # UI
npx tsx editor/src/server.ts                # file server + admin proxy
```

Open http://localhost:5180. Vite proxies `/api` and `/assets` to the file server on 5181.

### Environment

| Variable | Default | What it does |
| --- | --- | --- |
| `VALHALLA_ADMIN_URL` | `http://localhost:2568` | Admin HTTP API the editor proxies `/api/admin/*` to. Point it at the UE dedicated server. |
| `VALHALLA2_IMPORT_DIR` | `../Valhalla 2.0/Import` (relative to the repo root) | UE import folder. `Characters/Equipment/` inside it supplies the valid art ids. |
| `EDITOR_PORT` | `5181` | Port for the file server. |

Both resolved paths and the admin URL are printed when the server starts, so a mis-set env
var is obvious in the log.

The admin API is served by the **UE dedicated server build** (`ValhallaServer`, the target
that includes the admin HTTP module) — the 1.0 Colyseus server on :2567 serves the same
contract but in 1.0 pixel coordinates, so do not point the 2.0 editor at it. When nothing
answers, `/api/admin/*` returns `502 {"error": "...unreachable", "adminUrl": ..., "online": false}`
and the Admin dashboard shows the configured URL in its status bar.

Proxied admin actions (anything else is rejected with 404 by the editor server):
`state`, `spawn-npc`, `drop-item`, `kick-player`, `teleport-player`, `kill-npc`,
`respawn-npc`, `delete-npc`, `reload-data`, `player-action`, `player-inspect`,
`broadcast`, `spawn-point-action`, `account-action`. All coordinates in those
requests are **zone-local cm**.

## Editor server endpoints (2.0)

| Endpoint | Purpose |
| --- | --- |
| `GET /api/config` | Admin URL, import dir, resolved data dirs. |
| `GET /api/thumbs` | Zone ids that have a capture. |
| `GET /api/thumbs/<zone>.png` | The top-down capture (2048², orthographic). |
| `GET /api/thumbs/<zone>.json` | Capture metadata (below). |
| `GET /api/assets/mesh-ids` | Art ids from `Import/Characters/Equipment` (`[]` plus a `warning` when the folder is missing). |

The 1.0 routes (`/api/maps/*`, `/api/overlays/*`, `/api/assets/sprites/*`) and the 2.0
overlay routes (`/api/overlays2/*`) are retired.

### Accounts and validation (B-12, B-13)

| Endpoint | Purpose |
| --- | --- |
| `GET /api/accounts/search`, `GET /api/accounts/detail`, `GET /api/accounts/bans` | Account lookup for the Live Dashboard's **Accounts…** dialog, straight to the backend (`VALHALLA_BACKEND_URL`, default `http://127.0.0.1:2567`) with the server secret, so it works with the game server down. |
| `POST /api/accounts/{ban,unban,reset-password,delete,rename-character}` | The dialog's actions. After a ban, reset, delete or rename the game server (when up) is asked to kick that account's sessions (`account-action` `kick`). |
| `GET /api/validate` | The saved files checked by `shared/src/validation.ts` (same rules as `npm run validate`). The status bar shows the result after every Save. |
| `GET /api/validate/context` | Mesh files, icons and `maps/unreal-refs.json`, for the Validation page, which checks the in-memory (unsaved) data with the same rules. |
| `GET /api/data-sync` | SHA-1 of each data file the running game server loaded (admin `state.data`) against the file on disk: the dashboard's "live data = files" badge next to **Reload data**. |

## Coordinates

`maps/thumbs/<zone>.json`:

```json
{ "originX": 0, "originY": 0, "sizeX": 4096, "sizeY": 4096, "pixelsPerCm": 0.5 }
```

* Image-right is `+X`, image-down is `+Y`.
* A click at image pixel `(px, py)` is zone-local `(px / pixelsPerCm, py / pixelsPerCm)` cm —
  **no origin term**. `originX/originY` only say where the zone sits in UE world space; they
  are not applied to admin coordinates.
* `sizeX/sizeY` are the zone extents in cm, so `sizeX * pixelsPerCm` is the image width.

## Zone actors in the Live Dashboard

`GET /api/admin/state` lists, per zone, the actors placed in the Unreal levels alongside the
players, NPCs, loot bags and NPC Spawn Points (all x/y in zone-local cm):

```jsonc
"grasslands": {
  "zone": { "displayName": "Grasslands", "width": 4096, "height": 4096,
            "defaultSpawn": { "x": 1984, "y": 1024, "yaw": 0 } },
  "portals":      [{ "id": "L_Grasslands.Portal_to_desert", "label": "…", "x": 4000, "y": 864,
                     "targetZoneId": "desert", "targetEntryId": "entry_from_grasslands" }],
  "zoneEntries":  [{ "id": "…", "entryId": "entry_from_desert", "fromZoneId": "desert",
                     "x": 3680, "y": 864, "yaw": 180 }],
  "playerStarts": [{ "id": "…", "tag": "grasslands", "x": 1952, "y": 672, "yaw": 45 }],
  …
}
```

The dashboard draws portals (purple), zone entries (blue) and player starts (cyan; orange
when the start's tag is not this zone) from these, and the Teleport dialog defaults to the
target zone's first tagged player start, else its zone volume's default spawn. To move or
add one, edit the level in Unreal; the dashboard shows the change on the next poll after the
game server restarts (or at once in PIE).

The game server checks the placed actors at start and on `valhalla.CheckZones`: every
portal's target zone and entry exist, entry ids are unique, and every zone has a tagged
player start. `npm run validate` does the same checks on `maps/unreal-refs.json` (written by
`valhalla_tools/export_unreal_refs.py`). The Admin dashboard's **Reload data** button
(`POST /api/admin/reload-data`) re-reads `shared/data` JSON on the server.

## Item art (meshId)

`ItemTemplate.meshId` is optional and defaults to `spriteId`: Unreal's
`FValhallaItemTemplate` resolves the mesh by `meshId` when set, otherwise by `spriteId`,
loading `SK_<id>.glb` or `SM_<id>.glb` from `Import/Characters/Equipment/`. The item editor
offers the available ids as a dropdown with free text, and the validation panel warns when
an equipment item's effective art id is not one of them (the rule is skipped when the list
cannot be read).

## Regenerating a zone capture

In the Unreal editor's Python console, for each zone level:

```python
import ValhallaLevelTools
ValhallaLevelTools.capture_zone_topdown("grasslands")
```

It writes `maps/thumbs/<zone>.png` (2048², orthographic top-down) and
`maps/thumbs/<zone>.json` next to it. Re-run it after moving the level's bounds, then
reload the editor page — the capture and its metadata are served straight from disk.
