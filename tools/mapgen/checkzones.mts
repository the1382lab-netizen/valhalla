/**
 * Load the new zones the way the server does — base Tiled JSON plus the editor
 * overlay — and print what the client would actually receive.
 *   npx tsx tools/mapgen/checkzones.mts
 */
import { DataManager } from '../../server/src/systems/DataManager.js';
import { MapManager } from '../../server/src/systems/MapManager.js';

DataManager.initialize();

const mm = new MapManager();
for (const zone of ['grasslands_v2', 'cave_dungeon', 'grasslands']) {
  const d: any = mm.loadZone(zone);
  const payload: any = (mm as any).getMapDataPayload?.(zone) ?? null;
  console.log('\n==', zone, '==');
  console.log('  size          ', `${d.width}x${d.height}`);
  console.log('  spawnPoints   ', d.spawnPoints.length,
    d.spawnPoints.reduce((a: any, s: any) => (a[s.type] = (a[s.type] || 0) + 1, a), {}));
  console.log('  zoneConnections', d.zoneConnections.map(
    (c: any) => `${c.targetZone}@(${c.triggerRect.x},${c.triggerRect.y}) -> (${c.targetSpawn.x},${c.targetSpawn.y})`));
  if (payload) console.log('  client layers ', payload.tileLayers.map((l: any) => l.name));
}
