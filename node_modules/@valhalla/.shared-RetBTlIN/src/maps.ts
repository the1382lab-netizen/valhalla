/**
 * Map & Zone types for the Tiled map system.
 * Shared between server and client.
 */

// ── Zone identifiers ─────────────────────────────────────────

export enum ZoneId {
  GRASSLANDS = 'grasslands',
  DESERT = 'desert',
  CAVE_DUNGEON = 'cave_dungeon',
}

// ── Zone configuration ───────────────────────────────────────

export interface ZoneConnection {
  /** Unique id for this portal (from Tiled object name) */
  id: string;
  /** Rectangle trigger area in pixel space */
  triggerRect: { x: number; y: number; width: number; height: number };
  /** Destination zone */
  targetZone: ZoneId;
  /** Spawn position in the target zone (pixels) */
  targetSpawn: { x: number; y: number };
}

export interface ZoneConfig {
  id: ZoneId;
  name: string;
  mapFile: string;
  defaultSpawn: { x: number; y: number };
}

/** Registry of all zones. */
export const ZONE_REGISTRY: Record<ZoneId, ZoneConfig> = {
  [ZoneId.GRASSLANDS]: {
    id: ZoneId.GRASSLANDS,
    name: 'Grasslands',
    mapFile: 'grasslands.json',
    defaultSpawn: { x: 10 * 64 + 32, y: 10 * 64 + 32 },
  },
  [ZoneId.DESERT]: {
    id: ZoneId.DESERT,
    name: 'Scorched Desert',
    mapFile: 'desert.json',
    defaultSpawn: { x: 2 * 64 + 32, y: 57 * 64 + 32 },
  },
  [ZoneId.CAVE_DUNGEON]: {
    id: ZoneId.CAVE_DUNGEON,
    name: 'Cave Dungeon',
    mapFile: 'cave_dungeon.json',
    defaultSpawn: { x: 3 * 64 + 32, y: 3 * 64 + 32 },
  },
};

// ── Spawn point data (from Tiled object layers) ──────────────

export type SpawnPointType = 'enemy_spawn' | 'npc_spawn' | 'player_spawn' | 'item_spawn';

export interface SpawnPointData {
  id: string;
  type: SpawnPointType;
  x: number;
  y: number;
  width?: number;
  height?: number;
  properties: Record<string, any>;
}

// ── Tile layer info (for client rendering) ───────────────────

export interface TileLayerInfo {
  name: string;
  data: number[];
  width: number;
  height: number;
  visible: boolean;
  opacity: number;
}

// ── Tileset info (for GID → texture mapping) ─────────────────

export interface TilesetInfo {
  firstGid: number;
  name: string;
  tileCount: number;
  tileWidth: number;
  tileHeight: number;
  /** Maps local tile ID → custom properties */
  tileProperties: Record<number, Record<string, any>>;
}

// ── Parsed map data (output of TiledMapParser) ──────────────

export interface ParsedMapData {
  width: number;
  height: number;
  tileSize: number;
  orientation: 'orthogonal' | 'isometric';
  collisionGrid: number[];
  spawnPoints: SpawnPointData[];
  zoneConnections: ZoneConnection[];
  tileLayers: TileLayerInfo[];
  tilesets: TilesetInfo[];
}

// ── Map data sent to client over network ─────────────────────

export interface MapDataPayload {
  zoneId: string;
  zoneName: string;
  width: number;
  height: number;
  tileSize: number;
  orientation: 'orthogonal' | 'isometric';
  collisionGrid: number[];
  tileLayers: TileLayerInfo[];
  tilesets: TilesetInfo[];
  /** Only spawn points the client needs (player_spawn) */
  playerSpawn: { x: number; y: number };
}
