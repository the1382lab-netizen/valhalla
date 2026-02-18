import { Schema } from '@colyseus/schema';
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
}
//# sourceMappingURL=NPCState.d.ts.map