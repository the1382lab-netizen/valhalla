import { ParsedMapData } from '@valhalla/shared';
/**
 * Server-side collision system.
 * Loads a collision grid (from a parsed map or provided data) and provides
 * circle-vs-tile collision resolution.
 */
export declare class CollisionSystem {
    private grid;
    private mapW;
    private mapH;
    private tileSize;
    private mapWidthPx;
    private mapHeightPx;
    constructor(grid: number[], mapW: number, mapH: number, tileSize: number);
    /**
     * Create a CollisionSystem from a ParsedMapData object.
     */
    static fromParsedMap(mapData: ParsedMapData): CollisionSystem;
    /** Get the collision grid (for sharing with clients). */
    getGrid(): number[];
    /** Get map dimensions. */
    getMapWidth(): number;
    getMapHeight(): number;
    getTileSize(): number;
    getMapWidthPx(): number;
    getMapHeightPx(): number;
    /**
     * Check if a circle at (cx, cy) with the given radius overlaps any wall tile.
     * Uses AABB of the circle to determine which tiles to check, then
     * does circle-vs-AABB for each wall tile.
     */
    isCircleBlocked(cx: number, cy: number, radius?: number): boolean;
    /**
     * Try to move from (x,y) by (dx,dy). Resolves collisions per-axis
     * so the player slides along walls instead of stopping dead.
     */
    resolveMovement(x: number, y: number, dx: number, dy: number, radius?: number): {
        x: number;
        y: number;
    };
    /**
     * Clamp a position to be within the map bounds (accounting for player radius).
     */
    clampToMap(x: number, y: number, radius?: number): {
        x: number;
        y: number;
    };
}
//# sourceMappingURL=CollisionSystem.d.ts.map