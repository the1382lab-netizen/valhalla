/**
 * MapManager — loads, caches, and serves zone map data on the server.
 * Reads Tiled JSON files from the maps/ directory, falls back to
 * FallbackMapGenerator when a file is not found.
 */
import { readFileSync, existsSync, statSync } from 'fs';
import { resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import { parseTiledMap, generateFallbackMap, } from '@valhalla/shared';
import { DataManager } from './DataManager.js';
// Resolve the project root (valhalla/) relative to this file
// server/src/systems/MapManager.ts → three levels up = server/ → one more = valhalla/
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const MAPS_DIR = resolve(__dirname, '..', '..', '..', 'maps');
export class MapManager {
    constructor() {
        /** Tiled-JSON-only parsed data (static tiles, collision, Tiled-defined portals). */
        this.baseMaps = new Map();
        /** Last-known mtime of each zone's overlay file (0 = no overlay on last check). */
        this.overlayMtimes = new Map();
        /** Final merged zone data (base + overlay). */
        this.zones = new Map();
    }
    /**
     * Load a zone's map data.
     *
     * The Tiled JSON is parsed once and cached permanently (it never changes at
     * runtime).  The editor overlay (spawn points, portals placed via the Map
     * Editor) is re-merged whenever its file modification time changes, so
     * portal/spawn changes saved from the editor take effect immediately without
     * a server restart.
     */
    loadZone(zoneId) {
        const overlayPath = resolve(MAPS_DIR, 'overlays', `${zoneId}-overlay.json`);
        const overlayMtime = existsSync(overlayPath)
            ? statSync(overlayPath).mtimeMs
            : 0;
        const cachedMtime = this.overlayMtimes.get(zoneId) ?? -1;
        const cached = this.zones.get(zoneId);
        // Return cached result if the overlay file hasn't changed since last load.
        if (cached && overlayMtime === cachedMtime)
            return cached;
        // Load (or retrieve cached) Tiled base data.
        let base = this.baseMaps.get(zoneId);
        if (!base) {
            base = this.loadTiledData(zoneId);
            this.baseMaps.set(zoneId, base);
        }
        // Deep-clone the base so that mergeOverlay's mutations don't corrupt it.
        const mapData = structuredClone(base);
        // Merge editor overlay data (spawn points and zone connections).
        this.mergeOverlay(zoneId, mapData);
        this.zones.set(zoneId, mapData);
        this.overlayMtimes.set(zoneId, overlayMtime);
        return mapData;
    }
    /** Parse a zone's Tiled JSON from disk (or generate a fallback). */
    loadTiledData(zoneId) {
        const config = DataManager.instance.zones[zoneId];
        if (config) {
            const filePath = resolve(MAPS_DIR, config.mapFile);
            if (existsSync(filePath)) {
                try {
                    const json = JSON.parse(readFileSync(filePath, 'utf-8'));
                    const data = parseTiledMap(json);
                    console.log(`[MapManager] Parsed Tiled data for zone "${zoneId}" from ${config.mapFile} (${data.width}×${data.height})`);
                    return data;
                }
                catch (err) {
                    console.warn(`[MapManager] Failed to parse ${config.mapFile}, using fallback:`, err);
                }
            }
            else {
                console.warn(`[MapManager] Map file not found for zone "${zoneId}": ${filePath}, using fallback`);
            }
        }
        else {
            console.warn(`[MapManager] Unknown zone "${zoneId}", using fallback map`);
        }
        return generateFallbackMap();
    }
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
    mergeOverlay(zoneId, mapData) {
        const overlayPath = resolve(MAPS_DIR, 'overlays', `${zoneId}-overlay.json`);
        if (!existsSync(overlayPath))
            return;
        try {
            const overlay = JSON.parse(readFileSync(overlayPath, 'utf-8'));
            const spawnPoints = Array.isArray(overlay.spawnPoints) ? overlay.spawnPoints : [];
            // Separate portal objects from regular spawn points.
            // The editor stores portals as spawnPoints with type "portal"; the server
            // needs them as ZoneConnection objects.
            const regularSpawns = [];
            const portalConnections = [];
            for (const sp of spawnPoints) {
                if (sp.type === 'portal') {
                    const targetZone = sp.templateId || sp.label || '';
                    if (!targetZone) {
                        console.warn(`[MapManager] Portal "${sp.id}" in "${zoneId}" has no target zone, skipping`);
                        continue;
                    }
                    // Resolve the arrival position for the target zone.
                    // Priority: zone_entry marker in target zone's overlay that references us (fromZone / templateId === zoneId)
                    // Fallback: target zone's defaultSpawn from the DataManager registry.
                    const targetConfig = DataManager.instance.zones[targetZone];
                    let targetSpawn = targetConfig?.defaultSpawn ?? { x: 160, y: 160 };
                    const targetOverlayPath = resolve(MAPS_DIR, 'overlays', `${targetZone}-overlay.json`);
                    if (existsSync(targetOverlayPath)) {
                        try {
                            const targetOverlay = JSON.parse(readFileSync(targetOverlayPath, 'utf-8'));
                            const targetSpawnPoints = Array.isArray(targetOverlay.spawnPoints)
                                ? targetOverlay.spawnPoints : [];
                            const entryPoint = targetSpawnPoints.find((tsp) => tsp.type === 'zone_entry' &&
                                (tsp.fromZone === zoneId || tsp.templateId === zoneId));
                            if (entryPoint) {
                                targetSpawn = { x: entryPoint.x, y: entryPoint.y };
                                console.log(`[MapManager] Portal "${sp.id}" → "${targetZone}": using zone_entry at (${entryPoint.x}, ${entryPoint.y})`);
                            }
                        }
                        catch {
                            // Fall back to defaultSpawn silently
                        }
                    }
                    // The Map Editor saves portal (x, y) as the top-left corner of the
                    // trigger rect (matching how ctx.fillRect draws it).
                    portalConnections.push({
                        id: sp.id,
                        triggerRect: {
                            x: sp.x,
                            y: sp.y,
                            width: sp.width || 64,
                            height: sp.height || 64,
                        },
                        targetZone: targetZone,
                        targetSpawn,
                    });
                }
                else {
                    regularSpawns.push(sp);
                }
            }
            // Merge regular spawn points (non-portal) — overlay entries win by ID.
            if (regularSpawns.length > 0) {
                const overlayIds = new Set(regularSpawns.map((sp) => sp.id));
                const tiledOnly = mapData.spawnPoints.filter(sp => !overlayIds.has(sp.id));
                mapData.spawnPoints = [...tiledOnly, ...regularSpawns];
                console.log(`[MapManager] Merged ${regularSpawns.length} overlay spawn points for "${zoneId}"`);
            }
            // Portals: overlay takes FULL authority — completely replace any Tiled
            // zone connections.  This ensures the Map Editor is the single source of
            // truth.  (If the overlay has zero portals, all Tiled portals are cleared.)
            mapData.zoneConnections = portalConnections;
            console.log(`[MapManager] Overlay set ${portalConnections.length} portal(s) for "${zoneId}" (Tiled portals replaced)`);
        }
        catch (err) {
            console.warn(`[MapManager] Failed to load overlay for "${zoneId}":`, err);
        }
    }
    /**
     * Get the collision grid for a zone.
     */
    getCollisionData(zoneId) {
        const map = this.loadZone(zoneId);
        return {
            grid: map.collisionGrid,
            width: map.width,
            height: map.height,
            tileSize: map.tileSize,
        };
    }
    /**
     * Get the default player spawn position for a zone.
     */
    getPlayerSpawn(zoneId) {
        const map = this.loadZone(zoneId);
        // Look for a player_spawn object in the map
        const spawnPoint = map.spawnPoints.find((sp) => sp.type === 'player_spawn');
        if (spawnPoint) {
            return { x: spawnPoint.x, y: spawnPoint.y };
        }
        // Fall back to zone registry default
        const config = DataManager.instance.zones[zoneId];
        if (config) {
            return config.defaultSpawn;
        }
        // Ultimate fallback
        return { x: map.tileSize * 5 + map.tileSize / 2, y: map.tileSize * 5 + map.tileSize / 2 };
    }
    /**
     * Get all spawn points (for enemy/NPC systems) in a zone.
     */
    getSpawnPoints(zoneId) {
        return this.loadZone(zoneId).spawnPoints;
    }
    /**
     * Get zone connections (portals) for a zone.
     */
    getZoneConnections(zoneId) {
        return this.loadZone(zoneId).zoneConnections;
    }
    /**
     * Build the payload to send to a client when they enter a zone.
     */
    getMapDataForClient(zoneId) {
        const map = this.loadZone(zoneId);
        const config = DataManager.instance.zones[zoneId];
        const spawn = this.getPlayerSpawn(zoneId);
        // Filter out the collision layer from visual layers (client doesn't need it for rendering)
        const visualLayers = map.tileLayers.filter((l) => l.name.toLowerCase() !== 'collision');
        return {
            zoneId,
            zoneName: config?.name ?? zoneId,
            width: map.width,
            height: map.height,
            tileSize: map.tileSize,
            orientation: map.orientation,
            collisionGrid: map.collisionGrid,
            tileLayers: visualLayers,
            tilesets: map.tilesets,
            playerSpawn: spawn,
        };
    }
}
//# sourceMappingURL=MapManager.js.map