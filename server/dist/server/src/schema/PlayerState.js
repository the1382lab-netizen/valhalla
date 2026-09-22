import { Schema, ArraySchema, defineTypes } from '@colyseus/schema';
import { PLAYER_MAX_HP, SkillId, EquipSlotType } from '@valhalla/shared';
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
        // ── Shield (synced) ── remaining absorption from Shield of Faith etc.
        this.shieldHp = 0;
        // ── Casting State (synced for cast bar) ────────────
        this.castingSkillId = '';
        this.castingStartedAt = 0;
        this.castingDurationMs = 0;
        // ── Appearance (synced) ──────────────────────────────
        /** Paperdoll base body, e.g. 'body_tan'. */
        this.bodyId = '';
        // ── Equipment (synced) — empty string = nothing equipped ──
        this.equipWeapon = '';
        this.equipOffhand = '';
        this.equipHelm = '';
        this.equipChest = '';
        this.equipLegs = '';
        this.equipBoots = '';
        this.equipGloves = '';
        this.equipBack = '';
        this.equipRing = '';
        // ── Inventory (synced) ───────────────────────────────
        this.inventory = new ArraySchema();
        // ── Auto-Attack State (synced) ────────────────────────
        /** Whether auto-attack is currently active */
        this.autoAttackActive = false;
        /** Which auto-attack skill is running (melee_attack or ranged_attack) */
        this.autoAttackSkillId = '';
        /** Session ID of the auto-attack target */
        this.autoAttackTargetId = '';
        // ── Server-only (not synced) ──────────────────────────
        this.inputSeq = 0;
        /** @deprecated Replaced by auto-attack system. Kept for backward compat during transition. */
        this.fireCooldown = 0;
        /** @deprecated Replaced by auto-attack system. Kept for backward compat during transition. */
        this.meleeCooldown = 0;
        this.invulnerableUntil = 0;
        this.respawnAt = 0;
        /** Timestamp of next allowed auto-attack swing (server-only) */
        this.nextAutoAttackAt = 0;
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
    shieldHp: 'int16',
    castingSkillId: 'string',
    castingStartedAt: 'float64',
    castingDurationMs: 'uint16',
    autoAttackActive: 'boolean',
    autoAttackSkillId: 'string',
    autoAttackTargetId: 'string',
    inputSeq: 'uint32',
    bodyId: 'string',
    equipWeapon: 'string',
    equipOffhand: 'string',
    equipHelm: 'string',
    equipChest: 'string',
    equipLegs: 'string',
    equipBoots: 'string',
    equipGloves: 'string',
    equipBack: 'string',
    equipRing: 'string',
    inventory: [InventorySlotState],
});
// ── Shield Absorption Helper ─────────────────────────────────
/**
 * Apply incoming damage against the player's shield (shieldHp) first.
 * Returns the remaining damage that should be applied to the player's HP.
 * When the shield is fully depleted, removes the Shield of Faith buff and
 * resets shieldHp to 0.
 */
export function applyShieldAbsorption(player, incomingDamage) {
    if (player.shieldHp <= 0)
        return incomingDamage;
    const absorbed = Math.min(player.shieldHp, incomingDamage);
    player.shieldHp -= absorbed;
    if (player.shieldHp <= 0) {
        player.shieldHp = 0;
        // Remove the shield buff so SkillSystem doesn't double-expire it
        player.activeBuffs = player.activeBuffs.filter(b => b.skillId !== SkillId.CLERIC_SHIELD_OF_FAITH);
    }
    return Math.max(0, incomingDamage - absorbed);
}
// ── Equipment slot <-> PlayerState field ─────────────────────
//
// One map instead of a switch in every consumer, so a new slot is a single
// edit here plus its `defineTypes` entry above.
export const PLAYER_EQUIP_FIELD = {
    [EquipSlotType.WEAPON]: 'equipWeapon',
    [EquipSlotType.OFFHAND]: 'equipOffhand',
    [EquipSlotType.HELM]: 'equipHelm',
    [EquipSlotType.CHEST]: 'equipChest',
    [EquipSlotType.LEGS]: 'equipLegs',
    [EquipSlotType.BOOTS]: 'equipBoots',
    [EquipSlotType.GLOVES]: 'equipGloves',
    [EquipSlotType.BACK]: 'equipBack',
    [EquipSlotType.RING]: 'equipRing',
};
/** Read the equipped itemId for a slot. Empty string = nothing equipped. */
export function getEquipped(player, slot) {
    const field = PLAYER_EQUIP_FIELD[slot];
    return field ? (player[field] ?? '') : '';
}
/** Set the equipped itemId for a slot. */
export function setEquipped(player, slot, itemId) {
    const field = PLAYER_EQUIP_FIELD[slot];
    if (field)
        player[field] = itemId;
}
/** Every non-empty equipped slot, for persistence. */
export function equippedEntries(player) {
    const out = [];
    for (const slot of Object.keys(PLAYER_EQUIP_FIELD)) {
        const itemId = getEquipped(player, slot);
        if (itemId)
            out.push({ slotType: slot, itemId });
    }
    return out;
}
//# sourceMappingURL=PlayerState.js.map