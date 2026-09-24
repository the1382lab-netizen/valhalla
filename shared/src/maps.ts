/**
 * Map & Zone types for the Tiled map system.
 * Shared between server and client.
 */

// ── Zone identifiers ─────────────────────────────────────────

export enum ZoneId {
  GRASSLANDS = 'grasslands',
  DESERT = 'desert',
  CAVE_DUNGEON = 'cave_dungeon',
  GRASSLANDS_V2 = 'grasslands_v2',
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

/**
 * B-06: a zone's atmosphere — its fog, light and how far a player can see.
 *
 * Every field is optional, and a missing field (or a missing `atmosphere`
 * object) means "exactly what the world does today": the global L_World
 * lighting, no vision fog, the character's own camera limit and the class
 * vision range for relevancy. The Unreal client blends to a zone's profile over
 * about a second when the local player walks into it; the server uses
 * `netRelevancyRadiusCm` to stop sending actors the player cannot see.
 *
 * Distances are centimetres (1 tile = 64 cm). Colours are `#rrggbb` (sRGB).
 */
export interface ZoneAtmosphere {
  /** Designer note (e.g. "starting values, tune in playtest"). Ignored by the game. */
  notes?: string;
  /** Vision fog: ground distance from the player that stays fully clear. */
  visionClearRadiusCm?: number;
  /** Vision fog: width of the soft fade from clear to fully fogged. */
  visionFadeWidthCm?: number;
  /** Colour of the vision fog, and of the height fog (the horizon when the camera tilts up). */
  fogColor?: string;
  /** ExponentialHeightFog density. Today's global value is 0.012. */
  heightFogDensity?: number;
  /** ExponentialHeightFog start distance from the camera. Today's global value is 1800. */
  heightFogStartCm?: number;
  /** Multiplies the sun's intensity (1 = today; 0 = no sun, e.g. a cave). */
  sunIntensityScale?: number;
  /** Multiplies the sky light and the cool fill light (1 = today; 0 = off). */
  skyLightIntensityScale?: number;
  /** Colour grade tint, multiplied into the post-process colour gain (#ffffff = none). */
  gradeTint?: string;
  /** Furthest the camera may zoom out, boom length in cm (the character's own limit is 2600). */
  cameraMaxArmCm?: number;
  /**
   * Server relevancy: a player is not sent NPCs, players or loot bags further
   * than this from them. Caps the class vision range (1200–1800); never raises
   * it. Should sit at or just above the fully fogged distance
   * (visionClearRadiusCm + visionFadeWidthCm). Aggro range is unaffected.
   */
  netRelevancyRadiusCm?: number;
}

export interface ZoneConfig {
  id: ZoneId;
  name: string;
  mapFile: string;
  defaultSpawn: { x: number; y: number };
  /** B-06: optional per-zone fog, light and sight. See ZoneAtmosphere. */
  atmosphere?: ZoneAtmosphere;
}

/** The numeric ZoneAtmosphere fields, for editors and validators. */
export const ZONE_ATMOSPHERE_NUMBER_FIELDS = [
  'visionClearRadiusCm',
  'visionFadeWidthCm',
  'heightFogDensity',
  'heightFogStartCm',
  'sunIntensityScale',
  'skyLightIntensityScale',
  'cameraMaxArmCm',
  'netRelevancyRadiusCm',
] as const;

/** The colour ZoneAtmosphere fields (`#rrggbb`). */
export const ZONE_ATMOSPHERE_COLOR_FIELDS = ['fogColor', 'gradeTint'] as const;

/**
 * Check a zone's `atmosphere` object. Returns human-readable problems; an
 * empty list means it is valid (a missing atmosphere is valid).
 */
export function validateZoneAtmosphere(atmosphere: unknown): string[] {
  const errors: string[] = [];
  if (atmosphere === undefined) return errors;
  if (atmosphere === null || typeof atmosphere !== 'object' || Array.isArray(atmosphere)) {
    return ['atmosphere must be an object'];
  }
  const a = atmosphere as Record<string, unknown>;
  const known = new Set<string>(['notes', ...ZONE_ATMOSPHERE_NUMBER_FIELDS, ...ZONE_ATMOSPHERE_COLOR_FIELDS]);
  for (const key of Object.keys(a)) {
    if (!known.has(key)) errors.push(`atmosphere.${key} is not a known field`);
  }
  if (a.notes !== undefined && typeof a.notes !== 'string') {
    errors.push('atmosphere.notes must be text');
  }
  for (const key of ZONE_ATMOSPHERE_NUMBER_FIELDS) {
    const v = a[key];
    if (v === undefined) continue;
    if (typeof v !== 'number' || !Number.isFinite(v) || v < 0) {
      errors.push(`atmosphere.${key} must be a number >= 0`);
    }
  }
  for (const key of ZONE_ATMOSPHERE_COLOR_FIELDS) {
    const v = a[key];
    if (v === undefined) continue;
    if (typeof v !== 'string' || !/^#[0-9a-fA-F]{6}$/.test(v)) {
      errors.push(`atmosphere.${key} must be a colour like #8a9486`);
    }
  }
  const clear = a.visionClearRadiusCm;
  const relevancy = a.netRelevancyRadiusCm;
  if (typeof clear === 'number' && clear > 0 && typeof relevancy === 'number' && relevancy > 0) {
    const fade = typeof a.visionFadeWidthCm === 'number' ? a.visionFadeWidthCm : 0;
    if (relevancy < clear + fade) {
      errors.push('atmosphere.netRelevancyRadiusCm is inside the vision fog: actors would pop in where the fog is still see-through');
    }
  }
  return errors;
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
    name: 'Greyfell Cave',
    mapFile: 'cave_dungeon.json',
    // The entrance hall, at the south end of the carved interior. The old
    // (3,3) spawn predates maps/cave_dungeon.json existing at all — that tile
    // is solid rock now.
    defaultSpawn: { x: 32 * 64 + 32, y: 59 * 64 + 32 },
  },
  [ZoneId.GRASSLANDS_V2]: {
    id: ZoneId.GRASSLANDS_V2,
    name: 'Eldmoor Grasslands',
    mapFile: 'grasslands_v2.json',
    // The main street of Eldmoor, just south of the market square.
    defaultSpawn: { x: 34 * 64 + 32, y: 28 * 64 + 32 },
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
