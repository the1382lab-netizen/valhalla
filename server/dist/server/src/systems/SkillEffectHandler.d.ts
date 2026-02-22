/**
 * Extensible skill effect system — thin dispatcher.
 *
 * Custom per-skill handlers live in ./handlers/ and self-register via
 * registerEffectHandler(). If no custom handler is found for a skill,
 * the defaultEffect() in this file handles standard damage/healing/buff
 * patterns based on SkillTemplate data.
 *
 * To add a new skill handler:
 *   1. Create a file in ./handlers/  (e.g. mySkillHandler.ts)
 *   2. Import registerEffectHandler from ./handlers/registry.js
 *   3. Call registerEffectHandler(SkillId.MY_SKILL, myHandler)
 *   4. Import the file from ./handlers/index.ts for side-effect registration
 */
import { SkillTemplate } from '@valhalla/shared';
import { PlayerState, ActiveBuff } from '../schema/PlayerState.js';
import { NPCState } from '../schema/NPCState.js';
import type { PlayerMap } from './SkillSystem.js';
import './handlers/index.js';
import { isNpcTarget, registerEffectHandler, type CombatTarget, type SkillEffectContext, type SkillEvent, type SkillDamageEvent, type SkillHealEvent, type SkillBuffEvent, type SkillDebuffEvent, type SkillMissEvent, type EffectHandler } from './handlers/index.js';
export { isNpcTarget, registerEffectHandler, type CombatTarget, type SkillEffectContext, type SkillEvent, type SkillDamageEvent, type SkillHealEvent, type SkillBuffEvent, type SkillDebuffEvent, type SkillMissEvent, type EffectHandler, };
/**
 * Execute the effect of a completed skill cast.
 * Looks up a custom handler first, then falls back to default.
 */
export declare function executeSkillEffect(caster: PlayerState, target: CombatTarget | null, skill: SkillTemplate, allPlayers: PlayerMap, now: number, ctx?: SkillEffectContext): SkillEvent[];
/**
 * Sync server-side activeBuffs → the Colyseus-synced syncedBuffs ArraySchema
 * on an NPC. Call this any time activeBuffs changes.
 */
export declare function syncNpcBuffsToSchema(npc: NPCState): void;
/**
 * Apply a buff/debuff to an NPC (mirrors applyBuff for players).
 * Uses "replace" stacking semantics by default — re-applying the same
 * skillId from the same caster refreshes the duration.
 * Updates syncedBuffs so clients see the change immediately.
 */
export declare function applyNpcBuff(npc: NPCState, buff: ActiveBuff): void;
export declare function applyBuff(player: PlayerState, buff: ActiveBuff): void;
//# sourceMappingURL=SkillEffectHandler.d.ts.map