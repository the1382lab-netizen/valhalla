import Phaser from 'phaser';
/**
 * Main gameplay scene.
 * Handles tile map rendering, local player with client-side prediction,
 * remote player interpolation, projectile rendering, HP, death/respawn,
 * fog of war, and visibility-based entity filtering.
 */
export declare class GameScene extends Phaser.Scene {
    private network;
    private inputManager;
    private entityRenderer;
    private fogOfWar;
    private playerSprite;
    private aimLine;
    private localX;
    private localY;
    private localHp;
    private localMaxHp;
    private localAlive;
    private hpBarGfx;
    private deathOverlay;
    private deathText;
    private respawnTimer;
    private pendingInputs;
    private lastServerSeq;
    private collisionGrid;
    private collisionMapW;
    private collisionMapH;
    private connected;
    private statusText;
    constructor();
    create(): void;
    private setupNetworkCallbacks;
    private createLocalPlayer;
    /**
     * Build the visual tilemap from the collision grid data.
     */
    private buildTileMap;
    private showDeathScreen;
    private hideDeathScreen;
    private drawLocalHpBar;
    update(_time: number, delta: number): void;
    /**
     * Apply input locally for client-side prediction.
     * Mirrors the server's movement logic exactly.
     */
    private applyInputLocally;
    /**
     * Client-side collision resolution (mirrors server logic).
     */
    private resolveCollisionLocal;
    /**
     * Server reconciliation: when we get authoritative state, reapply
     * any inputs the server hasn't processed yet.
     */
    private reconcile;
    /**
     * Draw a short aim line from the player in the aim direction.
     */
    private drawAimLine;
}
//# sourceMappingURL=GameScene.d.ts.map