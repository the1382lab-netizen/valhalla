/**
 * Backstab effect handler.
 *
 * Deals melee (dexterity-scaled) damage to a single enemy target.
 * Bonus mechanics when attacking from the target's rear arc (±90°):
 *   • +25% crit chance on top of the caster's base crit
 *   • 1.5× damage multiplier
 *
 * The rear arc is defined as: the angle from the target to the attacker
 * is within ±90° of the direction opposite to the target's aimAngle.
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

/** Half-angle of the rear arc in radians. ±90° = the full back hemisphere. */
const BEHIND_HALF_ARC = Math.PI / 2;
/** Extra flat crit chance added when attacking from behind. */
const BEHIND_CRIT_BONUS = 0.25;
/** Damage multiplier applied when attacking from behind. */
const BEHIND_DAMAGE_MULT = 1.5;

/**
 * Returns true when the attacker is positioned in the rear arc of the target.
 * Uses the target's aimAngle (facing direction) to determine front vs back.
 */
function isAttackingFromBehind(caster: PlayerState, target: CombatTarget): boolean {
  const angleToAttacker = Math.atan2(caster.y - target.y, caster.x - target.x);
  // The target's "back" is opposite its facing direction
  const targetBack = target.aimAngle + Math.PI;
  let angleDiff = Math.abs(angleToAttacker - targetBack);
  // Normalise to [0, π]
  if (angleDiff > Math.PI) angleDiff = 2 * Math.PI - angleDiff;
  return angleDiff <= BEHIND_HALF_ARC;
}

function backstabHandler(
  caster: PlayerState,
  target: CombatTarget | null,
  skill: SkillTemplate,
  _allPlayers: PlayerMap,
  now: number,
  ctx?: SkillEffectContext,
): SkillEvent[] {
  if (!target || !target.alive) return [];

  const behind = isAttackingFromBehind(caster, target);

  // ── Damage roll ────────────────────────────────────────────
  const [min, max] = skill.baseDamage!;
  const rawDamage = min + Math.random() * (max - min);
  const dex = getStatValue(caster, skill.scalingStat);
  const scaledDamage = rawDamage + dex * 0.8;

  // ── Behind bonus ───────────────────────────────────────────
  const damageMult = behind ? BEHIND_DAMAGE_MULT : 1.0;
  const critChance = (caster.stats?.critChance ?? 0.05) + (behind ? BEHIND_CRIT_BONUS : 0);

  const isCrit = Math.random() < critChance;
  const critMult = isCrit ? 1 + (caster.stats?.critDamage ?? 0.5) : 1;
  const finalDamage = Math.round(scaledDamage * damageMult * critMult);

  // ── Apply damage ───────────────────────────────────────────
  if (isNpcTarget(target) && ctx?.damageNpc) {
    const { xpReward } = ctx.damageNpc(target.id, finalDamage, caster.id, now);
    if (xpReward > 0) {
      ctx?.awardXP ? ctx.awardXP(caster.id, xpReward) : (caster.xp = (caster.xp ?? 0) + xpReward);
    }
  } else {
    target.hp = Math.max(0, target.hp - finalDamage);
    if (target.hp <= 0) target.alive = false;
  }

  return [{ type: 'damage', targetId: target.id, damage: finalDamage, isCrit }];
}

registerEffectHandler(SkillId.ROGUE_BACKSTAB, backstabHandler);
