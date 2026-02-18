/**
 * Loot Table definitions.
 * Used by the game editor to create drop tables
 * and by the server to generate loot on enemy death.
 */
export interface LootEntry {
    itemId: string;
    weight: number;
    dropChance: number;
    minQuantity: number;
    maxQuantity: number;
}
export interface LootTable {
    id: string;
    name: string;
    entries: LootEntry[];
}
//# sourceMappingURL=loot-tables.d.ts.map