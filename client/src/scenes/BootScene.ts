import Phaser from 'phaser';
import { SkillId, SkillCategory, SERVER_URL, DEFAULT_EQUIP_SPRITE_CONFIG } from '@valhalla/shared';
import { PaperdollRegistry } from '../systems/PaperdollRegistry.js';
import { ClientDataManager } from '../systems/ClientDataManager.js';

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

  preload(): void {
    // Fallback player sprite sheet: 4 rows (Up, Left, Down, Right) × 9 frames, 64×64 px
    this.load.spritesheet('player_walk', 'assets/sprites/BODY_male_walking.png', {
      frameWidth: 64,
      frameHeight: 64,
    });
  }

  async create(): Promise<void> {
    // ── Load game data from server (editor JSON files) ──────
    try {
      await ClientDataManager.initialize(SERVER_URL);
    } catch (err) {
      console.warn('[BootScene] Failed to load data from server, using hardcoded fallbacks:', err);
    }

    // ── Generate tile textures (expanded set) ────────────────
    this.generateTileTextures();

    // ── Town, farm and cave tiles (GIDs 21-40) ──────────────
    this.generateTownTextures();

    // ── Generate player sprite texture ───────────────────────
    this.generatePlayerTexture();

    // ── Generate projectile texture ──────────────────────────
    this.generateProjectileTexture();

    // ── Generate NPC sprite texture ──────────────────────────
    this.generateNpcTexture();

    // ── Generate melee slash texture ─────────────────────────
    this.generateMeleeTexture();

    // ── Generate loot bag texture ─────────────────────────────
    this.generateLootBagTexture();

    // ── Generate skill icon textures ───────────────────────
    this.generateSkillIcons();

    // ── Load per-class character sprite sheets ─────────────
    await this.loadClassSprites();

    // ── Define per-class player animations ───────────────
    this.definePlayerAnimations();

    // ── Preload equipment overlays & inventory icons ────────
    await this.loadEquipmentAssets();

    // ── Paperdoll layers (bodies + the item sheets the catalog uses) ──
    await this.loadPaperdollAssets();

    // Transition to login screen
    this.scene.start('LoginScene');
  }

  private generateTileTextures(): void {
    const ts = 64; // tile size

    // Helper to create a tile texture
    const makeTile = (key: string, fn: (gfx: Phaser.GameObjects.Graphics) => void) => {
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

    // ── Desert tileset (GIDs 11–20) ────────────────────────

    // 11. Sand (light) — warm tan
    makeTile('tile_sand_light', (g) => {
      g.fillStyle(0xd4b483, 1);
      g.fillRect(0, 0, ts, ts);
      g.lineStyle(1, 0xc4a473, 0.3);
      g.strokeRect(0, 0, ts, ts);
      // Sand grain details
      g.fillStyle(0xe0c090, 0.4);
      g.fillCircle(15, 25, 1);
      g.fillCircle(40, 12, 1);
      g.fillCircle(50, 48, 1);
    });

    // 12. Sand (dark) — deeper tan
    makeTile('tile_sand_dark', (g) => {
      g.fillStyle(0xb8956a, 1);
      g.fillRect(0, 0, ts, ts);
      g.lineStyle(1, 0xa8855a, 0.3);
      g.strokeRect(0, 0, ts, ts);
      g.fillStyle(0xc8a57a, 0.3);
      g.fillCircle(20, 35, 1);
      g.fillCircle(48, 20, 1);
    });

    // 13. Sand road — packed sand path
    makeTile('tile_sand_road', (g) => {
      g.fillStyle(0xc9a96e, 1);
      g.fillRect(0, 0, ts, ts);
      g.lineStyle(1, 0xb89960, 0.4);
      g.strokeRect(0, 0, ts, ts);
      // Track marks
      g.lineStyle(1, 0xba9a60, 0.3);
      g.lineBetween(10, 10, 54, 10);
      g.lineBetween(10, 54, 54, 54);
    });

    // 14. Oasis water — blue-green
    makeTile('tile_oasis_water', (g) => {
      g.fillStyle(0x2299aa, 1);
      g.fillRect(0, 0, ts, ts);
      g.lineStyle(1, 0x33bbcc, 0.5);
      g.beginPath();
      g.moveTo(8, 22);
      g.lineTo(18, 20);
      g.lineTo(28, 24);
      g.lineTo(38, 20);
      g.lineTo(48, 24);
      g.lineTo(58, 20);
      g.strokePath();
      g.beginPath();
      g.moveTo(12, 44);
      g.lineTo(22, 42);
      g.lineTo(32, 46);
      g.lineTo(42, 42);
      g.lineTo(52, 46);
      g.strokePath();
    });

    // 15. Sandstone wall — warm brown stone
    makeTile('tile_sandstone_wall', (g) => {
      g.fillStyle(0xa0784c, 1);
      g.fillRect(0, 0, ts, ts);
      g.lineStyle(2, 0x8a6840, 1);
      g.strokeRect(2, 2, ts - 4, ts - 4);
      g.lineStyle(1, 0xb08858, 0.5);
      g.strokeRect(4, 4, ts - 8, ts - 8);
      // Brick lines
      g.lineStyle(1, 0x906848, 0.4);
      g.lineBetween(0, ts / 3, ts, ts / 3);
      g.lineBetween(0, (ts * 2) / 3, ts, (ts * 2) / 3);
      g.lineBetween(ts / 2, 0, ts / 2, ts / 3);
      g.lineBetween(ts / 4, ts / 3, ts / 4, (ts * 2) / 3);
    });

    // 16. Cactus — green cactus on sand
    makeTile('tile_cactus', (g) => {
      g.fillStyle(0xd4b483, 1);
      g.fillRect(0, 0, ts, ts); // sand base
      // Trunk
      g.fillStyle(0x2d8a2d, 1);
      g.fillRect(28, 16, 8, 38);
      // Arms
      g.fillRect(18, 24, 10, 6);
      g.fillRect(18, 18, 6, 12);
      g.fillRect(36, 30, 10, 6);
      g.fillRect(40, 24, 6, 12);
      // Spines
      g.lineStyle(1, 0x44aa44, 0.6);
      g.lineBetween(26, 20, 24, 18);
      g.lineBetween(38, 22, 40, 20);
      g.lineBetween(26, 36, 24, 34);
    });

    // 17. Dead tree — grey/brown bare tree on sand
    makeTile('tile_dead_tree', (g) => {
      g.fillStyle(0xd4b483, 1);
      g.fillRect(0, 0, ts, ts); // sand base
      // Trunk
      g.fillStyle(0x6B4226, 1);
      g.fillRect(28, 28, 8, 30);
      // Bare branches
      g.lineStyle(3, 0x7a5230, 1);
      g.lineBetween(32, 28, 18, 10);
      g.lineBetween(32, 28, 46, 8);
      g.lineBetween(32, 22, 26, 14);
      g.lineStyle(2, 0x8a6240, 0.7);
      g.lineBetween(18, 10, 12, 6);
      g.lineBetween(46, 8, 52, 4);
    });

    // 18. Desert rock — grey/brown boulder on sand
    makeTile('tile_desert_rock', (g) => {
      g.fillStyle(0xd4b483, 1);
      g.fillRect(0, 0, ts, ts); // sand base
      // Rock body
      g.fillStyle(0x8a7a6a, 1);
      g.beginPath();
      g.moveTo(16, 48);
      g.lineTo(12, 32);
      g.lineTo(20, 20);
      g.lineTo(36, 16);
      g.lineTo(50, 22);
      g.lineTo(52, 36);
      g.lineTo(48, 48);
      g.closePath();
      g.fillPath();
      // Highlight
      g.fillStyle(0x9a8a7a, 0.5);
      g.beginPath();
      g.moveTo(20, 20);
      g.lineTo(36, 16);
      g.lineTo(42, 24);
      g.lineTo(28, 26);
      g.closePath();
      g.fillPath();
    });

    // 19. Ruins floor — cracked sandstone
    makeTile('tile_ruins_floor', (g) => {
      g.fillStyle(0xb8956a, 1);
      g.fillRect(0, 0, ts, ts);
      g.lineStyle(1, 0xa8855a, 0.5);
      g.strokeRect(0, 0, ts, ts);
      // Cracks
      g.lineStyle(1, 0x907050, 0.6);
      g.lineBetween(ts / 2, 0, ts / 2, ts);
      g.lineBetween(0, ts / 2, ts, ts / 2);
      // Broken crack lines
      g.lineStyle(1, 0x806040, 0.4);
      g.lineBetween(10, 10, 30, 25);
      g.lineBetween(40, 35, 55, 55);
    });

    // 20. Quicksand — brownish swirl on sand
    makeTile('tile_quicksand', (g) => {
      g.fillStyle(0xc4a060, 1);
      g.fillRect(0, 0, ts, ts);
      // Swirl pattern
      g.lineStyle(2, 0xb09050, 0.6);
      g.strokeCircle(ts / 2, ts / 2, 20);
      g.lineStyle(1, 0xa08040, 0.5);
      g.strokeCircle(ts / 2, ts / 2, 14);
      g.strokeCircle(ts / 2, ts / 2, 8);
      g.fillStyle(0x907030, 0.3);
      g.fillCircle(ts / 2, ts / 2, 6);
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

  /**
   * Town, farm and cave tiles (GIDs 21-40).
   *
   * Same 64x64 flat-square convention as the base set: the client draws squares
   * on the isometric lattice rather than true diamonds, so these are authored to
   * match. Building tiles come in enterable and solid flavours -- walls, doors
   * and plank floors for interiors you can walk into, roofs for the ones you
   * cannot.
   */
  private generateTownTextures(): void {
    const ts = 64;
    const makeTile = (key: string, fn: (gfx: Phaser.GameObjects.Graphics) => void) => {
      const gfx = this.add.graphics();
      fn(gfx);
      gfx.generateTexture(key, ts, ts);
      gfx.destroy();
    };

    // 21. Cobblestone road
    makeTile('tile_road_cobble', (g) => {
      g.fillStyle(0x6e6a63, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x7d7970, 1);
      const stones: [number, number, number, number][] = [
        [3, 4, 14, 10], [20, 3, 12, 11], [35, 5, 16, 9], [54, 4, 8, 10],
        [2, 18, 11, 12], [16, 17, 15, 11], [34, 19, 11, 10], [48, 17, 14, 12],
        [4, 33, 16, 11], [23, 32, 12, 12], [38, 34, 14, 10], [55, 33, 7, 11],
        [1, 48, 13, 12], [17, 47, 16, 13], [36, 49, 12, 11], [51, 47, 12, 13],
      ];
      for (const [x, y, w, h] of stones) g.fillRoundedRect(x, y, w, h, 3);
      g.fillStyle(0x8b877d, 0.5);
      for (const [x, y, w] of stones) g.fillRect(x + 2, y + 1, w - 4, 2);
    });

    // 22. Plaster wall with timber framing
    makeTile('tile_plaster_wall', (g) => {
      g.fillStyle(0xd8cdb4, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x6b4a2e, 1);
      g.fillRect(0, 0, ts, 6);          // top plate
      g.fillRect(0, ts - 7, ts, 7);     // sill
      g.fillRect(0, 0, 6, ts);          // corner posts
      g.fillRect(ts - 6, 0, 6, ts);
      g.fillRect(29, 6, 6, ts - 13);    // centre stud
      g.fillStyle(0x5a3d25, 1);         // diagonal brace
      g.fillTriangle(6, ts - 7, 29, 6, 29, 16);
      g.fillStyle(0xc4b79a, 0.6);
      g.fillRect(8, 10, 19, 8);
    });

    // 23. Dark timber wall
    makeTile('tile_timber_wall', (g) => {
      g.fillStyle(0x7a5636, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x6a4a2e, 1);
      for (let y = 0; y < ts; y += 11) g.fillRect(0, y, ts, 2);
      g.fillStyle(0x8b6540, 0.7);
      for (let y = 4; y < ts; y += 11) g.fillRect(3, y, ts - 6, 3);
      g.fillStyle(0x4e3722, 1);
      g.fillRect(0, 0, 4, ts);
      g.fillRect(ts - 4, 0, 4, ts);
    });

    // 24. Thatched roof
    makeTile('tile_roof_thatch', (g) => {
      g.fillStyle(0xa8874a, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x93753e, 1);
      for (let y = 6; y < ts; y += 12) g.fillRect(0, y, ts, 4);
      g.fillStyle(0xc0a061, 0.7);
      for (let y = 0; y < ts; y += 12) {
        for (let x = (y / 12) % 2 === 0 ? 0 : 6; x < ts; x += 12) g.fillRect(x, y, 7, 3);
      }
    });

    // 25. Clay tile roof
    makeTile('tile_roof_tile', (g) => {
      g.fillStyle(0x9c4a35, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x863d2b, 1);
      for (let y = 0; y < ts; y += 10) g.fillRect(0, y + 8, ts, 2);
      g.fillStyle(0xb35a41, 1);
      for (let y = 0; y < ts; y += 10) {
        for (let x = (y / 10) % 2 === 0 ? 0 : 8; x < ts; x += 16) {
          g.fillRoundedRect(x + 1, y, 14, 7, 3);
        }
      }
    });

    // 26. Wooden door
    makeTile('tile_door_wood', (g) => {
      g.fillStyle(0xd8cdb4, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x6b4a2e, 1);
      g.fillRect(0, 0, ts, 6);
      g.fillRect(0, 0, 6, ts);
      g.fillRect(ts - 6, 0, 6, ts);
      g.fillStyle(0x5a3a20, 1);
      g.fillRoundedRect(14, 10, 36, ts - 10, 4);
      g.fillStyle(0x4a2f19, 1);
      g.fillRect(31, 12, 2, ts - 14);
      g.fillStyle(0x3d2715, 1);
      g.fillRect(16, 20, 32, 3);
      g.fillRect(16, 44, 32, 3);
      g.fillStyle(0xd9b24a, 1);
      g.fillCircle(26, 36, 2.5);
      g.fillCircle(38, 36, 2.5);
    });

    // 27. Shuttered window, lit from within
    makeTile('tile_window_lit', (g) => {
      g.fillStyle(0xd8cdb4, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x6b4a2e, 1);
      g.fillRect(0, 0, ts, 6);
      g.fillRect(0, ts - 7, ts, 7);
      g.fillRect(0, 0, 6, ts);
      g.fillRect(ts - 6, 0, 6, ts);
      g.fillStyle(0x3a2c1c, 1);
      g.fillRect(16, 16, 32, 26);
      g.fillStyle(0xf0c96a, 1);
      g.fillRect(19, 19, 26, 20);
      g.fillStyle(0x3a2c1c, 1);
      g.fillRect(30, 19, 3, 20);
      g.fillRect(19, 27, 26, 3);
      g.fillStyle(0x5a3d25, 1);   // shutters
      g.fillRect(10, 15, 6, 28);
      g.fillRect(48, 15, 6, 28);
    });

    // 28. Interior plank floor
    makeTile('tile_wood_floor', (g) => {
      g.fillStyle(0x9a7448, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x8a663d, 1);
      for (let y = 0; y < ts; y += 16) g.fillRect(0, y, ts, 2);
      g.fillStyle(0xa88151, 0.35);
      for (let y = 4; y < ts; y += 16) g.fillRect(2, y, ts - 4, 4);
      g.fillStyle(0x77552f, 1);
      for (let y = 0; y < ts; y += 16) {
        const off = (y / 16) % 2 === 0 ? 22 : 44;
        g.fillRect(off, y, 2, 16);
      }
    });

    // 29. Inn signpost
    makeTile('tile_signpost', (g) => {
      g.fillStyle(0x2d5a27, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x5a3d25, 1);
      g.fillRect(29, 18, 6, 40);
      g.fillRect(29, 14, 22, 5);
      g.fillStyle(0x6b4a2e, 1);
      g.fillRoundedRect(34, 20, 22, 18, 3);
      g.fillStyle(0xd9b24a, 1);
      g.fillCircle(45, 27, 4);
      g.fillRect(41, 31, 8, 3);
      g.fillStyle(0x3a2a18, 0.5);
      g.fillEllipse(32, 59, 16, 5);
    });

    // 30. Stone well
    makeTile('tile_well', (g) => {
      g.fillStyle(0x6e6a63, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x8a8a8a, 1);
      g.fillCircle(32, 36, 20);
      g.fillStyle(0x6a6a6a, 1);
      g.fillCircle(32, 36, 15);
      g.fillStyle(0x1f3b52, 1);
      g.fillCircle(32, 36, 12);
      g.fillStyle(0x2d5570, 0.8);
      g.fillCircle(30, 34, 6);
      g.fillStyle(0x5a3d25, 1);   // posts + crossbeam
      g.fillRect(12, 8, 5, 30);
      g.fillRect(47, 8, 5, 30);
      g.fillRect(10, 6, 44, 5);
      g.fillStyle(0x8a6a45, 1);
      g.fillRect(28, 11, 8, 6);
    });

    // 31. Market stall
    makeTile('tile_market_stall', (g) => {
      g.fillStyle(0x6e6a63, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x5a3d25, 1);
      g.fillRect(6, 30, 5, 28);
      g.fillRect(53, 30, 5, 28);
      g.fillStyle(0x8a6a45, 1);
      g.fillRect(8, 36, 48, 8);
      g.fillStyle(0xb8443a, 1);   // striped awning
      g.fillRect(2, 12, 60, 20);
      g.fillStyle(0xe8e0d0, 1);
      for (let x = 2; x < 62; x += 16) g.fillRect(x, 12, 8, 20);
      g.fillStyle(0x8a3730, 1);
      g.fillRect(2, 30, 60, 3);
      g.fillStyle(0xd9b24a, 1);   // wares
      g.fillCircle(20, 33, 3);
      g.fillCircle(32, 33, 3);
      g.fillStyle(0x7fb069, 1);
      g.fillCircle(44, 33, 3);
    });

    // 32. Tilled crop field
    makeTile('tile_crop_field', (g) => {
      g.fillStyle(0x7a5c38, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x6a4e2e, 1);
      for (let x = 0; x < ts; x += 10) g.fillRect(x, 0, 4, ts);
      g.fillStyle(0x6f8f3a, 1);
      for (let x = 5; x < ts; x += 10) {
        for (let y = 4; y < ts; y += 12) {
          g.fillRect(x - 1, y, 3, 7);
          g.fillRect(x - 3, y + 2, 7, 2);
        }
      }
      g.fillStyle(0x87a84a, 0.7);
      for (let x = 5; x < ts; x += 10) for (let y = 4; y < ts; y += 12) g.fillRect(x - 1, y, 2, 3);
    });

    // 33. Wooden bridge over water
    makeTile('tile_bridge_wood', (g) => {
      g.fillStyle(0x2255aa, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x8a6a45, 1);
      g.fillRect(0, 8, ts, 48);
      g.fillStyle(0x74552f, 1);
      for (let x = 0; x < ts; x += 9) g.fillRect(x, 8, 2, 48);
      g.fillStyle(0x5a3d25, 1);
      g.fillRect(0, 8, ts, 5);
      g.fillRect(0, 51, ts, 5);
      g.fillStyle(0xa8815a, 0.5);
      g.fillRect(0, 20, ts, 3);
      g.fillRect(0, 40, ts, 3);
    });

    // 34. Cave mouth in the rock face
    makeTile('tile_cave_mouth', (g) => {
      g.fillStyle(0x6a625a, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x574f48, 1);
      g.fillTriangle(0, 0, 26, 0, 0, 30);
      g.fillTriangle(ts, 0, 40, 0, ts, 26);
      g.fillStyle(0x0d0c0f, 1);
      g.beginPath();
      g.moveTo(14, ts);
      g.lineTo(15, 34);
      g.lineTo(24, 18);
      g.lineTo(40, 18);
      g.lineTo(49, 34);
      g.lineTo(50, ts);
      g.closePath();
      g.fillPath();
      g.fillStyle(0x241f26, 1);
      g.fillEllipse(32, 30, 22, 12);
      g.fillStyle(0x0d0c0f, 1);
      g.fillEllipse(32, 34, 18, 10);
      g.fillStyle(0x8a8078, 1);   // boulders at the lip
      g.fillCircle(11, 56, 6);
      g.fillCircle(54, 58, 5);
    });

    // 35. Cave floor
    makeTile('tile_cave_floor', (g) => {
      g.fillStyle(0x413a41, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x4a434b, 1);
      g.fillCircle(14, 18, 7);
      g.fillCircle(44, 12, 9);
      g.fillCircle(30, 42, 8);
      g.fillCircle(54, 48, 6);
      g.fillStyle(0x37313a, 1);
      g.fillCircle(22, 54, 5);
      g.fillCircle(52, 30, 4);
      g.fillStyle(0x554d57, 0.6);
      g.fillCircle(12, 16, 3);
      g.fillCircle(42, 10, 4);
    });

    // 36. Cave wall
    makeTile('tile_cave_wall', (g) => {
      g.fillStyle(0x2b262d, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x38323a, 1);
      g.fillTriangle(0, 0, 30, 0, 8, 26);
      g.fillTriangle(30, 0, ts, 0, ts, 22);
      g.fillTriangle(0, 30, 20, 64, 0, 64);
      g.fillTriangle(38, 64, ts, 36, ts, 64);
      g.fillStyle(0x211d23, 1);
      g.fillTriangle(18, 24, 44, 20, 32, 44);
      g.fillStyle(0x453e48, 0.7);
      g.fillRect(6, 4, 14, 3);
      g.fillRect(40, 46, 16, 3);
    });

    // 37. Boulder
    makeTile('tile_rock', (g) => {
      g.fillStyle(0x2d5a27, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x7d7970, 1);
      g.beginPath();
      g.moveTo(12, 52);
      g.lineTo(8, 34);
      g.lineTo(20, 18);
      g.lineTo(40, 14);
      g.lineTo(55, 24);
      g.lineTo(56, 42);
      g.lineTo(48, 54);
      g.closePath();
      g.fillPath();
      g.fillStyle(0x949086, 0.8);
      g.fillTriangle(20, 18, 40, 14, 44, 26);
      g.fillStyle(0x5d5a53, 0.8);
      g.fillTriangle(48, 54, 56, 42, 30, 50);
      g.fillStyle(0x1e3d1a, 0.4);
      g.fillEllipse(32, 56, 42, 8);
    });

    // 38. Wildflowers
    makeTile('tile_flowers', (g) => {
      g.fillStyle(0x2d5a27, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x3a7a30, 0.7);
      for (let i = 0; i < 10; i++) g.fillRect(4 + i * 6, 12 + (i % 4) * 12, 2, 7);
      const petals: [number, number, number][] = [
        [12, 20, 0xe8d05a], [30, 14, 0xdd6f8f], [46, 24, 0xe8d05a],
        [18, 40, 0x9d8fd6], [38, 46, 0xdd6f8f], [54, 40, 0x9d8fd6],
        [8, 52, 0xe8d05a], [28, 56, 0x9d8fd6],
      ];
      for (const [x, y, c] of petals) {
        g.fillStyle(c, 1);
        g.fillCircle(x, y, 3);
        g.fillStyle(0xfff3c4, 1);
        g.fillCircle(x, y, 1);
      }
    });

    // 39. Hedge
    makeTile('tile_hedge', (g) => {
      g.fillStyle(0x2d5a27, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x1f5a1c, 1);
      g.fillRoundedRect(2, 10, 60, 46, 8);
      g.fillStyle(0x2a7325, 1);
      for (const [x, y, r] of [[14, 22, 9], [32, 18, 10], [50, 24, 9],
                               [20, 40, 9], [40, 42, 10], [55, 40, 7]] as [number, number, number][]) {
        g.fillCircle(x, y, r);
      }
      g.fillStyle(0x379130, 0.7);
      g.fillCircle(16, 19, 4);
      g.fillCircle(34, 15, 4);
      g.fillStyle(0x143d12, 0.6);
      g.fillRect(2, 50, 60, 6);
    });

    // 40. Campfire
    makeTile('tile_campfire', (g) => {
      g.fillStyle(0x2d5a27, 1);
      g.fillRect(0, 0, ts, ts);
      g.fillStyle(0x6a6a6a, 1);   // stone ring
      for (let a = 0; a < Math.PI * 2; a += Math.PI / 5) {
        g.fillCircle(32 + Math.cos(a) * 20, 40 + Math.sin(a) * 13, 5);
      }
      g.fillStyle(0x3a2a18, 1);
      g.fillCircle(32, 40, 14);
      g.fillStyle(0x5a3d25, 1);   // logs
      g.fillRect(18, 38, 28, 5);
      g.fillRect(28, 28, 5, 24);
      g.fillStyle(0xe86a24, 1);
      g.fillTriangle(32, 14, 24, 38, 40, 38);
      g.fillStyle(0xf5a63a, 1);
      g.fillTriangle(32, 22, 27, 38, 37, 38);
      g.fillStyle(0xffe08a, 1);
      g.fillTriangle(32, 30, 29, 39, 35, 39);
    });
  }

  private generatePlayerTexture(): void {
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
    gfx.fillTriangle(
      size - 6, size / 2,
      size / 2 + 4, size / 2 - 6,
      size / 2 + 4, size / 2 + 6,
    );

    gfx.generateTexture('player', size, size);
    gfx.destroy();

    // Remote player — orange version
    const gfx2 = this.add.graphics();
    gfx2.fillStyle(0xff8844, 1);
    gfx2.fillCircle(size / 2, size / 2, size / 2 - 2);
    gfx2.lineStyle(2, 0xdd6622, 1);
    gfx2.strokeCircle(size / 2, size / 2, size / 2 - 2);
    gfx2.fillStyle(0xffffff, 0.9);
    gfx2.fillTriangle(
      size - 6, size / 2,
      size / 2 + 4, size / 2 - 6,
      size / 2 + 4, size / 2 + 6,
    );
    gfx2.generateTexture('remote_player', size, size);
    gfx2.destroy();
  }

  private generateProjectileTexture(): void {
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

  private generateSkillIcons(): void {
    const iconSize = 40;

    const categoryBorderColor: Record<string, number> = {
      [SkillCategory.OFFENSIVE]: 0xcc3333,
      [SkillCategory.DEFENSIVE]: 0x3366cc,
      [SkillCategory.HEALING]: 0x33cc33,
      [SkillCategory.BUFF]: 0x3399ff,
      [SkillCategory.DEBUFF]: 0xcc6600,
      [SkillCategory.UTILITY]: 0x999999,
    };

    const allSkills = ClientDataManager.instance.skills;
    for (const skillId of Object.keys(allSkills)) {
      const skill = allSkills[skillId];
      if (!skill) continue;

      const gfx = this.add.graphics();

      // Background fill with skill color
      gfx.fillStyle(skill.iconColor, 1);
      gfx.fillRoundedRect(0, 0, iconSize, iconSize, 4);

      // Darker inner area
      gfx.fillStyle(0x000000, 0.3);
      gfx.fillRoundedRect(2, 2, iconSize - 4, iconSize - 4, 3);

      // Category-colored border
      const borderColor = categoryBorderColor[skill.category] ?? 0x888888;
      gfx.lineStyle(2, borderColor, 0.9);
      gfx.strokeRoundedRect(1, 1, iconSize - 2, iconSize - 2, 4);

      // Generate texture
      gfx.generateTexture(`skill_${skillId}`, iconSize, iconSize);
      gfx.destroy();

      // Add text overlay — we need to draw the abbreviation onto the texture
      // Since Phaser graphics can't draw text, we'll create a render texture
      const rt = this.add.renderTexture(0, 0, iconSize, iconSize);
      rt.draw(`skill_${skillId}`, 0, 0);

      // Draw abbreviation text
      const text = this.add.text(iconSize / 2, iconSize / 2, skill.iconAbbrev, {
        fontSize: '12px',
        fontStyle: 'bold',
        color: '#ffffff',
        stroke: '#000000',
        strokeThickness: 2,
      });
      text.setOrigin(0.5, 0.5);
      rt.draw(text, 0, 0);

      // Save as final texture (overwrite)
      rt.saveTexture(`skill_${skillId}`);
      rt.destroy();
      text.destroy();
    }
  }

  private generateNpcTexture(): void {
    const size = 24;
    const gfx = this.add.graphics();

    // Diamond shape to visually distinguish NPCs from circular players
    const half = size / 2;
    gfx.fillStyle(0xffffff, 1);
    gfx.fillTriangle(half, 2, size - 2, half, half, size - 2);
    gfx.fillTriangle(half, 2, 2, half, half, size - 2);
    gfx.lineStyle(2, 0x888888, 1);
    gfx.strokeTriangle(half, 2, size - 2, half, half, size - 2);
    gfx.strokeTriangle(half, 2, 2, half, half, size - 2);

    gfx.generateTexture('npc_sprite', size, size);
    gfx.destroy();
  }

  private generateMeleeTexture(): void {
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

  private generateLootBagTexture(): void {
    const size = 28;
    const gfx = this.add.graphics();

    // Bag body — brown rounded sack shape
    gfx.fillStyle(0x8B6914, 1);
    gfx.fillRoundedRect(4, 8, size - 8, size - 10, 4);

    // Bag top — darker cinch
    gfx.fillStyle(0x6B4F10, 1);
    gfx.fillRect(8, 6, size - 16, 5);

    // Tie/knot
    gfx.fillStyle(0x9E7B1A, 1);
    gfx.fillCircle(size / 2, 6, 3);

    // Highlight
    gfx.fillStyle(0xB8942A, 0.5);
    gfx.fillRoundedRect(7, 12, 6, 8, 2);

    // Gold coin glint peeking out
    gfx.fillStyle(0xFFD700, 0.8);
    gfx.fillCircle(size / 2 + 3, 10, 2);

    gfx.generateTexture('loot_bag', size, size);
    gfx.destroy();
  }

  /**
   * Preload equipment overlay sprite sheets and inventory icon images
   * based on items in the data catalog. Returns a promise that resolves
   * once all assets are loaded (or immediately if none are needed).
   */
  private loadEquipmentAssets(): Promise<void> {
    const items = ClientDataManager.instance.items;
    let hasAssets = false;

    // Track which files we've already queued to avoid duplicates
    const queuedSheets = new Set<string>();
    const queuedIcons = new Set<string>();

    // Helper: queue a sheet if not already loaded
    const queueSheet = (filename: string) => {
      if (!filename || queuedSheets.has(filename)) return;
      queuedSheets.add(filename);
      const config = DEFAULT_EQUIP_SPRITE_CONFIG;
      this.load.spritesheet(
        `equip_sheet_${filename}`,
        `assets/sprites/equipment/${filename}`,
        { frameWidth: config.frameWidth, frameHeight: config.frameHeight },
      );
      hasAssets = true;
    };

    for (const itemId of Object.keys(items)) {
      const item = items[itemId];
      if (!item) continue;

      // Queue per-animation equipment overlay sheets
      if (item.equipSpriteSheet) queueSheet(item.equipSpriteSheet);
      if (item.meleeSpriteSheet) queueSheet(item.meleeSpriteSheet);
      if (item.rangedSpriteSheet) queueSheet(item.rangedSpriteSheet);
      if (item.castSpriteSheet) queueSheet(item.castSpriteSheet);

      // Inventory icon
      if (item.inventoryIcon && !queuedIcons.has(item.inventoryIcon)) {
        queuedIcons.add(item.inventoryIcon);
        this.load.image(
          `icon_${item.inventoryIcon}`,
          `assets/sprites/icons/${item.inventoryIcon}`,
        );
        hasAssets = true;
      }
    }

    if (!hasAssets) return Promise.resolve();

    // Start the loader and return a promise that resolves when done
    return new Promise<void>((resolve) => {
      this.load.once('complete', () => {
        const directions: [string, number][] = [
          ['up', 0], ['left', 1], ['down', 2], ['right', 3],
        ];
        const FRAMES_PER_ROW = DEFAULT_EQUIP_SPRITE_CONFIG.framesPerRow;

        // Define animations for each loaded equipment sheet
        for (const sheetFile of queuedSheets) {
          const textureKey = `equip_sheet_${sheetFile}`;
          if (!this.textures.exists(textureKey)) continue;

          // Determine which animation type(s) this sheet is used for
          const animTypes: string[] = [];
          for (const item of Object.values(items)) {
            if (item.equipSpriteSheet === sheetFile) { animTypes.push('walk', 'idle'); break; }
          }
          for (const item of Object.values(items)) {
            if (item.meleeSpriteSheet === sheetFile) { animTypes.push('melee'); break; }
          }
          for (const item of Object.values(items)) {
            if (item.rangedSpriteSheet === sheetFile) { animTypes.push('ranged'); break; }
          }
          for (const item of Object.values(items)) {
            if (item.castSpriteSheet === sheetFile) { animTypes.push('cast'); break; }
          }

          // Each sheet is a standard 4-row layout (up, left, down, right)
          for (const [dir, dirIndex] of directions) {
            if (animTypes.includes('walk')) {
              this.anims.create({
                key: `${textureKey}_walk_${dir}`,
                frames: this.anims.generateFrameNumbers(textureKey, {
                  start: dirIndex * FRAMES_PER_ROW,
                  end: dirIndex * FRAMES_PER_ROW + FRAMES_PER_ROW - 1,
                }),
                frameRate: 12,
                repeat: -1,
              });
            }
            if (animTypes.includes('idle')) {
              this.anims.create({
                key: `${textureKey}_idle_${dir}`,
                frames: [{ key: textureKey, frame: dirIndex * FRAMES_PER_ROW }],
                frameRate: 1,
                repeat: 0,
              });
            }
            if (animTypes.includes('melee')) {
              this.anims.create({
                key: `${textureKey}_melee_${dir}`,
                frames: this.anims.generateFrameNumbers(textureKey, {
                  start: dirIndex * FRAMES_PER_ROW,
                  end: dirIndex * FRAMES_PER_ROW + FRAMES_PER_ROW - 1,
                }),
                frameRate: 14,
                repeat: 0,
              });
            }
            if (animTypes.includes('ranged')) {
              this.anims.create({
                key: `${textureKey}_ranged_${dir}`,
                frames: this.anims.generateFrameNumbers(textureKey, {
                  start: dirIndex * FRAMES_PER_ROW,
                  end: dirIndex * FRAMES_PER_ROW + FRAMES_PER_ROW - 1,
                }),
                frameRate: 14,
                repeat: 0,
              });
            }
            if (animTypes.includes('cast')) {
              this.anims.create({
                key: `${textureKey}_cast_${dir}`,
                frames: this.anims.generateFrameNumbers(textureKey, {
                  start: dirIndex * FRAMES_PER_ROW,
                  end: dirIndex * FRAMES_PER_ROW + FRAMES_PER_ROW - 1,
                }),
                frameRate: 10,
                repeat: -1,
              });
            }
          }
        }
        console.log(`[BootScene] Loaded ${queuedSheets.size} equipment sheets, ${queuedIcons.size} inventory icons`);
        resolve();
      });
      this.load.start();
    });
  }

  /**
   * Load the paperdoll manifest, the base bodies, and only those item layers
   * the item catalog actually references. Falls through silently when the
   * manifest is absent, leaving the LPC path in charge.
   */
  private async loadPaperdollAssets(): Promise<void> {
    const items = ClientDataManager.instance.items;
    const needed = new Set<string>();
    for (const itemId of Object.keys(items)) {
      const spriteId = items[itemId]?.spriteId;
      if (spriteId) needed.add(spriteId);
    }
    try {
      await PaperdollRegistry.instance.load(this, needed);
    } catch (err) {
      console.warn('[BootScene] paperdoll assets failed to load:', err);
    }
  }

  /**
   * Load per-class character sprite sheets (walk, melee, ranged, cast).
   * Each class can have separate sprite sheets configured via the editor.
   * Falls back to the preloaded `player_walk` texture.
   */
  private loadClassSprites(): Promise<void> {
    const classes = ClientDataManager.instance.classes;
    let hasAssets = false;
    const queuedFiles = new Set<string>();

    // Animation type → class field name mapping
    const animFields = ['walk', 'melee', 'ranged', 'cast'] as const;

    for (const classId of Object.keys(classes)) {
      const cls = classes[classId];
      if (!cls) continue;

      for (const animType of animFields) {
        const field = `${animType}SpriteSheet` as keyof typeof cls;
        const filename = cls[field] as string | undefined;
        if (!filename) continue;

        const textureKey = `class_${classId}_${animType}`;
        if (queuedFiles.has(textureKey)) continue;
        queuedFiles.add(textureKey);

        this.load.spritesheet(textureKey, `assets/sprites/characters/${filename}`, {
          frameWidth: 64,
          frameHeight: 64,
        });
        hasAssets = true;
      }
    }

    if (!hasAssets) return Promise.resolve();

    return new Promise<void>((resolve) => {
      this.load.once('complete', () => {
        console.log(`[BootScene] Loaded ${queuedFiles.size} class sprite sheets`);
        resolve();
      });
      this.load.start();
    });
  }

  /**
   * Define per-class player animations.
   * For each class, creates: {classId}_walk_{dir}, {classId}_idle_{dir},
   * {classId}_melee_{dir}, {classId}_ranged_{dir}, {classId}_cast_{dir}.
   * Falls back to `player_walk` texture when a class has no custom sheet.
   */
  private definePlayerAnimations(): void {
    const directions: [string, number][] = [
      ['up',    0],
      ['left',  1],
      ['down',  2],
      ['right', 3],
    ];
    const FRAMES_PER_ROW = 9;
    const classes = ClientDataManager.instance.classes;

    // Animation types: [type, frameRate, repeat]
    const animConfig: [string, number, number][] = [
      ['walk',   12, -1],  // looping
      ['melee',  14,  0],  // play once
      ['ranged', 14,  0],  // play once
      ['cast',   10, -1],  // looping
    ];

    for (const classId of Object.keys(classes)) {
      for (const [dir, dirIndex] of directions) {
        // Walk + idle always created (from walk texture or fallback)
        const walkTexture = this.textures.exists(`class_${classId}_walk`)
          ? `class_${classId}_walk` : 'player_walk';

        this.anims.create({
          key: `${classId}_walk_${dir}`,
          frames: this.anims.generateFrameNumbers(walkTexture, {
            start: dirIndex * FRAMES_PER_ROW,
            end: dirIndex * FRAMES_PER_ROW + FRAMES_PER_ROW - 1,
          }),
          frameRate: 12,
          repeat: -1,
        });
        this.anims.create({
          key: `${classId}_idle_${dir}`,
          frames: [{ key: walkTexture, frame: dirIndex * FRAMES_PER_ROW }],
          frameRate: 1,
          repeat: 0,
        });

        // Action animations (melee, ranged, cast)
        for (const [animType, frameRate, repeat] of animConfig) {
          if (animType === 'walk') continue; // already handled above

          const classTexture = `class_${classId}_${animType}`;
          // Use class-specific texture if available, else fall back to walk texture
          const texture = this.textures.exists(classTexture) ? classTexture : walkTexture;

          this.anims.create({
            key: `${classId}_${animType}_${dir}`,
            frames: this.anims.generateFrameNumbers(texture, {
              start: dirIndex * FRAMES_PER_ROW,
              end: dirIndex * FRAMES_PER_ROW + FRAMES_PER_ROW - 1,
            }),
            frameRate,
            repeat,
          });
        }
      }
    }
  }
}
