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
import { MapManager } from './MapManager.js';
import { CollisionSystem } from './CollisionSystem.js';
import type { NPCTemplate } from '@valhalla/shared';
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
export declare class NPCSystem {
    private npcs;
    private nextNpcId;
    /**
     * Spawn all NPCs for a zone based on map spawn points and NPC templates.
     * Call this once per zone when the zone is loaded.
     */
    spawnZone(zoneId: string, mapManager: MapManager, gameNpcs: MapSchema<NPCState>): void;
    /**
     * Main update tick — handles respawns, aggro, movement, and NPC attacks.
     * Returns combat events to be broadcast to clients.
     */
    update(dt: number, now: number, players: MapSchema<PlayerState>, gameNpcs: MapSchema<NPCState>, getCollision: (zoneId: string) => CollisionSystem | null): CombatEvent[];
    /**
     * Apply damage to an NPC. Returns XP reward if the NPC dies, 0 otherwise.
     */
    damageNPC(npcId: string, damage: number, attackerId: string, now: number): {
        died: boolean;
        xpReward: number;
    };
    /**
     * Get an NPC by ID.
     */
    getNPC(npcId: string): SpawnedNPCData | undefined;
    /**
     * Get all alive NPCs in a zone.
     */
    getAliveNPCsInZone(zoneId: string): NPCState[];
    private createNPC;
    private updateAggro;
}
export {};
//# sourceMappingURL=NPCSystem.d.ts.map