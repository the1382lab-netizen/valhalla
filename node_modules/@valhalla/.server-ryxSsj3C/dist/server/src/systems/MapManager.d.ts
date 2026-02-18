/**
 * MapManager — loads, caches, and serves zone map data on the server.
 * Reads Tiled JSON files from the maps/ directory, falls back to
 * FallbackMapGenerator when a file is not found.
 */
import { ParsedMapData, MapDataPayload, SpawnPointData, ZoneConnection } from '@valhalla/shared';
export declare class MapManager {
    private zones;
    /**
     * Load a zone's map data. Reads from disk on first call, caches after that.
     * Merges in editor overlay data (spawn points, zone connections) if available.
     */
    loadZone(zoneId: string): ParsedMapData;
    /**
     * Load and merge an editor overlay file into the parsed map data.
     * Overlay data takes precedence — its spawn points and zone connections
     * replace any that came from the Tiled JSON.
     */
    private mergeOverlay;
    /**
     * Get the collision grid for a zone.
     */
    getCollisionData(zoneId: string): {
        grid: number[];
        width: number;
        height: number;
        tileSize: number;
    };
    /**
     * Get the default player spawn position for a zone.
     */
    getPlayerSpawn(zoneId: string): {
        x: number;
        y: number;
    };
    /**
     * Get all spawn points (for enemy/NPC systems) in a zone.
     */
    getSpawnPoints(zoneId: string): SpawnPointData[];
    /**
     * Get zone connections (portals) for a zone.
     */
    getZoneConnections(zoneId: string): ZoneConnection[];
    /**
     * Build the payload to send to a client when they enter a zone.
     */
    getMapDataForClient(zoneId: string): MapDataPayload;
}
//# sourceMappingURL=MapManager.d.ts.map