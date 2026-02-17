import Phaser from 'phaser';
/**
 * Manages rendering and interpolation for remote player entities,
 * projectiles, HP bars, class labels, and combat visual effects.
 */
export declare class EntityRenderer {
    private scene;
    private remotePlayers;
    private projectiles;
    constructor(scene: Phaser.Scene);
    addRemotePlayer(sessionId: string, x: number, y: number, classId?: string, level?: number, characterName?: string): void;
    removeRemotePlayer(sessionId: string): void;
    updateRemotePlayerTarget(sessionId: string, x: number, y: number, aimAngle: number): void;
    updateRemotePlayerHp(sessionId: string, hp: number, maxHp: number, alive: boolean, level?: number): void;
    addProjectile(id: string, x: number, y: number): void;
    removeProjectile(id: string): void;
    updateProjectileTarget(id: string, x: number, y: number): void;
    showMeleeSlash(x: number, y: number, angle: number): void;
    showDamageFlash(x: number, y: number, damage: number, isCrit?: boolean): void;
    showCombatText(x: number, y: number, label: string, color: string): void;
    private drawHpBar;
    /**
     * Interpolate remote player and projectile positions each frame.
     */
    update(): void;
    hasPlayer(sessionId: string): boolean;
    getPlayerPosition(sessionId: string): {
        x: number;
        y: number;
    } | null;
}
//# sourceMappingURL=EntityRenderer.d.ts.map