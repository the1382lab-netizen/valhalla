import { ProjectileState } from '../schema/ProjectileState.js';
import { PROJECTILE_SPEED, PROJECTILE_RADIUS, PROJECTILE_MAX_RANGE, PROJECTILE_DAMAGE, FIRE_COOLDOWN_MS, INVULNERABILITY_MS, RESPAWN_TIME_MS, PLAYER_COLLISION_RADIUS, MELEE_DAMAGE, MELEE_RANGE, MELEE_ARC, MELEE_COOLDOWN_MS, TILE_SIZE, } from '@valhalla/shared';
let nextProjectileId = 0;
/**
 * Handles projectile spawning, movement, collision, damage, and melee attacks.
 */
export class CombatSystem {
    constructor(collision) {
        this.collision = collision;
    }
    /**
     * Try to fire a projectile for the given player.
     * Returns the new ProjectileState if successful, null otherwise.
     */
    tryFire(player, now) {
        if (!player.alive)
            return null;
        if (now < player.fireCooldown)
            return null;
        player.fireCooldown = now + FIRE_COOLDOWN_MS;
        const proj = new ProjectileState();
        proj.id = `proj_${nextProjectileId++}`;
        proj.ownerId = player.id;
        // Spawn slightly ahead of the player so it doesn't immediately overlap them
        const spawnOffset = PLAYER_COLLISION_RADIUS + PROJECTILE_RADIUS + 2;
        proj.x = player.x + Math.cos(player.aimAngle) * spawnOffset;
        proj.y = player.y + Math.sin(player.aimAngle) * spawnOffset;
        proj.angle = player.aimAngle;
        proj.speed = PROJECTILE_SPEED;
        proj.damage = PROJECTILE_DAMAGE;
        proj.distanceTravelled = 0;
        return proj;
    }
    /**
     * Try to perform a melee attack. Returns a list of hit player IDs.
     */
    tryMelee(attacker, players, now) {
        const events = [];
        if (!attacker.alive)
            return events;
        if (now < attacker.meleeCooldown)
            return events;
        attacker.meleeCooldown = now + MELEE_COOLDOWN_MS;
        // Broadcast melee swing visual
        events.push({
            type: 'meleeAttack',
            data: { attackerId: attacker.id, angle: attacker.aimAngle },
        });
        // Check all players in range and within the arc
        players.forEach((target, targetId) => {
            if (targetId === attacker.id)
                return;
            if (!target.alive)
                return;
            if (now < target.invulnerableUntil)
                return;
            const dx = target.x - attacker.x;
            const dy = target.y - attacker.y;
            const dist = Math.sqrt(dx * dx + dy * dy);
            if (dist > MELEE_RANGE + PLAYER_COLLISION_RADIUS)
                return;
            // Check angle: is the target within the melee arc?
            const angleToTarget = Math.atan2(dy, dx);
            let angleDiff = angleToTarget - attacker.aimAngle;
            // Normalise to [-PI, PI]
            while (angleDiff > Math.PI)
                angleDiff -= 2 * Math.PI;
            while (angleDiff < -Math.PI)
                angleDiff += 2 * Math.PI;
            if (Math.abs(angleDiff) > MELEE_ARC / 2)
                return;
            // Hit!
            const hitEvents = this.applyDamage(target, MELEE_DAMAGE, attacker.id, now);
            events.push(...hitEvents);
        });
        return events;
    }
    /**
     * Update all projectiles: move, check wall collision, check player collision.
     * Returns projectile IDs to remove and any combat events.
     */
    updateProjectiles(projectiles, players, dt, now) {
        const toRemove = [];
        const events = [];
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
                if (hitPlayer)
                    return;
                if (playerId === proj.ownerId)
                    return; // can't hit yourself
                if (!player.alive)
                    return;
                if (now < player.invulnerableUntil)
                    return;
                const dx = player.x - proj.x;
                const dy = player.y - proj.y;
                const dist = Math.sqrt(dx * dx + dy * dy);
                const hitDist = PLAYER_COLLISION_RADIUS + PROJECTILE_RADIUS;
                if (dist < hitDist) {
                    hitPlayer = true;
                    toRemove.push(projId);
                    const hitEvents = this.applyDamage(player, proj.damage, proj.ownerId, now);
                    events.push(...hitEvents);
                }
            });
        });
        return { toRemove, events };
    }
    /**
     * Apply damage to a player. Returns events generated (hit, and possibly death).
     */
    applyDamage(target, damage, attackerId, now) {
        const events = [];
        target.hp -= damage;
        target.invulnerableUntil = now + INVULNERABILITY_MS;
        events.push({
            type: 'playerHit',
            data: {
                targetId: target.id,
                attackerId,
                damage,
                remainingHp: target.hp,
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
    checkRespawns(players, now) {
        const events = [];
        players.forEach((player) => {
            if (player.alive)
                return;
            if (player.respawnAt === 0 || now < player.respawnAt)
                return;
            // Respawn!
            player.alive = true;
            player.hp = player.maxHp;
            player.respawnAt = 0;
            player.invulnerableUntil = now + INVULNERABILITY_MS * 2; // extra i-frames on respawn
            // Respawn at a safe position (near spawn point)
            player.x = 5 * TILE_SIZE + TILE_SIZE / 2;
            player.y = 5 * TILE_SIZE + TILE_SIZE / 2;
            events.push({
                type: 'playerRespawned',
                data: { playerId: player.id },
            });
        });
        return events;
    }
}
//# sourceMappingURL=CombatSystem.js.map