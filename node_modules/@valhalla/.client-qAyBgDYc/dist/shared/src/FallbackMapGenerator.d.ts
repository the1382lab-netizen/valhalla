/**
 * FallbackMapGenerator — generates a ParsedMapData programmatically
 * when no Tiled JSON file is available. Useful for development and testing.
 */
import { ParsedMapData } from './maps.js';
/**
 * Generate a fallback map with the given dimensions.
 * Creates a simple outdoor zone with a town area, open fields, some trees, and a lake.
 */
export declare function generateFallbackMap(width?: number, height?: number, tileSize?: number): ParsedMapData;
//# sourceMappingURL=FallbackMapGenerator.d.ts.map