import Phaser from 'phaser';
/**
 * Manages rendering and interpolation for remote player entities,
 * projectiles, HP bars, and melee visual effects.
 */
export declare class EntityRenderer {
    private scene;
    private remotePlayers;
    private projectiles;
    constructor(scene: Phaser.Scene);
    addRemotePlayer(sessionId: string, x: number, y: number): void;
    removeRemotePlayer(sessionId: string): void;
    updateRemotePlayerTarget(sessionId: string, x: number, y: number, aimAngle: number): void;
    updateRemotePlayerHp(sessionId: string, hp: number, maxHp: number, alive: boolean): void;
    addProjectile(id: string, x: number, y: number): void;
    removeProjectile(id: string): void;
    updateProjectileTarget(id: string, x: number, y: number): void;
    showMeleeSlash(x: number, y: number, angle: number): void;
    showDamageFlash(x: number, y: number, damage: number): void;
    private drawHpBar;
    /**
     * Interpolate remote player and projectile positions each frame.
     */
    update(): void;
    hasPlayer(sessionId: string): boolean;
    /**
     * Show/hide remote players and projectiles based on server visibility data.
     */
    applyVisibility(visiblePlayers: Set<string>, visibleProjectiles: Set<string>): void;
    /**
     * Get the current interpolated position of a remote player.
     * Returns null if the player is not tracked.
     */
    getRemotePlayerPosition(sessionId: string): {
        x: number;
        y: number;
    } | null;
}
//# sourceMappingURL=EntityRenderer.d.ts.map