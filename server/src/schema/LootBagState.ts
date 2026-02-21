import { Schema, ArraySchema, defineTypes } from '@colyseus/schema';

// ── Bag Slot Schema (synced to client) ──────────────────────
export class BagSlotState extends Schema {
  itemId: string = '';
  quantity: number = 1;
}

defineTypes(BagSlotState, {
  itemId: 'string',
  quantity: 'uint16',
});

// ── Loot Bag Schema (synced to client) ──────────────────────
export class LootBagState extends Schema {
  id: string = '';
  zoneId: string = '';
  x: number = 0;
  y: number = 0;
  items: ArraySchema<BagSlotState> = new ArraySchema<BagSlotState>();

  // Server-only (NOT in defineTypes): for despawn tracking
  _createdAt: number = 0;
}

defineTypes(LootBagState, {
  id: 'string',
  zoneId: 'string',
  x: 'float32',
  y: 'float32',
  items: [BagSlotState],
});
