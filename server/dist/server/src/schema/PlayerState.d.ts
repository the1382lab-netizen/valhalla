import { Schema, ArraySchema } from '@colyseus/schema';
import { EquipSlotType } from '@valhalla/shared';
import type { ResolvedStats } from '@valhalla/shared';
export interface ActiveBuff {
    skillId: string;
    casterId: string;
    appliedAt: number;
    expiresAt: number;
    dotDamagePerSec?: number;
    hotHealPerSec?: number;
    /** Current stack count (default 1). Used with stackingMode === 'stack'. */
    stacks: number;
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
    shieldHp: number;
    castingSkillId: string;
    castingStartedAt: number;
    castingDurationMs: number;
    /** Paperdoll base body, e.g. 'body_tan'. */
    bodyId: string;
    equipWeapon: string;
    equipOffhand: string;
    equipHelm: string;
    equipChest: string;
    equipLegs: string;
    equipBoots: string;
    equipGloves: string;
    equipBack: string;
    equipRing: string;
    inventory: ArraySchema<InventorySlotState>;
    /** Whether auto-attack is currently active */
    autoAttackActive: boolean;
    /** Which auto-attack skill is running (melee_attack or ranged_attack) */
    autoAttackSkillId: string;
    /** Session ID of the auto-attack target */
    autoAttackTargetId: string;
    inputSeq: number;
    /** @deprecated Replaced by auto-attack system. Kept for backward compat during transition. */
    fireCooldown: number;
    /** @deprecated Replaced by auto-attack system. Kept for backward compat during transition. */
    meleeCooldown: number;
    invulnerableUntil: number;
    respawnAt: number;
    /** Timestamp of next allowed auto-attack swing (server-only) */
    nextAutoAttackAt: number;
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
/**
 * Apply incoming damage against the player's shield (shieldHp) first.
 * Returns the remaining damage that should be applied to the player's HP.
 * When the shield is fully depleted, removes the Shield of Faith buff and
 * resets shieldHp to 0.
 */
export declare function applyShieldAbsorption(player: PlayerState, incomingDamage: number): number;
export declare const PLAYER_EQUIP_FIELD: Record<EquipSlotType, keyof PlayerState>;
/** Read the equipped itemId for a slot. Empty string = nothing equipped. */
export declare function getEquipped(player: PlayerState, slot: EquipSlotType): string;
/** Set the equipped itemId for a slot. */
export declare function setEquipped(player: PlayerState, slot: EquipSlotType, itemId: string): void;
/** Every non-empty equipped slot, for persistence. */
export declare function equippedEntries(player: PlayerState): {
    slotType: EquipSlotType;
    itemId: string;
}[];
//# sourceMappingURL=PlayerState.d.ts.map