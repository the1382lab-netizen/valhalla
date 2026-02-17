import { PLAYER_COLLISION_RADIUS, } from '@valhalla/shared';
/**
 * Server-side collision system.
 * Loads a collision grid (from a parsed map or provided data) and provides
 * circle-vs-tile collision resolution.
 */
export class CollisionSystem {
    constructor(grid, mapW, mapH, tileSize) {
        this.grid = grid;
        this.mapW = mapW;
        this.mapH = mapH;
        this.tileSize = tileSize;
        this.mapWidthPx = mapW * tileSize;
        this.mapHeightPx = mapH * tileSize;
    }
    /**
     * Create a CollisionSystem from a ParsedMapData object.
     */
    static fromParsedMap(mapData) {
        return new CollisionSystem(mapData.collisionGrid, mapData.width, mapData.height, mapData.tileSize);
    }
    /** Get the collision grid (for sharing with clients). */
    getGrid() {
        return this.grid;
    }
    /** Get map dimensions. */
    getMapWidth() { return this.mapW; }
    getMapHeight() { return this.mapH; }
    getTileSize() { return this.tileSize; }
    getMapWidthPx() { return this.mapWidthPx; }
    getMapHeightPx() { return this.mapHeightPx; }
    /**
     * Check if a circle at (cx, cy) with the given radius overlaps any wall tile.
     * Uses AABB of the circle to determine which tiles to check, then
     * does circle-vs-AABB for each wall tile.
     */
    isCircleBlocked(cx, cy, radius = PLAYER_COLLISION_RADIUS) {
        const minTX = Math.floor((cx - radius) / this.tileSize);
        const maxTX = Math.floor((cx + radius) / this.tileSize);
        const minTY = Math.floor((cy - radius) / this.tileSize);
        const maxTY = Math.floor((cy + radius) / this.tileSize);
        for (let ty = minTY; ty <= maxTY; ty++) {
            for (let tx = minTX; tx <= maxTX; tx++) {
                if (tx < 0 || tx >= this.mapW || ty < 0 || ty >= this.mapH)
                    return true;
                if (this.grid[ty * this.mapW + tx] !== 1)
                    continue;
                // Circle vs AABB check
                const tileLeft = tx * this.tileSize;
                const tileTop = ty * this.tileSize;
                const tileRight = tileLeft + this.tileSize;
                const tileBottom = tileTop + this.tileSize;
                const closestX = Math.max(tileLeft, Math.min(cx, tileRight));
                const closestY = Math.max(tileTop, Math.min(cy, tileBottom));
                const dx = cx - closestX;
                const dy = cy - closestY;
                if (dx * dx + dy * dy < radius * radius)
                    return true;
            }
        }
        return false;
    }
    /**
     * Try to move from (x,y) by (dx,dy). Resolves collisions per-axis
     * so the player slides along walls instead of stopping dead.
     */
    resolveMovement(x, y, dx, dy, radius = PLAYER_COLLISION_RADIUS) {
        let newX = x + dx;
        let newY = y + dy;
        // Try full movement
        if (!this.isCircleBlocked(newX, newY, radius)) {
            return { x: newX, y: newY };
        }
        // Try X only
        newX = x + dx;
        newY = y;
        const canMoveX = !this.isCircleBlocked(newX, newY, radius);
        // Try Y only
        newX = x;
        newY = y + dy;
        const canMoveY = !this.isCircleBlocked(newX, newY, radius);
        if (canMoveX && canMoveY) {
            // Both axes work individually — prefer the one with larger movement
            if (Math.abs(dx) >= Math.abs(dy)) {
                return { x: x + dx, y };
            }
            return { x, y: y + dy };
        }
        if (canMoveX)
            return { x: x + dx, y };
        if (canMoveY)
            return { x, y: y + dy };
        return { x, y }; // stuck
    }
    /**
     * Clamp a position to be within the map bounds (accounting for player radius).
     */
    clampToMap(x, y, radius = PLAYER_COLLISION_RADIUS) {
        return {
            x: Math.max(radius, Math.min(this.mapWidthPx - radius, x)),
            y: Math.max(radius, Math.min(this.mapHeightPx - radius, y)),
        };
    }
}
//# sourceMappingURL=CollisionSystem.js.map