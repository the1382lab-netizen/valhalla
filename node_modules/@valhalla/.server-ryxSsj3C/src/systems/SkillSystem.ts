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
import { executeSkillEffect, SkillEvent } from './SkillEffectHandler.js';
import { DataManager } from './DataManager.js';

/** Duck-typed map interface compatible with both Map and Colyseus MapSchema. */
export interface PlayerMap {
  get(key: string): PlayerState | undefined;
  forEach(callback: (value: PlayerState, key: string) => void): void;
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
}

// ── SkillSystem ────────────────────────────────────────────

export class SkillSystem {
  private activeCasts: Map<string, ActiveCast> = new Map();
  private dotTickTracker: Map<string, number> = new Map(); // "buffKey" → last tick time

  /**
   * Attempt to start casting a skill.
   * Validates all preconditions. Returns result events.
   */
  tryStartCast(
    caster: PlayerState,
    skillId: string,
    targetId: string | null,
    allPlayers: PlayerMap,
    now: number,
  ): SkillSystemEvent[] {
    const skill = DataManager.instance.getSkill(skillId);
    if (!skill) {
      return [{ type: 'castFailed', casterId: caster.id, reason: 'Unknown skill' }];
    }

    // ── Validation pipeline ──
    const failReason = this.validateCast(caster, skill, targetId, allPlayers, now);
    if (failReason) {
      return [{ type: 'castFailed', casterId: caster.id, reason: failReason }];
    }

    // Resolve target
    const target = targetId ? allPlayers.get(targetId) ?? null : null;

    if (skill.castTimeMs === 0) {
      // ── Instant cast ──
      return this.executeInstantCast(caster, target, skill, allPlayers, now);
    } else {
      // ── Start channeled/cast-time cast ──
      return this.startTimedCast(caster, skill, targetId, now);
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
   */
  update(
    allPlayers: PlayerMap,
    dt: number,
    now: number,
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
          const target = cast.targetId ? allPlayers.get(cast.targetId) ?? null : null;
          const castEvents = this.completeCast(player, target, skill, allPlayers, now);
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

    // Range check (for targeted skills)
    if (targetId && skill.range > 0) {
      const target = allPlayers.get(targetId);
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

  private executeInstantCast(
    caster: PlayerState,
    target: PlayerState | null,
    skill: SkillTemplate,
    allPlayers: PlayerMap,
    now: number,
  ): SkillSystemEvent[] {
    // Deduct resource
    this.deductResource(caster, skill);

    // Start cooldown
    if (skill.cooldownMs > 0) {
      caster.skillCooldowns.set(skill.id, now + skill.cooldownMs);
    }

    // Execute effect
    const skillEvents = executeSkillEffect(caster, target, skill, allPlayers, now);

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
  ): SkillSystemEvent[] {
    // Store active cast
    this.activeCasts.set(caster.id, {
      playerId: caster.id,
      skillId: skill.id as SkillId,
      targetId,
      startedAt: now,
      durationMs: skill.castTimeMs,
      startX: caster.x,
      startY: caster.y,
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
    target: PlayerState | null,
    skill: SkillTemplate,
    allPlayers: PlayerMap,
    now: number,
  ): SkillSystemEvent[] {
    // Deduct resource
    this.deductResource(caster, skill);

    // Start cooldown
    if (skill.cooldownMs > 0) {
      caster.skillCooldowns.set(skill.id, now + skill.cooldownMs);
    }

    // Execute effect
    const skillEvents = executeSkillEffect(caster, target, skill, allPlayers, now);

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
