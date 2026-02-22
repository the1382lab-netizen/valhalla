import { Schema, ArraySchema, defineTypes } from '@colyseus/schema';
// ── Bag Slot Schema (synced to client) ──────────────────────
export class BagSlotState extends Schema {
    constructor() {
        super(...arguments);
        this.itemId = '';
        this.quantity = 1;
    }
}
defineTypes(BagSlotState, {
    itemId: 'string',
    quantity: 'uint16',
});
// ── Loot Bag Schema (synced to client) ──────────────────────
export class LootBagState extends Schema {
    constructor() {
        super(...arguments);
        this.id = '';
        this.zoneId = '';
        this.x = 0;
        this.y = 0;
        this.items = new ArraySchema();
        // Server-only (NOT in defineTypes): for despawn tracking
        this._createdAt = 0;
    }
}
defineTypes(LootBagState, {
    id: 'string',
    zoneId: 'string',
    x: 'float32',
    y: 'float32',
    items: [BagSlotState],
});
//# sourceMappingURL=LootBagState.js.map