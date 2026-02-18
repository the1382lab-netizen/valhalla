import { Schema, defineTypes } from '@colyseus/schema';
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
});
//# sourceMappingURL=NPCState.js.map