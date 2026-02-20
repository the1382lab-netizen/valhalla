import Phaser from 'phaser';
import { SkillId, SkillCategory, SERVER_URL, DEFAULT_EQUIP_SPRITE_CONFIG } from '@valhalla/shared';
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
    // Player walk sprite sheet: 4 rows (Up, Left, Down, Right) × 9 frames, 64×64 px
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

    // ── Generate player sprite texture ───────────────────────
    this.generatePlayerTexture();

    // ── Generate projectile texture ──────────────────────────
    this.generateProjectileTexture();

    // ── Generate NPC sprite texture ──────────────────────────
    this.generateNpcTexture();

    // ── Generate melee slash texture ─────────────────────────
    this.generateMeleeTexture();

    // ── Generate skill icon textures ───────────────────────
    this.generateSkillIcons();

    // ── Define player walk/idle animations ─────────────────
    this.definePlayerAnimations();

    // ── Preload equipment overlays & inventory icons ────────
    await this.loadEquipmentAssets();

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

    for (const itemId of Object.keys(items)) {
      const item = items[itemId];
      if (!item) continue;

      // Equipment overlay sprite sheet
      if (item.equipSpriteSheet && !queuedSheets.has(item.equipSpriteSheet)) {
        queuedSheets.add(item.equipSpriteSheet);
        const config = item.equipSpriteConfig ?? DEFAULT_EQUIP_SPRITE_CONFIG;
        this.load.spritesheet(
          `equip_sheet_${item.equipSpriteSheet}`,
          `assets/sprites/equipment/${item.equipSpriteSheet}`,
          { frameWidth: config.frameWidth, frameHeight: config.frameHeight },
        );
        hasAssets = true;
      }

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
        // Define walk/idle animations for each equipment overlay sheet
        for (const sheetFile of queuedSheets) {
          const textureKey = `equip_sheet_${sheetFile}`;
          // Find one item using this sheet to get its config
          const refItem = Object.values(items).find(i => i.equipSpriteSheet === sheetFile);
          const config = refItem?.equipSpriteConfig ?? DEFAULT_EQUIP_SPRITE_CONFIG;
          const FRAMES_PER_ROW = config.framesPerRow;
          const directions: [string, number][] = [
            ['up', 0], ['left', 1], ['down', 2], ['right', 3],
          ];
          for (const [dir, row] of directions) {
            if (row >= config.rows) continue;
            this.anims.create({
              key: `${textureKey}_walk_${dir}`,
              frames: this.anims.generateFrameNumbers(textureKey, {
                start: row * FRAMES_PER_ROW,
                end: row * FRAMES_PER_ROW + FRAMES_PER_ROW - 1,
              }),
              frameRate: 12,
              repeat: -1,
            });
            this.anims.create({
              key: `${textureKey}_idle_${dir}`,
              frames: [{ key: textureKey, frame: row * FRAMES_PER_ROW }],
              frameRate: 1,
              repeat: 0,
            });
          }
        }
        console.log(`[BootScene] Loaded ${queuedSheets.size} equipment sheets, ${queuedIcons.size} inventory icons`);
        resolve();
      });
      this.load.start();
    });
  }

  /**
   * Define walk and idle animations from the player sprite sheet.
   * Sheet layout: 4 rows (Up, Left, Down, Right), 9 frames per row, 64×64 px.
   */
  private definePlayerAnimations(): void {
    // Row order in the sprite sheet
    const directions: [string, number][] = [
      ['up',    0],
      ['left',  1],
      ['down',  2],
      ['right', 3],
    ];
    const FRAMES_PER_ROW = 9;

    for (const [dir, row] of directions) {
      // Walk animation — full 9-frame loop
      this.anims.create({
        key: `walk_${dir}`,
        frames: this.anims.generateFrameNumbers('player_walk', {
          start: row * FRAMES_PER_ROW,
          end: row * FRAMES_PER_ROW + FRAMES_PER_ROW - 1,
        }),
        frameRate: 12,
        repeat: -1,
      });

      // Idle animation — single first frame of the row
      this.anims.create({
        key: `idle_${dir}`,
        frames: [{ key: 'player_walk', frame: row * FRAMES_PER_ROW }],
        frameRate: 1,
        repeat: 0,
      });
    }
  }
}
