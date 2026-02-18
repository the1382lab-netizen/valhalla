/**
 * TiledMapParser — converts Tiled Map Editor JSON into ParsedMapData.
 * Pure function, no side effects. Used by both server and client.
 *
 * Expected Tiled JSON structure:
 * - Tile layers: "ground", "walls", "decoration", "collision"
 * - Object layers: "spawns", "portals"
 * - Embedded or external tilesets
 */
import { ParsedMapData } from './maps.js';
/**
 * Parse a Tiled JSON map into our game's ParsedMapData format.
 */
export declare function parseTiledMap(json: any): ParsedMapData;
//# sourceMappingURL=TiledMapParser.d.ts.map