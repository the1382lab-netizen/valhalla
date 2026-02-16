/**
 * Server-side collision system.
 * Loads a collision grid and provides circle-vs-tile collision resolution.
 */
export declare class CollisionSystem {
    private grid;
    private mapW;
    private mapH;
    constructor();
    /**
     * Build a simple collision grid: walls around the perimeter,
     * plus a few interior walls for testing.
     */
    private buildDefaultCollisionGrid;
    /** Get the collision grid (for sharing with clients). */
    getGrid(): number[];
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