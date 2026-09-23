# Valhalla 2.0

An old-school (EverQuest-style) MMORPG in Unreal Engine 5.8, with a small Node
backend for accounts and a web editor for game data. Everything lives in this
one repository.

| Folder | What it is |
|---|---|
| `Valhalla2/` | The Unreal project (`Valhalla2.uproject`): game client, dedicated server, editor tools. `Valhalla2/PLAN.md` is the development log. |
| `shared/` | `shared/data/*.json`, the game data (classes, items, skills, NPC templates, loot, zones, UI layout), plus the TypeScript types and loaders the backend and editor share. |
| `server/` | Account backend (Express + SQLite): login, characters, bans, the game server's load/save/verify routes, and `/api/data` for clients. Port 2567. |
| `editor/` | Web game editor + Live Dashboard (Vite on 5180, API on 5181). |
| `maps/` | `overlays-2.0/` (portals, player starts per zone) and `thumbs/` (top-down zone captures). |
| `Import/` | Source art Unreal imports: `.glb` meshes and `UI/Icons` PNGs. |
| `Blender assets/` | `.blend` sources for the kits. |
| `Tools/` | `fixtures/` (stat fixtures the Unreal tests check against) and `unreal-mcp-bridge/`. |

## Running it

```
npm install            # once, at the repo root
npm run dev:server     # account backend on :2567
npm run dev:editor     # web editor on :5180 (API :5181)
npm run smoke          # backend smoke test (throwaway database)
```

Then open `Valhalla2/Valhalla2.uproject` and press Play from `L_FrontEnd`.

The Unreal project reads game data from `../shared/data` (Project Settings →
Game → Valhalla Data → DataRoot). A packaged client downloads the same files
from the backend's `/api/data` after login.

## History

This repository was merged from two on 2026-09-22. The retired 1.0 browser game
(Phaser client, Colyseus game room, Tiled maps, sprite art) is preserved at the
git tag `archive/1.0-final` and on the `dev` branch.
