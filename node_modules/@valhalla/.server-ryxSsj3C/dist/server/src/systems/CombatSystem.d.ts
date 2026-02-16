import { MapSchema } from '@colyseus/schema';
import { PlayerState } from '../schema/PlayerState.js';
import { ProjectileState } from '../schema/ProjectileState.js';
import { CollisionSystem } from './CollisionSystem.js';
export interface CombatEvent {
    type: 'playerHit' | 'playerDied' | 'playerRespawned' | 'meleeAttack';
    data: any;
}
/**
 * Handles projectile spawning, movement, collision, damage, and melee attacks.
 */
export declare class CombatSystem {
    private collision;
    constructor(collision: CollisionSystem);
    /**
     * Try to fire a projectile for the given player.
     * Returns the new ProjectileState if successful, null otherwise.
     */
    tryFire(player: PlayerState, now: number): ProjectileState | null;
    /**
     * Try to perform a melee attack. Returns a list of hit player IDs.
     */
    tryMelee(attacker: PlayerState, players: MapSchema<PlayerState>, now: number): CombatEvent[];
    /**
     * Update all projectiles: move, check wall collision, check player collision.
     * Returns projectile IDs to remove and any combat events.
     */
    updateProjectiles(projectiles: MapSchema<ProjectileState>, players: MapSchema<PlayerState>, dt: number, now: number): {
        toRemove: string[];
        events: CombatEvent[];
    };
    /**
     * Apply damage to a player. Returns events generated (hit, and possibly death).
     */
    private applyDamage;
    /**
     * Check for dead players ready to respawn.
     */
    checkRespawns(players: MapSchema<PlayerState>, now: number): CombatEvent[];
}
//# sourceMappingURL=CombatSystem.d.ts.map