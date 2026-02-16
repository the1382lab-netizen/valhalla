import { TILE_SIZE } from './constants.js';

/**
 * Clamp a value between min and max.
 */
export function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}

/**
 * Convert pixel position to tile coordinate.
 */
export function pixelToTile(px: number): number {
  return Math.floor(px / TILE_SIZE);
}

/**
 * Convert tile coordinate to pixel position (top-left corner of tile).
 */
export function tileToPixel(tile: number): number {
  return tile * TILE_SIZE;
}

/**
 * Check if a tile coordinate is blocked in the collision grid.
 */
export function isTileBlocked(
  grid: number[],
  tileX: number,
  tileY: number,
  mapWidth: number,
  mapHeight: number,
): boolean {
  if (tileX < 0 || tileX >= mapWidth || tileY < 0 || tileY >= mapHeight) {
    return true; // out of bounds = blocked
  }
  return grid[tileY * mapWidth + tileX] === 1;
}

/**
 * Normalise a 2D vector. Returns {x:0, y:0} for zero-length vectors.
 */
export function normalise(x: number, y: number): { x: number; y: number } {
  const len = Math.sqrt(x * x + y * y);
  if (len === 0) return { x: 0, y: 0 };
  return { x: x / len, y: y / len };
}

/**
 * Linear interpolation.
 */
export function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t;
}

/**
 * Distance between two points.
 */
export function distance(x1: number, y1: number, x2: number, y2: number): number {
  const dx = x2 - x1;
  const dy = y2 - y1;
  return Math.sqrt(dx * dx + dy * dy);
}
