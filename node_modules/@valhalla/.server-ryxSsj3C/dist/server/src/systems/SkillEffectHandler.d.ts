/**
 * Extensible skill effect system.
 *
 * Each skill can register a custom EffectHandler. If none is registered,
 * the defaultEffect() handles standard damage/healing/buff patterns.
 * This gives us hook points to add unique per-skill mechanics later
 * without refactoring the core casting engine.
 */
import { SkillId, SkillTemplate } from '@valhalla/shared';
import { PlayerState } from '../schema/PlayerState.js';
import type { PlayerMap } from './SkillSystem.js';
export interface SkillDamageEvent {
    type: 'damage';
    targetId: string;
    damage: number;
    isCrit: boolean;
}
export interface SkillHealEvent {
    type: 'heal';
    targetId: string;
    amount: number;
}
export interface SkillBuffEvent {
    type: 'buff';
    targetId: string;
    skillId: string;
    durationMs: number;
}
export interface SkillDebuffEvent {
    type: 'debuff';
    targetId: string;
    skillId: string;
    durationMs: number;
}
export interface SkillMissEvent {
    type: 'miss';
    targetId: string;
}
export type SkillEvent = SkillDamageEvent | SkillHealEvent | SkillBuffEvent | SkillDebuffEvent | SkillMissEvent;
export type EffectHandler = (caster: PlayerState, target: PlayerState | null, skill: SkillTemplate, allPlayers: PlayerMap, now: number) => SkillEvent[];
/**
 * Register a custom effect handler for a skill.
 */
export declare function registerEffectHandler(skillId: SkillId, handler: EffectHandler): void;
/**
 * Execute the effect of a completed skill cast.
 * Looks up a custom handler first, then falls back to default.
 */
export declare function executeSkillEffect(caster: PlayerState, target: PlayerState | null, skill: SkillTemplate, allPlayers: PlayerMap, now: number): SkillEvent[];
//# sourceMappingURL=SkillEffectHandler.d.ts.map