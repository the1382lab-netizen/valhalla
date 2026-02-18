import { Schema } from '@colyseus/schema';
/**
 * Colyseus-synced state for a spell projectile (e.g. Fireball).
 *
 * Synced fields drive client interpolation and VFX.
 * Server-only fields (prefixed with _) are not registered in defineTypes
 * and are invisible to the state delta system.
 */
export declare class SpellProjectileState extends Schema {
    id: string;
    ownerId: string;
    /** Which skill spawned this (e.g. 'wizard_fireball') */
    skillId: string;
    x: number;
    y: number;
    /** Destination the projectile is travelling toward */
    targetX: number;
    targetY: number;
    speed: number;
    /** Scaled damage value to apply on detonation */
    _damage: number;
    /** AoE blast radius in pixels */
    _aoeRadius: number;
    /** Caster's crit chance (0–1) */
    _critChance: number;
    /** Caster's crit damage multiplier */
    _critDamage: number;
    /** Caster's dexterity (for hit rolls) */
    _attackerDex: number;
    /** Distance travelled so far (for max-range safety fallback) */
    _distanceTravelled: number;
    /** Zone the caster was in — used to scope AoE target queries */
    _zoneId: string;
}
//# sourceMappingURL=SpellProjectileState.d.ts.map