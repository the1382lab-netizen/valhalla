import { Schema, ArraySchema } from '@colyseus/schema';
import type { ResolvedStats } from '@valhalla/shared';
export interface ActiveBuff {
    skillId: string;
    casterId: string;
    appliedAt: number;
    expiresAt: number;
    dotDamagePerSec?: number;
    hotHealPerSec?: number;
    /** Generic effect data — specific handlers interpret this */
    effectData?: Record<string, any>;
}
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
    energy: number;
    maxEnergy: number;
    alive: boolean;
    castingSkillId: string;
    castingStartedAt: number;
    castingDurationMs: number;
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
    /** Skill cooldowns: skillId → timestamp when cooldown expires */
    skillCooldowns: Map<string, number>;
    /** Active buffs on this player */
    activeBuffs: ActiveBuff[];
    /** Action bar skill assignments (8 slots) */
    actionBar: string[];
    /**
     * Full resolved stat block — server-only, used for combat math.
     * Recomputed on join, level-up, and (later) gear changes.
     * NOT a Colyseus schema property — just a plain object.
     */
    stats: ResolvedStats | null;
}
//# sourceMappingURL=PlayerState.d.ts.map