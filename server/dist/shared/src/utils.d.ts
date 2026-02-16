/**
 * Clamp a value between min and max.
 */
export declare function clamp(value: number, min: number, max: number): number;
/**
 * Convert pixel position to tile coordinate.
 */
export declare function pixelToTile(px: number): number;
/**
 * Convert tile coordinate to pixel position (top-left corner of tile).
 */
export declare function tileToPixel(tile: number): number;
/**
 * Check if a tile coordinate is blocked in the collision grid.
 */
export declare function isTileBlocked(grid: number[], tileX: number, tileY: number, mapWidth: number, mapHeight: number): boolean;
/**
 * Normalise a 2D vector. Returns {x:0, y:0} for zero-length vectors.
 */
export declare function normalise(x: number, y: number): {
    x: number;
    y: number;
};
/**
 * Linear interpolation.
 */
export declare function lerp(a: number, b: number, t: number): number;
/**
 * Distance between two points.
 */
export declare function distance(x1: number, y1: number, x2: number, y2: number): number;
//# sourceMappingURL=utils.d.ts.map