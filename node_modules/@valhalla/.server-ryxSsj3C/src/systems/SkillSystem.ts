/**
 * Server-authoritative skill/spell casting system.
 *
 * Handles: cast validation, cast progression, interrupts,
 * cooldown management, energy regen, and buff/DoT/HoT ticking.
 *
 * Client sends intent → server validates → server executes.
 */

import {
  SkillId,
  SkillTemplate,
  ResourceType,
  ClassId,
} from '@valhalla/shared';
import { PlayerState } from '../schema/PlayerState.js';
import { NPCState } from '../schema/NPCState.js';
import { executeSkillEffect, SkillEvent, SkillEffectContext } from './SkillEffectHandler.js';
import { DataManager } from './DataManager.js';
import { SpellProjectileState } from '../schema/SpellProjectileState.js';
import { MapSchema } from '@colyseus/schema';

/** Duck-typed map interface compatible with both Map and Colyseus MapSchema. */
export interface PlayerMap {
  get(key: string): PlayerState | undefined;
  forEach(callback: (value: PlayerState, key: string) => void): void;
}

/** Duck-typed NPC map interface compatible with Colyseus MapSchema<NPCState>. */
export interface NPCMap {
  get(key: string): NPCState | undefined;
  forEach(callback: (value: NPCState, key: string) => void): void;
}

/**
 * Minimal interface for NPC combat operations via NPCSystem.
 * Keeps SkillSystem decoupled from the full NPCSystem class.
 */
export interface NPCCombatDelegate {
  damageNPC(npcId: string, damage: number, attackerId: string, now: number): { died: boolean; xpReward: number };
  tauntNpc(npcId: string, playerId: string, bonusThreat: number): void;
}

// ── Result types for communicating back to GameRoom ────────

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

export type SkillSystemEvent =
  | CastStartedResult
  | CastCompleteResult
  | CastFailedResult
  | CastInterruptedResult
  | BuffExpiredResult
  | DotTickResult
  | HotTickResult;

// ── Tracking cast-in-progress (server-only) ────────────────

interface ActiveCast {
  playerId: string;
  skillId: SkillId;
  targetId: string | null;
  startedAt: number;
  durationMs: number;
  /** Player position when cast started — for movement interrupt detection */
  startX: number;
  startY: number;
  /** Ground target position for AOE_GROUND skills (e.g. Fireball) */
  groundX: number | null;
  groundY: number | null;
}

// ── SkillSystem ────────────────────────────────────────────

export class SkillSystem {
  private activeCasts: Map<string, ActiveCast> = new Map();
  private dotTickTracker: Map<string, number> = new Map(); // "buffKey" → last tick time

  /**
   * Attempt to start casting a skill.
   * Validates all preconditions. Returns result events.
   *
   * @param groundX  World X of the ground target — required for AOE_GROUND skills
   * @param groundY  World Y of the ground target — required for AOE_GROUND skills
   * @param allNPCs  NPC map — enables single-target skills to target NPCs
   */
  tryStartCast(
    caster: PlayerState,
    skillId: string,
    targetId: string | null,
    allPlayers: PlayerMap,
    now: number,
    spellProjectiles?: MapSchema<SpellProjectileState>,
    groundX?: number | null,
    groundY?: number | null,
    allNPCs?: NPCMap,
    npcDelegate?: NPCCombatDelegate,
    awardXPDelegate?: (playerId: string, amount: number) => void,
  ): SkillSystemEvent[] {
    const skill = DataManager.instance.getSkill(skillId);
    if (!skill) {
      return [{ type: 'castFailed', casterId: caster.id, reason: 'Unknown skill' }];
    }

    // ── Validation pipeline ──
    const failReason = this.validateCast(caster, skill, targetId, allPlayers, now, groundX, groundY, allNPCs);
    if (failReason) {
      return [{ type: 'castFailed', casterId: caster.id, reason: failReason }];
    }

    // Resolve target (checks players first, then NPCs)
    const target = this.resolveTarget(targetId, allPlayers, allNPCs);

    const ctx: SkillEffectContext = {
      spellProjectiles,
      groundX: groundX ?? null,
      groundY: groundY ?? null,
      allNPCs,
      damageNpc: npcDelegate ? (id, dmg, attId, t) => npcDelegate.damageNPC(id, dmg, attId, t) : undefined,
      tauntNpc: npcDelegate ? (id, pId, bonus) => npcDelegate.tauntNpc(id, pId, bonus) : undefined,
      awardXP: awardXPDelegate ?? undefined,
    };

    if (skill.castTimeMs === 0) {
      // ── Instant cast ──
      return this.executeInstantCast(caster, target, skill, allPlayers, now, ctx);
    } else {
      // ── Start channeled/cast-time cast ──
      return this.startTimedCast(caster, skill, targetId, now, groundX ?? null, groundY ?? null);
    }
  }

  /**
   * Cancel an active cast (manual cancel or movement interrupt).
   */
  cancelCast(player: PlayerState, now: number): SkillSystemEvent[] {
    const cast = this.activeCasts.get(player.id);
    if (!cast) return [];

    this.activeCasts.delete(player.id);
    player.castingSkillId = '';
    player.castingStartedAt = 0;
    player.castingDurationMs = 0;

    return [{ type: 'castInterrupted', casterId: player.id, skillId: cast.skillId }];
  }

  /**
   * Main update tick. Call every server frame.
   * - Completes casts that have finished their cast time
   * - Ticks energy regen for non-caster classes
   * - Ticks DoTs and HoTs
   * - Expires finished buffs
   * - Checks for movement interrupts
   *
   * @param spellProjectiles  Room's spell projectile map — passed to effect handlers that spawn projectiles
   * @param allNPCs           NPC map — allows timed skill completions to resolve NPC targets
   */
  update(
    allPlayers: PlayerMap,
    dt: number,
    now: number,
    spellProjectiles?: MapSchema<SpellProjectileState>,
    allNPCs?: NPCMap,
    npcDelegate?: NPCCombatDelegate,
    awardXPDelegate?: (playerId: string, amount: number) => void,
  ): SkillSystemEvent[] {
    const events: SkillSystemEvent[] = [];

    // ── Complete casts ──
    for (const [playerId, cast] of this.activeCasts) {
      const player = allPlayers.get(playerId);
      if (!player || !player.alive) {
        this.activeCasts.delete(playerId);
        player && this.clearCastingState(player);
        continue;
      }

      // Check movement interrupt
      const dx = player.x - cast.startX;
      const dy = player.y - cast.startY;
      if (dx * dx + dy * dy > 4) { // > 2px movement threshold
        this.activeCasts.delete(playerId);
        this.clearCastingState(player);
        events.push({ type: 'castInterrupted', casterId: playerId, skillId: cast.skillId });
        continue;
      }

      // Check if cast completed
      if (now >= cast.startedAt + cast.durationMs) {
        this.activeCasts.delete(playerId);
        this.clearCastingState(player);

        const skill = DataManager.instance.getSkill(cast.skillId);
        if (skill) {
          const target = this.resolveTarget(cast.targetId, allPlayers, allNPCs);

          // Re-validate single-target skills at fire time: the target may have
          // died, disconnected, or been invalidated during the cast window.
          if (skill.targetType === 'singleEnemy' || skill.targetType === 'singleAlly') {
            if (!target || !target.alive) {
              events.push({ type: 'castFailed', casterId: player.id, reason: 'Target is no longer valid' });
              continue;
            }
            if (skill.targetType === 'singleAlly' && cast.targetId && !allPlayers.get(cast.targetId)) {
              events.push({ type: 'castFailed', casterId: player.id, reason: 'Invalid target' });
              continue;
            }
            if (skill.targetType === 'singleEnemy' && cast.targetId && allPlayers.get(cast.targetId)) {
              events.push({ type: 'castFailed', casterId: player.id, reason: 'Invalid target' });
              continue;
            }
          }

          const ctx: SkillEffectContext = {
            spellProjectiles,
            groundX: cast.groundX,
            groundY: cast.groundY,
            allNPCs,
            damageNpc: npcDelegate ? (id, dmg, attId, t) => npcDelegate.damageNPC(id, dmg, attId, t) : undefined,
            tauntNpc: npcDelegate ? (id, pId, bonus) => npcDelegate.tauntNpc(id, pId, bonus) : undefined,
            awardXP: awardXPDelegate ?? undefined,
          };
          const castEvents = this.completeCast(player, target, skill, allPlayers, now, ctx);
          events.push(...castEvents);
        }
      }
    }

    // ── Energy regen + buff ticking ──
    allPlayers.forEach(player => {
      if (!player.alive) return;

      // Energy regen for non-casters
      if (player.stats && player.maxEnergy > 0) {
        const regenAmount = player.stats.energyRegenRate * dt;
        player.energy = Math.min(player.maxEnergy, player.energy + regenAmount);
      }

      // Mana regen for casters (scales with Intelligence)
      if (player.stats && player.maxMana > 0 && player.stats.manaRegenRate > 0) {
        const regenAmount = player.stats.manaRegenRate * dt;
        player.mana = Math.min(player.maxMana, player.mana + regenAmount);
      }

      // Tick buffs (DoTs, HoTs, expirations)
      const buffEvents = this.tickBuffs(player, dt, now);
      events.push(...buffEvents);
    });

    return events;
  }

  // ── Private Methods ──────────────────────────────────────

  private validateCast(
    caster: PlayerState,
    skill: SkillTemplate,
    targetId: string | null,
    allPlayers: PlayerMap,
    now: number,
    groundX?: number | null,
    groundY?: number | null,
    allNPCs?: NPCMap,
  ): string | null {
    // Alive check
    if (!caster.alive) return 'You are dead';

    // Class check
    const classSkills = DataManager.instance.getClassSkills(caster.classId);
    if (!classSkills || !classSkills.includes(skill.id)) {
      return 'Your class cannot use this skill';
    }

    // Level check
    if (caster.level < skill.levelRequired) {
      return `Requires level ${skill.levelRequired}`;
    }

    // Already casting check
    if (this.activeCasts.has(caster.id)) {
      return 'Already casting';
    }

    // Cooldown check
    const cdExpiry = caster.skillCooldowns.get(skill.id);
    if (cdExpiry && now < cdExpiry) {
      const remaining = Math.ceil((cdExpiry - now) / 1000);
      return `On cooldown (${remaining}s)`;
    }

    // Resource check
    if (skill.resourceType === ResourceType.MANA) {
      if (caster.mana < skill.resourceCost) return 'Not enough mana';
    } else if (skill.resourceType === ResourceType.ENERGY) {
      if (caster.energy < skill.resourceCost) return 'Not enough energy';
    }

    // Range check for ground-targeted AoE skills
    if (skill.targetType === 'aoeGround') {
      if (groundX == null || groundY == null) {
        return 'No target position provided';
      }
      if (skill.range > 0) {
        const dx = groundX - caster.x;
        const dy = groundY - caster.y;
        if (dx * dx + dy * dy > skill.range * skill.range) {
          return 'Out of range';
        }
      }
    }

    // Target requirement check for single-target skills
    if (skill.targetType === 'singleEnemy' || skill.targetType === 'singleAlly') {
      if (!targetId) return 'No target selected';
      const target = this.resolveTarget(targetId, allPlayers, allNPCs);
      if (!target) return 'Invalid target';
      if (!target.alive) return 'Target is dead';
      // singleAlly must resolve to a player (not an NPC)
      if (skill.targetType === 'singleAlly' && !allPlayers.get(targetId)) {
        return 'Invalid target';
      }
      // singleEnemy must resolve to an NPC (not a player)
      if (skill.targetType === 'singleEnemy' && allPlayers.get(targetId)) {
        return 'Invalid target';
      }
    }

    // Range check (for single-target skills)
    if (targetId && skill.range > 0 && skill.targetType !== 'aoeGround') {
      const target = this.resolveTarget(targetId, allPlayers, allNPCs);
      if (target) {
        const dx = target.x - caster.x;
        const dy = target.y - caster.y;
        if (dx * dx + dy * dy > skill.range * skill.range) {
          return 'Out of range';
        }
        // Zone check
        if (target.zoneId !== caster.zoneId) {
          return 'Target is in a different zone';
        }
      }
    }

    return null; // All checks passed
  }

  /**
   * Resolve a target ID against the player map and (optionally) the NPC map.
   * Returns the first matching entity, or null if not found.
   */
  private resolveTarget(
    targetId: string | null,
    allPlayers: PlayerMap,
    allNPCs?: NPCMap,
  ): PlayerState | NPCState | null {
    if (!targetId) return null;
    const player = allPlayers.get(targetId);
    if (player) return player;
    const npc = allNPCs?.get(targetId);
    return npc ?? null;
  }

  private executeInstantCast(
    caster: PlayerState,
    target: PlayerState | NPCState | null,
    skill: SkillTemplate,
    allPlayers: PlayerMap,
    now: number,
    ctx?: SkillEffectContext,
  ): SkillSystemEvent[] {
    // Deduct resource
    this.deductResource(caster, skill);

    // Start cooldown
    if (skill.cooldownMs > 0) {
      caster.skillCooldowns.set(skill.id, now + skill.cooldownMs);
    }

    // Execute effect
    const skillEvents = executeSkillEffect(caster, target, skill, allPlayers, now, ctx);

    return [
      { type: 'castStarted', casterId: caster.id, skillId: skill.id, castTimeMs: 0 },
      { type: 'castComplete', casterId: caster.id, skillId: skill.id, events: skillEvents },
    ];
  }

  private startTimedCast(
    caster: PlayerState,
    skill: SkillTemplate,
    targetId: string | null,
    now: number,
    groundX: number | null = null,
    groundY: number | null = null,
  ): SkillSystemEvent[] {
    // Store active cast (including ground target for AOE_GROUND spells)
    this.activeCasts.set(caster.id, {
      playerId: caster.id,
      skillId: skill.id as SkillId,
      targetId,
      startedAt: now,
      durationMs: skill.castTimeMs,
      startX: caster.x,
      startY: caster.y,
      groundX,
      groundY,
    });

    // Set synced casting state (drives client cast bar)
    caster.castingSkillId = skill.id;
    caster.castingStartedAt = now;
    caster.castingDurationMs = skill.castTimeMs;

    return [
      { type: 'castStarted', casterId: caster.id, skillId: skill.id, castTimeMs: skill.castTimeMs },
    ];
  }

  private completeCast(
    caster: PlayerState,
    target: PlayerState | NPCState | null,
    skill: SkillTemplate,
    allPlayers: PlayerMap,
    now: number,
    ctx?: SkillEffectContext,
  ): SkillSystemEvent[] {
    // Deduct resource
    this.deductResource(caster, skill);

    // Start cooldown
    if (skill.cooldownMs > 0) {
      caster.skillCooldowns.set(skill.id, now + skill.cooldownMs);
    }

    // Execute effect
    const skillEvents = executeSkillEffect(caster, target, skill, allPlayers, now, ctx);

    return [
      { type: 'castComplete', casterId: caster.id, skillId: skill.id, events: skillEvents },
    ];
  }

  private deductResource(caster: PlayerState, skill: SkillTemplate): void {
    if (skill.resourceType === ResourceType.MANA) {
      caster.mana = Math.max(0, caster.mana - skill.resourceCost);
    } else if (skill.resourceType === ResourceType.ENERGY) {
      caster.energy = Math.max(0, caster.energy - skill.resourceCost);
    }
  }

  private clearCastingState(player: PlayerState): void {
    player.castingSkillId = '';
    player.castingStartedAt = 0;
    player.castingDurationMs = 0;
  }

  private tickBuffs(player: PlayerState, dt: number, now: number): SkillSystemEvent[] {
    const events: SkillSystemEvent[] = [];
    const remaining: typeof player.activeBuffs = [];

    for (const buff of player.activeBuffs) {
      // Check expiration
      if (now >= buff.expiresAt) {
        // Clear shield when Shield of Faith expires naturally
        if (buff.skillId === SkillId.CLERIC_SHIELD_OF_FAITH) {
          player.shieldHp = 0;
        }
        events.push({ type: 'buffExpired', targetId: player.id, skillId: buff.skillId });
        continue;
      }

      // Tick DoT
      if (buff.dotDamagePerSec && buff.dotDamagePerSec > 0) {
        const tickKey = `${player.id}:${buff.skillId}:${buff.casterId}`;
        const lastTick = this.dotTickTracker.get(tickKey) ?? buff.appliedAt;
        if (now - lastTick >= 1000) { // Tick once per second
          const damage = Math.round(buff.dotDamagePerSec);
          player.hp = Math.max(0, player.hp - damage);
          if (player.hp <= 0) player.alive = false;
          this.dotTickTracker.set(tickKey, now);
          events.push({ type: 'dotTick', targetId: player.id, skillId: buff.skillId, damage });
        }
      }

      // Tick HoT
      if (buff.hotHealPerSec && buff.hotHealPerSec > 0) {
        const tickKey = `${player.id}:hot:${buff.skillId}:${buff.casterId}`;
        const lastTick = this.dotTickTracker.get(tickKey) ?? buff.appliedAt;
        if (now - lastTick >= 1000) {
          const heal = Math.round(buff.hotHealPerSec);
          player.hp = Math.min(player.maxHp, player.hp + heal);
          this.dotTickTracker.set(tickKey, now);
          events.push({ type: 'hotTick', targetId: player.id, skillId: buff.skillId, heal });
        }
      }

      remaining.push(buff);
    }

    player.activeBuffs = remaining;
    return events;
  }
}
