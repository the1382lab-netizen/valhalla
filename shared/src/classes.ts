// ── Class Definitions ──────────────────────────────────────
// All class balance data lives here as plain data objects.
// No class hierarchies — just lookup tables the server reads.

export enum ClassId {
  WARRIOR = 'warrior',
  CLERIC = 'cleric',
  RANGER = 'ranger',
  ROGUE = 'rogue',
  SHAMAN = 'shaman',
  WIZARD = 'wizard',
}

export type ArmorType = 'cloth' | 'leather' | 'mail' | 'plate';

/**
 * Raw stat block shared between base stats and per-level growth.
 * All values are absolute (not percentages) except critChance/critDamage/blockRating/dodgeRating
 * which are stored as decimals (0.05 = 5%).
 */
export interface StatBlock {
  hp: number;
  mana: number;
  strength: number;
  stamina: number;
  dexterity: number;
  intelligence: number;
  wisdom: number;
  physicalResist: number;
  spellResist: number;
  critChance: number;      // decimal: 0.05 = 5%
  critDamage: number;      // decimal: 0.5 = +50% crit bonus
  physicalDefense: number;
  blockRating: number;     // decimal: 0.10 = 10% block chance
  dodgeRating: number;     // decimal: 0.05 = 5% dodge chance
}

/**
 * Represents a single item given to a character on creation.
 * `equipped: true` means it will be placed into the character_equipment table
 * at the slot derived from the item's equipSlot property.
 * `equipped: false` means it will be placed into the inventory_items table.
 */
export interface StartingItem {
  itemId: string;
  quantity: number;
  equipped: boolean;
}

export interface ClassTemplate {
  id: ClassId;
  name: string;
  description: string;
  baseStats: StatBlock;
  statsPerLevel: StatBlock;
  allowedArmor: ArmorType;
  baseSpeed: number;       // pixels per second
  canUseMana: boolean;
  /** Base melee attack speed in ms when no weapon is equipped. Scaled by dexterity. */
  baseMeleeAttackSpeedMs: number;
  /** Base ranged attack speed in ms when no weapon is equipped. Only meaningful for Ranger. */
  baseRangedAttackSpeedMs: number;
  /** Line-of-sight vision range in UE units (cm) */
  visionRange: number;
  startingItems?: StartingItem[];
  /**
   * Default paperdoll body for this class, e.g. `body_tan`. Overridden by the
   * character's own `bodyId` once appearance selection exists.
   */
  bodyId?: string;
}

// ── Class Templates ────────────────────────────────────────
// Tuned from GDD: classes are highly specialized, no overlap in core function.
// Level 1 values. Growth per level stacks linearly.

export const CLASS_TEMPLATES: Record<ClassId, ClassTemplate> = {
  [ClassId.WARRIOR]: {
    id: ClassId.WARRIOR,
    name: 'Warrior',
    description: 'Front-line defender. Holds aggro so the party survives.',
    baseStats: {
      hp: 150,
      mana: 0,
      strength: 20,
      stamina: 18,
      dexterity: 10,
      intelligence: 4,
      wisdom: 4,
      physicalResist: 8,
      spellResist: 3,
      critChance: 0.05,
      critDamage: 0.5,
      physicalDefense: 12,
      blockRating: 0.15,
      dodgeRating: 0.03,
    },
    statsPerLevel: {
      hp: 18,
      mana: 0,
      strength: 3,
      stamina: 3,
      dexterity: 1,
      intelligence: 0,
      wisdom: 0,
      physicalResist: 1,
      spellResist: 0,
      critChance: 0.005,
      critDamage: 0.02,
      physicalDefense: 2,
      blockRating: 0.01,
      dodgeRating: 0.002,
    },
    allowedArmor: 'plate',
    baseSpeed: 144,
    canUseMana: false,
    baseMeleeAttackSpeedMs: 3600,
    baseRangedAttackSpeedMs: 0,
    visionRange: 1200,
    startingItems: [
      { itemId: 'iron_sword', quantity: 1, equipped: true },
      { itemId: 'health_potion', quantity: 5, equipped: false },
      { itemId: 'mana_potion', quantity: 3, equipped: false },
    ],
  },

  [ClassId.CLERIC]: {
    id: ClassId.CLERIC,
    name: 'Cleric',
    description: 'The lifeline. Only true healer in the game.',
    baseStats: {
      hp: 100,
      mana: 120,
      strength: 8,
      stamina: 12,
      dexterity: 6,
      intelligence: 10,
      wisdom: 20,
      physicalResist: 4,
      spellResist: 8,
      critChance: 0.03,
      critDamage: 0.4,
      physicalDefense: 6,
      blockRating: 0.08,
      dodgeRating: 0.03,
    },
    statsPerLevel: {
      hp: 10,
      mana: 14,
      strength: 1,
      stamina: 1,
      dexterity: 0,
      intelligence: 1,
      wisdom: 3,
      physicalResist: 0,
      spellResist: 1,
      critChance: 0.003,
      critDamage: 0.02,
      physicalDefense: 1,
      blockRating: 0.005,
      dodgeRating: 0.002,
    },
    allowedArmor: 'mail',
    baseSpeed: 152,
    canUseMana: true,
    baseMeleeAttackSpeedMs: 4400,
    baseRangedAttackSpeedMs: 0,
    visionRange: 1200,
    startingItems: [
      { itemId: 'iron_mace', quantity: 1, equipped: true },
      { itemId: 'health_potion', quantity: 5, equipped: false },
      { itemId: 'mana_potion', quantity: 3, equipped: false },
    ],
  },

  [ClassId.RANGER]: {
    id: ClassId.RANGER,
    name: 'Ranger',
    description: 'Ranged damage dealer and party eyes. Fastest class.',
    baseStats: {
      hp: 110,
      mana: 0,
      strength: 10,
      stamina: 12,
      dexterity: 20,
      intelligence: 6,
      wisdom: 6,
      physicalResist: 4,
      spellResist: 4,
      critChance: 0.08,
      critDamage: 0.6,
      physicalDefense: 6,
      blockRating: 0.02,
      dodgeRating: 0.10,
    },
    statsPerLevel: {
      hp: 12,
      mana: 0,
      strength: 1,
      stamina: 1,
      dexterity: 3,
      intelligence: 0,
      wisdom: 0,
      physicalResist: 0,
      spellResist: 0,
      critChance: 0.006,
      critDamage: 0.03,
      physicalDefense: 1,
      blockRating: 0.002,
      dodgeRating: 0.008,
    },
    allowedArmor: 'leather',
    baseSpeed: 184,
    canUseMana: false,
    baseMeleeAttackSpeedMs: 3000,
    baseRangedAttackSpeedMs: 4000,
    visionRange: 1800,
    startingItems: [
      { itemId: 'short_bow', quantity: 1, equipped: true },
      { itemId: 'health_potion', quantity: 5, equipped: false },
      { itemId: 'mana_potion', quantity: 3, equipped: false },
    ],
  },

  [ClassId.ROGUE]: {
    id: ClassId.ROGUE,
    name: 'Rogue',
    description: 'Assassin and dungeon utility specialist. Highest burst.',
    baseStats: {
      hp: 90,
      mana: 0,
      strength: 14,
      stamina: 10,
      dexterity: 18,
      intelligence: 6,
      wisdom: 4,
      physicalResist: 3,
      spellResist: 3,
      critChance: 0.12,
      critDamage: 0.8,
      physicalDefense: 4,
      blockRating: 0.0,
      dodgeRating: 0.12,
    },
    statsPerLevel: {
      hp: 10,
      mana: 0,
      strength: 2,
      stamina: 1,
      dexterity: 3,
      intelligence: 0,
      wisdom: 0,
      physicalResist: 0,
      spellResist: 0,
      critChance: 0.008,
      critDamage: 0.04,
      physicalDefense: 1,
      blockRating: 0.0,
      dodgeRating: 0.01,
    },
    allowedArmor: 'leather',
    baseSpeed: 168,
    canUseMana: false,
    baseMeleeAttackSpeedMs: 2400,
    baseRangedAttackSpeedMs: 0,
    visionRange: 1350,
    startingItems: [
      { itemId: 'iron_dagger', quantity: 1, equipped: true },
      { itemId: 'health_potion', quantity: 5, equipped: false },
      { itemId: 'mana_potion', quantity: 3, equipped: false },
    ],
  },

  [ClassId.SHAMAN]: {
    id: ClassId.SHAMAN,
    name: 'Shaman',
    description: 'Force multiplier. Debuffs enemies, buffs the party.',
    baseStats: {
      hp: 110,
      mana: 100,
      strength: 10,
      stamina: 14,
      dexterity: 8,
      intelligence: 14,
      wisdom: 16,
      physicalResist: 5,
      spellResist: 6,
      critChance: 0.04,
      critDamage: 0.4,
      physicalDefense: 7,
      blockRating: 0.10,
      dodgeRating: 0.04,
    },
    statsPerLevel: {
      hp: 12,
      mana: 10,
      strength: 1,
      stamina: 2,
      dexterity: 1,
      intelligence: 2,
      wisdom: 2,
      physicalResist: 1,
      spellResist: 1,
      critChance: 0.004,
      critDamage: 0.02,
      physicalDefense: 1,
      blockRating: 0.005,
      dodgeRating: 0.003,
    },
    allowedArmor: 'mail',
    baseSpeed: 152,
    canUseMana: true,
    baseMeleeAttackSpeedMs: 4000,
    baseRangedAttackSpeedMs: 0,
    visionRange: 1200,
    startingItems: [
      { itemId: 'bone_totem', quantity: 1, equipped: true },
      { itemId: 'health_potion', quantity: 5, equipped: false },
      { itemId: 'mana_potion', quantity: 3, equipped: false },
    ],
  },

  [ClassId.WIZARD]: {
    id: ClassId.WIZARD,
    name: 'Wizard',
    description: 'Glass cannon. Highest magic burst and AoE damage.',
    baseStats: {
      hp: 70,
      mana: 150,
      strength: 4,
      stamina: 6,
      dexterity: 8,
      intelligence: 22,
      wisdom: 16,
      physicalResist: 2,
      spellResist: 6,
      critChance: 0.06,
      critDamage: 0.7,
      physicalDefense: 2,
      blockRating: 0.0,
      dodgeRating: 0.04,
    },
    statsPerLevel: {
      hp: 6,
      mana: 16,
      strength: 0,
      stamina: 0,
      dexterity: 1,
      intelligence: 4,
      wisdom: 2,
      physicalResist: 0,
      spellResist: 1,
      critChance: 0.005,
      critDamage: 0.04,
      physicalDefense: 0,
      blockRating: 0.0,
      dodgeRating: 0.003,
    },
    allowedArmor: 'cloth',
    baseSpeed: 152,
    canUseMana: true,
    baseMeleeAttackSpeedMs: 4800,
    baseRangedAttackSpeedMs: 0,
    visionRange: 1200,
    startingItems: [
      { itemId: 'oak_staff', quantity: 1, equipped: true },
      { itemId: 'health_potion', quantity: 5, equipped: false },
      { itemId: 'mana_potion', quantity: 3, equipped: false },
    ],
  },
};

/** All valid class IDs for validation */
export const ALL_CLASS_IDS = Object.values(ClassId);

/** Class display colors (for tinting sprites and UI) */
export const CLASS_COLORS: Record<ClassId, number> = {
  [ClassId.WARRIOR]: 0xcc4444,  // red
  [ClassId.CLERIC]: 0xffee88,  // gold
  [ClassId.RANGER]: 0x44aa44,  // green
  [ClassId.ROGUE]: 0x8844aa,  // purple
  [ClassId.SHAMAN]: 0x4488cc,  // blue
  [ClassId.WIZARD]: 0x66ccff,  // cyan
};
