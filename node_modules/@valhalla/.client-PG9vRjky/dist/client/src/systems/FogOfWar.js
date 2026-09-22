import { orthoToIso } from '@valhalla/shared';
import { ENTITY_DEPTH_BASE } from './EntityRenderer.js';
// These constants aren't in shared yet — define locally until the FoW system is fully integrated
const FOG_EXPLORED_ALPHA = 0.5;
const FOG_HIDDEN_ALPHA = 0.95;
/** FogOfWar depth sits above entities but below UI */
const FOG_DEPTH = ENTITY_DEPTH_BASE + 100_000;
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
 *
 * NOTE: The visibility polygon from the server is in **orthogonal** world space.
 * Tile iteration and point-in-polygon tests all operate in ortho space.
 * Only the final fog-tile rendering converts to ISO screen coords.
 */
export class FogOfWar {
    scene;
    // The fog overlay rendered each frame
    fogGraphics;
    // Tracked explored areas (set of tile coordinates that have been seen)
    exploredTiles = new Set();
    // Current visibility polygon from the server (ortho world space)
    currentPolygon = [];
    visiblePlayers = new Set();
    visibleProjectiles = new Set();
    // Smoothing: interpolate between polygon updates
    targetPolygon = [];
    lerpFactor = 0;
    // Dynamic map dimensions
    tileSize;
    mapWidthTiles;
    mapHeightTiles;
    mapWidthPx;
    mapHeightPx;
    isIso;
    constructor(scene, tileSize, mapWidthTiles, mapHeightTiles, isIso = false) {
        this.scene = scene;
        this.tileSize = tileSize;
        this.mapWidthTiles = mapWidthTiles;
        this.mapHeightTiles = mapHeightTiles;
        this.mapWidthPx = mapWidthTiles * tileSize;
        this.mapHeightPx = mapHeightTiles * tileSize;
        this.isIso = isIso;
        // Create the fog graphics object (drawn each frame)
        this.fogGraphics = scene.add.graphics();
        this.fogGraphics.setDepth(FOG_DEPTH);
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
     * Polygon is in orthogonal world space.
     */
    markExploredTiles(polygon) {
        if (polygon.length < 3)
            return;
        // Find bounding box of the polygon (ortho space)
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
        const ts = this.tileSize;
        const startTX = Math.max(0, Math.floor(minX / ts));
        const endTX = Math.min(this.mapWidthTiles - 1, Math.floor(maxX / ts));
        const startTY = Math.max(0, Math.floor(minY / ts));
        const endTY = Math.min(this.mapHeightTiles - 1, Math.floor(maxY / ts));
        for (let ty = startTY; ty <= endTY; ty++) {
            for (let tx = startTX; tx <= endTX; tx++) {
                // Check if tile center is inside the polygon (ortho coords)
                const cx = tx * ts + ts / 2;
                const cy = ty * ts + ts / 2;
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
        const ts = this.tileSize;
        if (this.currentPolygon.length < 3) {
            // No visibility data yet — render full fog
            // For ISO maps we need to cover the full iso screen area
            if (this.isIso) {
                this.renderFullFogIso(FOG_HIDDEN_ALPHA);
            }
            else {
                this.fogGraphics.fillStyle(0x000000, FOG_HIDDEN_ALPHA);
                this.fogGraphics.fillRect(0, 0, this.mapWidthPx, this.mapHeightPx);
            }
            return;
        }
        // Get the camera viewport to only render visible tiles
        const cam = this.scene.cameras.main;
        const camLeft = cam.scrollX - ts * 2;
        const camRight = cam.scrollX + cam.width + ts * 2;
        const camTop = cam.scrollY - ts * 2;
        const camBottom = cam.scrollY + cam.height + ts * 2;
        for (let ty = 0; ty < this.mapHeightTiles; ty++) {
            for (let tx = 0; tx < this.mapWidthTiles; tx++) {
                // Tile center in ortho space
                const orthoCx = tx * ts + ts / 2;
                const orthoCy = ty * ts + ts / 2;
                // Screen position (ISO or ortho)
                let screenX, screenY;
                if (this.isIso) {
                    const iso = orthoToIso(orthoCx, orthoCy);
                    screenX = iso.x;
                    screenY = iso.y;
                }
                else {
                    screenX = orthoCx;
                    screenY = orthoCy;
                }
                // Cull tiles outside camera viewport
                if (screenX + ts < camLeft || screenX - ts > camRight ||
                    screenY + ts < camTop || screenY - ts > camBottom) {
                    continue;
                }
                // Visibility test in ortho space
                const isVisible = this.isPointInPolygon(orthoCx, orthoCy, this.currentPolygon);
                if (isVisible) {
                    // Currently visible — no fog
                    continue;
                }
                const isExplored = this.exploredTiles.has(`${tx},${ty}`);
                const alpha = isExplored ? FOG_EXPLORED_ALPHA : FOG_HIDDEN_ALPHA;
                if (this.isIso) {
                    // Draw diamond-shaped fog tile for ISO
                    this.fogGraphics.fillStyle(0x000000, alpha);
                    const halfW = ts; // ISO tile half-width = ortho tileSize
                    const halfH = ts / 2; // ISO tile half-height
                    this.fogGraphics.beginPath();
                    this.fogGraphics.moveTo(screenX, screenY - halfH);
                    this.fogGraphics.lineTo(screenX + halfW, screenY);
                    this.fogGraphics.lineTo(screenX, screenY + halfH);
                    this.fogGraphics.lineTo(screenX - halfW, screenY);
                    this.fogGraphics.closePath();
                    this.fogGraphics.fillPath();
                }
                else {
                    this.fogGraphics.fillStyle(0x000000, alpha);
                    this.fogGraphics.fillRect(tx * ts, ty * ts, ts, ts);
                }
            }
        }
    }
    /**
     * Render full fog covering the entire ISO map area.
     */
    renderFullFogIso(alpha) {
        const ts = this.tileSize;
        this.fogGraphics.fillStyle(0x000000, alpha);
        for (let ty = 0; ty < this.mapHeightTiles; ty++) {
            for (let tx = 0; tx < this.mapWidthTiles; tx++) {
                const orthoCx = tx * ts + ts / 2;
                const orthoCy = ty * ts + ts / 2;
                const iso = orthoToIso(orthoCx, orthoCy);
                const halfW = ts;
                const halfH = ts / 2;
                this.fogGraphics.beginPath();
                this.fogGraphics.moveTo(iso.x, iso.y - halfH);
                this.fogGraphics.lineTo(iso.x + halfW, iso.y);
                this.fogGraphics.lineTo(iso.x, iso.y + halfH);
                this.fogGraphics.lineTo(iso.x - halfW, iso.y);
                this.fogGraphics.closePath();
                this.fogGraphics.fillPath();
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