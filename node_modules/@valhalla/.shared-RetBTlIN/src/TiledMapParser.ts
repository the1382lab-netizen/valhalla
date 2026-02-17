/**
 * TiledMapParser — converts Tiled Map Editor JSON into ParsedMapData.
 * Pure function, no side effects. Used by both server and client.
 *
 * Expected Tiled JSON structure:
 * - Tile layers: "ground", "walls", "decoration", "collision"
 * - Object layers: "spawns", "portals"
 * - Embedded or external tilesets
 */

import {
  ParsedMapData,
  TileLayerInfo,
  TilesetInfo,
  SpawnPointData,
  SpawnPointType,
  ZoneConnection,
  ZoneId,
} from './maps.js';

// ── Tiled JSON type stubs (subset we care about) ────────────

interface TiledMap {
  width: number;
  height: number;
  tilewidth: number;
  tileheight: number;
  layers: TiledLayer[];
  tilesets: TiledTileset[];
}

interface TiledLayer {
  name: string;
  type: 'tilelayer' | 'objectgroup' | 'imagelayer' | 'group';
  data?: number[];
  width?: number;
  height?: number;
  visible: boolean;
  opacity: number;
  objects?: TiledObject[];
}

interface TiledObject {
  id: number;
  name: string;
  type: string;
  x: number;
  y: number;
  width: number;
  height: number;
  properties?: TiledProperty[];
}

interface TiledProperty {
  name: string;
  type: string;
  value: any;
}

interface TiledTileset {
  firstgid: number;
  name: string;
  tilecount: number;
  tilewidth: number;
  tileheight: number;
  tiles?: TiledTile[];
}

interface TiledTile {
  id: number;
  properties?: TiledProperty[];
}

// ── Parser ───────────────────────────────────────────────────

/**
 * Parse a Tiled JSON map into our game's ParsedMapData format.
 */
export function parseTiledMap(json: any): ParsedMapData {
  const map = json as TiledMap;

  const width = map.width;
  const height = map.height;
  const tileSize = map.tilewidth;

  // Parse tilesets
  const tilesets = parseTilesets(map.tilesets ?? []);

  // Separate tile layers from object layers
  const tileLayers: TileLayerInfo[] = [];
  let collisionGrid: number[] = new Array(width * height).fill(0);
  const spawnPoints: SpawnPointData[] = [];
  const zoneConnections: ZoneConnection[] = [];

  for (const layer of map.layers ?? []) {
    if (layer.type === 'tilelayer' && layer.data) {
      // Is this the collision layer?
      if (layer.name.toLowerCase() === 'collision') {
        collisionGrid = layer.data.map((gid) => (gid !== 0 ? 1 : 0));
      }

      // Store all tile layers for client rendering (including collision for debug)
      tileLayers.push({
        name: layer.name,
        data: layer.data,
        width: layer.width ?? width,
        height: layer.height ?? height,
        visible: layer.visible !== false,
        opacity: layer.opacity ?? 1,
      });
    } else if (layer.type === 'objectgroup' && layer.objects) {
      for (const obj of layer.objects) {
        const props = parseProperties(obj.properties);
        const objType = (obj.type || props['type'] || '').toLowerCase();

        if (objType === 'zone_portal' || objType === 'portal') {
          zoneConnections.push({
            id: obj.name || `portal_${obj.id}`,
            triggerRect: {
              x: obj.x,
              y: obj.y,
              width: obj.width,
              height: obj.height,
            },
            targetZone: (props['targetZone'] ?? props['target_zone'] ?? '') as ZoneId,
            targetSpawn: {
              x: Number(props['targetSpawnX'] ?? props['target_spawn_x'] ?? 0),
              y: Number(props['targetSpawnY'] ?? props['target_spawn_y'] ?? 0),
            },
          });
        } else if (isSpawnType(objType)) {
          spawnPoints.push({
            id: obj.name || `spawn_${obj.id}`,
            type: objType as SpawnPointType,
            x: obj.x,
            y: obj.y,
            width: obj.width || undefined,
            height: obj.height || undefined,
            properties: props,
          });
        }
      }
    }
  }

  return {
    width,
    height,
    tileSize,
    collisionGrid,
    spawnPoints,
    zoneConnections,
    tileLayers,
    tilesets,
  };
}

// ── Helpers ──────────────────────────────────────────────────

function parseTilesets(tilesets: TiledTileset[]): TilesetInfo[] {
  return tilesets.map((ts) => {
    const tileProperties: Record<number, Record<string, any>> = {};
    if (ts.tiles) {
      for (const tile of ts.tiles) {
        if (tile.properties) {
          tileProperties[tile.id] = parseProperties(tile.properties);
        }
      }
    }
    return {
      firstGid: ts.firstgid,
      name: ts.name,
      tileCount: ts.tilecount,
      tileWidth: ts.tilewidth,
      tileHeight: ts.tileheight,
      tileProperties,
    };
  });
}

function parseProperties(props?: TiledProperty[]): Record<string, any> {
  const result: Record<string, any> = {};
  if (props) {
    for (const p of props) {
      result[p.name] = p.value;
    }
  }
  return result;
}

const VALID_SPAWN_TYPES = new Set<string>([
  'enemy_spawn',
  'npc_spawn',
  'player_spawn',
  'item_spawn',
]);

function isSpawnType(type: string): boolean {
  return VALID_SPAWN_TYPES.has(type);
}
