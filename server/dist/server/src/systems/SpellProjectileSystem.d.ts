/**
 * SpellProjectileSystem
 *
 * Updates all active spell projectiles each server tick:
 *   - Moves each projectile toward its ground target
 *   - Detonates on first entity hit (player or NPC), wall collision,
 *     or on reaching the target position
 *   - Applies AoE damage with linear distance-based falloff to all
 *     entities within the blast radius at the detonation point
 */
import { MapSchema } from '@colyseus/schema';
import { PlayerState } from '../schema/PlayerState.js';
import { SpellProjectileState } from '../schema/SpellProjectileState.js';
import { NPCState } from '../schema/NPCState.js';
import { CollisionSystem } from './CollisionSystem.js';
import type { NPCSystem } from './NPCSystem.js';
export interface SpellProjectileEvent {
    type: 'playerHit' | 'playerDied' | 'npcHit' | 'npcDied' | 'spellImpact';
    data: any;
}
export declare class SpellProjectileSystem {
    private collision;
    constructor(collision: CollisionSystem);
    /**
     * Advance all spell projectiles and resolve detonations.
     * Called once per server tick from GameRoom.update().
     *
     * @returns IDs to remove from the state map + any combat/VFX events
     */
    update(spellProjectiles: MapSchema<SpellProjectileState>, players: MapSchema<PlayerState>, npcs: MapSchema<NPCState>, npcSystem: NPCSystem, dt: number, now: number): {
        toRemove: string[];
        events: SpellProjectileEvent[];
    };
    /**
     * Explode the projectile at (detonateX, detonateY).
     *
     * Finds all entities within aoeRadius, applies distance-based damage
     * falloff, and emits a SPELL_IMPACT VFX event.
     */
    private detonate;
    /**
     * Apply AoE spell damage to a single player target.
     * Runs the full stat-driven pipeline (hit → dodge → crit → block → defense)
     * then scales by distance falloff.
     */
    private applyAoeDamage;
    /**
     * Linear falloff: 1.0 at center → FIREBALL_DAMAGE_FALLOFF_MIN at edge.
     */
    private computeFalloff;
}
//# sourceMappingURL=SpellProjectileSystem.d.ts.map