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
  ZONE_REGISTRY,
  parseTiledMap,
  generateFallbackMap,
} from '@valhalla/shared';

// Resolve the project root (valhalla/) relative to this file
// server/src/systems/MapManager.ts → three levels up = server/ → one more = valhalla/
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const MAPS_DIR = resolve(__dirname, '..', '..', '..', 'maps');

export class MapManager {
  private zones: Map<string, ParsedMapData> = new Map();

  /**
   * Load a zone's map data. Reads from disk on first call, caches after that.
   */
  loadZone(zoneId: string): ParsedMapData {
    const cached = this.zones.get(zoneId);
    if (cached) return cached;

    const config = ZONE_REGISTRY[zoneId as ZoneId];
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

    this.zones.set(zoneId, mapData);
    return mapData;
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
    const config = ZONE_REGISTRY[zoneId as ZoneId];
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
    const config = ZONE_REGISTRY[zoneId as ZoneId];
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
