import Phaser from 'phaser';
/**
 * BootScene — loads assets and transitions to GameScene.
 * For Phase 1, we generate placeholder textures at runtime.
 */
export class BootScene extends Phaser.Scene {
    constructor() {
        super({ key: 'BootScene' });
    }
    preload() {
        // We'll generate textures in create() instead of loading external files
    }
    create() {
        // ── Generate placeholder tile textures ──────────────────
        this.generateTileTextures();
        // ── Generate player sprite texture ─────────────────────
        this.generatePlayerTexture();
        // ── Generate projectile texture ────────────────────────
        this.generateProjectileTexture();
        // ── Generate melee slash texture ───────────────────────
        this.generateMeleeTexture();
        // Transition to game
        this.scene.start('GameScene');
    }
    generateTileTextures() {
        const tileSize = 64;
        // Ground tile — dark green grass
        const groundGfx = this.add.graphics();
        groundGfx.fillStyle(0x2d5a27, 1);
        groundGfx.fillRect(0, 0, tileSize, tileSize);
        groundGfx.lineStyle(1, 0x245020, 0.3);
        groundGfx.strokeRect(0, 0, tileSize, tileSize);
        groundGfx.generateTexture('tile_ground', tileSize, tileSize);
        groundGfx.destroy();
        // Wall tile — grey stone
        const wallGfx = this.add.graphics();
        wallGfx.fillStyle(0x555566, 1);
        wallGfx.fillRect(0, 0, tileSize, tileSize);
        wallGfx.lineStyle(2, 0x3a3a4a, 1);
        wallGfx.strokeRect(2, 2, tileSize - 4, tileSize - 4);
        wallGfx.lineStyle(1, 0x6a6a7a, 0.5);
        wallGfx.strokeRect(4, 4, tileSize - 8, tileSize - 8);
        wallGfx.generateTexture('tile_wall', tileSize, tileSize);
        wallGfx.destroy();
    }
    generatePlayerTexture() {
        const size = 48;
        const gfx = this.add.graphics();
        // Body circle
        gfx.fillStyle(0x4488ff, 1);
        gfx.fillCircle(size / 2, size / 2, size / 2 - 2);
        // Outline
        gfx.lineStyle(2, 0x2266dd, 1);
        gfx.strokeCircle(size / 2, size / 2, size / 2 - 2);
        // Direction indicator (small triangle pointing right)
        gfx.fillStyle(0xffffff, 0.9);
        gfx.fillTriangle(size - 6, size / 2, size / 2 + 4, size / 2 - 6, size / 2 + 4, size / 2 + 6);
        gfx.generateTexture('player', size, size);
        gfx.destroy();
        // Remote player — orange version
        const gfx2 = this.add.graphics();
        gfx2.fillStyle(0xff8844, 1);
        gfx2.fillCircle(size / 2, size / 2, size / 2 - 2);
        gfx2.lineStyle(2, 0xdd6622, 1);
        gfx2.strokeCircle(size / 2, size / 2, size / 2 - 2);
        gfx2.fillStyle(0xffffff, 0.9);
        gfx2.fillTriangle(size - 6, size / 2, size / 2 + 4, size / 2 - 6, size / 2 + 4, size / 2 + 6);
        gfx2.generateTexture('remote_player', size, size);
        gfx2.destroy();
    }
    generateProjectileTexture() {
        const size = 12;
        const gfx = this.add.graphics();
        // Bright yellow-white projectile
        gfx.fillStyle(0xffff88, 1);
        gfx.fillCircle(size / 2, size / 2, 5);
        gfx.fillStyle(0xffffff, 0.8);
        gfx.fillCircle(size / 2, size / 2, 2);
        gfx.generateTexture('projectile', size, size);
        gfx.destroy();
    }
    generateMeleeTexture() {
        // A simple arc/slash shape for melee visual feedback
        const size = 80;
        const gfx = this.add.graphics();
        gfx.lineStyle(4, 0xffffff, 0.8);
        gfx.beginPath();
        gfx.arc(size / 2, size / 2, 30, -Math.PI / 4, Math.PI / 4, false);
        gfx.strokePath();
        gfx.lineStyle(2, 0xffaa44, 0.6);
        gfx.beginPath();
        gfx.arc(size / 2, size / 2, 25, -Math.PI / 4, Math.PI / 4, false);
        gfx.strokePath();
        gfx.generateTexture('melee_slash', size, size);
        gfx.destroy();
    }
}
//# sourceMappingURL=BootScene.js.map