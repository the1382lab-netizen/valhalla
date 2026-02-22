/**
 * Centralized handler registry for skill effects and buff cleanup.
 *
 * Individual handler files import and call registerEffectHandler / registerBuffCleanup
 * as a side-effect of being imported, so all handlers are registered by the time
 * the game room starts processing casts.
 */
import { SkillId, SkillTemplate } from '@valhalla/shared';
import { PlayerState, ActiveBuff } from '../../schema/PlayerState.js';
import { NPCState } from '../../schema/NPCState.js';
import type { PlayerMap, NPCMap } from '../SkillSystem.js';
import { MapSchema } from '@colyseus/schema';
import { SpellProjectileState } from '../../schema/SpellProjectileState.js';
/** A valid skill target — either a player or an NPC. */
export type CombatTarget = PlayerState | NPCState;
/** Returns true when the target is an NPCState (has templateId, which PlayerState lacks). */
export declare function isNpcTarget(t: CombatTarget): t is NPCState;
export interface SkillEffectContext {
    spellProjectiles?: MapSchema<SpellProjectileState>;
    groundX: number | null;
    groundY: number | null;
    allNPCs?: NPCMap;
    damageNpc?: (npcId: string, damage: number, attackerId: string, now: number) => {
        died: boolean;
        xpReward: number;
    };
    tauntNpc?: (npcId: string, playerId: string, bonusThreat: number) => void;
    awardXP?: (playerId: string, amount: number) => void;
    /** Returns true when playerA and playerB are in the same party. Used to prevent friendly fire. */
    isPartyMember?: (playerIdA: string, playerIdB: string) => boolean;
}
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
export type EffectHandler = (caster: PlayerState, target: CombatTarget | null, skill: SkillTemplate, allPlayers: PlayerMap, now: number, ctx?: SkillEffectContext) => SkillEvent[];
/**
 * Called when a buff expires naturally (expiresAt reached) or is explicitly removed.
 * Use this to clean up any side-effects the buff created (e.g. clear shieldHp).
 */
export type BuffCleanupHandler = (player: PlayerState, buff: ActiveBuff) => void;
/** Register a custom effect handler for a skill. */
export declare function registerEffectHandler(skillId: SkillId, handler: EffectHandler): void;
/** Register a cleanup handler called when a buff expires. */
export declare function registerBuffCleanup(skillId: string, handler: BuffCleanupHandler): void;
/** Get the custom effect handler for a skill (if any). */
export declare function getEffectHandler(skillId: SkillId): EffectHandler | undefined;
/** Get the buff cleanup handler for a skill (if any). */
export declare function getBuffCleanup(skillId: string): BuffCleanupHandler | undefined;
//# sourceMappingURL=registry.d.ts.map