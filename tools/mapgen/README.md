# tools/mapgen — Valhalla map generator

Maps are *authored as code* rather than hand-placed in Tiled, because the
things that break a map are exactly the things a script can check: a chamber
sealed behind its own rubble, a spawn inside a wall, a road that stops at the
river. Every build runs those checks and refuses to write a map that fails.

```
python3 build_grasslands_v2.py ../../maps/grasslands_v2.json
python3 build_cave_dungeon.py  ../../maps/cave_dungeon.json
python3 build_overlays.py      ../../maps/overlays
python3 iso_preview.py build_grasslands_v2 grasslands_v2_iso.png
npx tsx checkmaps.ts        # parse through the game's own TiledMapParser
npx tsx checkzones.mts      # load through MapManager, overlay merge included
```

## Files

| file | what it is |
|---|---|
| `tiles.py` | name → (GID, solid). The single source of truth for both the tile ids and the collision layer. |
| `canvas.py` | `MapCanvas`: terrain/road/river/building primitives, derived collision, Tiled-JSON emitter. |
| `build_grasslands_v2.py` | the 128×128 surface map. |
| `build_cave_dungeon.py` | the 64×64 carved dungeon interior. |
| `build_overlays.py` | player/NPC/enemy spawns and every portal, as editor overlays. |
| `validate.py` | flood fill from the spawn; reachability of named points and whole regions. |
| `preview.py` / `iso_preview.py` | top-down and isometric debug renders. |
| `checkmaps.ts` / `checkzones.mts` | run the output through the game's real parser and MapManager. |

## Rules the pipeline exists to enforce

These are properties of the *engine*, discovered by reading it, not conventions
you can choose to follow:

- **GIDs are global and hardcoded.** `TilesetInfo.firstgid` is ignored; the
  client maps a GID straight through `GameScene.GID_TEXTURE_MAP`. Anything
  outside 1–40 renders as `tile_void`. `tiles.py` must stay in step with that
  table.
- **Collision is the layer literally named `collision`.** Any non-zero GID in
  it blocks. The server strips that layer by name before sending tiles to the
  client, so it is never drawn.
- **Layer array order is render order.** No name other than `collision` is ever
  matched.
- **Portals must live in the overlay.** `MapManager.mergeOverlay` replaces
  `zoneConnections` wholesale whenever a `<zone>-overlay.json` exists — even
  with zero portals in it. A portal defined in the Tiled objectgroup is
  silently discarded. Spawns are the same story in practice, so *everything*
  non-tile goes in the overlay and the objectgroup stays empty.
- **Tiled object `type` strings are literal.** `enemy_spawn`, `npc_spawn`,
  `player_spawn`, `item_spawn`, `portal`. The original shipped maps use
  `"spawn"`, which is why none of their Tiled spawns do anything.
- **Base Tiled data is cached permanently.** Editing a `maps/*.json` needs a
  server restart. Overlays hot-reload on mtime.
- **A portal's arrival point** is the `zone_entry` in the *target* zone's
  overlay whose `fromZone`/`templateId` names the source zone; otherwise the
  target's `defaultSpawn`. Put the entry a few tiles clear of the return
  portal's trigger rect or the player bounces straight back.

## Client-side cost

One Phaser sprite per non-zero tile, so 128×128 across three layers is roughly
22k sprites against the old map's 5.5k. `GameScene.cullTiles()` buckets them
into 8×8 tile chunks and toggles whole buckets by camera visibility; the
per-frame work is a few hundred rectangle tests rather than tens of thousands.
