/**
 * Item definitions, enums, and catalog for the inventory system.
 * Follows the same pattern as classes.ts: enum IDs + Record lookup table.
 */
import type { PaperdollSlot } from './paperdoll.js';
import { WeaponStyle } from './paperdoll.js';
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
    OFFHAND = "offhand",
    HELM = "helm",
    CHEST = "chest",
    LEGS = "legs",
    BOOTS = "boots",
    GLOVES = "gloves",
    BACK = "back",
    RING = "ring"
}
/** Every equip slot, in the order the character panel lists them. */
export declare const EQUIP_SLOTS: EquipSlotType[];
/**
 * Equip slot -> paperdoll layer slot. `ring` has no visual layer, and the
 * weapon hand is called `mainhand` in the sprite pack.
 */
export declare const EQUIP_SLOT_TO_PAPERDOLL: Partial<Record<EquipSlotType, PaperdollSlot>>;
/**
 * Equip slot -> the PlayerState schema field holding it.
 *
 * One table, shared: the server reads and writes these fields, and the client
 * rebuilds its equipment record from the synced schema using the same names.
 * `as const` keeps the values as literal types, which is what lets
 * `server/src/schema/PlayerState.ts` prove every one is a real
 * `keyof PlayerState` — rename a schema field and the build breaks instead of
 * a slot silently vanishing from the client.
 */
export declare const EQUIP_SLOT_FIELD: {
    readonly weapon: "equipWeapon";
    readonly offhand: "equipOffhand";
    readonly helm: "equipHelm";
    readonly chest: "equipChest";
    readonly legs: "equipLegs";
    readonly boots: "equipBoots";
    readonly gloves: "equipGloves";
    readonly back: "equipBack";
    readonly ring: "equipRing";
};
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
    /** Attack speed in ms when this weapon is equipped. Overrides class base attack speed. */
    attackSpeedMs?: number;
    /**
     * Flat bonus added to the base damage constant before stat scaling.
     * Stacks on top of BASE_MELEE_DAMAGE / BASE_RANGED_DAMAGE / BASE_SPELL_DAMAGE.
     * Configurable per-item in the game editor.
     */
    attackDamage?: number;
    /** Whether this is a ranged weapon (bow, crossbow). Enables ranged auto-attack when equipped. */
    isRangedWeapon?: boolean;
    /** Filename of walk/idle equipment sprite sheet overlay (in assets/sprites/equipment/) */
    equipSpriteSheet?: string;
    /** Filename of melee animation equipment sprite sheet overlay */
    meleeSpriteSheet?: string;
    /** Filename of ranged animation equipment sprite sheet overlay */
    rangedSpriteSheet?: string;
    /** Filename of cast animation equipment sprite sheet overlay */
    castSpriteSheet?: string;
    /** Sprite sheet layout config — defaults to matching the character body sheet */
    equipSpriteConfig?: EquipSpriteConfig;
    /** Filename of inventory icon image (in assets/sprites/icons/) */
    inventoryIcon?: string;
    /**
     * Layer id in `assets/sprites/paperdoll/manifest.json`, e.g.
     * `chest_iron_plate`. When set, the client draws this item as a paperdoll
     * layer and ignores the four LPC sheet fields.
     */
    spriteId?: string;
    /**
     * How a weapon reads, and therefore which attack cycle the character plays:
     * sword/greatsword/mace -> `attack`, bow -> `shoot`, staff -> `cast`.
     * Weapons only.
     */
    weaponStyle?: WeaponStyle;
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