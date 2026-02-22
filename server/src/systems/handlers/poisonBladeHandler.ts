/**
 * Poison Blade effect handler.
 *
 * Applies a poison DoT to a single enemy target.
 * Damage per second scales with the caster's Dexterity.
 *
 * Both player and NPC targets receive a ticking DoT buff:
 *   - Player targets: tracked in activeBuffs, ticked every second by SkillSystem.tickBuffs().
 *   - NPC targets:    tracked in NPCState.activeBuffs, ticked every second by NPCSystem.tickNpcBuffs().
 *     The NPC's syncedBuffs schema field is updated so clients see the debuff in the target pane.
 */

import { SkillId, SkillTemplate } from '@valhalla/shared';
import { PlayerState } from '../../schema/PlayerState.js';
import {
  registerEffectHandler,
  isNpcTarget,
  type SkillEvent,
  type SkillEffectContext,
  type CombatTarget,
} from './registry.js';
import type { PlayerMap } from '../SkillSystem.js';
import { getStatValue } from './utils.js';
import { applyBuff, applyNpcBuff } from '../SkillEffectHandler.js';

/** Duration of the poison DoT on the target (ms). */
const POISON_DURATION_MS = 6_000;
/** Base damage per second before stat scaling. */
const BASE_DOT_DPS = 4;
/** Additional DoT DPS per point of Dexterity. */
const DEX_SCALE = 0.1;

function poisonBladeHandler(
  caster: PlayerState,
  target: CombatTarget | null,
  _skill: SkillTemplate,
  _allPlayers: PlayerMap,
  now: number,
  _ctx?: SkillEffectContext,
): SkillEvent[] {
  if (!target || !target.alive) return [];

  const dex = getStatValue(caster, 'dexterity');
  const dotDps = Math.round(BASE_DOT_DPS + dex * DEX_SCALE);

  if (isNpcTarget(target)) {
    // NPC target — apply a ticking poison buff tracked by NPCSystem.tickNpcBuffs().
    // The buff will tick 1 damage per second server-side and the client sees it
    // via syncedBuffs on the NPCState schema.
    applyNpcBuff(target, {
      skillId: SkillId.ROGUE_POISON_BLADE,
      casterId: caster.id,
      appliedAt: now,
      expiresAt: now + POISON_DURATION_MS,
      dotDamagePerSec: dotDps,
      stacks: 1,
    });
    return [{ type: 'debuff', targetId: target.id, skillId: SkillId.ROGUE_POISON_BLADE, durationMs: POISON_DURATION_MS }];
  }

  // Player target — apply a ticking poison buff.
  applyBuff(target, {
    skillId: SkillId.ROGUE_POISON_BLADE,
    casterId: caster.id,
    appliedAt: now,
    expiresAt: now + POISON_DURATION_MS,
    dotDamagePerSec: dotDps,
    stacks: 1,
  });

  return [{ type: 'debuff', targetId: target.id, skillId: SkillId.ROGUE_POISON_BLADE, durationMs: POISON_DURATION_MS }];
}

registerEffectHandler(SkillId.ROGUE_POISON_BLADE, poisonBladeHandler);
