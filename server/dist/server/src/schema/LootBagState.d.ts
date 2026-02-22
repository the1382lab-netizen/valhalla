import { Schema, ArraySchema } from '@colyseus/schema';
export declare class BagSlotState extends Schema {
    itemId: string;
    quantity: number;
}
export declare class LootBagState extends Schema {
    id: string;
    zoneId: string;
    x: number;
    y: number;
    items: ArraySchema<BagSlotState>;
    _createdAt: number;
}
//# sourceMappingURL=LootBagState.d.ts.map