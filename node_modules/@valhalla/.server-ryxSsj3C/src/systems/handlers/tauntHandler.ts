/**
 * Taunt effect handler.
 *
 * Adds a level-scaled threat bonus and forces the NPC to target the caster.
 * Base threat: 50, scaling: +20 per level → Lv1 = 70, Lv10 = 250, Lv25 = 550.
 */

import { SkillId, SkillTemplate } from '@valhalla/shared';
import { PlayerState } from '../../schema/PlayerState.js';
import {
  registerEffectHandler,
  isNpcTarget,
  SkillEvent,
  SkillEffectContext,
  CombatTarget,
} from './registry.js';
import type { PlayerMap } from '../SkillSystem.js';

const TAUNT_BASE_THREAT = 50;
const TAUNT_PER_LEVEL   = 20;

function tauntHandler(
  caster: PlayerState,
  target: CombatTarget | null,
  skill: SkillTemplate,
  _allPlayers: PlayerMap,
  _now: number,
  ctx?: SkillEffectContext,
): SkillEvent[] {
  if (!target || !isNpcTarget(target) || !target.alive) return [];
  if (!ctx?.tauntNpc) return [];

  const bonusThreat = TAUNT_BASE_THREAT + TAUNT_PER_LEVEL * caster.level;
  ctx.tauntNpc(target.id, caster.id, bonusThreat);

  return [{
    type: 'buff',
    targetId: target.id,
    skillId: skill.id,
    durationMs: skill.buffDurationMs ?? 6000,
  }];
}

registerEffectHandler(SkillId.WARRIOR_TAUNT, tauntHandler);
