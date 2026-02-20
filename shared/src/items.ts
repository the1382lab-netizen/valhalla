/**
 * Item definitions, enums, and catalog for the inventory system.
 * Follows the same pattern as classes.ts: enum IDs + Record lookup table.
 */

import type { StatBlock } from './classes.js';

// ── Constants ────────────────────────────────────────────────

export const INVENTORY_MAX_SLOTS = 32;

// ── Enums ────────────────────────────────────────────────────

export enum ItemId {
  // Weapons
  IRON_SWORD = 'iron_sword',
  OAK_STAFF = 'oak_staff',
  SHORT_BOW = 'short_bow',
  IRON_DAGGER = 'iron_dagger',
  BONE_TOTEM = 'bone_totem',
  IRON_MACE = 'iron_mace',

  // Armor — Helm
  LEATHER_HELM = 'leather_helm',
  IRON_HELM = 'iron_helm',
  CLOTH_HOOD = 'cloth_hood',

  // Armor — Chest
  LEATHER_TUNIC = 'leather_tunic',
  CHAINMAIL = 'chainmail',
  CLOTH_ROBE = 'cloth_robe',

  // Armor — Legs
  LEATHER_LEGGINGS = 'leather_leggings',
  IRON_GREAVES = 'iron_greaves',
  CLOTH_PANTS = 'cloth_pants',

  // Armor — Boots
  LEATHER_BOOTS = 'leather_boots',
  IRON_BOOTS = 'iron_boots',
  CLOTH_SANDALS = 'cloth_sandals',

  // Rings
  RING_OF_STRENGTH = 'ring_of_strength',
  RING_OF_WISDOM = 'ring_of_wisdom',

  // Consumables
  HEALTH_POTION = 'health_potion',
  MANA_POTION = 'mana_potion',

  // Misc / Quest
  RAT_TAIL = 'rat_tail',
  GOLD_COIN = 'gold_coin',
}

export enum EquipSlotType {
  WEAPON = 'weapon',
  HELM = 'helm',
  CHEST = 'chest',
  LEGS = 'legs',
  BOOTS = 'boots',
  RING = 'ring',
}

export enum ItemCategory {
  EQUIPMENT = 'equipment',
  CONSUMABLE = 'consumable',
  QUEST = 'quest',
  MISC = 'misc',
}

export enum ItemRarity {
  COMMON = 'common',
  UNCOMMON = 'uncommon',
  RARE = 'rare',
  EPIC = 'epic',
  LEGENDARY = 'legendary',
}

// ── Rarity display colors (hex strings for client rendering) ─

export const RARITY_COLORS: Record<ItemRarity, string> = {
  [ItemRarity.COMMON]: '#cccccc',
  [ItemRarity.UNCOMMON]: '#33cc33',
  [ItemRarity.RARE]: '#3399ff',
  [ItemRarity.EPIC]: '#aa44ff',
  [ItemRarity.LEGENDARY]: '#ff8800',
};

// ── Interfaces ───────────────────────────────────────────────

/** Configuration for how an equipment sprite sheet is structured. */
export interface EquipSpriteConfig {
  frameWidth: number;   // default 64
  frameHeight: number;  // default 64
  framesPerRow: number; // default 9
  rows: number;         // default 4 (up, left, down, right)
}

/** Default sprite config matching the character body sheet (4×9, 64×64). */
export const DEFAULT_EQUIP_SPRITE_CONFIG: EquipSpriteConfig = {
  frameWidth: 64,
  frameHeight: 64,
  framesPerRow: 9,
  rows: 4,
};

export interface ItemTemplate {
  id: ItemId;
  name: string;
  description: string;
  category: ItemCategory;
  equipSlot?: EquipSlotType;
  rarity: ItemRarity;
  stackable: boolean;
  maxStack: number;
  /** Stat bonuses when equipped (equipment only) */
  statBonuses?: Partial<StatBlock>;
  /** Filename of equipment sprite sheet overlay (in assets/sprites/equipment/) */
  equipSpriteSheet?: string;
  /** Sprite sheet layout config — defaults to matching the character body sheet */
  equipSpriteConfig?: EquipSpriteConfig;
  /** Filename of inventory icon image (in assets/sprites/icons/) */
  inventoryIcon?: string;
}

/** A single inventory slot (shared interface, not Colyseus schema). */
export interface InventorySlot {
  itemId: ItemId;
  quantity: number;
}

// ── Item Catalog ─────────────────────────────────────────────

export const ITEM_CATALOG: Record<ItemId, ItemTemplate> = {
  // ── Weapons ───────────────────────────────────────────
  [ItemId.IRON_SWORD]: {
    id: ItemId.IRON_SWORD,
    name: 'Iron Sword',
    description: 'A sturdy blade favored by warriors.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.WEAPON,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { strength: 3 },
  },
  [ItemId.OAK_STAFF]: {
    id: ItemId.OAK_STAFF,
    name: 'Oak Staff',
    description: 'A gnarled staff humming with faint arcane energy.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.WEAPON,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { intelligence: 3 },
  },
  [ItemId.SHORT_BOW]: {
    id: ItemId.SHORT_BOW,
    name: 'Short Bow',
    description: 'A compact bow ideal for quick shots.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.WEAPON,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { dexterity: 3 },
  },
  [ItemId.IRON_DAGGER]: {
    id: ItemId.IRON_DAGGER,
    name: 'Iron Dagger',
    description: 'A swift blade for those who strike from the shadows.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.WEAPON,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { dexterity: 2, strength: 1 },
  },
  [ItemId.BONE_TOTEM]: {
    id: ItemId.BONE_TOTEM,
    name: 'Bone Totem',
    description: 'A carved totem channeling primal spirits.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.WEAPON,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { wisdom: 2, intelligence: 1 },
  },
  [ItemId.IRON_MACE]: {
    id: ItemId.IRON_MACE,
    name: 'Iron Mace',
    description: 'A heavy mace blessed for divine service.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.WEAPON,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { wisdom: 2, strength: 1 },
  },

  // ── Helms ─────────────────────────────────────────────
  [ItemId.LEATHER_HELM]: {
    id: ItemId.LEATHER_HELM,
    name: 'Leather Helm',
    description: 'A basic leather cap offering modest protection.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.HELM,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { stamina: 1, physicalDefense: 1 },
  },
  [ItemId.IRON_HELM]: {
    id: ItemId.IRON_HELM,
    name: 'Iron Helm',
    description: 'A solid iron helm for frontline fighters.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.HELM,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { stamina: 2, physicalDefense: 2 },
  },
  [ItemId.CLOTH_HOOD]: {
    id: ItemId.CLOTH_HOOD,
    name: 'Cloth Hood',
    description: 'A thin hood woven with minor enchantments.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.HELM,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { intelligence: 1, spellResist: 1 },
  },

  // ── Chest ─────────────────────────────────────────────
  [ItemId.LEATHER_TUNIC]: {
    id: ItemId.LEATHER_TUNIC,
    name: 'Leather Tunic',
    description: 'Flexible armor suitable for scouts and rangers.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.CHEST,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { stamina: 2, physicalDefense: 2, dodgeRating: 0.01 },
  },
  [ItemId.CHAINMAIL]: {
    id: ItemId.CHAINMAIL,
    name: 'Chainmail',
    description: 'Interlocking rings of iron providing solid defense.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.CHEST,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { stamina: 3, physicalDefense: 4 },
  },
  [ItemId.CLOTH_ROBE]: {
    id: ItemId.CLOTH_ROBE,
    name: 'Cloth Robe',
    description: 'A flowing robe that enhances magical focus.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.CHEST,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { intelligence: 2, wisdom: 1, spellResist: 2 },
  },

  // ── Legs ──────────────────────────────────────────────
  [ItemId.LEATHER_LEGGINGS]: {
    id: ItemId.LEATHER_LEGGINGS,
    name: 'Leather Leggings',
    description: 'Sturdy leather pants for the adventurous.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.LEGS,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { stamina: 1, physicalDefense: 1 },
  },
  [ItemId.IRON_GREAVES]: {
    id: ItemId.IRON_GREAVES,
    name: 'Iron Greaves',
    description: 'Heavy leg armor for the well-armored fighter.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.LEGS,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { stamina: 2, physicalDefense: 3 },
  },
  [ItemId.CLOTH_PANTS]: {
    id: ItemId.CLOTH_PANTS,
    name: 'Cloth Pants',
    description: 'Simple cloth leggings, comfortable for spellcasting.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.LEGS,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { wisdom: 1, spellResist: 1 },
  },

  // ── Boots ─────────────────────────────────────────────
  [ItemId.LEATHER_BOOTS]: {
    id: ItemId.LEATHER_BOOTS,
    name: 'Leather Boots',
    description: 'Light boots that keep your feet quick.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.BOOTS,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { dexterity: 1, dodgeRating: 0.01 },
  },
  [ItemId.IRON_BOOTS]: {
    id: ItemId.IRON_BOOTS,
    name: 'Iron Boots',
    description: 'Heavy iron boots — slow but protective.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.BOOTS,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { stamina: 1, physicalDefense: 2 },
  },
  [ItemId.CLOTH_SANDALS]: {
    id: ItemId.CLOTH_SANDALS,
    name: 'Cloth Sandals',
    description: 'Light sandals that let you move freely.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.BOOTS,
    rarity: ItemRarity.COMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { wisdom: 1 },
  },

  // ── Rings ─────────────────────────────────────────────
  [ItemId.RING_OF_STRENGTH]: {
    id: ItemId.RING_OF_STRENGTH,
    name: 'Ring of Strength',
    description: 'A simple band that bolsters physical power.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.RING,
    rarity: ItemRarity.UNCOMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { strength: 2 },
  },
  [ItemId.RING_OF_WISDOM]: {
    id: ItemId.RING_OF_WISDOM,
    name: 'Ring of Wisdom',
    description: 'A faintly glowing ring that sharpens the mind.',
    category: ItemCategory.EQUIPMENT,
    equipSlot: EquipSlotType.RING,
    rarity: ItemRarity.UNCOMMON,
    stackable: false,
    maxStack: 1,
    statBonuses: { wisdom: 2 },
  },

  // ── Consumables ───────────────────────────────────────
  [ItemId.HEALTH_POTION]: {
    id: ItemId.HEALTH_POTION,
    name: 'Health Potion',
    description: 'Restores a moderate amount of health.',
    category: ItemCategory.CONSUMABLE,
    rarity: ItemRarity.COMMON,
    stackable: true,
    maxStack: 20,
  },
  [ItemId.MANA_POTION]: {
    id: ItemId.MANA_POTION,
    name: 'Mana Potion',
    description: 'Restores a moderate amount of mana.',
    category: ItemCategory.CONSUMABLE,
    rarity: ItemRarity.COMMON,
    stackable: true,
    maxStack: 20,
  },

  // ── Quest / Misc ──────────────────────────────────────
  [ItemId.RAT_TAIL]: {
    id: ItemId.RAT_TAIL,
    name: 'Rat Tail',
    description: 'A severed rat tail. Someone might want this.',
    category: ItemCategory.QUEST,
    rarity: ItemRarity.COMMON,
    stackable: true,
    maxStack: 99,
  },
  [ItemId.GOLD_COIN]: {
    id: ItemId.GOLD_COIN,
    name: 'Gold Coin',
    description: 'A shiny gold coin. Currency of the realm.',
    category: ItemCategory.MISC,
    rarity: ItemRarity.COMMON,
    stackable: true,
    maxStack: 9999,
  },
};

/** Array of all ItemId values for validation. */
export const ALL_ITEM_IDS: ItemId[] = Object.values(ItemId);
