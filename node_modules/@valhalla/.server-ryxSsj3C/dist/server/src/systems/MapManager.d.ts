/**
 * MapManager — loads, caches, and serves zone map data on the server.
 * Reads Tiled JSON files from the maps/ directory, falls back to
 * FallbackMapGenerator when a file is not found.
 */
import { ParsedMapData, MapDataPayload, SpawnPointData, ZoneConnection } from '@valhalla/shared';
export declare class MapManager {
    /** Tiled-JSON-only parsed data (static tiles, collision, Tiled-defined portals). */
    private baseMaps;
    /** Last-known mtime of each zone's overlay file (0 = no overlay on last check). */
    private overlayMtimes;
    /** Final merged zone data (base + overlay). */
    private zones;
    /**
     * Load a zone's map data.
     *
     * The Tiled JSON is parsed once and cached permanently (it never changes at
     * runtime).  The editor overlay (spawn points, portals placed via the Map
     * Editor) is re-merged whenever its file modification time changes, so
     * portal/spawn changes saved from the editor take effect immediately without
     * a server restart.
     */
    loadZone(zoneId: string): ParsedMapData;
    /** Parse a zone's Tiled JSON from disk (or generate a fallback). */
    private loadTiledData;
    /**
     * Load and merge an editor overlay file into the parsed map data.
     *
     * Portals are managed exclusively by the Map Editor overlay.  When an
     * overlay file exists for a zone, its portal list completely replaces any
     * zone connections that came from the Tiled JSON — even if the overlay has
     * zero portals.  This makes the editor the single source of truth for all
     * portals.  Zones without an overlay file fall back to Tiled-defined portals.
     *
     * Non-portal spawn points (player, enemy, NPC) are merged by ID as before.
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