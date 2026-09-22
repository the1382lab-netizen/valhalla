# Valhalla Editor (running against Valhalla 2.0)

React 18 + zustand + Vite editor for the game data in `shared/data`, the map overlays in
`maps/`, and live server administration. It talks to a tiny Express file server
(`editor/src/server.ts`) that reads and writes the repo, and proxies `/api/admin/*` to the
running game server.

Since Valhalla 2.0 the world lives in Unreal: zones are UE levels, coordinates are
**zone-local centimetres**, and the editor draws on top-down captures of the levels instead
of Tiled maps. The 1.0 editors still work unchanged for the old game.

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
`respawn-npc`, `delete-npc`, `reload-overlays`, `reload-data`. All coordinates in those
requests are **zone-local cm**.

## Editor server endpoints (2.0)

| Endpoint | Purpose |
| --- | --- |
| `GET /api/config` | Admin URL, import dir, resolved data dirs. |
| `GET /api/thumbs` | Zone ids that have a capture. |
| `GET /api/thumbs/<zone>.png` | The top-down capture (2048², orthographic). |
| `GET /api/thumbs/<zone>.json` | Capture metadata (below). |
| `GET /api/overlays2/<zone>` | Overlay 2.0; returns an empty skeleton when the file does not exist. |
| `PUT /api/overlays2/<zone>` | Validates then writes `maps/overlays-2.0/<zone>.json`; non-`2.0` documents are rejected with `400 {error, errors[]}`. |
| `GET /api/assets/mesh-ids` | Art ids from `Import/Characters/Equipment` (`[]` plus a `warning` when the folder is missing). |

The 1.0 routes (`/api/data/*`, `/api/maps/*`, `/api/overlays/*`, `/api/assets/sprites/*`) are
unchanged.

## Coordinates

`maps/thumbs/<zone>.json`:

```json
{ "originX": 0, "originY": 0, "sizeX": 4096, "sizeY": 4096, "pixelsPerCm": 0.5 }
```

* Image-right is `+X`, image-down is `+Y`.
* A click at image pixel `(px, py)` is zone-local `(px / pixelsPerCm, py / pixelsPerCm)` cm —
  **no origin term**. `originX/originY` only say where the zone sits in UE world space; they
  are not applied to overlay or admin coordinates.
* `sizeX/sizeY` are the zone extents in cm, so `sizeX * pixelsPerCm` is the image width.

## Overlay 2.0 schema

`maps/overlays-2.0/<zone>.json`:

```jsonc
{
  "version": "2.0",
  "units": "cm",
  "zoneId": "grasslands",
  "spawnPoints": [
    { "id": "player_spawn_grasslands_0", "type": "player_spawn", "x": 1952, "y": 672, "label": "…" },
    { "id": "enemy_field_west", "type": "enemy_spawn", "x": 672, "y": 1440,
      "templateId": "npc_1771431708366", "count": 3, "radius": 320 },
    { "id": "portal_to_desert", "type": "portal", "x": 4000, "y": 864,
      "targetZone": "desert", "targetEntry": "entry_from_grasslands" },
    { "id": "entry_from_desert", "type": "zone_entry", "x": 3680, "y": 864, "fromZone": "desert" }
  ]
}
```

* `type`: `player_spawn` | `enemy_spawn` | `npc_spawn` | `portal` | `zone_entry`.
* `x`/`y`: zone-local cm. `radius` is cm, `count` is a positive integer.
* `templateId` is required for `enemy_spawn` / `npc_spawn`, `targetZone` for `portal`,
  `fromZone` for `zone_entry`. Ids must be unique inside a file.

How the UE server uses it: enemy and NPC spawns are **created from this file**; player
spawns, portals and zone entries are **validated against actors placed in the level** and
must match within **128 cm**. Moving a portal in the editor therefore also needs the level
author to move the matching actor in UE. The map editor's status line repeats this, and the
128 cm box is what it draws around portals and entries.

The UE server loads the overlays at start and on the console command
`valhalla.ReloadOverlays`; the map editor's **Reload in game** button does the same thing
over HTTP (`POST /api/admin/reload-overlays`). The Admin dashboard's **Reload data** button
(`POST /api/admin/reload-data`) re-reads `shared/data` JSON on the server.

## Map editor modes

The Maps section defaults to **2.0** for any zone that has a capture and falls back to **1.0**
otherwise; the toolbar switch overrides that and the choice is remembered in `localStorage`
(`valhalla.mapEditor.mode`). 2.0 mode draws the capture, works in cm and writes overlay 2.0;
1.0 mode is the old Tiled-map editor writing `maps/overlays/<zone>-overlay.json` in pixels.

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
