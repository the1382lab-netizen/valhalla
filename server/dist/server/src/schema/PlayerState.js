import { Schema, ArraySchema, defineTypes } from '@colyseus/schema';
import { PLAYER_MAX_HP } from '@valhalla/shared';
// ── Inventory Slot Schema (synced to client) ─────────────────
export class InventorySlotState extends Schema {
    constructor() {
        super(...arguments);
        this.itemId = '';
        this.quantity = 1;
    }
}
defineTypes(InventorySlotState, {
    itemId: 'string',
    quantity: 'uint8',
});
export class PlayerState extends Schema {
    constructor() {
        super(...arguments);
        this.id = '';
        this.x = 0;
        this.y = 0;
        this.aimAngle = 0;
        this.speed = 0;
        // ── Identity & Progression (synced) ─────────────────────
        this.characterName = '';
        this.classId = 'warrior';
        this.level = 1;
        this.xp = 0;
        this.zoneId = 'grasslands';
        // ── Vitals (synced) ──────────────────────────────────
        this.hp = PLAYER_MAX_HP;
        this.maxHp = PLAYER_MAX_HP;
        this.mana = 0;
        this.maxMana = 0;
        this.energy = 0;
        this.maxEnergy = 0;
        this.alive = true;
        // ── Casting State (synced for cast bar) ────────────
        this.castingSkillId = '';
        this.castingStartedAt = 0;
        this.castingDurationMs = 0;
        // ── Equipment (synced) — empty string = nothing equipped ──
        this.equipWeapon = '';
        this.equipHelm = '';
        this.equipChest = '';
        this.equipLegs = '';
        this.equipBoots = '';
        this.equipRing = '';
        // ── Inventory (synced) ───────────────────────────────
        this.inventory = new ArraySchema();
        // ── Server-only (not synced) ──────────────────────────
        this.inputSeq = 0;
        this.fireCooldown = 0;
        this.meleeCooldown = 0;
        this.invulnerableUntil = 0;
        this.respawnAt = 0;
        // ── Skill Server-only State ───────────────────────────
        /** Skill cooldowns: skillId → timestamp when cooldown expires */
        this.skillCooldowns = new Map();
        /** Active buffs on this player */
        this.activeBuffs = [];
        /** Action bar skill assignments (8 slots) */
        this.actionBar = ['', '', '', '', '', '', '', ''];
        /**
         * Full resolved stat block — server-only, used for combat math.
         * Recomputed on join, level-up, and (later) gear changes.
         * NOT a Colyseus schema property — just a plain object.
         */
        this.stats = null;
    }
}
defineTypes(PlayerState, {
    id: 'string',
    x: 'float32',
    y: 'float32',
    aimAngle: 'float32',
    speed: 'float32',
    characterName: 'string',
    classId: 'string',
    level: 'uint8',
    xp: 'uint16',
    zoneId: 'string',
    hp: 'int16',
    maxHp: 'int16',
    mana: 'int16',
    maxMana: 'int16',
    energy: 'float32',
    maxEnergy: 'int16',
    alive: 'boolean',
    castingSkillId: 'string',
    castingStartedAt: 'float64',
    castingDurationMs: 'uint16',
    inputSeq: 'uint32',
    equipWeapon: 'string',
    equipHelm: 'string',
    equipChest: 'string',
    equipLegs: 'string',
    equipBoots: 'string',
    equipRing: 'string',
    inventory: [InventorySlotState],
});
//# sourceMappingURL=PlayerState.js.map