// ── Stat Computation ───────────────────────────────────────
// Single source of truth for resolving a character's stats.
// Called server-side on join, level-up, and (later) gear changes.
import { ClassId, CLASS_TEMPLATES } from './classes.js';
/**
 * Compute the full stat block for a character.
 * Formula: base + (level - 1) * perLevel
 * Level 1 = base stats. Each level after adds perLevel.
 */
export function computeDerivedStats(classId, level) {
    const template = CLASS_TEMPLATES[classId];
    const base = template.baseStats;
    const growth = template.statsPerLevel;
    const lvl = Math.max(1, Math.min(20, level)); // clamp 1–20
    const levelsGained = lvl - 1;
    const stats = {
        hp: base.hp + growth.hp * levelsGained,
        mana: base.mana + growth.mana * levelsGained,
        strength: base.strength + growth.strength * levelsGained,
        stamina: base.stamina + growth.stamina * levelsGained,
        dexterity: base.dexterity + growth.dexterity * levelsGained,
        intelligence: base.intelligence + growth.intelligence * levelsGained,
        wisdom: base.wisdom + growth.wisdom * levelsGained,
        physicalResist: base.physicalResist + growth.physicalResist * levelsGained,
        spellResist: base.spellResist + growth.spellResist * levelsGained,
        critChance: base.critChance + growth.critChance * levelsGained,
        critDamage: base.critDamage + growth.critDamage * levelsGained,
        physicalDefense: base.physicalDefense + growth.physicalDefense * levelsGained,
        blockRating: base.blockRating + growth.blockRating * levelsGained,
        dodgeRating: base.dodgeRating + growth.dodgeRating * levelsGained,
    };
    // Energy for non-casters, 0 for casters
    const maxEnergy = template.canUseMana ? 0 : 100 + stats.stamina * 2;
    const energyRegenRate = template.canUseMana ? 0 : 10 + stats.stamina * 0.5;
    // Mana regen for casters, 0 for non-casters
    const manaRegenRate = template.canUseMana ? 5 + stats.intelligence * 0.4 : 0;
    return {
        ...stats,
        maxHp: stats.hp,
        maxMana: stats.mana,
        maxEnergy,
        energyRegenRate,
        manaRegenRate,
        speed: template.baseSpeed,
    };
}
// ── Damage Formulas ────────────────────────────────────────
/** Base damage constants — the "weapon" component before stats scale it */
export const BASE_MELEE_DAMAGE = 10;
export const BASE_RANGED_DAMAGE = 8;
export const BASE_SPELL_DAMAGE = 12;
/**
 * Physical damage = baseDamage + strength * scaling
 * Scaling is intentionally gentle at low levels to avoid one-shots.
 */
export function computePhysicalDamage(strength, baseDamage) {
    return baseDamage + strength * 0.8;
}
/**
 * Spell damage = baseDamage + intelligence * scaling
 */
export function computeSpellDamage(intelligence, baseDamage) {
    return baseDamage + intelligence * 0.9;
}
/**
 * Apply defense reduction. Uses a diminishing returns formula so
 * defense never fully negates damage (minimum 15% of raw damage gets through).
 *
 * reduction = defense / (defense + 50)
 * At 10 defense: 16.7% reduction
 * At 25 defense: 33% reduction
 * At 50 defense: 50% reduction
 */
export function applyDefenseReduction(rawDamage, defense) {
    const reduction = defense / (defense + 50);
    const minDamage = rawDamage * 0.15;
    return Math.max(minDamage, rawDamage * (1 - reduction));
}
/**
 * Roll for a critical hit. Returns the final damage multiplier.
 * Normal hit = 1.0, Crit = 1.0 + critDamage
 */
export function rollCrit(critChance, critDamage) {
    const isCrit = Math.random() < critChance;
    return {
        isCrit,
        multiplier: isCrit ? 1 + critDamage : 1,
    };
}
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
export function computeHitChance(dexterity) {
    const raw = 0.65 + dexterity / (dexterity + 40);
    return Math.min(0.99, raw);
}
/**
 * Roll for hit. Returns true if the attack lands.
 * Miss = the attack doesn't connect at all (no damage, no dodge/block needed).
 */
export function rollHit(dexterity) {
    return Math.random() < computeHitChance(dexterity);
}
/**
 * Roll for dodge. Returns true if the attack is dodged.
 */
export function rollDodge(dodgeRating) {
    return Math.random() < dodgeRating;
}
/**
 * Roll for block. Returns true if the attack is blocked.
 * Blocked attacks deal 50% damage.
 */
export function rollBlock(blockRating) {
    return Math.random() < blockRating;
}
/**
 * Compute fire cooldown modified by dexterity.
 * Higher dex = faster attacks. Caps at 40% reduction.
 */
export function computeFireCooldown(baseCooldownMs, dexterity) {
    const reduction = Math.min(0.4, dexterity * 0.012);
    return baseCooldownMs * (1 - reduction);
}
/**
 * Compute melee cooldown modified by dexterity.
 * Same formula as fire cooldown.
 */
export function computeMeleeCooldown(baseCooldownMs, dexterity) {
    const reduction = Math.min(0.4, dexterity * 0.012);
    return baseCooldownMs * (1 - reduction);
}
// ── XP & Leveling ──────────────────────────────────────────
export const BASE_XP_PER_LEVEL = 100;
export const XP_SCALING = 2;
export const MAX_LEVEL = 20;
/**
 * XP required to reach the next level.
 * Level 1→2: 100 XP, Level 2→3: 200 XP, etc.
 */
export function xpRequiredForLevel(currentLevel) {
    if (currentLevel >= MAX_LEVEL)
        return Infinity;
    return BASE_XP_PER_LEVEL * Math.pow(XP_SCALING, currentLevel - 1);
}
/**
 * Determine whether the player's class uses ranged attacks as physical or magical.
 * Used to decide which damage formula to apply for projectiles.
 */
export function isRangedMagic(classId) {
    return classId === ClassId.WIZARD || classId === ClassId.CLERIC || classId === ClassId.SHAMAN;
}
/**
 * Determine whether the player's class supports a ranged attack (right-click fire).
 * Currently only Rangers have a ranged basic attack; all other classes are melee-only.
 */
export function hasRangedAttack(classId) {
    return classId === ClassId.RANGER;
}
//# sourceMappingURL=stats.js.map