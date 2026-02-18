import { Schema, defineTypes } from '@colyseus/schema';
/**
 * Colyseus-synced state for a spell projectile (e.g. Fireball).
 *
 * Synced fields drive client interpolation and VFX.
 * Server-only fields (prefixed with _) are not registered in defineTypes
 * and are invisible to the state delta system.
 */
export class SpellProjectileState extends Schema {
    constructor() {
        super(...arguments);
        this.id = '';
        this.ownerId = '';
        /** Which skill spawned this (e.g. 'wizard_fireball') */
        this.skillId = '';
        this.x = 0;
        this.y = 0;
        /** Destination the projectile is travelling toward */
        this.targetX = 0;
        this.targetY = 0;
        this.speed = 0;
        // ── Server-only (not synced) ──────────────────────────────
        /** Scaled damage value to apply on detonation */
        this._damage = 0;
        /** AoE blast radius in pixels */
        this._aoeRadius = 0;
        /** Caster's crit chance (0–1) */
        this._critChance = 0.05;
        /** Caster's crit damage multiplier */
        this._critDamage = 0.5;
        /** Caster's dexterity (for hit rolls) */
        this._attackerDex = 10;
        /** Distance travelled so far (for max-range safety fallback) */
        this._distanceTravelled = 0;
        /** Zone the caster was in — used to scope AoE target queries */
        this._zoneId = '';
    }
}
defineTypes(SpellProjectileState, {
    id: 'string',
    ownerId: 'string',
    skillId: 'string',
    x: 'float32',
    y: 'float32',
    targetX: 'float32',
    targetY: 'float32',
    speed: 'float32',
});
//# sourceMappingURL=SpellProjectileState.js.map