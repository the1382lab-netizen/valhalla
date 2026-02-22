/**
 * SpellProjectileSystem
 *
 * Updates all active spell projectiles each server tick:
 *   - Moves each projectile toward its ground target
 *   - Detonates on first entity hit (player or NPC), wall collision,
 *     or on reaching the target position
 *   - Applies AoE damage with linear distance-based falloff to all
 *     entities within the blast radius at the detonation point
 */

import { MapSchema } from '@colyseus/schema';
import { PlayerState, applyShieldAbsorption } from '../schema/PlayerState.js';
import { SpellProjectileState } from '../schema/SpellProjectileState.js';
import { NPCState } from '../schema/NPCState.js';
import { CollisionSystem } from './CollisionSystem.js';
import type { NPCSystem } from './NPCSystem.js';
import {
  FIREBALL_PROJECTILE_RADIUS,
  FIREBALL_DAMAGE_FALLOFF_MIN,
  PLAYER_COLLISION_RADIUS,
  INVULNERABILITY_MS,
  RESPAWN_TIME_MS,
  applyDefenseReduction,
  rollHit,
  rollCrit,
  rollDodge,
  rollBlock,
} from '@valhalla/shared';

export interface SpellProjectileEvent {
  type:
    | 'playerHit'
    | 'playerDied'
    | 'npcHit'
    | 'npcDied'
    | 'spellImpact';
  data: any;
}

/** Maximum distance a spell projectile can travel before force-detonating. */
const SPELL_PROJECTILE_MAX_RANGE = 1600;

export class SpellProjectileSystem {
  private collision: CollisionSystem;

  constructor(collision: CollisionSystem) {
    this.collision = collision;
  }

  /**
   * Advance all spell projectiles and resolve detonations.
   * Called once per server tick from GameRoom.update().
   *
   * @returns IDs to remove from the state map + any combat/VFX events
   */
  update(
    spellProjectiles: MapSchema<SpellProjectileState>,
    players: MapSchema<PlayerState>,
    npcs: MapSchema<NPCState>,
    npcSystem: NPCSystem,
    dt: number,
    now: number,
    isPartyMember?: (playerIdA: string, playerIdB: string) => boolean,
  ): { toRemove: string[]; events: SpellProjectileEvent[] } {
    const toRemove: string[] = [];
    const events: SpellProjectileEvent[] = [];

    spellProjectiles.forEach((proj, projId) => {
      // ── Movement ──────────────────────────────────────────
      const dx = proj.targetX - proj.x;
      const dy = proj.targetY - proj.y;
      const distToTarget = Math.sqrt(dx * dx + dy * dy);
      const stepDist = proj.speed * dt;

      // Have we reached (or overshot) the target this tick?
      if (distToTarget <= stepDist) {
        // Snap to target and detonate
        proj.x = proj.targetX;
        proj.y = proj.targetY;
        toRemove.push(projId);
        const detonationEvents = this.detonate(proj, proj.x, proj.y, players, npcs, npcSystem, now, isPartyMember);
        events.push(...detonationEvents);
        return;
      }

      // Move toward target
      const nx = dx / distToTarget; // normalised direction
      const ny = dy / distToTarget;
      proj.x += nx * stepDist;
      proj.y += ny * stepDist;
      proj._distanceTravelled += stepDist;

      // ── Safety max-range check ────────────────────────────
      if (proj._distanceTravelled >= SPELL_PROJECTILE_MAX_RANGE) {
        toRemove.push(projId);
        const detonationEvents = this.detonate(proj, proj.x, proj.y, players, npcs, npcSystem, now, isPartyMember);
        events.push(...detonationEvents);
        return;
      }

      // ── Wall collision ────────────────────────────────────
      if (this.collision.isCircleBlocked(proj.x, proj.y, FIREBALL_PROJECTILE_RADIUS)) {
        toRemove.push(projId);
        const detonationEvents = this.detonate(proj, proj.x, proj.y, players, npcs, npcSystem, now, isPartyMember);
        events.push(...detonationEvents);
        return;
      }

      // ── Player collision ──────────────────────────────────
      let hitEntity = false;
      players.forEach((player) => {
        if (hitEntity) return;
        if (player.id === proj.ownerId) return; // can't hit yourself
        if (!player.alive) return;
        if (player.zoneId !== proj._zoneId) return;
        if (isPartyMember?.(proj.ownerId, player.id)) return; // no friendly fire

        const edx = player.x - proj.x;
        const edy = player.y - proj.y;
        const hitDist = PLAYER_COLLISION_RADIUS + FIREBALL_PROJECTILE_RADIUS;
        if (edx * edx + edy * edy < hitDist * hitDist) {
          hitEntity = true;
          toRemove.push(projId);
          const detonationEvents = this.detonate(proj, proj.x, proj.y, players, npcs, npcSystem, now, isPartyMember);
          events.push(...detonationEvents);
        }
      });

      // ── NPC collision ─────────────────────────────────────
      if (!hitEntity) {
        npcs.forEach((npc) => {
          if (hitEntity) return;
          if (!npc.alive) return;
          if (npc.zoneId !== proj._zoneId) return;

          const edx = npc.x - proj.x;
          const edy = npc.y - proj.y;
          const hitDist = PLAYER_COLLISION_RADIUS + FIREBALL_PROJECTILE_RADIUS;
          if (edx * edx + edy * edy < hitDist * hitDist) {
            hitEntity = true;
            toRemove.push(projId);
            const detonationEvents = this.detonate(proj, proj.x, proj.y, players, npcs, npcSystem, now, isPartyMember);
            events.push(...detonationEvents);
          }
        });
      }
    });

    return { toRemove, events };
  }

  // ── Detonation ─────────────────────────────────────────────

  /**
   * Explode the projectile at (detonateX, detonateY).
   *
   * Finds all entities within aoeRadius, applies distance-based damage
   * falloff, and emits a SPELL_IMPACT VFX event.
   */
  private detonate(
    proj: SpellProjectileState,
    detonateX: number,
    detonateY: number,
    players: MapSchema<PlayerState>,
    npcs: MapSchema<NPCState>,
    npcSystem: NPCSystem,
    now: number,
    isPartyMember?: (playerIdA: string, playerIdB: string) => boolean,
  ): SpellProjectileEvent[] {
    const events: SpellProjectileEvent[] = [];
    const radius = proj._aoeRadius;

    // ── Damage players in radius ──────────────────────────
    players.forEach((player) => {
      if (player.id === proj.ownerId) return; // caster is immune
      if (!player.alive) return;
      if (player.zoneId !== proj._zoneId) return;
      if (now < player.invulnerableUntil) return;
      if (isPartyMember?.(proj.ownerId, player.id)) return; // no friendly fire

      const dx = player.x - detonateX;
      const dy = player.y - detonateY;
      const dist = Math.sqrt(dx * dx + dy * dy);

      if (dist > radius) return;

      const hitEvents = this.applyAoeDamage(
        player,
        proj._damage,
        dist,
        radius,
        proj._attackerDex,
        proj._critChance,
        proj._critDamage,
        proj.ownerId,
        now,
      );
      events.push(...hitEvents);
    });

    // ── Damage NPCs in radius ─────────────────────────────
    npcs.forEach((npc, npcId) => {
      if (!npc.alive) return;
      if (npc.zoneId !== proj._zoneId) return;

      const dx = npc.x - detonateX;
      const dy = npc.y - detonateY;
      const dist = Math.sqrt(dx * dx + dy * dy);

      if (dist > radius) return;

      const falloff = this.computeFalloff(dist, radius);
      const damage = Math.max(1, Math.floor(proj._damage * falloff));

      const { died, xpReward } = npcSystem.damageNPC(npcId, damage, proj.ownerId, now);

      events.push({
        type: 'npcHit',
        data: {
          targetId: npcId,
          attackerId: proj.ownerId,
          damage,
          remainingHp: npc.hp,
          isCrit: false,
          blocked: false,
        },
      });

      if (died) {
        events.push({
          type: 'npcDied',
          data: { targetId: npcId, killerId: proj.ownerId, xpReward },
        });
        // XP is now awarded centrally by GameRoom.awardKillXP via broadcastSpellProjectileEvents
      }
    });

    // ── VFX event ─────────────────────────────────────────
    events.push({
      type: 'spellImpact',
      data: {
        skillId: proj.skillId,
        x: detonateX,
        y: detonateY,
        radius,
      },
    });

    return events;
  }

  // ── Damage helpers ─────────────────────────────────────────

  /**
   * Apply AoE spell damage to a single player target.
   * Runs the full stat-driven pipeline (hit → dodge → crit → block → defense)
   * then scales by distance falloff.
   */
  private applyAoeDamage(
    target: PlayerState,
    rawDamage: number,
    dist: number,
    radius: number,
    attackerDex: number,
    critChance: number,
    critDamage: number,
    attackerId: string,
    now: number,
  ): SpellProjectileEvent[] {
    const events: SpellProjectileEvent[] = [];
    const tStats = target.stats;

    // 1. Hit roll
    if (!rollHit(attackerDex)) {
      return []; // miss — AoE misses are silent (no floater spam)
    }

    // 2. Dodge roll
    if (rollDodge(tStats?.dodgeRating ?? 0)) {
      return []; // dodged — silent for AoE
    }

    // 3. Crit roll
    const crit = rollCrit(critChance, critDamage);
    let damage = rawDamage * crit.multiplier;

    // 4. Block roll (reduces by 50%)
    let blocked = false;
    if (rollBlock(tStats?.blockRating ?? 0)) {
      damage *= 0.5;
      blocked = true;
    }

    // 5. Spell resist reduction
    damage = applyDefenseReduction(damage, tStats?.spellResist ?? 0);

    // 6. Distance falloff
    const falloff = this.computeFalloff(dist, radius);
    damage *= falloff;

    // 7. Floor, minimum 1
    damage = Math.max(1, Math.floor(damage));

    // Apply shield absorption (Shield of Faith) before HP damage
    damage = applyShieldAbsorption(target, damage);

    // Apply
    target.hp -= damage;
    target.invulnerableUntil = now + INVULNERABILITY_MS;

    events.push({
      type: 'playerHit',
      data: {
        targetId: target.id,
        attackerId,
        damage,
        remainingHp: Math.max(0, target.hp),
        isCrit: crit.isCrit,
        blocked,
      },
    });

    if (target.hp <= 0) {
      target.hp = 0;
      target.alive = false;
      target.respawnAt = now + RESPAWN_TIME_MS;
      events.push({
        type: 'playerDied',
        data: { targetId: target.id, killerId: attackerId },
      });
    }

    return events;
  }

  /**
   * Linear falloff: 1.0 at center → FIREBALL_DAMAGE_FALLOFF_MIN at edge.
   */
  private computeFalloff(dist: number, radius: number): number {
    if (radius <= 0) return 1;
    const t = Math.min(dist / radius, 1); // 0 at center, 1 at edge
    return 1 - t * (1 - FIREBALL_DAMAGE_FALLOFF_MIN);
  }
}
