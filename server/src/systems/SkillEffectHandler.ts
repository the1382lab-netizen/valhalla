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
  FIREBALL_PROJECTILE_SPEED,
  FIREBALL_AOE_RADIUS,
  FIREBALL_PROJECTILE_RADIUS,
  PLAYER_COLLISION_RADIUS,
} from '@valhalla/shared';
import { PlayerState, ActiveBuff } from '../schema/PlayerState.js';
import { NPCState } from '../schema/NPCState.js';
import { SpellProjectileState } from '../schema/SpellProjectileState.js';
import { MapSchema } from '@colyseus/schema';
import type { PlayerMap, NPCMap } from './SkillSystem.js';

// ── Combat Target Union ─────────────────────────────────────

/** A valid skill target — either a player or an NPC. */
export type CombatTarget = PlayerState | NPCState;

/** Returns true when the target is an NPCState (has templateId, which PlayerState lacks). */
export function isNpcTarget(t: CombatTarget): t is NPCState {
  return 'templateId' in t;
}

// ── Effect Context ─────────────────────────────────────────

/**
 * Optional context passed from SkillSystem to effect handlers.
 * Allows handlers to spawn spell projectiles or access ground target.
 */
export interface SkillEffectContext {
  /** Room's spell projectile map — handlers can add new projectiles here */
  spellProjectiles?: MapSchema<SpellProjectileState>;
  /** Ground target X for AOE_GROUND skills */
  groundX: number | null;
  /** Ground target Y for AOE_GROUND skills */
  groundY: number | null;
  /** NPC map — available for effect handlers that need NPC access */
  allNPCs?: NPCMap;
  /**
   * Delegate to apply damage to an NPC via NPCSystem.
   * Using this instead of mutating target.hp directly ensures aggro is triggered.
   */
  damageNpc?: (npcId: string, damage: number, attackerId: string, now: number) => { died: boolean; xpReward: number };
}

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
  target: CombatTarget | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
  now: number,
  ctx?: SkillEffectContext,
) => SkillEvent[];

// ── Handler Registry ───────────────────────────────────────

/**
 * Custom per-skill handlers. Register specific skill logic here.
 * If a skill has no registered handler, defaultEffect() is used.
 */
const EFFECT_HANDLERS: Partial<Record<SkillId, EffectHandler>> = {};

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
  target: CombatTarget | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
  now: number,
  ctx?: SkillEffectContext,
): SkillEvent[] {
  const handler = EFFECT_HANDLERS[skill.id];
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
  _ctx?: SkillEffectContext,
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

      // Apply DoT — only player targets have activeBuffs
      if (!isNpcTarget(t) && skill.dotDamagePerSec && skill.buffDurationMs) {
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

  // ── Healing skills (player targets only) ──
  if (skill.baseHealing) {
    // Healing only applies to players — NPCs don't receive heals from skills
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

    // Apply HoT if present (players only)
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

  // ── Buff/debuff-only skills (player targets only) ──
  if (!skill.baseDamage && !skill.baseHealing && skill.buffDurationMs) {
    const rawTarget = skill.category === SkillCategory.DEBUFF
      ? (target ?? caster)
      : (skill.targetType === 'self' ? caster : (target ?? caster));

    // Only apply buffs/debuffs to players — NPCs don't have activeBuffs
    if (!isNpcTarget(rawTarget)) {
      applyBuff(rawTarget, {
        skillId: skill.id,
        casterId: caster.id,
        appliedAt: now,
        expiresAt: now + skill.buffDurationMs,
      });
      const eventType = skill.category === SkillCategory.DEBUFF ? 'debuff' : 'buff';
      events.push({ type: eventType, targetId: rawTarget.id, skillId: skill.id, durationMs: skill.buffDurationMs } as SkillEvent);
    }
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
 * Determine which entities are affected by a skill based on target type.
 * Returns a union of PlayerState | NPCState for single-target skills,
 * and PlayerState[] for AoE skills (NPCs are not yet scanned for AoE — that lives in SpellProjectileSystem).
 */
function getAffectedTargets(
  caster: PlayerState,
  target: CombatTarget | null,
  skill: SkillTemplate,
  allPlayers: PlayerMap,
): CombatTarget[] {
  switch (skill.targetType) {
    case 'singleEnemy':
      // Can be any non-self entity — includes NPCs
      return target && target.id !== caster.id ? [target] : [];

    case 'singleAlly':
      // Allies are other players only — NPCs are not ally targets
      return (target && !isNpcTarget(target) && target.id !== caster.id) ? [target] : [];

    case 'aoeSelf': {
      // All enemies within skill.range of caster (players only for now)
      const targets: CombatTarget[] = [];
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
      // Enemies in a cone in front of caster (players only for now)
      const targets: CombatTarget[] = [];
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
      // Ground-targeted AoE — handled by custom per-skill handlers that spawn spell projectiles.
      // The default effect handler doesn't apply damage here; see e.g. fireballHandler below.
      return [];

    default:
      return target ? [target] : [];
  }
}

// ── Fireball Handler ───────────────────────────────────────

let _nextSpellProjId = 0;

/**
 * Fireball effect handler.
 *
 * Instead of dealing damage immediately, this spawns a SpellProjectileState
 * that travels toward the ground target. The SpellProjectileSystem handles
 * movement, collision, and AoE detonation each tick.
 */
function fireballHandler(
  caster: PlayerState,
  _target: CombatTarget | null,
  skill: SkillTemplate,
  _allPlayers: PlayerMap,
  _now: number,
  ctx?: SkillEffectContext,
): SkillEvent[] {
  if (!ctx?.spellProjectiles || ctx.groundX == null || ctx.groundY == null) {
    // Fallback: no projectile map or no target position — skip silently
    return [];
  }

  // Roll damage (will be applied at detonation)
  const [min, max] = skill.baseDamage!;
  const rawDamage = min + Math.random() * (max - min);
  const intel = getStatValue(caster, skill.scalingStat);
  const scaledDamage = rawDamage + intel * 0.8;

  // Spawn offset: start the fireball just ahead of the caster
  const spawnOffset = PLAYER_COLLISION_RADIUS + FIREBALL_PROJECTILE_RADIUS + 2;
  const angle = Math.atan2(ctx.groundY - caster.y, ctx.groundX - caster.x);

  const proj = new SpellProjectileState();
  proj.id = `spell_${_nextSpellProjId++}`;
  proj.ownerId = caster.id;
  proj.skillId = skill.id;
  proj.x = caster.x + Math.cos(angle) * spawnOffset;
  proj.y = caster.y + Math.sin(angle) * spawnOffset;
  proj.targetX = ctx.groundX;
  proj.targetY = ctx.groundY;
  proj.speed = FIREBALL_PROJECTILE_SPEED;

  // Server-only payload for detonation
  proj._damage = scaledDamage;
  proj._aoeRadius = skill.aoeRadius ?? FIREBALL_AOE_RADIUS;
  proj._critChance = caster.stats?.critChance ?? 0.05;
  proj._critDamage = caster.stats?.critDamage ?? 0.5;
  proj._attackerDex = caster.stats?.dexterity ?? 10;
  proj._distanceTravelled = 0;
  proj._zoneId = caster.zoneId ?? '';

  ctx.spellProjectiles.set(proj.id, proj);

  // Return no immediate skill events — damage fires on detonation
  return [];
}

// Register the Fireball handler
registerEffectHandler(SkillId.WIZARD_FIREBALL, fireballHandler);

// ── Magic Missile Handler ────────────────────────────────────

/**
 * Magic Missile effect handler.
 *
 * Deals instant arcane damage to the selected single enemy target.
 * Unlike default attacks this spell ALWAYS hits — no dodge or miss roll is
 * applied. Critical strikes still occur normally.
 */
function magicMissileHandler(
  caster: PlayerState,
  target: CombatTarget | null,
  skill: SkillTemplate,
  _allPlayers: PlayerMap,
  now: number,
  ctx?: SkillEffectContext,
): SkillEvent[] {
  if (!target || !target.alive || target.id === caster.id) return [];

  const [min, max] = skill.baseDamage!;
  const rawDamage = min + Math.random() * (max - min);
  const intel = getStatValue(caster, skill.scalingStat);
  const scaledDamage = rawDamage + intel * 0.8;

  // Guaranteed hit — no dodge/miss check.
  // Critical strike still applies.
  const isCrit = Math.random() < (caster.stats?.critChance ?? 0.05);
  const critMult = isCrit ? 1 + (caster.stats?.critDamage ?? 0.5) : 1;
  const finalDamage = Math.round(scaledDamage * critMult);

  // Route NPC damage through NPCSystem so aggro is triggered correctly.
  // Fall back to direct mutation for player targets (PvP) where no delegate is needed.
  if (isNpcTarget(target) && ctx?.damageNpc) {
    ctx.damageNpc(target.id, finalDamage, caster.id, now);
  } else {
    target.hp = Math.max(0, target.hp - finalDamage);
    if (target.hp <= 0) {
      target.alive = false;
    }
  }

  return [{ type: 'damage', targetId: target.id, damage: finalDamage, isCrit }];
}

// Register the Magic Missile handler
registerEffectHandler(SkillId.WIZARD_MAGIC_MISSILE, magicMissileHandler);
