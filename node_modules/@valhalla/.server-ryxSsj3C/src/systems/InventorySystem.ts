/**
 * Stateless inventory helper functions.
 * Operates on a PlayerState's inventory ArraySchema.
 */

import {
  INVENTORY_MAX_SLOTS,
  ItemId,
  EquipSlotType,
} from '@valhalla/shared';
import { InventorySlotState } from '../schema/PlayerState.js';
import type { PlayerState } from '../schema/PlayerState.js';
import { DataManager } from './DataManager.js';

/**
 * Add an item to a player's inventory.
 * Stacks onto existing slots if the item is stackable, otherwise uses the first empty slot.
 * @returns true if the item was added, false if inventory is full.
 */
export function addItem(player: PlayerState, itemId: ItemId, qty: number = 1): boolean {
  const template = DataManager.instance.getItem(itemId);
  if (!template) return false;

  let remaining = qty;

  // If stackable, try to fill existing stacks first
  if (template.stackable) {
    for (let i = 0; i < player.inventory.length && remaining > 0; i++) {
      const slot = player.inventory[i];
      if (slot.itemId === itemId && slot.quantity < template.maxStack) {
        const canAdd = Math.min(remaining, template.maxStack - slot.quantity);
        slot.quantity += canAdd;
        remaining -= canAdd;
      }
    }
  }

  // Place remaining into empty slots
  while (remaining > 0) {
    if (player.inventory.length >= INVENTORY_MAX_SLOTS) {
      return false; // Inventory full
    }

    const slot = new InventorySlotState();
    slot.itemId = itemId;

    if (template.stackable) {
      const toAdd = Math.min(remaining, template.maxStack);
      slot.quantity = toAdd;
      remaining -= toAdd;
    } else {
      slot.quantity = 1;
      remaining -= 1;
    }

    player.inventory.push(slot);
  }

  return true;
}

/**
 * Remove a quantity of items from a specific inventory slot.
 * If quantity reaches 0, the slot is removed from the array.
 * @returns true if the removal succeeded, false if slot doesn't exist or insufficient quantity.
 */
export function removeItem(player: PlayerState, slotIndex: number, qty: number = 1): boolean {
  if (slotIndex < 0 || slotIndex >= player.inventory.length) return false;

  const slot = player.inventory[slotIndex];
  if (slot.quantity < qty) return false;

  slot.quantity -= qty;
  if (slot.quantity <= 0) {
    player.inventory.splice(slotIndex, 1);
  }

  return true;
}

/**
 * Check whether a player has at least one of a given item.
 */
export function hasItem(player: PlayerState, itemId: ItemId): boolean {
  for (let i = 0; i < player.inventory.length; i++) {
    if (player.inventory[i].itemId === itemId) return true;
  }
  return false;
}

/**
 * Count the total quantity of a given item across all inventory slots.
 */
export function getItemCount(player: PlayerState, itemId: ItemId): number {
  let total = 0;
  for (let i = 0; i < player.inventory.length; i++) {
    if (player.inventory[i].itemId === itemId) {
      total += player.inventory[i].quantity;
    }
  }
  return total;
}

// ── Equipment Helpers ─────────────────────────────────────────

/** Read the equipped itemId for a given slot type. */
function getEquipField(player: PlayerState, slotType: EquipSlotType): string {
  switch (slotType) {
    case EquipSlotType.WEAPON: return player.equipWeapon;
    case EquipSlotType.HELM:   return player.equipHelm;
    case EquipSlotType.CHEST:  return player.equipChest;
    case EquipSlotType.LEGS:   return player.equipLegs;
    case EquipSlotType.BOOTS:  return player.equipBoots;
    case EquipSlotType.RING:   return player.equipRing;
    default: return '';
  }
}

/** Set the equipped itemId for a given slot type. */
function setEquipField(player: PlayerState, slotType: EquipSlotType, itemId: string): void {
  switch (slotType) {
    case EquipSlotType.WEAPON: player.equipWeapon = itemId; break;
    case EquipSlotType.HELM:   player.equipHelm = itemId;   break;
    case EquipSlotType.CHEST:  player.equipChest = itemId;  break;
    case EquipSlotType.LEGS:   player.equipLegs = itemId;   break;
    case EquipSlotType.BOOTS:  player.equipBoots = itemId;  break;
    case EquipSlotType.RING:   player.equipRing = itemId;   break;
  }
}

/**
 * Equip an item from inventory.
 * Removes it from the inventory slot and places it in the correct equip field.
 * If something is already equipped in that slot, swaps it back into inventory.
 * @returns true if the item was equipped successfully.
 */
export function equipItem(player: PlayerState, inventorySlotIndex: number): boolean {
  if (inventorySlotIndex < 0 || inventorySlotIndex >= player.inventory.length) return false;

  const slot = player.inventory[inventorySlotIndex];
  const template = DataManager.instance.getItem(slot.itemId);
  if (!template || !template.equipSlot) return false; // Not equippable

  const slotType = template.equipSlot;
  const currentlyEquipped = getEquipField(player, slotType);

  // Remove item from inventory (always qty 1 for equipment)
  player.inventory.splice(inventorySlotIndex, 1);

  // If something was already equipped, put it back in inventory
  if (currentlyEquipped) {
    const returnSlot = new InventorySlotState();
    returnSlot.itemId = currentlyEquipped;
    returnSlot.quantity = 1;
    player.inventory.push(returnSlot);
  }

  // Equip the new item
  setEquipField(player, slotType, template.id);

  return true;
}

/**
 * Swap two inventory slots by index.
 * Uses splice-replace to ensure Colyseus ArraySchema change detection fires.
 */
export function swapInventorySlots(player: PlayerState, fromIndex: number, toIndex: number): boolean {
  if (fromIndex === toIndex) return false;
  if (fromIndex < 0 || fromIndex >= player.inventory.length) return false;
  if (toIndex < 0 || toIndex >= player.inventory.length) return false;

  // Read current data from both slots
  const fromItemId = player.inventory[fromIndex].itemId;
  const fromQty = player.inventory[fromIndex].quantity;
  const toItemId = player.inventory[toIndex].itemId;
  const toQty = player.inventory[toIndex].quantity;

  // Create new slot objects with swapped data
  const newFromSlot = new InventorySlotState();
  newFromSlot.itemId = toItemId;
  newFromSlot.quantity = toQty;

  const newToSlot = new InventorySlotState();
  newToSlot.itemId = fromItemId;
  newToSlot.quantity = fromQty;

  // IMPORTANT: Process the HIGHER index first so the first splice doesn't
  // corrupt Colyseus ArraySchema's internal ChangeTree tracking for the
  // lower index. Two sequential splices at arbitrary order can cause the
  // ChangeTree to try deleting a stale/non-existing index.
  const hi = Math.max(fromIndex, toIndex);
  const lo = Math.min(fromIndex, toIndex);
  const hiSlot = hi === fromIndex ? newFromSlot : newToSlot;
  const loSlot = hi === fromIndex ? newToSlot : newFromSlot;

  player.inventory.splice(hi, 1, hiSlot);
  player.inventory.splice(lo, 1, loSlot);

  return true;
}

/**
 * Drop an item from inventory, returning the item data for loot bag spawn.
 * @returns { itemId, quantity } or null if the slot was invalid.
 */
export function dropInventoryItem(player: PlayerState, slotIndex: number): { itemId: string; quantity: number } | null {
  if (slotIndex < 0 || slotIndex >= player.inventory.length) return null;
  const slot = player.inventory[slotIndex];
  const itemData = { itemId: slot.itemId, quantity: slot.quantity };
  player.inventory.splice(slotIndex, 1);
  return itemData;
}

/**
 * Drop an equipped item, returning the item data for loot bag spawn.
 * @returns { itemId, quantity: 1 } or null if the slot was empty.
 */
export function dropEquippedItem(player: PlayerState, slotType: EquipSlotType): { itemId: string; quantity: number } | null {
  const currentlyEquipped = getEquipField(player, slotType);
  if (!currentlyEquipped) return null;
  setEquipField(player, slotType, '');
  return { itemId: currentlyEquipped, quantity: 1 };
}

/**
 * Unequip an item and return it to inventory (appended at end).
 * @returns true if the item was unequipped successfully, false if inventory full or slot empty.
 */
export function unequipItem(player: PlayerState, slotType: EquipSlotType): boolean {
  const currentlyEquipped = getEquipField(player, slotType);
  if (!currentlyEquipped) return false; // Nothing equipped

  // Check if inventory has room
  if (player.inventory.length >= INVENTORY_MAX_SLOTS) return false;

  // Move to inventory
  const slot = new InventorySlotState();
  slot.itemId = currentlyEquipped;
  slot.quantity = 1;
  player.inventory.push(slot);

  // Clear equip slot
  setEquipField(player, slotType, '');

  return true;
}

/**
 * Unequip an item and place it at a specific inventory slot index.
 * If targetIndex has an item, swaps the equipped item with that inventory item
 * (equipping the inventory item if it fits the same slot, otherwise just placing it there).
 * If targetIndex is beyond the array, appends to the end.
 */
export function unequipItemToSlot(player: PlayerState, slotType: EquipSlotType, targetIndex: number): boolean {
  const currentlyEquipped = getEquipField(player, slotType);
  if (!currentlyEquipped) return false;

  if (targetIndex >= 0 && targetIndex < player.inventory.length) {
    // Target slot has an item — check if we can swap-equip it
    const targetSlot = player.inventory[targetIndex];
    const targetTemplate = DataManager.instance.getItem(targetSlot.itemId);

    if (targetTemplate?.equipSlot === slotType) {
      // The target item fits the same equip slot — swap: equip target, place current in inventory
      const targetItemId = targetSlot.itemId;
      setEquipField(player, slotType, targetItemId);

      // Replace the inventory slot with the previously equipped item
      const newSlot = new InventorySlotState();
      newSlot.itemId = currentlyEquipped;
      newSlot.quantity = 1;
      player.inventory.splice(targetIndex, 1, newSlot);
    } else {
      // Target item doesn't fit equip slot — just unequip to end of inventory
      if (player.inventory.length >= INVENTORY_MAX_SLOTS) return false;
      setEquipField(player, slotType, '');
      const slot = new InventorySlotState();
      slot.itemId = currentlyEquipped;
      slot.quantity = 1;
      player.inventory.push(slot);
    }
  } else {
    // Target is an empty slot or out of range — unequip to end
    if (player.inventory.length >= INVENTORY_MAX_SLOTS) return false;
    setEquipField(player, slotType, '');
    const slot = new InventorySlotState();
    slot.itemId = currentlyEquipped;
    slot.quantity = 1;
    player.inventory.push(slot);
  }

  return true;
}
