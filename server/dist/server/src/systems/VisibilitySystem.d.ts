/**
 * A point in 2D space.
 */
interface Point {
    x: number;
    y: number;
}
/**
 * VisibilitySystem — computes 2D visibility polygons using raycasting
 * with directional cone support.
 *
 * Approach:
 * 1. Extract wall edge segments from the collision grid (done once at construction)
 * 2. For a given player position and aim direction, gather nearby wall segments
 * 3. Collect unique wall endpoints within the vision cone
 * 4. Cast rays to each endpoint (+ tiny angle offsets to peek around corners)
 * 5. Find the closest wall/boundary intersection for each ray
 * 6. Clamp results to the vision radius
 * 7. Insert the player origin at cone edges, sort by angle, return polygon
 */
export declare class VisibilitySystem {
    private segments;
    private mapW;
    private mapH;
    private tileSize;
    constructor(collisionGrid: number[], mapWidth: number, mapHeight: number, tileSize: number);
    /**
     * Extract wall edge segments from the collision grid.
     */
    private extractWallSegments;
    /**
     * Compute the visibility polygon for a player at (px, py) looking in direction aimAngle.
     *
     * @param px        Player X position
     * @param py        Player Y position
     * @param aimAngle  Direction the player is facing (radians, 0 = right)
     * @param radius    Maximum vision distance in pixels
     * @param coneAngle Total field of view angle in radians (e.g. 100° ≈ 1.745 rad)
     * @returns         Array of {x,y} forming the visibility polygon, sorted by angle.
     */
    computeVisibilityPolygon(px: number, py: number, aimAngle?: number, radius?: number, coneAngle?: number): Point[];
    /**
     * Ray-segment intersection.
     * Returns the intersection point and distance parameter, or null.
     */
    private raySegmentIntersect;
    /**
     * Check if a point is inside a visibility polygon (ray-casting algorithm).
     */
    isPointVisible(polygon: Point[], px: number, py: number): boolean;
    /**
     * Check if a circle (entity) is at least partially visible.
     */
    isCircleVisible(polygon: Point[], cx: number, cy: number, radius: number): boolean;
}
export {};
//# sourceMappingURL=VisibilitySystem.d.ts.map