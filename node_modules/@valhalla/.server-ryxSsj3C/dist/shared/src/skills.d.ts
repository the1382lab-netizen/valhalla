/**
 * Skill & Spell Definitions — shared between client and server.
 * This is the single source of truth for all skill data.
 *
 * 38 skills across 6 classes, populated from the design spreadsheet.
 * Individual skill effect handlers live server-side; this file
 * contains only the data model and catalog.
 */
import { ClassId } from './classes.js';
export declare enum SkillId {
    MELEE_ATTACK = "melee_attack",
    RANGED_ATTACK = "ranged_attack",
    WARRIOR_SHIELD_BASH = "warrior_shield_bash",
    WARRIOR_TAUNT = "warrior_taunt",
    WARRIOR_CLEAVE = "warrior_cleave",
    WARRIOR_BATTLE_SHOUT = "warrior_battle_shout",
    WARRIOR_CHARGE = "warrior_charge",
    WARRIOR_SHIELD_WALL = "warrior_shield_wall",
    CLERIC_MINOR_HEAL = "cleric_minor_heal",
    CLERIC_SMITE = "cleric_smite",
    CLERIC_SHIELD_OF_FAITH = "cleric_shield_of_faith",
    CLERIC_CURE_AILMENT = "cleric_cure_ailment",
    CLERIC_HOLY_LIGHT = "cleric_holy_light",
    CLERIC_DIVINE_HAMMER = "cleric_divine_hammer",
    CLERIC_RESURRECTION = "cleric_resurrection",
    RANGER_AIMED_SHOT = "ranger_aimed_shot",
    RANGER_SERPENT_ARROW = "ranger_serpent_arrow",
    RANGER_TRAP = "ranger_trap",
    RANGER_MULTI_SHOT = "ranger_multi_shot",
    RANGER_CAMOUFLAGE = "ranger_camouflage",
    RANGER_SNIPE = "ranger_snipe",
    ROGUE_BACKSTAB = "rogue_backstab",
    ROGUE_POISON_BLADE = "rogue_poison_blade",
    ROGUE_STEALTH = "rogue_stealth",
    ROGUE_KIDNEY_SHOT = "rogue_kidney_shot",
    ROGUE_EVASION = "rogue_evasion",
    ROGUE_SHADOW_DANCE = "rogue_shadow_dance",
    SHAMAN_LIGHTNING_BOLT = "shaman_lightning_bolt",
    SHAMAN_EARTH_SHIELD = "shaman_earth_shield",
    SHAMAN_HEX = "shaman_hex",
    SHAMAN_ANCESTRAL_SPIRIT = "shaman_ancestral_spirit",
    SHAMAN_FLAME_SHOCK = "shaman_flame_shock",
    SHAMAN_BLOODLUST = "shaman_bloodlust",
    WIZARD_FIREBALL = "wizard_fireball",
    WIZARD_MAGIC_MISSILE = "wizard_magic_missile",
    WIZARD_FROST_NOVA = "wizard_frost_nova",
    WIZARD_BLINK = "wizard_blink",
    WIZARD_ARCANE_MISSILES = "wizard_arcane_missiles",
    WIZARD_MANA_SHIELD = "wizard_mana_shield",
    WIZARD_METEOR = "wizard_meteor"
}
export declare enum ResourceType {
    MANA = "mana",
    ENERGY = "energy",
    NONE = "none"
}
export declare enum SkillTargetType {
    SELF = "self",
    SINGLE_ENEMY = "singleEnemy",
    SINGLE_ALLY = "singleAlly",
    AOE_GROUND = "aoeGround",
    AOE_SELF = "aoeSelf",
    CONE = "cone",
    PASSIVE_TOGGLE = "passiveToggle"
}
export declare enum SkillCategory {
    OFFENSIVE = "offensive",
    DEFENSIVE = "defensive",
    HEALING = "healing",
    BUFF = "buff",
    DEBUFF = "debuff",
    UTILITY = "utility"
}
export interface SkillTemplate {
    id: SkillId;
    name: string;
    description: string;
    classId: ClassId | null;
    levelRequired: number;
    resourceType: ResourceType;
    resourceCost: number;
    castTimeMs: number;
    cooldownMs: number;
    range: number;
    targetType: SkillTargetType;
    category: SkillCategory;
    iconColor: number;
    iconAbbrev: string;
    scalingStat: string;
    baseDamage?: [number, number];
    baseHealing?: [number, number];
    buffDurationMs?: number;
    dotDamagePerSec?: number;
    hotHealPerSec?: number;
    /** Blast radius in pixels for AOE_GROUND spells (e.g. Fireball, Meteor). */
    aoeRadius?: number;
    /** Projectile metadata — if present, the skill spawns a projectile rather than applying damage instantly. */
    projectile?: {
        speed: number;
        radius: number;
    };
    /** Cooldown group ID — all skills sharing the same group share a cooldown. */
    cooldownGroup?: string;
    /** Buff stacking mode: 'replace' (default), 'stack', or 'extend'. */
    stackingMode?: 'replace' | 'stack' | 'extend';
    /** Max stacks for stackable buffs (only used when stackingMode === 'stack'). Default 1. */
    maxStacks?: number;
    /** If true, this skill auto-repeats at the player's attack speed interval. */
    isAutoAttack?: boolean;
    /** If true, the skill cannot be used without a weapon equipped. */
    requiresWeapon?: boolean;
    effectNotes: string;
}
export declare const SKILL_CATALOG: Record<SkillId, SkillTemplate>;
/** Maps each class to its skill list, ordered by level requirement. */
export declare const CLASS_SKILLS: Record<ClassId, SkillId[]>;
/**
 * Get all skills available to a class at or below the given level.
 */
export declare function getAvailableSkills(classId: ClassId, level: number): SkillTemplate[];
/**
 * Get the resource type used by a class.
 */
export declare function getClassResourceType(classId: ClassId): ResourceType;
/** Action bar size (number of slots). */
export declare const ACTION_BAR_SLOTS = 8;
//# sourceMappingURL=skills.d.ts.map