import { MapSchema } from '@colyseus/schema';
import { PlayerState } from '../schema/PlayerState.js';
import { ProjectileState } from '../schema/ProjectileState.js';
import { NPCState } from '../schema/NPCState.js';
import { CollisionSystem } from './CollisionSystem.js';
import type { NPCSystem } from './NPCSystem.js';
import {
  PROJECTILE_SPEED,
  PROJECTILE_RADIUS,
  PROJECTILE_MAX_RANGE,
  FIRE_COOLDOWN_MS,
  INVULNERABILITY_MS,
  RESPAWN_TIME_MS,
  PLAYER_COLLISION_RADIUS,
  MELEE_RANGE,
  MELEE_ARC,
  MELEE_COOLDOWN_MS,
  ClassId,
  isRangedMagic,
  computePhysicalDamage,
  computeSpellDamage,
  applyDefenseReduction,
  rollHit,
  rollCrit,
  rollDodge,
  rollBlock,
  computeFireCooldown,
  computeMeleeCooldown,
  BASE_MELEE_DAMAGE,
  BASE_RANGED_DAMAGE,
  BASE_SPELL_DAMAGE,
} from '@valhalla/shared';

let nextProjectileId = 0;

export type DamageType = 'physical' | 'magical';

export interface CombatEvent {
  type: 'playerHit' | 'playerDied' | 'playerRespawned' | 'meleeAttack' | 'missed' | 'dodged' | 'blocked' | 'npcHit' | 'npcDied';
  data: any;
}

/**
 * Handles projectile spawning, movement, collision, damage, and melee attacks.
 * All damage is now stat-driven: uses attacker stats for offense, target stats for defense.
 */
export class CombatSystem {
  private collision: CollisionSystem;

  constructor(collision: CollisionSystem) {
    this.collision = collision;
  }

  /**
   * Try to fire a projectile for the given player.
   * Damage and cooldown scale with player stats.
   */
  tryFire(player: PlayerState, now: number): ProjectileState | null {
    if (!player.alive) return null;
    if (now < player.fireCooldown) return null;

    const stats = player.stats;
    const dex = stats?.dexterity ?? 10;

    // Cooldown scales with dexterity
    player.fireCooldown = now + computeFireCooldown(FIRE_COOLDOWN_MS, dex);

    // Compute projectile damage based on class type
    const classId = player.classId as ClassId;
    let damage: number;
    let damageType: DamageType;
    if (isRangedMagic(classId)) {
      damage = computeSpellDamage(stats?.intelligence ?? 10, BASE_SPELL_DAMAGE);
      damageType = 'magical';
    } else {
      damage = computePhysicalDamage(stats?.strength ?? 10, BASE_RANGED_DAMAGE);
      damageType = 'physical';
    }

    const proj = new ProjectileState();
    proj.id = `proj_${nextProjectileId++}`;
    proj.ownerId = player.id;
    // Spawn slightly ahead of the player so it doesn't immediately overlap them
    const spawnOffset = PLAYER_COLLISION_RADIUS + PROJECTILE_RADIUS + 2;
    proj.x = player.x + Math.cos(player.aimAngle) * spawnOffset;
    proj.y = player.y + Math.sin(player.aimAngle) * spawnOffset;
    proj.angle = player.aimAngle;
    proj.speed = PROJECTILE_SPEED;
    proj.damage = damage;
    proj.distanceTravelled = 0;
    // Store damage type on the projectile for defense calculations on hit
    (proj as any)._damageType = damageType;
    // Store attacker stats for resolution on hit
    (proj as any)._attackerDex = dex;
    (proj as any)._critChance = stats?.critChance ?? 0.05;
    (proj as any)._critDamage = stats?.critDamage ?? 0.5;

    return proj;
  }

  /**
   * Try to perform a melee attack. Returns a list of combat events.
   * Melee is always physical damage.
   */
  tryMelee(
    attacker: PlayerState,
    players: MapSchema<PlayerState>,
    npcs: MapSchema<NPCState>,
    npcSystem: NPCSystem,
    now: number,
  ): CombatEvent[] {
    const events: CombatEvent[] = [];
    if (!attacker.alive) return events;
    if (now < attacker.meleeCooldown) return events;

    const stats = attacker.stats;
    const dex = stats?.dexterity ?? 10;

    attacker.meleeCooldown = now + computeMeleeCooldown(MELEE_COOLDOWN_MS, dex);

    // Broadcast melee swing visual
    events.push({
      type: 'meleeAttack',
      data: { attackerId: attacker.id, angle: attacker.aimAngle },
    });

    // Compute melee damage from strength
    const rawDamage = computePhysicalDamage(stats?.strength ?? 10, BASE_MELEE_DAMAGE);

    // Check all players in range and within the arc
    players.forEach((target, targetId) => {
      if (targetId === attacker.id) return;
      if (!target.alive) return;
      if (now < target.invulnerableUntil) return;

      const dx = target.x - attacker.x;
      const dy = target.y - attacker.y;
      const dist = Math.sqrt(dx * dx + dy * dy);

      if (dist > MELEE_RANGE + PLAYER_COLLISION_RADIUS) return;

      // Check angle: is the target within the melee arc?
      const angleToTarget = Math.atan2(dy, dx);
      let angleDiff = angleToTarget - attacker.aimAngle;
      // Normalise to [-PI, PI]
      while (angleDiff > Math.PI) angleDiff -= 2 * Math.PI;
      while (angleDiff < -Math.PI) angleDiff += 2 * Math.PI;

      if (Math.abs(angleDiff) > MELEE_ARC / 2) return;

      // Hit! Apply stat-driven damage
      const hitEvents = this.applyStatDamage(
        target,
        rawDamage,
        'physical',
        dex,
        stats?.critChance ?? 0.05,
        stats?.critDamage ?? 0.5,
        attacker.id,
        now,
      );
      events.push(...hitEvents);
    });

    // ── NPC targets ──
    npcs.forEach((npc, npcId) => {
      if (!npc.alive) return;
      if (npc.zoneId !== attacker.zoneId) return;

      const dx = npc.x - attacker.x;
      const dy = npc.y - attacker.y;
      const dist = Math.sqrt(dx * dx + dy * dy);

      if (dist > MELEE_RANGE + PLAYER_COLLISION_RADIUS) return;

      // Check angle: is the NPC within the melee arc?
      const angleToTarget = Math.atan2(dy, dx);
      let angleDiff = angleToTarget - attacker.aimAngle;
      while (angleDiff > Math.PI) angleDiff -= 2 * Math.PI;
      while (angleDiff < -Math.PI) angleDiff += 2 * Math.PI;

      if (Math.abs(angleDiff) > MELEE_ARC / 2) return;

      // Apply damage through NPCSystem (flat damage, no stat rolls for NPC targets)
      const { died, xpReward } = npcSystem.damageNPC(npcId, rawDamage, attacker.id, now);

      events.push({
        type: 'npcHit',
        data: {
          targetId: npcId,
          attackerId: attacker.id,
          damage: rawDamage,
          remainingHp: npc.hp,
          isCrit: false,
          blocked: false,
        },
      });

      if (died) {
        events.push({
          type: 'npcDied',
          data: { targetId: npcId, killerId: attacker.id, xpReward },
        });
        // Award XP to the attacker
        attacker.xp = (attacker.xp ?? 0) + xpReward;
      }
    });

    return events;
  }

  /**
   * Update all projectiles: move, check wall collision, check player collision.
   */
  updateProjectiles(
    projectiles: MapSchema<ProjectileState>,
    players: MapSchema<PlayerState>,
    npcs: MapSchema<NPCState>,
    npcSystem: NPCSystem,
    dt: number,
    now: number,
  ): { toRemove: string[]; events: CombatEvent[] } {
    const toRemove: string[] = [];
    const events: CombatEvent[] = [];

    projectiles.forEach((proj, projId) => {
      const moveX = Math.cos(proj.angle) * proj.speed * dt;
      const moveY = Math.sin(proj.angle) * proj.speed * dt;

      proj.x += moveX;
      proj.y += moveY;
      proj.distanceTravelled += Math.sqrt(moveX * moveX + moveY * moveY);

      // Check max range
      if (proj.distanceTravelled >= PROJECTILE_MAX_RANGE) {
        toRemove.push(projId);
        return;
      }

      // Check wall collision
      if (this.collision.isCircleBlocked(proj.x, proj.y, PROJECTILE_RADIUS)) {
        toRemove.push(projId);
        return;
      }

      // Check player collision
      let hitPlayer = false;
      players.forEach((player, playerId) => {
        if (hitPlayer) return;
        if (playerId === proj.ownerId) return; // can't hit yourself
        if (!player.alive) return;
        if (now < player.invulnerableUntil) return;

        const dx = player.x - proj.x;
        const dy = player.y - proj.y;
        const dist = Math.sqrt(dx * dx + dy * dy);
        const hitDist = PLAYER_COLLISION_RADIUS + PROJECTILE_RADIUS;

        if (dist < hitDist) {
          hitPlayer = true;
          toRemove.push(projId);

          // Stat-driven damage with projectile's stored attacker stats
          const damageType: DamageType = (proj as any)._damageType ?? 'physical';
          const attackerDex: number = (proj as any)._attackerDex ?? 10;
          const critChance: number = (proj as any)._critChance ?? 0.05;
          const critDamage: number = (proj as any)._critDamage ?? 0.5;

          const hitEvents = this.applyStatDamage(
            player,
            proj.damage,
            damageType,
            attackerDex,
            critChance,
            critDamage,
            proj.ownerId,
            now,
          );
          events.push(...hitEvents);
        }
      });

      // ── NPC collision (only if we didn't already hit a player) ──
      if (!hitPlayer) {
        let hitNpc = false;
        npcs.forEach((npc, npcId) => {
          if (hitNpc) return;
          if (!npc.alive) return;

          const dx = npc.x - proj.x;
          const dy = npc.y - proj.y;
          const dist = Math.sqrt(dx * dx + dy * dy);

          if (dist < PLAYER_COLLISION_RADIUS + PROJECTILE_RADIUS) {
            hitNpc = true;
            toRemove.push(projId);

            const { died, xpReward } = npcSystem.damageNPC(npcId, proj.damage, proj.ownerId, now);

            events.push({
              type: 'npcHit',
              data: {
                targetId: npcId,
                attackerId: proj.ownerId,
                damage: proj.damage,
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
              // Award XP to the projectile owner
              const killer = players.get(proj.ownerId);
              if (killer) {
                killer.xp = (killer.xp ?? 0) + xpReward;
              }
            }
          }
        });
      }
    });

    return { toRemove, events };
  }

  /**
   * Apply stat-driven damage to a target.
   * Rolls hit → dodge → block → crit → defense reduction.
   *
   * Hit chance is determined by the attacker's dexterity.
   * If the attack misses, nothing else is checked.
   * If it hits, the target can still dodge (based on dodgeRating)
   * or block (based on blockRating, reduces damage by 50%).
   */
  private applyStatDamage(
    target: PlayerState,
    rawDamage: number,
    damageType: DamageType,
    attackerDex: number,
    attackerCritChance: number,
    attackerCritDamage: number,
    attackerId: string,
    now: number,
  ): CombatEvent[] {
    const events: CombatEvent[] = [];
    const tStats = target.stats;

    // 1. Hit roll — attacker's dexterity determines chance to connect
    if (!rollHit(attackerDex)) {
      events.push({
        type: 'missed',
        data: { targetId: target.id, attackerId },
      });
      return events;
    }

    // 2. Dodge roll — target's dodge rating
    if (rollDodge(tStats?.dodgeRating ?? 0)) {
      events.push({
        type: 'dodged',
        data: { targetId: target.id, attackerId },
      });
      return events;
    }

    // 4. Crit roll
    const crit = rollCrit(attackerCritChance, attackerCritDamage);
    let damage = rawDamage * crit.multiplier;

    // 5. Block roll (only reduces, doesn't negate)
    let blocked = false;
    if (rollBlock(tStats?.blockRating ?? 0)) {
      damage *= 0.5;
      blocked = true;
    }

    // 6. Defense reduction
    const defense = damageType === 'physical'
      ? (tStats?.physicalDefense ?? 0)
      : (tStats?.spellResist ?? 0);
    damage = applyDefenseReduction(damage, defense);

    // Floor the final damage (minimum 1)
    damage = Math.max(1, Math.floor(damage));

    // Apply
    target.hp -= damage;
    target.invulnerableUntil = now + INVULNERABILITY_MS;

    if (blocked) {
      events.push({
        type: 'blocked',
        data: { targetId: target.id, attackerId },
      });
    }

    events.push({
      type: 'playerHit',
      data: {
        targetId: target.id,
        attackerId,
        damage,
        remainingHp: target.hp,
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
        data: {
          targetId: target.id,
          killerId: attackerId,
        },
      });
    }

    return events;
  }

  /**
   * Check for dead players ready to respawn.
   */
  checkRespawns(
    players: MapSchema<PlayerState>,
    now: number,
    respawnPoint?: { x: number; y: number },
  ): CombatEvent[] {
    const events: CombatEvent[] = [];
    const spawnX = respawnPoint?.x ?? 352;
    const spawnY = respawnPoint?.y ?? 352;

    players.forEach((player) => {
      if (player.alive) return;
      if (player.respawnAt === 0 || now < player.respawnAt) return;

      // Respawn!
      player.alive = true;
      player.hp = player.maxHp;
      player.mana = player.maxMana;
      player.respawnAt = 0;
      player.invulnerableUntil = now + INVULNERABILITY_MS * 2; // extra i-frames on respawn

      // Respawn at the zone's spawn point
      player.x = spawnX;
      player.y = spawnY;

      events.push({
        type: 'playerRespawned',
        data: { playerId: player.id },
      });
    });

    return events;
  }
}
