import { ClassId, StatBlock } from './classes.js';
/**
 * The full resolved stat block for a character at a given level.
 * This is what the server uses for all combat/movement math.
 */
export interface ResolvedStats extends StatBlock {
    maxHp: number;
    maxMana: number;
    speed: number;
}
/**
 * Compute the full stat block for a character.
 * Formula: base + (level - 1) * perLevel
 * Level 1 = base stats. Each level after adds perLevel.
 */
export declare function computeDerivedStats(classId: ClassId, level: number): ResolvedStats;
/** Base damage constants — the "weapon" component before stats scale it */
export declare const BASE_MELEE_DAMAGE = 10;
export declare const BASE_RANGED_DAMAGE = 8;
export declare const BASE_SPELL_DAMAGE = 12;
/**
 * Physical damage = baseDamage + strength * scaling
 * Scaling is intentionally gentle at low levels to avoid one-shots.
 */
export declare function computePhysicalDamage(strength: number, baseDamage: number): number;
/**
 * Spell damage = baseDamage + intelligence * scaling
 */
export declare function computeSpellDamage(intelligence: number, baseDamage: number): number;
/**
 * Apply defense reduction. Uses a diminishing returns formula so
 * defense never fully negates damage (minimum 15% of raw damage gets through).
 *
 * reduction = defense / (defense + 50)
 * At 10 defense: 16.7% reduction
 * At 25 defense: 33% reduction
 * At 50 defense: 50% reduction
 */
export declare function applyDefenseReduction(rawDamage: number, defense: number): number;
/**
 * Roll for a critical hit. Returns the final damage multiplier.
 * Normal hit = 1.0, Crit = 1.0 + critDamage
 */
export declare function rollCrit(critChance: number, critDamage: number): {
    isCrit: boolean;
    multiplier: number;
};
/**
 * Compute hit chance from attacker's dexterity.
 * Uses a soft-cap formula: hitChance = 0.65 + dex / (dex + 40)
 *
 * This gives a baseline ~65% hit chance at 0 dex, scaling up with diminishing returns:
 *   Dex  6 → 78%   (Wizard / Cleric baseline)
 *   Dex 10 → 85%   (Warrior / Shaman baseline)
 *   Dex 18 → 96%   (Rogue baseline)
 *   Dex 20 → 98%   (Ranger baseline)
 *
 * Caps at 99% — attacks can always miss (1% minimum miss chance).
 */
export declare function computeHitChance(dexterity: number): number;
/**
 * Roll for hit. Returns true if the attack lands.
 * Miss = the attack doesn't connect at all (no damage, no dodge/block needed).
 */
export declare function rollHit(dexterity: number): boolean;
/**
 * Roll for dodge. Returns true if the attack is dodged.
 */
export declare function rollDodge(dodgeRating: number): boolean;
/**
 * Roll for block. Returns true if the attack is blocked.
 * Blocked attacks deal 50% damage.
 */
export declare function rollBlock(blockRating: number): boolean;
/**
 * Compute fire cooldown modified by dexterity.
 * Higher dex = faster attacks. Caps at 40% reduction.
 */
export declare function computeFireCooldown(baseCooldownMs: number, dexterity: number): number;
/**
 * Compute melee cooldown modified by dexterity.
 * Same formula as fire cooldown.
 */
export declare function computeMeleeCooldown(baseCooldownMs: number, dexterity: number): number;
export declare const BASE_XP_PER_LEVEL = 100;
export declare const XP_SCALING = 2;
export declare const MAX_LEVEL = 20;
/**
 * XP required to reach the next level.
 * Level 1→2: 100 XP, Level 2→3: 200 XP, etc.
 */
export declare function xpRequiredForLevel(currentLevel: number): number;
/**
 * Determine whether the player's class uses ranged attacks as physical or magical.
 * Used to decide which damage formula to apply for projectiles.
 */
export declare function isRangedMagic(classId: ClassId): boolean;
//# sourceMappingURL=stats.d.ts.map