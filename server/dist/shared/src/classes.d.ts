export declare enum ClassId {
    WARRIOR = "warrior",
    CLERIC = "cleric",
    RANGER = "ranger",
    ROGUE = "rogue",
    SHAMAN = "shaman",
    WIZARD = "wizard"
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
    critChance: number;
    critDamage: number;
    physicalDefense: number;
    blockRating: number;
    dodgeRating: number;
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
    baseSpeed: number;
    canUseMana: boolean;
    startingItems?: StartingItem[];
}
export declare const CLASS_TEMPLATES: Record<ClassId, ClassTemplate>;
/** All valid class IDs for validation */
export declare const ALL_CLASS_IDS: ClassId[];
/** Class display colors (for tinting sprites and UI) */
export declare const CLASS_COLORS: Record<ClassId, number>;
//# sourceMappingURL=classes.d.ts.map