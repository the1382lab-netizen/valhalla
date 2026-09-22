import Phaser from 'phaser';
interface VisibilityData {
    polygon: {
        x: number;
        y: number;
    }[];
    visiblePlayers: string[];
    visibleProjectiles: string[];
}
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
export declare class FogOfWar {
    private scene;
    private fogGraphics;
    private exploredTiles;
    private currentPolygon;
    private visiblePlayers;
    private visibleProjectiles;
    private targetPolygon;
    private lerpFactor;
    private tileSize;
    private mapWidthTiles;
    private mapHeightTiles;
    private mapWidthPx;
    private mapHeightPx;
    private isIso;
    constructor(scene: Phaser.Scene, tileSize: number, mapWidthTiles: number, mapHeightTiles: number, isIso?: boolean);
    /**
     * Update the visibility data from the server.
     */
    updateVisibility(data: VisibilityData): void;
    /**
     * Mark tiles covered by the visibility polygon as explored.
     * Polygon is in orthogonal world space.
     */
    private markExploredTiles;
    /**
     * Point-in-polygon test (ray casting algorithm).
     */
    private isPointInPolygon;
    /**
     * Check if a player is currently visible.
     */
    isPlayerVisible(sessionId: string): boolean;
    /**
     * Check if a projectile is currently visible.
     */
    isProjectileVisible(projectileId: string): boolean;
    /**
     * Render the fog of war overlay.
     * Called each frame from GameScene.update().
     */
    render(): void;
    /**
     * Render full fog covering the entire ISO map area.
     */
    private renderFullFogIso;
    /**
     * Clean up resources.
     */
    destroy(): void;
}
export {};
//# sourceMappingURL=FogOfWar.d.ts.map