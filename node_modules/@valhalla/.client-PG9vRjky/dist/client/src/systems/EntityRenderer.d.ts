import Phaser from 'phaser';
import type { PaperdollAnim, PaperdollDir } from '@valhalla/shared';
export declare const ENTITY_DEPTH_BASE = 600000;
export declare const NAMEPLATE_DEPTH = 800000;
export declare const UI_DEPTH_BASE = 1000000;
/**
 * Vertical offset from a sprite's origin to just above its head.
 *
 * LPC sprites use Phaser's centred origin; paperdoll sprites are anchored at
 * the feet so they stand on the tile they occupy. Anything that floats above a
 * character (nameplate, HP bar) has to read the origin rather than assume one.
 */
export declare function headroom(sprite: Phaser.GameObjects.Sprite): number;
/**
 * Normalise an animation name to a paperdoll cycle.
 *
 * Combat still speaks the old LPC words in places ('melee', 'ranged'); this is
 * the single boundary where they become cycles, so no call site has to be
 * hunted down and renamed.
 */
export declare function toPaperdollAnim(name: string): PaperdollAnim;
/** True when this sprite is drawn from the paperdoll pack. */
export declare function isPaperdollSprite(sprite: Phaser.GameObjects.Sprite): boolean;
/** The paperdoll layer id behind a sprite, e.g. `pd_body_tan` -> `body_tan`. */
export declare function paperdollLayerOf(sprite: Phaser.GameObjects.Sprite): string;
/**
 * Base character sprite. Uses the paperdoll body when the pack is loaded and
 * the body exists, otherwise the class LPC sheet, otherwise the fallback body.
 */
export declare function createCharacterSprite(scene: Phaser.Scene, x: number, y: number, classId: string, bodyId?: string): Phaser.GameObjects.Sprite;
/**
 * The animation key for a character sprite, in whichever system it belongs to.
 * Returns null when the animation does not exist, so callers can skip the play.
 */
export declare function characterAnimKey(scene: Phaser.Scene, sprite: Phaser.GameObjects.Sprite, classId: string, anim: PaperdollAnim, dir: PaperdollDir): string | null;
/** Play a cycle on a character sprite. No-op when that animation is missing. */
export declare function playCharacterAnim(scene: Phaser.Scene, sprite: Phaser.GameObjects.Sprite, classId: string, anim: PaperdollAnim, dir: PaperdollDir, ignoreIfPlaying?: boolean): void;
/** Equipment overlay sprites layered on top of the character. */
interface EquipmentOverlay {
    slot: string;
    itemId: string;
    sprite: Phaser.GameObjects.Sprite;
    /** Paperdoll layer id when this overlay is a paperdoll layer, else undefined. */
    layerId?: string;
    /** Paperdoll composite slot ('mainhand', 'chest', ...), else undefined. */
    pdSlot?: string;
}
/** Lightweight buff descriptor received from the server's NpcBuffInfo schema. */
export interface NpcBuffData {
    skillId: string;
    expiresAt: number;
    dotDamagePerSec: number;
}
export interface PlayerTargetInfo {
    characterName: string;
    classId: string;
    level: number;
    hp: number;
    maxHp: number;
    alive: boolean;
}
export interface NpcTargetInfo {
    name: string;
    level: number;
    hp: number;
    maxHp: number;
    alive: boolean;
    npcType: string;
    /** Active debuffs visible in the target pane. */
    buffs: NpcBuffData[];
}
export declare class EntityRenderer {
    private scene;
    private remotePlayers;
    private npcs;
    private projectiles;
    private spellProjectiles;
    private lootBags;
    /** Called when a remote player sprite is left-clicked. */
    onPlayerClick?: (sessionId: string) => void;
    /** Called when an NPC sprite is left-clicked. */
    onNpcClick?: (npcId: string) => void;
    /** Called when an NPC sprite is right-clicked. */
    onNpcRightClick?: (npcId: string) => void;
    /** Called when a loot bag sprite is clicked (button: 0=left, 2=right). */
    onBagClick?: (bagId: string, button: number) => void;
    constructor(scene: Phaser.Scene);
    addRemotePlayer(sessionId: string, x: number, y: number, classId?: string, level?: number, characterName?: string, bodyId?: string): void;
    removeRemotePlayer(sessionId: string): void;
    updateRemotePlayerTarget(sessionId: string, x: number, y: number, aimAngle: number): void;
    updateRemotePlayerHp(sessionId: string, hp: number, maxHp: number, alive: boolean, level?: number): void;
    /**
     * Update equipment overlays for a remote player.
     * @param equipment - Record of slot → itemId (empty string = nothing equipped)
     */
    updateRemotePlayerEquipment(sessionId: string, equipment: Record<string, string>): void;
    /**
     * Create/update/remove overlay sprites to match the given equipment map.
     * Works for both remote players and can be called externally for the local player.
     */
    syncOverlays(overlays: EquipmentOverlay[], equipment: Record<string, string>, baseSprite: Phaser.GameObjects.Sprite): void;
    /**
     * Extract the animation type and direction from a class-prefixed animation key.
     * e.g. "warrior_melee_down" → { animType: "melee", dir: "down" }
     * e.g. "warrior_idle_up" → { animType: "idle", dir: "up" }
     */
    private parseAnimKey;
    /**
     * Sync overlay sprite positions, animations, and visibility with a base sprite.
     * Each overlay can have different sprite sheets per animation type.
     * Called each frame from the update loop and externally for the local player.
     */
    updateOverlayPositions(overlays: EquipmentOverlay[], baseSprite: Phaser.GameObjects.Sprite): void;
    addNPC(id: string, x: number, y: number, name: string, level: number, npcType: string, spriteColor: number, spriteSize: number): void;
    removeNPC(id: string): void;
    updateNPCTarget(id: string, x: number, y: number, aimAngle: number): void;
    updateNPCHp(id: string, hp: number, maxHp: number, alive: boolean): void;
    /** Update the cached active buff list for an NPC (from syncedBuffs schema changes). */
    updateNPCBuffs(id: string, buffs: NpcBuffData[]): void;
    hasNPC(id: string): boolean;
    getNPCPosition(id: string): {
        x: number;
        y: number;
    } | null;
    addLootBag(bagId: string, x: number, y: number, zoneId: string): void;
    removeLootBag(bagId: string): void;
    hasLootBag(bagId: string): boolean;
    getLootBagPosition(bagId: string): {
        x: number;
        y: number;
    } | null;
    /** Get the ortho world position of a loot bag. */
    getLootBagOrthoPosition(bagId: string): {
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
     *
     * Graphics are drawn at (0, 0) in local space; the objects are positioned
     * at (x, y) in world space. Phaser's tween then scales around (x, y),
     * not around the world origin.
     */
    showSpellImpact(x: number, y: number, radius: number, _skillId: string): void;
    /**
     * Draw the fireball visual at local origin (0, 0).
     * The caller is responsible for positioning the returned Graphics object
     * in world space by setting gfx.x / gfx.y.
     */
    private drawFireball;
    showMeleeSlash(x: number, y: number, angle: number, isIso?: boolean): void;
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
    /**
     * Play an action animation (melee, ranged, cast) on a remote player.
     * animType: 'melee' | 'ranged' | 'cast'
     * direction: optional override; defaults to the player's current lastDir.
     */
    playRemoteActionAnim(sessionId: string, animType: string, direction?: string): void;
    /** Stop a looping action animation (e.g. cast) on a remote player. */
    stopRemoteActionAnim(sessionId: string): void;
    getPlayerTargetInfo(sessionId: string): PlayerTargetInfo | null;
    getNpcTargetInfo(id: string): NpcTargetInfo | null;
    /**
     * Play the Magic Missile visual effect:
     *   1. A bright arcane burst at the caster's feet (launch flash).
     *   2. A glowing blue-white bolt that travels to the target.
     *   3. An arcane impact burst at the target position.
     *
     * All effects are purely cosmetic Phaser Graphics tweens — no game state
     * is modified here.
     */
    showMagicMissileVFX(fromX: number, fromY: number, toX: number, toY: number): void;
}
export {};
//# sourceMappingURL=EntityRenderer.d.ts.map