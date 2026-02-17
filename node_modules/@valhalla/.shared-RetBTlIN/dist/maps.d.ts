/**
 * Map & Zone types for the Tiled map system.
 * Shared between server and client.
 */
export declare enum ZoneId {
    GRASSLANDS = "grasslands",
    CAVE_DUNGEON = "cave_dungeon"
}
export interface ZoneConnection {
    /** Unique id for this portal (from Tiled object name) */
    id: string;
    /** Rectangle trigger area in pixel space */
    triggerRect: {
        x: number;
        y: number;
        width: number;
        height: number;
    };
    /** Destination zone */
    targetZone: ZoneId;
    /** Spawn position in the target zone (pixels) */
    targetSpawn: {
        x: number;
        y: number;
    };
}
export interface ZoneConfig {
    id: ZoneId;
    name: string;
    mapFile: string;
    defaultSpawn: {
        x: number;
        y: number;
    };
}
/** Registry of all zones. */
export declare const ZONE_REGISTRY: Record<ZoneId, ZoneConfig>;
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
export interface TileLayerInfo {
    name: string;
    data: number[];
    width: number;
    height: number;
    visible: boolean;
    opacity: number;
}
export interface TilesetInfo {
    firstGid: number;
    name: string;
    tileCount: number;
    tileWidth: number;
    tileHeight: number;
    /** Maps local tile ID → custom properties */
    tileProperties: Record<number, Record<string, any>>;
}
export interface ParsedMapData {
    width: number;
    height: number;
    tileSize: number;
    collisionGrid: number[];
    spawnPoints: SpawnPointData[];
    zoneConnections: ZoneConnection[];
    tileLayers: TileLayerInfo[];
    tilesets: TilesetInfo[];
}
export interface MapDataPayload {
    zoneId: string;
    zoneName: string;
    width: number;
    height: number;
    tileSize: number;
    collisionGrid: number[];
    tileLayers: TileLayerInfo[];
    tilesets: TilesetInfo[];
    /** Only spawn points the client needs (player_spawn) */
    playerSpawn: {
        x: number;
        y: number;
    };
}
//# sourceMappingURL=maps.d.ts.map