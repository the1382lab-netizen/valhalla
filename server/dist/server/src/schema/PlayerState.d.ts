import { Schema, ArraySchema } from '@colyseus/schema';
import type { ResolvedStats } from '@valhalla/shared';
export declare class InventorySlotState extends Schema {
    itemId: string;
    quantity: number;
}
export declare class PlayerState extends Schema {
    id: string;
    x: number;
    y: number;
    aimAngle: number;
    speed: number;
    characterName: string;
    classId: string;
    level: number;
    xp: number;
    zoneId: string;
    hp: number;
    maxHp: number;
    mana: number;
    maxMana: number;
    alive: boolean;
    equipWeapon: string;
    equipHelm: string;
    equipChest: string;
    equipLegs: string;
    equipBoots: string;
    equipRing: string;
    inventory: ArraySchema<InventorySlotState>;
    inputSeq: number;
    fireCooldown: number;
    meleeCooldown: number;
    invulnerableUntil: number;
    respawnAt: number;
    /**
     * Full resolved stat block — server-only, used for combat math.
     * Recomputed on join, level-up, and (later) gear changes.
     * NOT a Colyseus schema property — just a plain object.
     */
    stats: ResolvedStats | null;
}
//# sourceMappingURL=PlayerState.d.ts.map