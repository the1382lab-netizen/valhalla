import Phaser from 'phaser';
/**
 * BootScene — loads assets and transitions to LoginScene.
 * Generates placeholder textures at runtime for all tile types and entities.
 *
 * Tile GID → texture mapping (matches the Tiled tileset):
 *   1 = tile_grass_light    5 = tile_stone_wall     9 = tile_portal
 *   2 = tile_grass_dark     6 = tile_wood_fence    10 = tile_void
 *   3 = tile_dirt            7 = tile_tree
 *   4 = tile_water           8 = tile_stone_floor
 */
export class BootScene extends Phaser.Scene {
    constructor() {
        super({ key: 'BootScene' });
    }
    preload() {
        // We generate textures in create() instead of loading external files
    }
    create() {
        // ── Generate tile textures (expanded set) ────────────────
        this.generateTileTextures();
        // ── Generate player sprite texture ───────────────────────
        this.generatePlayerTexture();
        // ── Generate projectile texture ──────────────────────────
        this.generateProjectileTexture();
        // ── Generate melee slash texture ─────────────────────────
        this.generateMeleeTexture();
        // Transition to login screen
        this.scene.start('LoginScene');
    }
    generateTileTextures() {
        const ts = 64; // tile size
        // Helper to create a tile texture
        const makeTile = (key, fn) => {
            const gfx = this.add.graphics();
            fn(gfx);
            gfx.generateTexture(key, ts, ts);
            gfx.destroy();
        };
        // 1. Grass (light) — bright green with subtle grid
        makeTile('tile_grass_light', (g) => {
            g.fillStyle(0x2d5a27, 1);
            g.fillRect(0, 0, ts, ts);
            g.lineStyle(1, 0x245020, 0.3);
            g.strokeRect(0, 0, ts, ts);
            // A few tiny grass blades
            g.fillStyle(0x3a7a30, 0.6);
            g.fillRect(10, 20, 2, 6);
            g.fillRect(30, 40, 2, 5);
            g.fillRect(50, 15, 2, 6);
        });
        // 2. Grass (dark) — darker variant
        makeTile('tile_grass_dark', (g) => {
            g.fillStyle(0x224a1e, 1);
            g.fillRect(0, 0, ts, ts);
            g.lineStyle(1, 0x1c3e18, 0.3);
            g.strokeRect(0, 0, ts, ts);
            g.fillStyle(0x2e6426, 0.5);
            g.fillRect(15, 30, 2, 5);
            g.fillRect(45, 10, 2, 6);
        });
        // 3. Dirt path — brown
        makeTile('tile_dirt', (g) => {
            g.fillStyle(0x8B7355, 1);
            g.fillRect(0, 0, ts, ts);
            g.lineStyle(1, 0x7a6248, 0.3);
            g.strokeRect(0, 0, ts, ts);
            // Pebble details
            g.fillStyle(0x9e8868, 0.4);
            g.fillCircle(12, 18, 2);
            g.fillCircle(40, 45, 3);
            g.fillCircle(52, 20, 2);
        });
        // 4. Water — blue with wave effect
        makeTile('tile_water', (g) => {
            g.fillStyle(0x2255aa, 1);
            g.fillRect(0, 0, ts, ts);
            // Wave highlights
            g.lineStyle(1, 0x3377cc, 0.5);
            g.beginPath();
            g.moveTo(5, 20);
            g.lineTo(15, 18);
            g.lineTo(25, 22);
            g.lineTo(35, 18);
            g.lineTo(45, 22);
            g.lineTo(55, 18);
            g.strokePath();
            g.beginPath();
            g.moveTo(10, 42);
            g.lineTo(20, 40);
            g.lineTo(30, 44);
            g.lineTo(40, 40);
            g.lineTo(50, 44);
            g.lineTo(60, 40);
            g.strokePath();
            g.lineStyle(1, 0x1a4488, 0.3);
            g.strokeRect(0, 0, ts, ts);
        });
        // 5. Stone wall — grey stone with inner frame
        makeTile('tile_stone_wall', (g) => {
            g.fillStyle(0x555566, 1);
            g.fillRect(0, 0, ts, ts);
            g.lineStyle(2, 0x3a3a4a, 1);
            g.strokeRect(2, 2, ts - 4, ts - 4);
            g.lineStyle(1, 0x6a6a7a, 0.5);
            g.strokeRect(4, 4, ts - 8, ts - 8);
            // Brick lines
            g.lineStyle(1, 0x4a4a5a, 0.4);
            g.lineBetween(0, ts / 3, ts, ts / 3);
            g.lineBetween(0, (ts * 2) / 3, ts, (ts * 2) / 3);
            g.lineBetween(ts / 2, 0, ts / 2, ts / 3);
            g.lineBetween(ts / 4, ts / 3, ts / 4, (ts * 2) / 3);
            g.lineBetween((ts * 3) / 4, ts / 3, (ts * 3) / 4, (ts * 2) / 3);
            g.lineBetween(ts / 2, (ts * 2) / 3, ts / 2, ts);
        });
        // 6. Wood fence — brown wooden planks
        makeTile('tile_wood_fence', (g) => {
            g.fillStyle(0x2d5a27, 1);
            g.fillRect(0, 0, ts, ts); // grass underneath
            // Fence post
            g.fillStyle(0x8B6914, 1);
            g.fillRect(28, 10, 8, 44);
            // Crossbeams
            g.fillStyle(0x9E7B1A, 1);
            g.fillRect(4, 18, 56, 5);
            g.fillRect(4, 38, 56, 5);
            // Post cap
            g.fillStyle(0x6B4F10, 1);
            g.fillRect(26, 8, 12, 4);
        });
        // 7. Tree — green canopy on brown trunk
        makeTile('tile_tree', (g) => {
            g.fillStyle(0x224a1e, 1);
            g.fillRect(0, 0, ts, ts); // dark grass base
            // Trunk
            g.fillStyle(0x6B4226, 1);
            g.fillRect(26, 30, 12, 28);
            // Canopy (overlapping circles)
            g.fillStyle(0x1a6b1a, 1);
            g.fillCircle(ts / 2, 22, 20);
            g.fillStyle(0x228B22, 0.8);
            g.fillCircle(ts / 2 - 8, 18, 14);
            g.fillCircle(ts / 2 + 8, 20, 14);
            g.fillCircle(ts / 2, 14, 12);
        });
        // 8. Stone floor — lighter interior stone
        makeTile('tile_stone_floor', (g) => {
            g.fillStyle(0x8a8a8a, 1);
            g.fillRect(0, 0, ts, ts);
            g.lineStyle(1, 0x7a7a7a, 0.5);
            g.strokeRect(0, 0, ts, ts);
            // Tile pattern
            g.lineStyle(1, 0x6a6a6a, 0.3);
            g.lineBetween(ts / 2, 0, ts / 2, ts);
            g.lineBetween(0, ts / 2, ts, ts / 2);
        });
        // 9. Portal — swirling purple/blue
        makeTile('tile_portal', (g) => {
            g.fillStyle(0x1a1a2e, 1);
            g.fillRect(0, 0, ts, ts);
            // Glowing rings
            g.lineStyle(3, 0x8844ff, 0.8);
            g.strokeCircle(ts / 2, ts / 2, 24);
            g.lineStyle(2, 0xaa66ff, 0.6);
            g.strokeCircle(ts / 2, ts / 2, 18);
            g.lineStyle(2, 0x6633cc, 0.4);
            g.strokeCircle(ts / 2, ts / 2, 12);
            g.fillStyle(0xcc88ff, 0.3);
            g.fillCircle(ts / 2, ts / 2, 8);
        });
        // 10. Void — black/empty
        makeTile('tile_void', (g) => {
            g.fillStyle(0x0a0a0a, 1);
            g.fillRect(0, 0, ts, ts);
        });
        // Legacy aliases (for backward compat if anything still references these)
        // 'tile_ground' = tile_grass_light, 'tile_wall' = tile_stone_wall
        // We generate them as copies
        makeTile('tile_ground', (g) => {
            g.fillStyle(0x2d5a27, 1);
            g.fillRect(0, 0, ts, ts);
            g.lineStyle(1, 0x245020, 0.3);
            g.strokeRect(0, 0, ts, ts);
        });
        makeTile('tile_wall', (g) => {
            g.fillStyle(0x555566, 1);
            g.fillRect(0, 0, ts, ts);
            g.lineStyle(2, 0x3a3a4a, 1);
            g.strokeRect(2, 2, ts - 4, ts - 4);
            g.lineStyle(1, 0x6a6a7a, 0.5);
            g.strokeRect(4, 4, ts - 8, ts - 8);
        });
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