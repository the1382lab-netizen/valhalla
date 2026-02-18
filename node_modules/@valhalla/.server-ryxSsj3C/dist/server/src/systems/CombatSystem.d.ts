import { MapSchema } from '@colyseus/schema';
import { PlayerState } from '../schema/PlayerState.js';
import { ProjectileState } from '../schema/ProjectileState.js';
import { NPCState } from '../schema/NPCState.js';
import { CollisionSystem } from './CollisionSystem.js';
import type { NPCSystem } from './NPCSystem.js';
export type DamageType = 'physical' | 'magical';
export interface CombatEvent {
    type: 'playerHit' | 'playerDied' | 'playerRespawned' | 'meleeAttack' | 'missed' | 'dodged' | 'blocked' | 'npcHit' | 'npcDied';
    data: any;
}
/**
 * Handles projectile spawning, movement, collision, damage, and melee attacks.
 * All damage is now stat-driven: uses attacker stats for offense, target stats for defense.
 */
export declare class CombatSystem {
    private collision;
    constructor(collision: CollisionSystem);
    /**
     * Try to fire a projectile for the given player.
     * Damage and cooldown scale with player stats.
     */
    tryFire(player: PlayerState, now: number): ProjectileState | null;
    /**
     * Try to perform a melee attack. Returns a list of combat events.
     * Melee is always physical damage.
     */
    tryMelee(attacker: PlayerState, players: MapSchema<PlayerState>, npcs: MapSchema<NPCState>, npcSystem: NPCSystem, now: number): CombatEvent[];
    /**
     * Update all projectiles: move, check wall collision, check player collision.
     */
    updateProjectiles(projectiles: MapSchema<ProjectileState>, players: MapSchema<PlayerState>, npcs: MapSchema<NPCState>, npcSystem: NPCSystem, dt: number, now: number): {
        toRemove: string[];
        events: CombatEvent[];
    };
    /**
     * Apply stat-driven damage to a target.
     * Rolls hit → dodge → block → crit → defense reduction.
     *
     * Hit chance is determined by the attacker's dexterity.
     * If the attack misses, nothing else is checked.
     * If it hits, the target can still dodge (based on dodgeRating)
     * or block (based on blockRating, reduces damage by 50%).
     */
    private applyStatDamage;
    /**
     * Check for dead players ready to respawn.
     */
    checkRespawns(players: MapSchema<PlayerState>, now: number, respawnPoint?: {
        x: number;
        y: number;
    }): CombatEvent[];
}
//# sourceMappingURL=CombatSystem.d.ts.map