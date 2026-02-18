import { Schema, defineTypes } from '@colyseus/schema';

/**
 * Colyseus-synced state for a spell projectile (e.g. Fireball).
 *
 * Synced fields drive client interpolation and VFX.
 * Server-only fields (prefixed with _) are not registered in defineTypes
 * and are invisible to the state delta system.
 */
export class SpellProjectileState extends Schema {
  id: string = '';
  ownerId: string = '';
  /** Which skill spawned this (e.g. 'wizard_fireball') */
  skillId: string = '';
  x: number = 0;
  y: number = 0;
  /** Destination the projectile is travelling toward */
  targetX: number = 0;
  targetY: number = 0;
  speed: number = 0;

  // ── Server-only (not synced) ──────────────────────────────
  /** Scaled damage value to apply on detonation */
  _damage: number = 0;
  /** AoE blast radius in pixels */
  _aoeRadius: number = 0;
  /** Caster's crit chance (0–1) */
  _critChance: number = 0.05;
  /** Caster's crit damage multiplier */
  _critDamage: number = 0.5;
  /** Caster's dexterity (for hit rolls) */
  _attackerDex: number = 10;
  /** Distance travelled so far (for max-range safety fallback) */
  _distanceTravelled: number = 0;
  /** Zone the caster was in — used to scope AoE target queries */
  _zoneId: string = '';
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
