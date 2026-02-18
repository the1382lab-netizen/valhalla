/**
 * Server-authoritative skill/spell casting system.
 *
 * Handles: cast validation, cast progression, interrupts,
 * cooldown management, energy regen, and buff/DoT/HoT ticking.
 *
 * Client sends intent → server validates → server executes.
 */
import { PlayerState } from '../schema/PlayerState.js';
import { SkillEvent } from './SkillEffectHandler.js';
import { SpellProjectileState } from '../schema/SpellProjectileState.js';
import { MapSchema } from '@colyseus/schema';
/** Duck-typed map interface compatible with both Map and Colyseus MapSchema. */
export interface PlayerMap {
    get(key: string): PlayerState | undefined;
    forEach(callback: (value: PlayerState, key: string) => void): void;
}
export interface CastStartedResult {
    type: 'castStarted';
    casterId: string;
    skillId: string;
    castTimeMs: number;
}
export interface CastCompleteResult {
    type: 'castComplete';
    casterId: string;
    skillId: string;
    events: SkillEvent[];
}
export interface CastFailedResult {
    type: 'castFailed';
    casterId: string;
    reason: string;
}
export interface CastInterruptedResult {
    type: 'castInterrupted';
    casterId: string;
    skillId: string;
}
export interface BuffExpiredResult {
    type: 'buffExpired';
    targetId: string;
    skillId: string;
}
export interface DotTickResult {
    type: 'dotTick';
    targetId: string;
    skillId: string;
    damage: number;
}
export interface HotTickResult {
    type: 'hotTick';
    targetId: string;
    skillId: string;
    heal: number;
}
export type SkillSystemEvent = CastStartedResult | CastCompleteResult | CastFailedResult | CastInterruptedResult | BuffExpiredResult | DotTickResult | HotTickResult;
export declare class SkillSystem {
    private activeCasts;
    private dotTickTracker;
    /**
     * Attempt to start casting a skill.
     * Validates all preconditions. Returns result events.
     *
     * @param groundX  World X of the ground target — required for AOE_GROUND skills
     * @param groundY  World Y of the ground target — required for AOE_GROUND skills
     */
    tryStartCast(caster: PlayerState, skillId: string, targetId: string | null, allPlayers: PlayerMap, now: number, spellProjectiles?: MapSchema<SpellProjectileState>, groundX?: number | null, groundY?: number | null): SkillSystemEvent[];
    /**
     * Cancel an active cast (manual cancel or movement interrupt).
     */
    cancelCast(player: PlayerState, now: number): SkillSystemEvent[];
    /**
     * Main update tick. Call every server frame.
     * - Completes casts that have finished their cast time
     * - Ticks energy regen for non-caster classes
     * - Ticks DoTs and HoTs
     * - Expires finished buffs
     * - Checks for movement interrupts
     *
     * @param spellProjectiles  Room's spell projectile map — passed to effect handlers that spawn projectiles
     */
    update(allPlayers: PlayerMap, dt: number, now: number, spellProjectiles?: MapSchema<SpellProjectileState>): SkillSystemEvent[];
    private validateCast;
    private executeInstantCast;
    private startTimedCast;
    private completeCast;
    private deductResource;
    private clearCastingState;
    private tickBuffs;
}
//# sourceMappingURL=SkillSystem.d.ts.map