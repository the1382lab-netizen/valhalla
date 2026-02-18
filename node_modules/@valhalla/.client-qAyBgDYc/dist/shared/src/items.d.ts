/**
 * Item definitions, enums, and catalog for the inventory system.
 * Follows the same pattern as classes.ts: enum IDs + Record lookup table.
 */
import type { StatBlock } from './classes.js';
export declare const INVENTORY_MAX_SLOTS = 32;
export declare enum ItemId {
    IRON_SWORD = "iron_sword",
    OAK_STAFF = "oak_staff",
    SHORT_BOW = "short_bow",
    IRON_DAGGER = "iron_dagger",
    BONE_TOTEM = "bone_totem",
    IRON_MACE = "iron_mace",
    LEATHER_HELM = "leather_helm",
    IRON_HELM = "iron_helm",
    CLOTH_HOOD = "cloth_hood",
    LEATHER_TUNIC = "leather_tunic",
    CHAINMAIL = "chainmail",
    CLOTH_ROBE = "cloth_robe",
    LEATHER_LEGGINGS = "leather_leggings",
    IRON_GREAVES = "iron_greaves",
    CLOTH_PANTS = "cloth_pants",
    LEATHER_BOOTS = "leather_boots",
    IRON_BOOTS = "iron_boots",
    CLOTH_SANDALS = "cloth_sandals",
    RING_OF_STRENGTH = "ring_of_strength",
    RING_OF_WISDOM = "ring_of_wisdom",
    HEALTH_POTION = "health_potion",
    MANA_POTION = "mana_potion",
    RAT_TAIL = "rat_tail",
    GOLD_COIN = "gold_coin"
}
export declare enum EquipSlotType {
    WEAPON = "weapon",
    HELM = "helm",
    CHEST = "chest",
    LEGS = "legs",
    BOOTS = "boots",
    RING = "ring"
}
export declare enum ItemCategory {
    EQUIPMENT = "equipment",
    CONSUMABLE = "consumable",
    QUEST = "quest",
    MISC = "misc"
}
export declare enum ItemRarity {
    COMMON = "common",
    UNCOMMON = "uncommon",
    RARE = "rare",
    EPIC = "epic",
    LEGENDARY = "legendary"
}
export declare const RARITY_COLORS: Record<ItemRarity, string>;
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
}
/** A single inventory slot (shared interface, not Colyseus schema). */
export interface InventorySlot {
    itemId: ItemId;
    quantity: number;
}
export declare const ITEM_CATALOG: Record<ItemId, ItemTemplate>;
/** Array of all ItemId values for validation. */
export declare const ALL_ITEM_IDS: ItemId[];
//# sourceMappingURL=items.d.ts.map