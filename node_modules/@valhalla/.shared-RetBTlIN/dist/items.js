/**
 * Item definitions, enums, and catalog for the inventory system.
 * Follows the same pattern as classes.ts: enum IDs + Record lookup table.
 */
// ── Constants ────────────────────────────────────────────────
export const INVENTORY_MAX_SLOTS = 32;
// ── Loot Bag Constants ──────────────────────────────────────
export const LOOT_BAG_MERGE_RANGE = 80; // pixels: nearby bags merge within this radius
export const LOOT_BAG_PICKUP_RANGE = 200; // pixels: max distance to loot from a bag
export const LOOT_BAG_DESPAWN_MS = 300_000; // 5 minutes
export const LOOT_BAG_MAX_SLOTS = 18; // max items per bag
// ── Enums ────────────────────────────────────────────────────
export var ItemId;
(function (ItemId) {
    // Weapons
    ItemId["IRON_SWORD"] = "iron_sword";
    ItemId["OAK_STAFF"] = "oak_staff";
    ItemId["SHORT_BOW"] = "short_bow";
    ItemId["IRON_DAGGER"] = "iron_dagger";
    ItemId["BONE_TOTEM"] = "bone_totem";
    ItemId["IRON_MACE"] = "iron_mace";
    // Armor — Helm
    ItemId["LEATHER_HELM"] = "leather_helm";
    ItemId["IRON_HELM"] = "iron_helm";
    ItemId["CLOTH_HOOD"] = "cloth_hood";
    // Armor — Chest
    ItemId["LEATHER_TUNIC"] = "leather_tunic";
    ItemId["CHAINMAIL"] = "chainmail";
    ItemId["CLOTH_ROBE"] = "cloth_robe";
    // Armor — Legs
    ItemId["LEATHER_LEGGINGS"] = "leather_leggings";
    ItemId["IRON_GREAVES"] = "iron_greaves";
    ItemId["CLOTH_PANTS"] = "cloth_pants";
    // Armor — Boots
    ItemId["LEATHER_BOOTS"] = "leather_boots";
    ItemId["IRON_BOOTS"] = "iron_boots";
    ItemId["CLOTH_SANDALS"] = "cloth_sandals";
    // Rings
    ItemId["RING_OF_STRENGTH"] = "ring_of_strength";
    ItemId["RING_OF_WISDOM"] = "ring_of_wisdom";
    // Consumables
    ItemId["HEALTH_POTION"] = "health_potion";
    ItemId["MANA_POTION"] = "mana_potion";
    // Misc / Quest
    ItemId["RAT_TAIL"] = "rat_tail";
    ItemId["GOLD_COIN"] = "gold_coin";
})(ItemId || (ItemId = {}));
export var EquipSlotType;
(function (EquipSlotType) {
    EquipSlotType["WEAPON"] = "weapon";
    EquipSlotType["OFFHAND"] = "offhand";
    EquipSlotType["HELM"] = "helm";
    EquipSlotType["CHEST"] = "chest";
    EquipSlotType["LEGS"] = "legs";
    EquipSlotType["BOOTS"] = "boots";
    EquipSlotType["GLOVES"] = "gloves";
    EquipSlotType["BACK"] = "back";
    EquipSlotType["RING"] = "ring";
})(EquipSlotType || (EquipSlotType = {}));
/** Every equip slot, in the order the character panel lists them. */
export const EQUIP_SLOTS = [
    EquipSlotType.WEAPON, EquipSlotType.OFFHAND, EquipSlotType.HELM,
    EquipSlotType.CHEST, EquipSlotType.LEGS, EquipSlotType.BOOTS,
    EquipSlotType.GLOVES, EquipSlotType.BACK, EquipSlotType.RING,
];
/**
 * Equip slot -> paperdoll layer slot. `ring` has no visual layer, and the
 * weapon hand is called `mainhand` in the sprite pack.
 */
export const EQUIP_SLOT_TO_PAPERDOLL = {
    [EquipSlotType.WEAPON]: 'mainhand',
    [EquipSlotType.OFFHAND]: 'offhand',
    [EquipSlotType.HELM]: 'helm',
    [EquipSlotType.CHEST]: 'chest',
    [EquipSlotType.LEGS]: 'legs',
    [EquipSlotType.BOOTS]: 'boots',
    [EquipSlotType.GLOVES]: 'gloves',
    [EquipSlotType.BACK]: 'back',
};
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
export const EQUIP_SLOT_FIELD = {
    [EquipSlotType.WEAPON]: 'equipWeapon',
    [EquipSlotType.OFFHAND]: 'equipOffhand',
    [EquipSlotType.HELM]: 'equipHelm',
    [EquipSlotType.CHEST]: 'equipChest',
    [EquipSlotType.LEGS]: 'equipLegs',
    [EquipSlotType.BOOTS]: 'equipBoots',
    [EquipSlotType.GLOVES]: 'equipGloves',
    [EquipSlotType.BACK]: 'equipBack',
    [EquipSlotType.RING]: 'equipRing',
};
export var ItemCategory;
(function (ItemCategory) {
    ItemCategory["EQUIPMENT"] = "equipment";
    ItemCategory["CONSUMABLE"] = "consumable";
    ItemCategory["QUEST"] = "quest";
    ItemCategory["MISC"] = "misc";
})(ItemCategory || (ItemCategory = {}));
export var ItemRarity;
(function (ItemRarity) {
    ItemRarity["COMMON"] = "common";
    ItemRarity["UNCOMMON"] = "uncommon";
    ItemRarity["RARE"] = "rare";
    ItemRarity["EPIC"] = "epic";
    ItemRarity["LEGENDARY"] = "legendary";
})(ItemRarity || (ItemRarity = {}));
// ── Rarity display colors (hex strings for client rendering) ─
export const RARITY_COLORS = {
    [ItemRarity.COMMON]: '#cccccc',
    [ItemRarity.UNCOMMON]: '#33cc33',
    [ItemRarity.RARE]: '#3399ff',
    [ItemRarity.EPIC]: '#aa44ff',
    [ItemRarity.LEGENDARY]: '#ff8800',
};
/** Default sprite config matching the character body sheet (4×9, 64×64). */
export const DEFAULT_EQUIP_SPRITE_CONFIG = {
    frameWidth: 64,
    frameHeight: 64,
    framesPerRow: 9,
    rows: 4,
};
// ── Item Catalog ─────────────────────────────────────────────
export const ITEM_CATALOG = {
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
        attackSpeedMs: 1600,
        attackDamage: 5,
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
        attackSpeedMs: 2200,
        attackDamage: 4,
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
        attackSpeedMs: 1800,
        attackDamage: 4,
        isRangedWeapon: true,
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
        attackSpeedMs: 1000,
        attackDamage: 3,
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
        attackSpeedMs: 1800,
        attackDamage: 4,
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
        attackSpeedMs: 2000,
        attackDamage: 6,
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
export const ALL_ITEM_IDS = Object.values(ItemId);
//# sourceMappingURL=items.js.map