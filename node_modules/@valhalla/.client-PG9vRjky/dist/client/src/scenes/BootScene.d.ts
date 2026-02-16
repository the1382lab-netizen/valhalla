import Phaser from 'phaser';
/**
 * BootScene — loads assets and transitions to GameScene.
 * For Phase 1, we generate placeholder textures at runtime.
 */
export declare class BootScene extends Phaser.Scene {
    constructor();
    preload(): void;
    create(): void;
    private generateTileTextures;
    private generatePlayerTexture;
    private generateProjectileTexture;
    private generateMeleeTexture;
}
//# sourceMappingURL=BootScene.d.ts.map