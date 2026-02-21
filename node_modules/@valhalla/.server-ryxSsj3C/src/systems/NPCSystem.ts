/**
 * NPCSystem — spawns and manages NPC/enemy entities from map overlay spawn points.
 *
 * Reads enemy_spawn and npc_spawn points from MapManager, looks up their
 * templateId in DataManager's NPC templates, and creates live NPCState
 * instances in GameState.
 *
 * Handles:
 * - Initial spawn of all NPCs for a zone
 * - Respawn timers for dead enemies
 * - Basic AI: aggro detection, movement toward targets, returning to spawn
 * - NPC melee attacks against aggroed players
 * - Combat integration: NPCs take damage from players, award XP on death
 */

import { MapSchema } from '@colyseus/schema';
import { NPCState } from '../schema/NPCState.js';
import { PlayerState } from '../schema/PlayerState.js';
import { DataManager } from './DataManager.js';
import { MapManager } from './MapManager.js';
import { CollisionSystem } from './CollisionSystem.js';
import type { NPCTemplate, SpawnPointData } from '@valhalla/shared';
import { INVULNERABILITY_MS, RESPAWN_TIME_MS } from '@valhalla/shared';
import type { CombatEvent } from './CombatSystem.js';

interface SpawnedNPCData {
  /** The NPC instance */
  npc: NPCState;
  /** Original spawn position */
  spawnX: number;
  spawnY: number;
  /** Template reference */
  template: NPCTemplate;
  /** When this NPC should respawn (0 = alive) */
  respawnAt: number;
  /** Current aggro target (player sessionId) */
  aggroTarget: string | null;
  /** Leash distance — max dist from spawn before resetting */
  leashRange: number;
  /** Last time this NPC attacked (ms timestamp) */
  lastAttackTime: number;
}

export class NPCSystem {
  private npcs: Map<string, SpawnedNPCData> = new Map();
  private nextNpcId: number = 0;

  /**
   * Spawn all NPCs for a zone based on map spawn points and NPC templates.
   * Call this once per zone when the zone is loaded.
   */
  spawnZone(
    zoneId: string,
    mapManager: MapManager,
    gameNpcs: MapSchema<NPCState>,
  ): void {
    const spawnPoints = mapManager.getSpawnPoints(zoneId);
    const dm = DataManager.instance;

    let spawned = 0;
    for (const sp of spawnPoints) {
      if (sp.type !== 'enemy_spawn' && sp.type !== 'npc_spawn') continue;

      const templateId = (sp as any).templateId ?? sp.properties?.templateId;
      if (!templateId) {
        console.warn(`[NPCSystem] Spawn point "${sp.id}" in zone "${zoneId}" has no templateId, skipping`);
        continue;
      }

      const template = dm.npcTemplates[templateId];
      if (!template) {
        console.warn(`[NPCSystem] Unknown NPC template "${templateId}" for spawn "${sp.id}", skipping`);
        continue;
      }

      const npc = this.createNPC(zoneId, sp, template);
      gameNpcs.set(npc.id, npc);

      this.npcs.set(npc.id, {
        npc,
        spawnX: sp.x,
        spawnY: sp.y,
        template,
        respawnAt: 0,
        aggroTarget: null,
        leashRange: template.leashRange ?? (template.aggroRange ?? 200) * 3,
        lastAttackTime: 0,
      });

      spawned++;
    }

    if (spawned > 0) {
      console.log(`[NPCSystem] Spawned ${spawned} NPC(s) in zone "${zoneId}"`);
    }
  }

  /**
   * Main update tick — handles respawns, aggro, movement, and NPC attacks.
   * Returns combat events to be broadcast to clients.
   */
  update(
    dt: number,
    now: number,
    players: MapSchema<PlayerState>,
    gameNpcs: MapSchema<NPCState>,
    getCollision: (zoneId: string) => CollisionSystem | null,
  ): CombatEvent[] {
    const events: CombatEvent[] = [];

    for (const [id, data] of this.npcs) {
      const { npc, template, spawnX, spawnY } = data;

      // ── Respawn check ──
      if (!npc.alive) {
        if (data.respawnAt > 0 && now >= data.respawnAt) {
          // Respawn
          npc.alive = true;
          npc.hp = template.hp;
          npc.x = spawnX;
          npc.y = spawnY;
          data.respawnAt = 0;
          data.aggroTarget = null;

          // Re-add to synced state if it was removed
          if (!gameNpcs.has(id)) {
            gameNpcs.set(id, npc);
          }
        }
        continue;
      }

      // ── Aggro detection ──
      if (template.behaviorType === 'aggressive' || template.behaviorType === 'patrol') {
        this.updateAggro(data, players);
      }

      // ── Movement + Attack ──
      if (data.aggroTarget && template.behaviorType !== 'stationary') {
        const target = players.get(data.aggroTarget);
        if (target && target.alive && target.zoneId === npc.zoneId) {
          // Move toward target
          const dx = target.x - npc.x;
          const dy = target.y - npc.y;
          const dist = Math.sqrt(dx * dx + dy * dy);

          // Check leash
          const dxSpawn = npc.x - spawnX;
          const dySpawn = npc.y - spawnY;
          if (dxSpawn * dxSpawn + dySpawn * dySpawn > data.leashRange * data.leashRange) {
            // Too far from spawn, reset
            data.aggroTarget = null;
            npc.x = spawnX;
            npc.y = spawnY;
            npc.hp = template.hp; // Full heal on reset
            continue;
          }

          const chaseSpeed = template.moveSpeed ?? 60;
          if (dist > 30) {
            const moveX = (dx / dist) * chaseSpeed * dt;
            const moveY = (dy / dist) * chaseSpeed * dt;
            const col = getCollision(npc.zoneId);
            if (col) {
              const resolved = col.resolveMovement(npc.x, npc.y, moveX, moveY);
              const clamped  = col.clampToMap(resolved.x, resolved.y);
              npc.x = clamped.x;
              npc.y = clamped.y;
            } else {
              npc.x += moveX;
              npc.y += moveY;
            }
          }

          // Face the target
          npc.aimAngle = Math.atan2(dy, dx);

          // ── Attack if within range and off cooldown ──
          const attackRange = template.attackRange ?? 40;
          const attackSpeed = template.attackSpeed ?? 1500;
          const baseDamage = template.damage ?? 5;

          if (dist <= attackRange && now >= data.lastAttackTime + attackSpeed) {
            data.lastAttackTime = now;

            if (target.alive && now >= target.invulnerableUntil) {
              const damage = Math.max(1, baseDamage);
              target.hp -= damage;
              target.invulnerableUntil = now + INVULNERABILITY_MS;

              events.push({
                type: 'playerHit',
                data: {
                  targetId: target.id,
                  attackerId: npc.id,
                  damage,
                  remainingHp: target.hp,
                  isCrit: false,
                  blocked: false,
                },
              });

              if (target.hp <= 0) {
                target.hp = 0;
                target.alive = false;
                target.respawnAt = now + RESPAWN_TIME_MS;
                data.aggroTarget = null;

                events.push({
                  type: 'playerDied',
                  data: { targetId: target.id, killerId: npc.id },
                });
              }
            }
          }
        } else {
          // Target invalid, return to spawn
          data.aggroTarget = null;
        }
      } else if (!data.aggroTarget && (npc.x !== spawnX || npc.y !== spawnY)) {
        // Return to spawn position
        const returnSpeed = (template.moveSpeed ?? 60) * 0.66;
        const dx = spawnX - npc.x;
        const dy = spawnY - npc.y;
        const dist = Math.sqrt(dx * dx + dy * dy);
        if (dist > 2) {
          const moveX = (dx / dist) * returnSpeed * dt;
          const moveY = (dy / dist) * returnSpeed * dt;
          const col = getCollision(npc.zoneId);
          if (col) {
            const resolved = col.resolveMovement(npc.x, npc.y, moveX, moveY);
            const clamped  = col.clampToMap(resolved.x, resolved.y);
            npc.x = clamped.x;
            npc.y = clamped.y;
          } else {
            npc.x += moveX;
            npc.y += moveY;
          }
        } else {
          npc.x = spawnX;
          npc.y = spawnY;
        }
      }
    }

    return events;
  }

  /**
   * Apply damage to an NPC. Returns XP reward if the NPC dies, 0 otherwise.
   */
  damageNPC(npcId: string, damage: number, attackerId: string, now: number): { died: boolean; xpReward: number } {
    const data = this.npcs.get(npcId);
    if (!data || !data.npc.alive) return { died: false, xpReward: 0 };

    data.npc.hp = Math.max(0, data.npc.hp - damage);

    // Aggro toward attacker (only if canAggro allows it)
    const canAggro = data.template.canAggro ?? (data.template.type === 'enemy');
    if (!data.aggroTarget && canAggro) {
      data.aggroTarget = attackerId;
    }

    if (data.npc.hp <= 0) {
      data.npc.alive = false;
      data.respawnAt = now + data.template.respawnMs;
      data.aggroTarget = null;
      return { died: true, xpReward: data.template.xpReward };
    }

    return { died: false, xpReward: 0 };
  }

  /**
   * Get an NPC by ID.
   */
  getNPC(npcId: string): SpawnedNPCData | undefined {
    return this.npcs.get(npcId);
  }

  /**
   * Get all alive NPCs in a zone.
   */
  getAliveNPCsInZone(zoneId: string): NPCState[] {
    const result: NPCState[] = [];
    for (const [, data] of this.npcs) {
      if (data.npc.zoneId === zoneId && data.npc.alive) {
        result.push(data.npc);
      }
    }
    return result;
  }

  // ── Private ──

  private createNPC(zoneId: string, spawnPoint: SpawnPointData, template: NPCTemplate): NPCState {
    const npc = new NPCState();
    npc.id = `npc_${zoneId}_${this.nextNpcId++}`;
    npc.templateId = template.id;
    npc.name = template.name;
    npc.npcType = template.type;
    npc.zoneId = zoneId;
    npc.x = spawnPoint.x;
    npc.y = spawnPoint.y;
    npc.hp = template.hp;
    npc.maxHp = template.hp;
    npc.level = template.level;
    npc.alive = true;
    npc.spriteColor = template.spriteColor;
    npc.spriteSize = template.spriteSize;
    return npc;
  }

  private updateAggro(data: SpawnedNPCData, players: MapSchema<PlayerState>): void {
    const { npc, template } = data;

    // Respect the canAggro toggle (defaults: true for enemies, false for friendly NPCs)
    const canAggro = template.canAggro ?? (template.type === 'enemy');
    if (!canAggro) return;

    const aggroRange = template.aggroRange ?? 200;

    // If already aggroed, verify target still valid
    if (data.aggroTarget) {
      const target = players.get(data.aggroTarget);
      if (!target || !target.alive || target.zoneId !== npc.zoneId) {
        data.aggroTarget = null;
      } else {
        return; // Keep current target
      }
    }

    // Scan for nearest player in aggro range
    let nearest: string | null = null;
    let nearestDist = aggroRange * aggroRange;

    players.forEach((player, sessionId) => {
      if (!player.alive || player.zoneId !== npc.zoneId) return;
      const dx = player.x - npc.x;
      const dy = player.y - npc.y;
      const distSq = dx * dx + dy * dy;
      if (distSq < nearestDist) {
        nearestDist = distSq;
        nearest = sessionId;
      }
    });

    data.aggroTarget = nearest;
  }
}
