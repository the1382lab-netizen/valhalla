import { MAP_WIDTH_PX, MAP_HEIGHT_PX, FOG_EXPLORED_ALPHA, FOG_HIDDEN_ALPHA, } from '@valhalla/shared';
/**
 * FogOfWar — renders a fog overlay on the game world.
 *
 * Uses two layers:
 * 1. "Explored" texture — permanent record of everywhere the player has been.
 *    Once an area is seen, it becomes "explored" and stays dimly visible.
 * 2. "Active vision" — the current visibility polygon rendered every frame.
 *    This is fully transparent (no fog) within the polygon.
 *
 * The final composite:
 * - Never seen: dark fog (FOG_HIDDEN_ALPHA)
 * - Previously explored but not currently visible: dim fog (FOG_EXPLORED_ALPHA)
 * - Currently visible: no fog (fully transparent)
 */
export class FogOfWar {
    scene;
    // The fog overlay rendered each frame
    fogGraphics;
    // Tracked explored areas (set of tile coordinates that have been seen)
    exploredTiles = new Set();
    // Current visibility polygon from the server
    currentPolygon = [];
    visiblePlayers = new Set();
    visibleProjectiles = new Set();
    // Smoothing: interpolate between polygon updates
    targetPolygon = [];
    lerpFactor = 0;
    constructor(scene) {
        this.scene = scene;
        // Create the fog graphics object (drawn each frame)
        this.fogGraphics = scene.add.graphics();
        this.fogGraphics.setDepth(50); // above entities, below UI
    }
    /**
     * Update the visibility data from the server.
     */
    updateVisibility(data) {
        this.targetPolygon = data.polygon;
        this.visiblePlayers = new Set(data.visiblePlayers);
        this.visibleProjectiles = new Set(data.visibleProjectiles);
        // Mark tiles within the polygon as explored
        this.markExploredTiles(data.polygon);
        // Snap to new polygon (smooth interpolation is optional enhancement)
        this.currentPolygon = data.polygon;
    }
    /**
     * Mark tiles covered by the visibility polygon as explored.
     */
    markExploredTiles(polygon) {
        if (polygon.length < 3)
            return;
        // Find bounding box of the polygon
        let minX = Infinity, maxX = -Infinity;
        let minY = Infinity, maxY = -Infinity;
        for (const p of polygon) {
            if (p.x < minX)
                minX = p.x;
            if (p.x > maxX)
                maxX = p.x;
            if (p.y < minY)
                minY = p.y;
            if (p.y > maxY)
                maxY = p.y;
        }
        // Convert to tile coordinates and check each tile
        const tileSize = 64;
        const startTX = Math.max(0, Math.floor(minX / tileSize));
        const endTX = Math.min(Math.floor(MAP_WIDTH_PX / tileSize) - 1, Math.floor(maxX / tileSize));
        const startTY = Math.max(0, Math.floor(minY / tileSize));
        const endTY = Math.min(Math.floor(MAP_HEIGHT_PX / tileSize) - 1, Math.floor(maxY / tileSize));
        for (let ty = startTY; ty <= endTY; ty++) {
            for (let tx = startTX; tx <= endTX; tx++) {
                // Check if tile center is inside the polygon
                const cx = tx * tileSize + tileSize / 2;
                const cy = ty * tileSize + tileSize / 2;
                if (this.isPointInPolygon(cx, cy, polygon)) {
                    this.exploredTiles.add(`${tx},${ty}`);
                }
            }
        }
    }
    /**
     * Point-in-polygon test (ray casting algorithm).
     */
    isPointInPolygon(px, py, polygon) {
        let inside = false;
        const n = polygon.length;
        for (let i = 0, j = n - 1; i < n; j = i++) {
            const xi = polygon[i].x, yi = polygon[i].y;
            const xj = polygon[j].x, yj = polygon[j].y;
            const intersect = (yi > py) !== (yj > py) &&
                px < ((xj - xi) * (py - yi)) / (yj - yi) + xi;
            if (intersect)
                inside = !inside;
        }
        return inside;
    }
    /**
     * Check if a player is currently visible.
     */
    isPlayerVisible(sessionId) {
        return this.visiblePlayers.has(sessionId);
    }
    /**
     * Check if a projectile is currently visible.
     */
    isProjectileVisible(projectileId) {
        return this.visibleProjectiles.has(projectileId);
    }
    /**
     * Render the fog of war overlay.
     * Called each frame from GameScene.update().
     */
    render() {
        this.fogGraphics.clear();
        if (this.currentPolygon.length < 3) {
            // No visibility data yet — render full fog
            this.fogGraphics.fillStyle(0x000000, FOG_HIDDEN_ALPHA);
            this.fogGraphics.fillRect(0, 0, MAP_WIDTH_PX, MAP_HEIGHT_PX);
            return;
        }
        // Strategy: Draw the fog as a large rectangle with the visibility polygon cut out.
        // Phaser's Graphics doesn't support true masking easily, so we use an approach where
        // we draw the fog tile-by-tile with different alphas.
        const tileSize = 64;
        const tilesW = Math.ceil(MAP_WIDTH_PX / tileSize);
        const tilesH = Math.ceil(MAP_HEIGHT_PX / tileSize);
        // Get the camera viewport to only render visible tiles
        const cam = this.scene.cameras.main;
        const camLeft = cam.scrollX - tileSize;
        const camRight = cam.scrollX + cam.width + tileSize;
        const camTop = cam.scrollY - tileSize;
        const camBottom = cam.scrollY + cam.height + tileSize;
        const startTX = Math.max(0, Math.floor(camLeft / tileSize));
        const endTX = Math.min(tilesW - 1, Math.floor(camRight / tileSize));
        const startTY = Math.max(0, Math.floor(camTop / tileSize));
        const endTY = Math.min(tilesH - 1, Math.floor(camBottom / tileSize));
        for (let ty = startTY; ty <= endTY; ty++) {
            for (let tx = startTX; tx <= endTX; tx++) {
                const cx = tx * tileSize + tileSize / 2;
                const cy = ty * tileSize + tileSize / 2;
                const isVisible = this.isPointInPolygon(cx, cy, this.currentPolygon);
                if (isVisible) {
                    // Currently visible — no fog
                    continue;
                }
                const isExplored = this.exploredTiles.has(`${tx},${ty}`);
                if (isExplored) {
                    // Explored but not currently visible — dim fog
                    this.fogGraphics.fillStyle(0x000000, FOG_EXPLORED_ALPHA);
                }
                else {
                    // Never seen — dark fog
                    this.fogGraphics.fillStyle(0x000000, FOG_HIDDEN_ALPHA);
                }
                this.fogGraphics.fillRect(tx * tileSize, ty * tileSize, tileSize, tileSize);
            }
        }
    }
    /**
     * Clean up resources.
     */
    destroy() {
        this.fogGraphics.destroy();
    }
}
//# sourceMappingURL=FogOfWar.js.map