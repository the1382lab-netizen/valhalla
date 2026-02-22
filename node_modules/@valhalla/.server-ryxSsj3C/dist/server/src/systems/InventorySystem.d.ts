/**
 * Stateless inventory helper functions.
 * Operates on a PlayerState's inventory ArraySchema.
 */
import { ItemId, EquipSlotType } from '@valhalla/shared';
import type { PlayerState } from '../schema/PlayerState.js';
/**
 * Add an item to a player's inventory.
 * Stacks onto existing slots if the item is stackable, otherwise uses the first empty slot.
 * @returns true if the item was added, false if inventory is full.
 */
export declare function addItem(player: PlayerState, itemId: ItemId, qty?: number): boolean;
/**
 * Remove a quantity of items from a specific inventory slot.
 * If quantity reaches 0, the slot is removed from the array.
 * @returns true if the removal succeeded, false if slot doesn't exist or insufficient quantity.
 */
export declare function removeItem(player: PlayerState, slotIndex: number, qty?: number): boolean;
/**
 * Check whether a player has at least one of a given item.
 */
export declare function hasItem(player: PlayerState, itemId: ItemId): boolean;
/**
 * Count the total quantity of a given item across all inventory slots.
 */
export declare function getItemCount(player: PlayerState, itemId: ItemId): number;
/**
 * Equip an item from inventory.
 * Removes it from the inventory slot and places it in the correct equip field.
 * If something is already equipped in that slot, swaps it back into inventory.
 * @returns true if the item was equipped successfully.
 */
export declare function equipItem(player: PlayerState, inventorySlotIndex: number): boolean;
/**
 * Swap two inventory slots by index.
 * Uses splice-replace to ensure Colyseus ArraySchema change detection fires.
 */
export declare function swapInventorySlots(player: PlayerState, fromIndex: number, toIndex: number): boolean;
/**
 * Drop an item from inventory, returning the item data for loot bag spawn.
 * @returns { itemId, quantity } or null if the slot was invalid.
 */
export declare function dropInventoryItem(player: PlayerState, slotIndex: number): {
    itemId: string;
    quantity: number;
} | null;
/**
 * Drop an equipped item, returning the item data for loot bag spawn.
 * @returns { itemId, quantity: 1 } or null if the slot was empty.
 */
export declare function dropEquippedItem(player: PlayerState, slotType: EquipSlotType): {
    itemId: string;
    quantity: number;
} | null;
/**
 * Unequip an item and return it to inventory (appended at end).
 * @returns true if the item was unequipped successfully, false if inventory full or slot empty.
 */
export declare function unequipItem(player: PlayerState, slotType: EquipSlotType): boolean;
/**
 * Unequip an item and place it at a specific inventory slot index.
 * If targetIndex has an item, swaps the equipped item with that inventory item
 * (equipping the inventory item if it fits the same slot, otherwise just placing it there).
 * If targetIndex is beyond the array, appends to the end.
 */
export declare function unequipItemToSlot(player: PlayerState, slotType: EquipSlotType, targetIndex: number): boolean;
//# sourceMappingURL=InventorySystem.d.ts.map