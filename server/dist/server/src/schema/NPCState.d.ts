import { Schema, ArraySchema } from '@colyseus/schema';
import type { ActiveBuff } from './PlayerState.js';
/**
 * Lightweight buff descriptor synced to clients so they can display active
 * debuffs on the enemy target pane (e.g. Poison Blade DoT pill).
 */
export declare class NpcBuffInfo extends Schema {
    skillId: string;
    /** Server timestamp (ms) when this buff expires */
    expiresAt: number;
    /** DoT damage per second (0 = not a DoT) */
    dotDamagePerSec: number;
}
/**
 * Synced NPC/Enemy state. Sent to all clients for rendering.
 */
export declare class NPCState extends Schema {
    /** Unique instance ID (e.g., "npc_grasslands_0") */
    id: string;
    /** Template ID (references NPCTemplate from DataManager) */
    templateId: string;
    /** Display name */
    name: string;
    /** "enemy" or "npc" */
    npcType: string;
    /** Current zone */
    zoneId: string;
    /** Position */
    x: number;
    y: number;
    /** Current HP */
    hp: number;
    /** Max HP */
    maxHp: number;
    /** Level */
    level: number;
    /** Is alive */
    alive: boolean;
    /** Sprite rendering */
    spriteColor: number;
    spriteSize: number;
    /** Aim angle (for facing direction) */
    aimAngle: number;
    /** Active debuffs/buffs visible to clients — kept in sync by applyNpcBuff / tickNpcBuffs */
    syncedBuffs: ArraySchema<NpcBuffInfo>;
    /** Full buff data for server-side ticking (mirrors PlayerState.activeBuffs pattern) */
    activeBuffs: ActiveBuff[];
}
//# sourceMappingURL=NPCState.d.ts.map