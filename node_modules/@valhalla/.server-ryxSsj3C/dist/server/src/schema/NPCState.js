import { Schema, ArraySchema, defineTypes } from '@colyseus/schema';
// ── NPC Buff Info (synced to clients for target pane display) ─────────────
/**
 * Lightweight buff descriptor synced to clients so they can display active
 * debuffs on the enemy target pane (e.g. Poison Blade DoT pill).
 */
export class NpcBuffInfo extends Schema {
    constructor() {
        super(...arguments);
        this.skillId = '';
        /** Server timestamp (ms) when this buff expires */
        this.expiresAt = 0;
        /** DoT damage per second (0 = not a DoT) */
        this.dotDamagePerSec = 0;
    }
}
defineTypes(NpcBuffInfo, {
    skillId: 'string',
    expiresAt: 'float64',
    dotDamagePerSec: 'float32',
});
/**
 * Synced NPC/Enemy state. Sent to all clients for rendering.
 */
export class NPCState extends Schema {
    constructor() {
        super(...arguments);
        /** Unique instance ID (e.g., "npc_grasslands_0") */
        this.id = '';
        /** Template ID (references NPCTemplate from DataManager) */
        this.templateId = '';
        /** Display name */
        this.name = '';
        /** "enemy" or "npc" */
        this.npcType = 'enemy';
        /** Current zone */
        this.zoneId = '';
        /** Position */
        this.x = 0;
        this.y = 0;
        /** Current HP */
        this.hp = 0;
        /** Max HP */
        this.maxHp = 0;
        /** Level */
        this.level = 1;
        /** Is alive */
        this.alive = true;
        /** Sprite rendering */
        this.spriteColor = 0xff0000;
        this.spriteSize = 24;
        /** Aim angle (for facing direction) */
        this.aimAngle = 0;
        // ── Synced buff display (for client target pane) ─────────────────────────
        /** Active debuffs/buffs visible to clients — kept in sync by applyNpcBuff / tickNpcBuffs */
        this.syncedBuffs = new ArraySchema();
        // ── Server-only buff tracking (NOT synced) ───────────────────────────────
        /** Full buff data for server-side ticking (mirrors PlayerState.activeBuffs pattern) */
        this.activeBuffs = [];
    }
}
defineTypes(NPCState, {
    id: 'string',
    templateId: 'string',
    name: 'string',
    npcType: 'string',
    zoneId: 'string',
    x: 'float32',
    y: 'float32',
    hp: 'float32',
    maxHp: 'float32',
    level: 'uint8',
    alive: 'boolean',
    spriteColor: 'uint32',
    spriteSize: 'uint8',
    aimAngle: 'float32',
    syncedBuffs: [NpcBuffInfo],
});
//# sourceMappingURL=NPCState.js.map