/**
 * Extensible skill effect system.
 *
 * Each skill can register a custom EffectHandler. If none is registered,
 * the defaultEffect() handles standard damage/healing/buff patterns.
 * This gives us hook points to add unique per-skill mechanics later
 * without refactoring the core casting engine.
 */

import {
  SkillId,
  SkillTemplate,
  SkillCategory,
  ResourceType,
} from '@valhalla/shared';
import { PlayerState, ActiveBuff } from '../schema/PlayerState.js';
import type { PlayerMap } from './SkillSystem.js';

// ── Skill Event Types ──────────────────────────────────────

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

// ── Effect Handler Type ────────────────────────────────────

export type EffectHandler = (
  caster: PlayerState,
  target: PlayerState | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
  now: number,
) => SkillEvent[];

// ── Handler Registry ───────────────────────────────────────

/**
 * Custom per-skill handlers. Register specific skill logic here.
 * If a skill has no registered handler, defaultEffect() is used.
 */
const EFFECT_HANDLERS: Partial<Record<SkillId, EffectHandler>> = {
  // Example: future custom handler
  // [SkillId.WIZARD_BLINK]: blinkHandler,
};

/**
 * Register a custom effect handler for a skill.
 */
export function registerEffectHandler(skillId: SkillId, handler: EffectHandler): void {
  EFFECT_HANDLERS[skillId] = handler;
}

// ── Execute Effect ─────────────────────────────────────────

/**
 * Execute the effect of a completed skill cast.
 * Looks up a custom handler first, then falls back to default.
 */
export function executeSkillEffect(
  caster: PlayerState,
  target: PlayerState | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
  now: number,
): SkillEvent[] {
  const handler = EFFECT_HANDLERS[skill.id];
  if (handler) {
    return handler(caster, target, skill, allPlayers, now);
  }
  return defaultEffect(caster, target, skill, allPlayers, now);
}

// ── Default Effect Handler ─────────────────────────────────

function defaultEffect(
  caster: PlayerState,
  target: PlayerState | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
  now: number,
): SkillEvent[] {
  const events: SkillEvent[] = [];

  // ── Damage skills ──
  if (skill.baseDamage) {
    const targets = getAffectedTargets(caster, target, skill, allPlayers);
    for (const t of targets) {
      if (!t.alive) continue;
      const [min, max] = skill.baseDamage;
      const rawDamage = min + Math.random() * (max - min);

      // Scale with primary stat
      const statValue = getStatValue(caster, skill.scalingStat);
      const scaledDamage = rawDamage + statValue * 0.8;

      // Simple crit check
      const isCrit = Math.random() < (caster.stats?.critChance ?? 0.05);
      const critMult = isCrit ? 1 + (caster.stats?.critDamage ?? 0.5) : 1;
      const finalDamage = Math.round(scaledDamage * critMult);

      t.hp = Math.max(0, t.hp - finalDamage);
      if (t.hp <= 0) {
        t.alive = false;
      }

      events.push({ type: 'damage', targetId: t.id, damage: finalDamage, isCrit });

      // Apply DoT if present
      if (skill.dotDamagePerSec && skill.buffDurationMs) {
        applyBuff(t, {
          skillId: skill.id,
          casterId: caster.id,
          appliedAt: now,
          expiresAt: now + skill.buffDurationMs,
          dotDamagePerSec: skill.dotDamagePerSec,
        });
        events.push({ type: 'debuff', targetId: t.id, skillId: skill.id, durationMs: skill.buffDurationMs });
      }
    }
  }

  // ── Healing skills ──
  if (skill.baseHealing) {
    const healTarget = target ?? caster;
    if (healTarget.alive) {
      const [min, max] = skill.baseHealing;
      const rawHeal = min + Math.random() * (max - min);
      const statValue = getStatValue(caster, skill.scalingStat);
      const scaledHeal = rawHeal + statValue * 0.9;
      const finalHeal = Math.round(scaledHeal);

      healTarget.hp = Math.min(healTarget.maxHp, healTarget.hp + finalHeal);
      events.push({ type: 'heal', targetId: healTarget.id, amount: finalHeal });
    }

    // Apply HoT if present
    if (skill.hotHealPerSec && skill.buffDurationMs) {
      applyBuff(healTarget, {
        skillId: skill.id,
        casterId: caster.id,
        appliedAt: now,
        expiresAt: now + skill.buffDurationMs,
        hotHealPerSec: skill.hotHealPerSec,
      });
      events.push({ type: 'buff', targetId: healTarget.id, skillId: skill.id, durationMs: skill.buffDurationMs });
    }
  }

  // ── Buff/debuff-only skills (no damage or healing) ──
  if (!skill.baseDamage && !skill.baseHealing && skill.buffDurationMs) {
    const buffTarget = skill.category === SkillCategory.DEBUFF
      ? (target ?? caster)
      : (skill.targetType === 'self' ? caster : (target ?? caster));

    applyBuff(buffTarget, {
      skillId: skill.id,
      casterId: caster.id,
      appliedAt: now,
      expiresAt: now + skill.buffDurationMs,
    });

    const eventType = skill.category === SkillCategory.DEBUFF ? 'debuff' : 'buff';
    events.push({ type: eventType, targetId: buffTarget.id, skillId: skill.id, durationMs: skill.buffDurationMs } as SkillEvent);
  }

  return events;
}

// ── Helpers ────────────────────────────────────────────────

function getStatValue(player: PlayerState, statName: string): number {
  if (!player.stats) return 0;
  return (player.stats as any)[statName] ?? 0;
}

function applyBuff(player: PlayerState, buff: ActiveBuff): void {
  // Remove existing buff of same skill from same caster (refresh)
  player.activeBuffs = player.activeBuffs.filter(
    b => !(b.skillId === buff.skillId && b.casterId === buff.casterId),
  );
  player.activeBuffs.push(buff);
}

/**
 * Determine which players are affected by a skill based on target type.
 */
function getAffectedTargets(
  caster: PlayerState,
  target: PlayerState | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
): PlayerState[] {
  switch (skill.targetType) {
    case 'singleEnemy':
      return target && target.id !== caster.id ? [target] : [];

    case 'aoeSelf': {
      // All enemies within skill.range of caster
      const targets: PlayerState[] = [];
      allPlayers.forEach(p => {
        if (p.id === caster.id || !p.alive || p.zoneId !== caster.zoneId) return;
        const dx = p.x - caster.x;
        const dy = p.y - caster.y;
        if (dx * dx + dy * dy <= skill.range * skill.range) {
          targets.push(p);
        }
      });
      return targets;
    }

    case 'cone': {
      // Enemies in a cone in front of caster
      const targets: PlayerState[] = [];
      const coneHalfAngle = Math.PI / 4; // 45° half-cone
      allPlayers.forEach(p => {
        if (p.id === caster.id || !p.alive || p.zoneId !== caster.zoneId) return;
        const dx = p.x - caster.x;
        const dy = p.y - caster.y;
        const distSq = dx * dx + dy * dy;
        if (distSq > skill.range * skill.range) return;
        const angleToTarget = Math.atan2(dy, dx);
        let angleDiff = Math.abs(angleToTarget - caster.aimAngle);
        if (angleDiff > Math.PI) angleDiff = 2 * Math.PI - angleDiff;
        if (angleDiff <= coneHalfAngle) {
          targets.push(p);
        }
      });
      return targets;
    }

    case 'aoeGround':
      // For now, treat as single target (ground targeting needs position data)
      return target && target.id !== caster.id ? [target] : [];

    default:
      return target ? [target] : [];
  }
}
