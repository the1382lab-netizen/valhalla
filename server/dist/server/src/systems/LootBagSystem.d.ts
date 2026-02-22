/**
 * LootBagSystem — manages ground loot bags: spawn, merge, pickup, despawn.
 * Bags are Colyseus-synced MapSchema entities visible to all clients.
 */
import type { MapSchema } from '@colyseus/schema';
import { LootBagState } from '../schema/LootBagState.js';
import type { PlayerState } from '../schema/PlayerState.js';
export declare class LootBagSystem {
    private nextBagId;
    /**
     * Spawn a loot bag at the given position, or merge items into a nearby bag.
     * Returns the bag ID.
     */
    spawnBag(zoneId: string, x: number, y: number, items: Array<{
        itemId: string;
        quantity: number;
    }>, lootBags: MapSchema<LootBagState>): string;
    /**
     * Loot a single item (or partial stack) from a bag into the player's inventory.
     * Returns true on success.
     */
    lootItem(player: PlayerState, bag: LootBagState, slotIndex: number, quantity: number): boolean;
    /**
     * Loot all items from a bag (respecting inventory limits).
     * Returns the number of slots successfully looted.
     */
    lootAll(player: PlayerState, bag: LootBagState): number;
    /**
     * Remove empty and expired bags. Call once per update tick.
     */
    update(now: number, lootBags: MapSchema<LootBagState>): void;
    /**
     * Roll a loot table and return the dropped items.
     * Each entry in the loot table is rolled independently:
     *   - dropChance (0-1) determines if the entry drops at all
     *   - quantity is randomized between minQuantity and maxQuantity
     */
    rollLootTable(lootTableId: string): Array<{
        itemId: string;
        quantity: number;
    }>;
    /**
     * Check if a player is close enough to loot from a bag.
     */
    private canPlayerReachBag;
    /**
     * Find an existing bag within merge range at the given position.
     */
    private findNearbyBag;
    /**
     * Add items to a bag, handling stacking for stackable items.
     */
    private addItemsToBag;
}
//# sourceMappingURL=LootBagSystem.d.ts.map