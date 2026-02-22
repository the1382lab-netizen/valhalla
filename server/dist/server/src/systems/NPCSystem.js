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
import { NPCState } from '../schema/NPCState.js';
import { applyShieldAbsorption } from '../schema/PlayerState.js';
import { DataManager } from './DataManager.js';
import { syncNpcBuffsToSchema } from './SkillEffectHandler.js';
import { INVULNERABILITY_MS, RESPAWN_TIME_MS } from '@valhalla/shared';
export class NPCSystem {
    constructor() {
        this.npcs = new Map();
        this.nextNpcId = 0;
        /**
         * Tracks the last DoT tick timestamp for each active buff on an NPC.
         * Key format: "npcId:skillId:casterId"
         */
        this.npcDotTickTracker = new Map();
    }
    /**
     * Spawn all NPCs for a zone based on map spawn points and NPC templates.
     * Call this once per zone when the zone is loaded.
     */
    spawnZone(zoneId, mapManager, gameNpcs) {
        const spawnPoints = mapManager.getSpawnPoints(zoneId);
        const dm = DataManager.instance;
        let spawned = 0;
        for (const sp of spawnPoints) {
            if (sp.type !== 'enemy_spawn' && sp.type !== 'npc_spawn')
                continue;
            const templateId = sp.templateId ?? sp.properties?.templateId;
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
                threatTable: new Map(),
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
    update(dt, now, players, gameNpcs, getCollision) {
        const events = [];
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
                    data.threatTable.clear();
                    // Clear any leftover DoT buffs from previous life
                    npc.activeBuffs = [];
                    npc.syncedBuffs.clear();
                    for (const k of [...this.npcDotTickTracker.keys()]) {
                        if (k.startsWith(`${npc.id}:`))
                            this.npcDotTickTracker.delete(k);
                    }
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
                        data.threatTable.clear();
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
                            const clamped = col.clampToMap(resolved.x, resolved.y);
                            npc.x = clamped.x;
                            npc.y = clamped.y;
                        }
                        else {
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
                            let damage = Math.max(1, baseDamage);
                            // Apply shield absorption (Shield of Faith) before HP damage
                            damage = applyShieldAbsorption(target, damage);
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
                }
                else {
                    // Target invalid, return to spawn
                    data.aggroTarget = null;
                }
            }
            else if (!data.aggroTarget && (npc.x !== spawnX || npc.y !== spawnY)) {
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
                        const clamped = col.clampToMap(resolved.x, resolved.y);
                        npc.x = clamped.x;
                        npc.y = clamped.y;
                    }
                    else {
                        npc.x += moveX;
                        npc.y += moveY;
                    }
                }
                else {
                    npc.x = spawnX;
                    npc.y = spawnY;
                }
            }
        }
        // ── Buff ticking for all alive NPCs ──
        for (const [, data] of this.npcs) {
            if (data.npc.alive && data.npc.activeBuffs.length > 0) {
                const buffEvents = this.tickNpcBuffs(data, now);
                for (const e of buffEvents)
                    events.push(e);
            }
        }
        return events;
    }
    /**
     * Apply damage to an NPC. Returns XP reward if the NPC dies, 0 otherwise.
     */
    damageNPC(npcId, damage, attackerId, now) {
        const data = this.npcs.get(npcId);
        if (!data || !data.npc.alive)
            return { died: false, xpReward: 0 };
        data.npc.hp = Math.max(0, data.npc.hp - damage);
        // Accumulate threat — damage dealt = threat generated
        const canAggro = data.template.canAggro ?? (data.template.type === 'enemy');
        if (canAggro) {
            data.threatTable.set(attackerId, (data.threatTable.get(attackerId) ?? 0) + damage);
        }
        if (data.npc.hp <= 0) {
            data.npc.alive = false;
            data.respawnAt = now + data.template.respawnMs;
            data.aggroTarget = null;
            data.threatTable.clear();
            return { died: true, xpReward: data.template.xpReward };
        }
        return { died: false, xpReward: 0 };
    }
    /**
     * Taunt an NPC — add bonus threat to the player's current threat and
     * immediately force the NPC to target them.
     */
    tauntNpc(npcId, playerId, bonusThreat) {
        const data = this.npcs.get(npcId);
        if (!data || !data.npc.alive)
            return;
        const current = data.threatTable.get(playerId) ?? 0;
        data.threatTable.set(playerId, current + bonusThreat);
        data.aggroTarget = playerId;
    }
    /**
     * Get an NPC by ID.
     */
    getNPC(npcId) {
        return this.npcs.get(npcId);
    }
    /**
     * Get all alive NPCs in a zone.
     */
    getAliveNPCsInZone(zoneId) {
        const result = [];
        for (const [, data] of this.npcs) {
            if (data.npc.zoneId === zoneId && data.npc.alive) {
                result.push(data.npc);
            }
        }
        return result;
    }
    // ── Admin API (called from admin REST routes) ──
    /**
     * Register an externally-created NPC (e.g. from admin spawn) so the
     * AI system tracks it like any other NPC.
     */
    registerAdminNPC(npcId, npc, spawnX, spawnY, template) {
        this.npcs.set(npcId, {
            npc,
            spawnX,
            spawnY,
            template,
            respawnAt: 0,
            aggroTarget: null,
            leashRange: template.leashRange ?? (template.aggroRange ?? 200) * 3,
            lastAttackTime: 0,
            threatTable: new Map(),
        });
    }
    /**
     * Force-kill an NPC from admin. Sets it dead and starts respawn timer.
     */
    adminKill(npcId) {
        const data = this.npcs.get(npcId);
        if (!data)
            return;
        data.npc.alive = false;
        data.npc.hp = 0;
        data.respawnAt = Date.now() + data.template.respawnMs;
        data.aggroTarget = null;
        data.threatTable.clear();
    }
    /**
     * Force-respawn a dead NPC immediately from admin.
     */
    adminRespawn(npcId, gameNpcs) {
        const data = this.npcs.get(npcId);
        if (!data)
            return;
        data.npc.alive = true;
        data.npc.hp = data.template.hp;
        data.npc.x = data.spawnX;
        data.npc.y = data.spawnY;
        data.respawnAt = 0;
        data.aggroTarget = null;
        data.threatTable.clear();
        if (!gameNpcs.has(npcId)) {
            gameNpcs.set(npcId, data.npc);
        }
    }
    /**
     * Permanently delete an NPC from admin — removes it from the game state
     * and from the AI tracking map so it will never respawn.
     */
    adminDelete(npcId, gameNpcs) {
        // Remove from AI tracker first (cancels any pending respawn timer)
        this.npcs.delete(npcId);
        // Remove from Colyseus state so clients stop seeing it
        gameNpcs.delete(npcId);
    }
    // ── Private ──
    /**
     * Tick active buffs (DoTs) on an NPC.
     * - Removes expired buffs.
     * - Applies 1 second of DoT damage once per second.
     * - Handles NPC death from DoT (fires npcDied event).
     * - Keeps syncedBuffs in sync with activeBuffs.
     */
    tickNpcBuffs(data, now) {
        const events = [];
        const { npc, template } = data;
        // Remove expired buffs
        const prevLen = npc.activeBuffs.length;
        npc.activeBuffs = npc.activeBuffs.filter(b => b.expiresAt > now);
        const changed = npc.activeBuffs.length !== prevLen;
        // Apply DoT damage (1 tick per second per buff)
        for (const buff of npc.activeBuffs) {
            if (!buff.dotDamagePerSec)
                continue;
            const key = `${npc.id}:${buff.skillId}:${buff.casterId}`;
            const trackedTick = this.npcDotTickTracker.get(key);
            // If the tracker has a stale entry from a previous buff application, reset
            // to the current buff's appliedAt to prevent rapid catch-up ticks.
            const lastTick = (trackedTick !== undefined && trackedTick >= buff.appliedAt)
                ? trackedTick
                : buff.appliedAt;
            if (now - lastTick >= 1000) {
                this.npcDotTickTracker.set(key, lastTick + 1000);
                const dmg = buff.dotDamagePerSec;
                npc.hp = Math.max(0, npc.hp - dmg);
                events.push({
                    type: 'npcHit',
                    data: {
                        targetId: npc.id,
                        attackerId: buff.casterId,
                        damage: dmg,
                        remainingHp: npc.hp,
                        isCrit: false,
                        blocked: false,
                    },
                });
                if (npc.hp <= 0) {
                    npc.alive = false;
                    data.respawnAt = now + template.respawnMs;
                    data.aggroTarget = null;
                    data.threatTable.clear();
                    // Clear all DoT trackers for this NPC
                    for (const k of [...this.npcDotTickTracker.keys()]) {
                        if (k.startsWith(`${npc.id}:`))
                            this.npcDotTickTracker.delete(k);
                    }
                    // Clear synced buffs — NPC is dead
                    npc.activeBuffs = [];
                    npc.syncedBuffs.clear();
                    events.push({
                        type: 'npcDied',
                        data: { targetId: npc.id, killerId: buff.casterId, xpReward: template.xpReward },
                    });
                    return events; // Stop processing further buffs
                }
            }
        }
        // Keep syncedBuffs in sync with activeBuffs if anything changed
        if (changed) {
            syncNpcBuffsToSchema(npc);
        }
        return events;
    }
    createNPC(zoneId, spawnPoint, template) {
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
    updateAggro(data, players) {
        const { npc, template } = data;
        // Respect the canAggro toggle (defaults: true for enemies, false for friendly NPCs)
        const canAggro = template.canAggro ?? (template.type === 'enemy');
        if (!canAggro)
            return;
        const aggroRange = template.aggroRange ?? 200;
        // ── Phase 1: Prune threat table — remove dead / disconnected / wrong-zone entries ──
        for (const [sessionId] of data.threatTable) {
            const p = players.get(sessionId);
            if (!p || !p.alive || p.zoneId !== npc.zoneId) {
                data.threatTable.delete(sessionId);
            }
        }
        // ── Phase 2: Threat-based targeting — pick highest-threat valid player ──
        if (data.threatTable.size > 0) {
            let bestId = null;
            let bestThreat = -1;
            for (const [sessionId, threat] of data.threatTable) {
                if (threat > bestThreat) {
                    bestThreat = threat;
                    bestId = sessionId;
                }
            }
            data.aggroTarget = bestId;
            return;
        }
        // ── Phase 3: Proximity fallback — no one has dealt damage yet ──
        let nearest = null;
        let nearestDist = aggroRange * aggroRange;
        players.forEach((player, sessionId) => {
            if (!player.alive || player.zoneId !== npc.zoneId)
                return;
            const dx = player.x - npc.x;
            const dy = player.y - npc.y;
            const distSq = dx * dx + dy * dy;
            if (distSq < nearestDist) {
                nearestDist = distSq;
                nearest = sessionId;
            }
        });
        if (nearest) {
            // Seed a small initial threat so proximity-aggroed players appear in the table
            data.threatTable.set(nearest, 1);
        }
        data.aggroTarget = nearest;
    }
}
//# sourceMappingURL=NPCSystem.js.map