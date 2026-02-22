/**
 * Item definitions, enums, and catalog for the inventory system.
 * Follows the same pattern as classes.ts: enum IDs + Record lookup table.
 */
import type { StatBlock } from './classes.js';
export declare const INVENTORY_MAX_SLOTS = 32;
export declare const LOOT_BAG_MERGE_RANGE = 80;
export declare const LOOT_BAG_PICKUP_RANGE = 200;
export declare const LOOT_BAG_DESPAWN_MS = 300000;
export declare const LOOT_BAG_MAX_SLOTS = 18;
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
/** Configuration for how an equipment sprite sheet is structured. */
export interface EquipSpriteConfig {
    frameWidth: number;
    frameHeight: number;
    framesPerRow: number;
    rows: number;
}
/** Default sprite config matching the character body sheet (4×9, 64×64). */
export declare const DEFAULT_EQUIP_SPRITE_CONFIG: EquipSpriteConfig;
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
export declare const ITEM_CATALOG: Record<ItemId, ItemTemplate>;
/** Array of all ItemId values for validation. */
export declare const ALL_ITEM_IDS: ItemId[];
//# sourceMappingURL=items.d.ts.map