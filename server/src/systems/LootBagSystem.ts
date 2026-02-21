/**
 * LootBagSystem — manages ground loot bags: spawn, merge, pickup, despawn.
 * Bags are Colyseus-synced MapSchema entities visible to all clients.
 */

import type { MapSchema } from '@colyseus/schema';
import { LootBagState, BagSlotState } from '../schema/LootBagState.js';
import type { PlayerState } from '../schema/PlayerState.js';
import { DataManager } from './DataManager.js';
import { addItem } from './InventorySystem.js';
import {
  LOOT_BAG_MERGE_RANGE,
  LOOT_BAG_PICKUP_RANGE,
  LOOT_BAG_DESPAWN_MS,
  LOOT_BAG_MAX_SLOTS,
} from '@valhalla/shared';

export class LootBagSystem {
  private nextBagId: number = 0;

  // ── Public API ──────────────────────────────────────────────

  /**
   * Spawn a loot bag at the given position, or merge items into a nearby bag.
   * Returns the bag ID.
   */
  spawnBag(
    zoneId: string,
    x: number,
    y: number,
    items: Array<{ itemId: string; quantity: number }>,
    lootBags: MapSchema<LootBagState>,
  ): string {
    if (items.length === 0) return '';

    // Try to merge into a nearby existing bag
    const nearbyBag = this.findNearbyBag(zoneId, x, y, lootBags);
    if (nearbyBag) {
      this.addItemsToBag(nearbyBag, items);
      return nearbyBag.id;
    }

    // Create a new bag
    const bagId = `bag_${this.nextBagId++}`;
    const bag = new LootBagState();
    bag.id = bagId;
    bag.zoneId = zoneId;
    bag.x = x;
    bag.y = y;
    bag._createdAt = Date.now();

    this.addItemsToBag(bag, items);
    lootBags.set(bagId, bag);

    return bagId;
  }

  /**
   * Loot a single item (or partial stack) from a bag into the player's inventory.
   * Returns true on success.
   */
  lootItem(
    player: PlayerState,
    bag: LootBagState,
    slotIndex: number,
    quantity: number,
  ): boolean {
    if (!this.canPlayerReachBag(player, bag)) return false;
    if (slotIndex < 0 || slotIndex >= bag.items.length) return false;

    const bagSlot = bag.items[slotIndex];
    const actualQty = Math.min(quantity, bagSlot.quantity);
    if (actualQty <= 0) return false;

    // Try to add to player inventory
    if (!addItem(player, bagSlot.itemId as any, actualQty)) {
      return false; // inventory full
    }

    // Remove from bag
    bagSlot.quantity -= actualQty;
    if (bagSlot.quantity <= 0) {
      bag.items.splice(slotIndex, 1);
    }

    return true;
  }

  /**
   * Loot all items from a bag (respecting inventory limits).
   * Returns the number of slots successfully looted.
   */
  lootAll(player: PlayerState, bag: LootBagState): number {
    if (!this.canPlayerReachBag(player, bag)) return 0;

    let lootedCount = 0;

    // Iterate backwards to avoid index shifting issues
    for (let i = bag.items.length - 1; i >= 0; i--) {
      const bagSlot = bag.items[i];
      if (addItem(player, bagSlot.itemId as any, bagSlot.quantity)) {
        bag.items.splice(i, 1);
        lootedCount++;
      }
    }

    return lootedCount;
  }

  /**
   * Remove empty and expired bags. Call once per update tick.
   */
  update(now: number, lootBags: MapSchema<LootBagState>): void {
    const toRemove: string[] = [];

    for (const [bagId, bag] of lootBags) {
      // Remove empty bags immediately
      if (bag.items.length === 0) {
        toRemove.push(bagId);
        continue;
      }

      // Remove expired bags
      if (bag._createdAt > 0 && now - bag._createdAt > LOOT_BAG_DESPAWN_MS) {
        toRemove.push(bagId);
      }
    }

    for (const bagId of toRemove) {
      lootBags.delete(bagId);
    }
  }

  /**
   * Roll a loot table and return the dropped items.
   * Each entry in the loot table is rolled independently:
   *   - dropChance (0-1) determines if the entry drops at all
   *   - quantity is randomized between minQuantity and maxQuantity
   */
  rollLootTable(lootTableId: string): Array<{ itemId: string; quantity: number }> {
    const table = DataManager.instance.lootTables[lootTableId];
    if (!table || !table.entries) return [];

    const result: Array<{ itemId: string; quantity: number }> = [];

    for (const entry of table.entries) {
      // Roll whether this entry drops
      if (Math.random() > entry.dropChance) continue;

      // Roll quantity
      const qty = entry.minQuantity === entry.maxQuantity
        ? entry.minQuantity
        : Math.floor(Math.random() * (entry.maxQuantity - entry.minQuantity + 1)) + entry.minQuantity;

      if (qty > 0) {
        result.push({ itemId: entry.itemId, quantity: qty });
      }
    }

    return result;
  }

  // ── Private Helpers ──────────────────────────────────────────

  /**
   * Check if a player is close enough to loot from a bag.
   */
  private canPlayerReachBag(player: PlayerState, bag: LootBagState): boolean {
    if (player.zoneId !== bag.zoneId) return false;
    const dx = player.x - bag.x;
    const dy = player.y - bag.y;
    return (dx * dx + dy * dy) <= LOOT_BAG_PICKUP_RANGE * LOOT_BAG_PICKUP_RANGE;
  }

  /**
   * Find an existing bag within merge range at the given position.
   */
  private findNearbyBag(
    zoneId: string,
    x: number,
    y: number,
    lootBags: MapSchema<LootBagState>,
  ): LootBagState | null {
    const rangeSq = LOOT_BAG_MERGE_RANGE * LOOT_BAG_MERGE_RANGE;
    for (const [, bag] of lootBags) {
      if (bag.zoneId !== zoneId) continue;
      if (bag.items.length >= LOOT_BAG_MAX_SLOTS) continue; // bag is full
      const dx = bag.x - x;
      const dy = bag.y - y;
      if (dx * dx + dy * dy <= rangeSq) {
        return bag;
      }
    }
    return null;
  }

  /**
   * Add items to a bag, handling stacking for stackable items.
   */
  private addItemsToBag(
    bag: LootBagState,
    items: Array<{ itemId: string; quantity: number }>,
  ): void {
    for (const item of items) {
      const template = DataManager.instance.getItem(item.itemId);
      let remaining = item.quantity;

      // Try stacking into existing slots
      if (template?.stackable) {
        for (let i = 0; i < bag.items.length && remaining > 0; i++) {
          const slot = bag.items[i];
          if (slot.itemId === item.itemId && slot.quantity < template.maxStack) {
            const canAdd = Math.min(remaining, template.maxStack - slot.quantity);
            slot.quantity += canAdd;
            remaining -= canAdd;
          }
        }
      }

      // Place remaining into new slots (up to max)
      while (remaining > 0 && bag.items.length < LOOT_BAG_MAX_SLOTS) {
        const slot = new BagSlotState();
        slot.itemId = item.itemId;

        if (template?.stackable) {
          const toAdd = Math.min(remaining, template.maxStack);
          slot.quantity = toAdd;
          remaining -= toAdd;
        } else {
          slot.quantity = 1;
          remaining -= 1;
        }

        bag.items.push(slot);
      }
    }
  }
}
