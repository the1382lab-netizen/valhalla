/**
 * Isometric coordinate helpers — shared between client and server.
 *
 * The game world is stored in orthogonal (square-grid) coordinates.
 * Only the client renders in isometric screen space.
 *
 * Tile dimensions in Tiled:
 *   ISO tile width  = 128 px (diamond width on screen)
 *   ISO tile height =  64 px (diamond height on screen)
 *   Ortho tile size =  64 px (logical grid cell size used by game logic)
 */
/** Width of an isometric diamond tile on screen (px). */
export const ISO_TILE_WIDTH = 128;
/** Height of an isometric diamond tile on screen (px). */
export const ISO_TILE_HEIGHT = 64;
/** Logical tile size in orthogonal world space (px). */
export const ORTHO_TILE_SIZE = 64;
/**
 * Convert an orthogonal world position to isometric screen position.
 *
 * The standard 2:1 isometric projection:
 *   screenX = worldX − worldY
 *   screenY = (worldX + worldY) / 2
 */
export function orthoToIso(x, y) {
    return { x: x - y, y: (x + y) / 2 };
}
/**
 * Convert an isometric screen position back to orthogonal world position.
 * Inverse of orthoToIso.
 */
export function isoToOrtho(isoX, isoY) {
    return { x: isoX / 2 + isoY, y: -isoX / 2 + isoY };
}
/**
 * Convert a tile grid coordinate (col, row) to isometric screen centre.
 * Each tile occupies ORTHO_TILE_SIZE × ORTHO_TILE_SIZE in world space.
 */
export function tileToIso(tx, ty) {
    const worldX = tx * ORTHO_TILE_SIZE + ORTHO_TILE_SIZE / 2;
    const worldY = ty * ORTHO_TILE_SIZE + ORTHO_TILE_SIZE / 2;
    return orthoToIso(worldX, worldY);
}
//# sourceMappingURL=iso.js.map