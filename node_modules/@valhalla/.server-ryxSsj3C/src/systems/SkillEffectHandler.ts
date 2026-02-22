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

import {
  SkillTemplate,
  SkillCategory,
} from '@valhalla/shared';
import { PlayerState, ActiveBuff } from '../schema/PlayerState.js';
import { NPCState, NpcBuffInfo } from '../schema/NPCState.js';
import type { PlayerMap } from './SkillSystem.js';
import { DataManager } from './DataManager.js';

// Import handlers barrel — triggers all handler registrations
import './handlers/index.js';

// Import from handlers — used locally and re-exported for backward compat
import {
  getEffectHandler,
  isNpcTarget,
  registerEffectHandler,
  getStatValue,
  type CombatTarget,
  type SkillEffectContext,
  type SkillEvent,
  type SkillDamageEvent,
  type SkillHealEvent,
  type SkillBuffEvent,
  type SkillDebuffEvent,
  type SkillMissEvent,
  type EffectHandler,
} from './handlers/index.js';

// Re-export so existing consumers of SkillEffectHandler don't break
export {
  isNpcTarget,
  registerEffectHandler,
  type CombatTarget,
  type SkillEffectContext,
  type SkillEvent,
  type SkillDamageEvent,
  type SkillHealEvent,
  type SkillBuffEvent,
  type SkillDebuffEvent,
  type SkillMissEvent,
  type EffectHandler,
};

// ── Execute Effect ─────────────────────────────────────────

/**
 * Execute the effect of a completed skill cast.
 * Looks up a custom handler first, then falls back to default.
 */
export function executeSkillEffect(
  caster: PlayerState,
  target: CombatTarget | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
  now: number,
  ctx?: SkillEffectContext,
): SkillEvent[] {
  const handler = getEffectHandler(skill.id);
  if (handler) {
    return handler(caster, target, skill, allPlayers, now, ctx);
  }
  return defaultEffect(caster, target, skill, allPlayers, now, ctx);
}

// ── Default Effect Handler ─────────────────────────────────

function defaultEffect(
  caster: PlayerState,
  target: CombatTarget | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
  now: number,
  ctx?: SkillEffectContext,
): SkillEvent[] {
  const events: SkillEvent[] = [];

  // ── Damage skills ──
  if (skill.baseDamage) {
    const targets = getAffectedTargets(caster, target, skill, allPlayers, ctx?.isPartyMember);
    for (const t of targets) {
      if (!t.alive) continue;
      const [min, max] = skill.baseDamage;
      const rawDamage = min + Math.random() * (max - min);

      const statValue = getStatValue(caster, skill.scalingStat);
      const scaledDamage = rawDamage + statValue * 0.8;

      const isCrit = Math.random() < (caster.stats?.critChance ?? 0.05);
      const critMult = isCrit ? 1 + (caster.stats?.critDamage ?? 0.5) : 1;
      const finalDamage = Math.round(scaledDamage * critMult);

      if (isNpcTarget(t) && ctx?.damageNpc) {
        const { xpReward } = ctx.damageNpc(t.id, finalDamage, caster.id, now);
        if (xpReward > 0) {
          ctx?.awardXP ? ctx.awardXP(caster.id, xpReward) : (caster.xp = (caster.xp ?? 0) + xpReward);
        }
      } else {
        t.hp = Math.max(0, t.hp - finalDamage);
        if (t.hp <= 0) {
          t.alive = false;
        }
      }

      events.push({ type: 'damage', targetId: t.id, damage: finalDamage, isCrit });

      // Apply DoT — only player targets have activeBuffs
      if (!isNpcTarget(t) && skill.dotDamagePerSec && skill.buffDurationMs) {
        applyBuff(t, {
          skillId: skill.id,
          casterId: caster.id,
          appliedAt: now,
          expiresAt: now + skill.buffDurationMs,
          dotDamagePerSec: skill.dotDamagePerSec,
          stacks: 1,
        });
        events.push({ type: 'debuff', targetId: t.id, skillId: skill.id, durationMs: skill.buffDurationMs });
      }
    }
  }

  // ── Healing skills (player targets only) ──
  if (skill.baseHealing) {
    if (skill.targetType === 'singleAlly' && (!target || isNpcTarget(target))) {
      return events;
    }
    const healTarget = (target && !isNpcTarget(target)) ? target : caster;
    if (healTarget.alive) {
      const [min, max] = skill.baseHealing;
      const rawHeal = min + Math.random() * (max - min);
      const statValue = getStatValue(caster, skill.scalingStat);
      const scaledHeal = rawHeal + statValue * 0.9;
      const finalHeal = Math.round(scaledHeal);

      healTarget.hp = Math.min(healTarget.maxHp, healTarget.hp + finalHeal);
      events.push({ type: 'heal', targetId: healTarget.id, amount: finalHeal });
    }

    if (skill.hotHealPerSec && skill.buffDurationMs) {
      const healTarget2 = (target && !isNpcTarget(target)) ? target : caster;
      applyBuff(healTarget2, {
        skillId: skill.id,
        casterId: caster.id,
        appliedAt: now,
        expiresAt: now + skill.buffDurationMs,
        hotHealPerSec: skill.hotHealPerSec,
        stacks: 1,
      });
      events.push({ type: 'buff', targetId: healTarget2.id, skillId: skill.id, durationMs: skill.buffDurationMs });
    }
  }

  // ── Buff/debuff-only skills (player targets only) ──
  if (!skill.baseDamage && !skill.baseHealing && skill.buffDurationMs) {
    const rawTarget = skill.category === SkillCategory.DEBUFF
      ? (target ?? caster)
      : (skill.targetType === 'self' ? caster : (target ?? caster));

    if (!isNpcTarget(rawTarget)) {
      applyBuff(rawTarget, {
        skillId: skill.id,
        casterId: caster.id,
        appliedAt: now,
        expiresAt: now + skill.buffDurationMs,
        stacks: 1,
      });
      const eventType = skill.category === SkillCategory.DEBUFF ? 'debuff' : 'buff';
      events.push({ type: eventType, targetId: rawTarget.id, skillId: skill.id, durationMs: skill.buffDurationMs } as SkillEvent);
    }
  }

  return events;
}

// ── Helpers ────────────────────────────────────────────────

/**
 * Sync server-side activeBuffs → the Colyseus-synced syncedBuffs ArraySchema
 * on an NPC. Call this any time activeBuffs changes.
 */
export function syncNpcBuffsToSchema(npc: NPCState): void {
  npc.syncedBuffs.clear();
  for (const b of npc.activeBuffs) {
    const info = new NpcBuffInfo();
    info.skillId = b.skillId;
    info.expiresAt = b.expiresAt;
    info.dotDamagePerSec = b.dotDamagePerSec ?? 0;
    npc.syncedBuffs.push(info);
  }
}

/**
 * Apply a buff/debuff to an NPC (mirrors applyBuff for players).
 * Uses "replace" stacking semantics by default — re-applying the same
 * skillId from the same caster refreshes the duration.
 * Updates syncedBuffs so clients see the change immediately.
 */
export function applyNpcBuff(npc: NPCState, buff: ActiveBuff): void {
  if (buff.stacks == null) buff.stacks = 1;

  // Remove any existing entry from the same caster+skill (replace semantics)
  npc.activeBuffs = npc.activeBuffs.filter(
    b => !(b.skillId === buff.skillId && b.casterId === buff.casterId),
  );
  npc.activeBuffs.push(buff);

  // Keep the synced schema in sync
  syncNpcBuffsToSchema(npc);
}

export function applyBuff(player: PlayerState, buff: ActiveBuff): void {
  if (buff.stacks == null) buff.stacks = 1;

  const skillTemplate = DataManager.instance.getSkill(buff.skillId);
  const mode = skillTemplate?.stackingMode ?? 'replace';

  const existing = player.activeBuffs.find(
    b => b.skillId === buff.skillId && b.casterId === buff.casterId,
  );

  if (existing) {
    switch (mode) {
      case 'stack': {
        const maxStacks = skillTemplate?.maxStacks ?? 1;
        existing.stacks = Math.min(existing.stacks + 1, maxStacks);
        existing.appliedAt = buff.appliedAt;
        existing.expiresAt = buff.expiresAt;
        if (buff.dotDamagePerSec != null) existing.dotDamagePerSec = buff.dotDamagePerSec;
        if (buff.hotHealPerSec != null) existing.hotHealPerSec = buff.hotHealPerSec;
        return;
      }
      case 'extend': {
        const remainingMs = Math.max(0, existing.expiresAt - buff.appliedAt);
        const extensionMs = buff.expiresAt - buff.appliedAt;
        existing.expiresAt = buff.appliedAt + remainingMs + extensionMs;
        return;
      }
      case 'replace':
      default:
        player.activeBuffs = player.activeBuffs.filter(
          b => !(b.skillId === buff.skillId && b.casterId === buff.casterId),
        );
        break;
    }
  }

  player.activeBuffs.push(buff);
}

function getAffectedTargets(
  caster: PlayerState,
  target: CombatTarget | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
  isPartyMember?: (a: string, b: string) => boolean,
): CombatTarget[] {
  switch (skill.targetType) {
    case 'singleEnemy':
      return target && target.id !== caster.id ? [target] : [];

    case 'singleAlly':
      return (target && !isNpcTarget(target) && target.id !== caster.id) ? [target] : [];

    case 'aoeSelf': {
      const targets: CombatTarget[] = [];
      allPlayers.forEach(p => {
        if (p.id === caster.id || !p.alive || p.zoneId !== caster.zoneId) return;
        if (isPartyMember?.(caster.id, p.id)) return; // no friendly fire
        const dx = p.x - caster.x;
        const dy = p.y - caster.y;
        if (dx * dx + dy * dy <= skill.range * skill.range) {
          targets.push(p);
        }
      });
      return targets;
    }

    case 'cone': {
      const targets: CombatTarget[] = [];
      const coneHalfAngle = Math.PI / 4;
      allPlayers.forEach(p => {
        if (p.id === caster.id || !p.alive || p.zoneId !== caster.zoneId) return;
        if (isPartyMember?.(caster.id, p.id)) return; // no friendly fire
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
      // Handled by projectile handlers or spawnProjectileFromSkill
      return [];

    default:
      return target ? [target] : [];
  }
}
