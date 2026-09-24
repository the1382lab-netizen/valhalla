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
 * vision range for line of sight and relevancy. The Unreal client blends to a
 * zone's profile over about a second when the local player walks into it.
 *
 * Vision is relative to each player (see resolveZoneVision): the zone scales
 * the player's class vision range (classes.json `visionRange`), so a ranger
 * still sees further than a wizard in the mist.
 *
 * Distances are centimetres (1 tile = 64 cm). Colours are `#rrggbb` (sRGB).
 */
export interface ZoneAtmosphere {
  /** Designer note (e.g. "starting values, tune in playtest"). Ignored by the game. */
  notes?: string;
  /**
   * Vision fog on, and how far each player sees in this zone: their class
   * vision range x this = their effective range, where the fog is fully
   * opaque. Absent = no vision fog and the class range unchanged.
   * E.g. 1.3333 turns a 12 m class into 16 m.
   */
  visionScale?: number;
  /** The part of the effective range that stays clear (0 to <1). Default 0.625. */
  visionClearFraction?: number;
  /**
   * The server sends NPCs, players and loot bags out to the effective range
   * plus this margin (line of sight and net relevancy). Default 100.
   * Aggro range is unaffected.
   */
  relevancyMarginCm?: number;
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
   * How strongly fires, braziers and lamp posts (lights tagged by
   * fire_lights.py / lamp_lights.py, or `ValhallaBeacon`) glow through the
   * vision fog. 1 = default, 0 = off. Only matters with vision fog.
   */
  firelightGlow?: number;
  /** Glows fade out beyond this ground distance from the player. Default 1.5 x the player's effective range. */
  firelightRangeCm?: number;
}

/** Defaults for the relative vision fields (same numbers as FValhallaAtmosphereProfile). */
export const DEFAULT_VISION_CLEAR_FRACTION = 0.625;
export const DEFAULT_RELEVANCY_MARGIN_CM = 100;
/** classes.json `visionRange` when a class has none (Valhalla::DefaultVisionRange). */
export const DEFAULT_CLASS_VISION_RANGE_CM = 1200;

/** One player's vision in a zone; every distance comes from the effective range. */
export interface ZoneVision {
  /** The player's own range: class vision range (x buff/race modifiers, later). */
  baseRangeCm: number;
  /** base x visionScale: fully fogged here; the hide and target limit. */
  effectiveRangeCm: number;
  /** Clear out to here: effective x visionClearFraction. */
  clearRadiusCm: number;
  /** Server line of sight and relevancy: effective + relevancyMarginCm. */
  relevancyRangeCm: number;
  hasVisionFog: boolean;
}

/**
 * The vision formula — the TypeScript twin of the game's
 * ValhallaAtmosphere::ResolveVision (ValhallaZoneAtmosphere.h). Used by the
 * web editor's per-class preview; keep the two in step.
 */
export function resolveZoneVision(classRangeCm: number | undefined, atmosphere: ZoneAtmosphere | undefined): ZoneVision {
  const base = classRangeCm && classRangeCm > 0 ? classRangeCm : DEFAULT_CLASS_VISION_RANGE_CM;
  const scale = atmosphere?.visionScale ?? 0;
  if (!(scale > 0)) {
    return { baseRangeCm: base, effectiveRangeCm: base, clearRadiusCm: base, relevancyRangeCm: base, hasVisionFog: false };
  }
  const effective = Math.max(1, base * scale);
  const fraction = Math.min(0.99, Math.max(0, atmosphere?.visionClearFraction ?? DEFAULT_VISION_CLEAR_FRACTION));
  const margin = Math.max(0, atmosphere?.relevancyMarginCm ?? DEFAULT_RELEVANCY_MARGIN_CM);
  return {
    baseRangeCm: base,
    effectiveRangeCm: effective,
    clearRadiusCm: effective * fraction,
    relevancyRangeCm: effective + margin,
    hasVisionFog: true,
  };
}

export interface ZoneConfig {
  id: ZoneId;
  name: string;
  mapFile: string;
  defaultSpawn: { x: number; y: number };
  /** B-06: optional per-zone fog, light and sight. See ZoneAtmosphere. */
  atmosphere?: ZoneAtmosphere;
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
