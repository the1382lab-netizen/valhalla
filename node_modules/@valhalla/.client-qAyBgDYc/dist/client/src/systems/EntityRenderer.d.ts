import Phaser from 'phaser';
export declare class EntityRenderer {
    private scene;
    private remotePlayers;
    private npcs;
    private projectiles;
    private spellProjectiles;
    constructor(scene: Phaser.Scene);
    addRemotePlayer(sessionId: string, x: number, y: number, classId?: string, level?: number, characterName?: string): void;
    removeRemotePlayer(sessionId: string): void;
    updateRemotePlayerTarget(sessionId: string, x: number, y: number, aimAngle: number): void;
    updateRemotePlayerHp(sessionId: string, hp: number, maxHp: number, alive: boolean, level?: number): void;
    addNPC(id: string, x: number, y: number, name: string, level: number, npcType: string, spriteColor: number, spriteSize: number): void;
    removeNPC(id: string): void;
    updateNPCTarget(id: string, x: number, y: number, aimAngle: number): void;
    updateNPCHp(id: string, hp: number, maxHp: number, alive: boolean): void;
    hasNPC(id: string): boolean;
    getNPCPosition(id: string): {
        x: number;
        y: number;
    } | null;
    addProjectile(id: string, x: number, y: number): void;
    removeProjectile(id: string): void;
    updateProjectileTarget(id: string, x: number, y: number): void;
    addSpellProjectile(id: string, x: number, y: number, targetX: number, targetY: number, skillId: string): void;
    updateSpellProjectileTarget(id: string, x: number, y: number): void;
    removeSpellProjectile(id: string): void;
    /**
     * Play a fireball explosion at the given world position.
     * Called when the server sends a SPELL_IMPACT event.
     */
    showSpellImpact(x: number, y: number, radius: number, _skillId: string): void;
    /** Draw the fireball as a glowing orange circle (placeholder until a sprite asset exists). */
    private drawFireball;
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