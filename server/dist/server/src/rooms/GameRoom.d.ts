import { Room, Client } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
import { PlayerState } from '../schema/PlayerState.js';
import { CollisionSystem } from '../systems/CollisionSystem.js';
import { ZoneConnection } from '@valhalla/shared';
import { JwtPayload } from '../services/AuthService.js';
/** Cached per-zone data for multi-zone support within a single room. */
interface ZoneCacheEntry {
    collision: CollisionSystem;
    connections: ZoneConnection[];
    respawnPoint: {
        x: number;
        y: number;
    };
}
export declare function getActiveGameRoom(): GameRoom | null;
export declare class GameRoom extends Room<{
    state: GameState;
}> {
    private movement;
    private combat;
    private spellProjectileSystem;
    private mapManager;
    private skillSystem;
    private npcSystem;
    private lootBagSystem;
    private inputQueues;
    /** Maps sessionId → persistent character data for save/load. */
    private sessionData;
    /** partyId → Set of member sessionIds */
    private parties;
    /** sessionId → partyId */
    private playerParty;
    /** invitee sessionId → inviter sessionId */
    private pendingInvites;
    private nextPartyId;
    /** Interval handle for periodic saves. */
    private saveInterval;
    /** Default zone for new players. */
    private defaultZoneId;
    /** Per-zone collision, connections, and respawn caches. */
    private zoneCache;
    onCreate(): void;
    /**
     * Lazily load and cache a zone's collision, connections, and respawn point.
     * Public so admin routes can trigger zone loading for teleports.
     */
    loadZoneCache(zoneId: string): ZoneCacheEntry;
    /**
     * Get the cached zone entry for a player's current zone.
     */
    private getPlayerZone;
    /**
     * Find a safe (non-colliding) position near the given coordinates.
     * Uses a spiral search pattern, expanding outward by one tile at a time.
     * Returns the original position if already safe, or the zone respawn as last resort.
     */
    private findSafeSpawn;
    /**
     * Colyseus auth hook — validates JWT before allowing the client to join.
     * Returned value is stored on client.auth.
     */
    onAuth(client: Client, options: {
        token?: string;
        characterId?: number;
    }): Promise<JwtPayload>;
    onJoin(client: Client, options: {
        token?: string;
        characterId?: number;
    }): void;
    onLeave(client: Client): void;
    /**
     * Compute and apply derived stats from class + level.
     * Call this on level-up and (later) gear changes.
     */
    applyStats(player: PlayerState): void;
    /**
     * Main server update loop — processes inputs, combat, projectiles, respawns, zone transitions.
     */
    private update;
    /**
     * Check if any player has walked into a zone portal trigger rect.
     * On transition: loads the target zone, updates player state, and sends new map data to the client.
     */
    private checkZoneTransitions;
    /**
     * Multi-zone respawn: each dead player respawns at their zone's spawn point.
     */
    private checkRespawnsMultiZone;
    /**
     * Send combat events to all clients.
     */
    private broadcastCombatEvents;
    /**
     * Send spell projectile events to all clients.
     * playerHit/playerDied/npcHit/npcDied use the same message types as combat.
     * spellImpact is a new VFX event the client uses to play the explosion.
     */
    private broadcastSpellProjectileEvents;
    /**
     * Look up an NPC's loot table and spawn a loot bag at its position.
     */
    private spawnNpcLoot;
    /**
     * Send skill system events to relevant clients.
     * Some events go only to the caster, others are broadcast.
     */
    private broadcastSkillEvents;
    /** Send a system chat message to a single client. */
    private sendSystemChat;
    /** Send PARTY_UPDATE to all members of a party. */
    private sendPartyUpdate;
    /**
     * Remove a player from their party. If the party has <2 members, disband.
     * @param silent  If true, skip "X left the party" notifications (used for party-switch on accept)
     */
    private removeFromParty;
    /**
     * Returns true when two players (by sessionId) are in the same party.
     * Used to prevent friendly-fire from AoE skills and spell projectiles.
     */
    private isPartyMember;
    /**
     * Award kill XP to a player (or their party if applicable).
     * Party members in the same zone split a +10% bonus equally.
     */
    private awardKillXP;
    /**
     * Extract current player state and save to database.
     */
    private savePlayer;
    /**
     * Save all connected players. Called periodically by the save interval.
     */
    private saveAllPlayers;
    onDispose(): void;
}
export {};
//# sourceMappingURL=GameRoom.d.ts.map