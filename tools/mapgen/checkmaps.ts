/**
 * Parse the generated maps through the game's own TiledMapParser.
 * A map that python is happy with can still be one the server drops on the
 * floor, so the check has to run the real parser.
 *   npx tsx tools/mapgen/checkmaps.ts
 */
import { readFileSync } from 'fs';
import { resolve } from 'path';
import { parseTiledMap } from '../../shared/src/TiledMapParser';

const ROOT = resolve(__dirname, '../..');

for (const [zone, file] of [
  ['grasslands_v2', 'maps/grasslands_v2.json'],
  ['cave_dungeon', 'maps/cave_dungeon.json'],
] as const) {
  const raw = JSON.parse(readFileSync(resolve(ROOT, file), 'utf-8'));
  const d: any = parseTiledMap(raw, zone as any);
  console.log(zone, {
    size: `${d.width}x${d.height}`,
    tileSize: d.tileSize,
    orientation: d.orientation,
    tileLayers: d.tileLayers.map((l: any) => l.name),
    solid: d.collisionGrid.filter((g: number) => g).length,
    spawnPoints: d.spawnPoints.length,
    zoneConnections: d.zoneConnections.length,
  });
}
