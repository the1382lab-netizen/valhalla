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
export declare class BootScene extends Phaser.Scene {
    constructor();
    preload(): void;
    create(): Promise<void>;
    private generateTileTextures;
    private generatePlayerTexture;
    private generateProjectileTexture;
    private generateSkillIcons;
    private generateNpcTexture;
    private generateMeleeTexture;
    private generateLootBagTexture;
    /**
     * Preload equipment overlay sprite sheets and inventory icon images
     * based on items in the data catalog. Returns a promise that resolves
     * once all assets are loaded (or immediately if none are needed).
     */
    private loadEquipmentAssets;
    /**
     * Load the paperdoll manifest, the base bodies, and only those item layers
     * the item catalog actually references. Falls through silently when the
     * manifest is absent, leaving the LPC path in charge.
     */
    private loadPaperdollAssets;
    /**
     * Load per-class character sprite sheets (walk, melee, ranged, cast).
     * Each class can have separate sprite sheets configured via the editor.
     * Falls back to the preloaded `player_walk` texture.
     */
    private loadClassSprites;
    /**
     * Define per-class player animations.
     * For each class, creates: {classId}_walk_{dir}, {classId}_idle_{dir},
     * {classId}_melee_{dir}, {classId}_ranged_{dir}, {classId}_cast_{dir}.
     * Falls back to `player_walk` texture when a class has no custom sheet.
     */
    private definePlayerAnimations;
}
//# sourceMappingURL=BootScene.d.ts.map