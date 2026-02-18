/**
 * MapManager — loads, caches, and serves zone map data on the server.
 * Reads Tiled JSON files from the maps/ directory, falls back to
 * FallbackMapGenerator when a file is not found.
 */

import { readFileSync, existsSync } from 'fs';
import { resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import {
  ParsedMapData,
  MapDataPayload,
  SpawnPointData,
  ZoneConnection,
  ZoneId,
  parseTiledMap,
  generateFallbackMap,
} from '@valhalla/shared';
import { DataManager } from './DataManager.js';

// Resolve the project root (valhalla/) relative to this file
// server/src/systems/MapManager.ts → three levels up = server/ → one more = valhalla/
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const MAPS_DIR = resolve(__dirname, '..', '..', '..', 'maps');

export class MapManager {
  private zones: Map<string, ParsedMapData> = new Map();

  /**
   * Load a zone's map data. Reads from disk on first call, caches after that.
   * Merges in editor overlay data (spawn points, zone connections) if available.
   */
  loadZone(zoneId: string): ParsedMapData {
    const cached = this.zones.get(zoneId);
    if (cached) return cached;

    const config = DataManager.instance.zones[zoneId];
    let mapData: ParsedMapData;

    if (config) {
      const filePath = resolve(MAPS_DIR, config.mapFile);
      if (existsSync(filePath)) {
        try {
          const json = JSON.parse(readFileSync(filePath, 'utf-8'));
          mapData = parseTiledMap(json);
          console.log(`[MapManager] Loaded zone "${zoneId}" from ${config.mapFile} (${mapData.width}×${mapData.height})`);
        } catch (err) {
          console.warn(`[MapManager] Failed to parse ${config.mapFile}, using fallback:`, err);
          mapData = generateFallbackMap();
        }
      } else {
        console.warn(`[MapManager] Map file not found: ${filePath}, using fallback`);
        mapData = generateFallbackMap();
      }
    } else {
      console.warn(`[MapManager] Unknown zone "${zoneId}", using fallback map`);
      mapData = generateFallbackMap();
    }

    // Merge editor overlay data (spawn points and zone connections)
    this.mergeOverlay(zoneId, mapData);

    this.zones.set(zoneId, mapData);
    return mapData;
  }

  /**
   * Load and merge an editor overlay file into the parsed map data.
   * Overlay data takes precedence — its spawn points and zone connections
   * replace any that came from the Tiled JSON.
   */
  private mergeOverlay(zoneId: string, mapData: ParsedMapData): void {
    const overlayPath = resolve(MAPS_DIR, 'overlays', `${zoneId}-overlay.json`);
    if (!existsSync(overlayPath)) return;

    try {
      const overlay = JSON.parse(readFileSync(overlayPath, 'utf-8'));

      if (Array.isArray(overlay.spawnPoints) && overlay.spawnPoints.length > 0) {
        // Separate portal objects from regular spawn points.
        // The editor stores portals in spawnPoints with type "portal",
        // but the server needs them as ZoneConnection objects in zoneConnections.
        const regularSpawns: SpawnPointData[] = [];
        const portalConnections: ZoneConnection[] = [];

        for (const sp of overlay.spawnPoints) {
          if (sp.type === 'portal') {
            // Convert portal spawn point → ZoneConnection
            const targetZone = sp.templateId || sp.label || '';
            if (!targetZone) {
              console.warn(`[MapManager] Portal "${sp.id}" in "${zoneId}" has no target zone, skipping`);
              continue;
            }

            // Look up the target zone's default spawn for the arrival position
            const targetConfig = DataManager.instance.zones[targetZone];
            const targetSpawn = targetConfig?.defaultSpawn ?? { x: 160, y: 160 };

            portalConnections.push({
              id: sp.id,
              triggerRect: {
                x: sp.x - (sp.width || 64) / 2,
                y: sp.y - (sp.height || 64) / 2,
                width: sp.width || 64,
                height: sp.height || 64,
              },
              targetZone: targetZone as ZoneId,
              targetSpawn,
            });
          } else {
            regularSpawns.push(sp);
          }
        }

        // Merge regular spawn points (non-portal)
        if (regularSpawns.length > 0) {
          const overlayIds = new Set(regularSpawns.map((sp: any) => sp.id));
          const tiledOnly = mapData.spawnPoints.filter(sp => !overlayIds.has(sp.id));
          mapData.spawnPoints = [...tiledOnly, ...regularSpawns];
          console.log(`[MapManager] Merged ${regularSpawns.length} overlay spawn points for "${zoneId}"`);
        }

        // Merge portal connections
        if (portalConnections.length > 0) {
          const portalIds = new Set(portalConnections.map(zc => zc.id));
          const tiledOnly = mapData.zoneConnections.filter(zc => !portalIds.has(zc.id));
          mapData.zoneConnections = [...tiledOnly, ...portalConnections];
          console.log(`[MapManager] Merged ${portalConnections.length} overlay portals for "${zoneId}"`);
        }
      }

      // Also merge explicit zoneConnections if present in overlay
      if (Array.isArray(overlay.zoneConnections) && overlay.zoneConnections.length > 0) {
        const overlayPortalIds = new Set(overlay.zoneConnections.map((zc: any) => zc.id));
        const tiledOnly = mapData.zoneConnections.filter(zc => !overlayPortalIds.has(zc.id));
        mapData.zoneConnections = [...tiledOnly, ...overlay.zoneConnections];

        console.log(`[MapManager] Merged ${overlay.zoneConnections.length} overlay zone connections for "${zoneId}"`);
      }
    } catch (err) {
      console.warn(`[MapManager] Failed to load overlay for "${zoneId}":`, err);
    }
  }

  /**
   * Get the collision grid for a zone.
   */
  getCollisionData(zoneId: string): { grid: number[]; width: number; height: number; tileSize: number } {
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
  getPlayerSpawn(zoneId: string): { x: number; y: number } {
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
  getSpawnPoints(zoneId: string): SpawnPointData[] {
    return this.loadZone(zoneId).spawnPoints;
  }

  /**
   * Get zone connections (portals) for a zone.
   */
  getZoneConnections(zoneId: string): ZoneConnection[] {
    return this.loadZone(zoneId).zoneConnections;
  }

  /**
   * Build the payload to send to a client when they enter a zone.
   */
  getMapDataForClient(zoneId: string): MapDataPayload {
    const map = this.loadZone(zoneId);
    const config = DataManager.instance.zones[zoneId];
    const spawn = this.getPlayerSpawn(zoneId);

    // Filter out the collision layer from visual layers (client doesn't need it for rendering)
    const visualLayers = map.tileLayers.filter(
      (l) => l.name.toLowerCase() !== 'collision',
    );

    return {
      zoneId,
      zoneName: config?.name ?? zoneId,
      width: map.width,
      height: map.height,
      tileSize: map.tileSize,
      collisionGrid: map.collisionGrid,
      tileLayers: visualLayers,
      tilesets: map.tilesets,
      playerSpawn: spawn,
    };
  }
}
