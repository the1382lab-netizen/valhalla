import { Room, Client } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
import { PlayerState } from '../schema/PlayerState.js';
import { JwtPayload } from '../services/AuthService.js';
export declare class GameRoom extends Room<{
    state: GameState;
}> {
    private collision;
    private movement;
    private combat;
    private mapManager;
    private inputQueues;
    /** Maps sessionId → persistent character data for save/load. */
    private sessionData;
    /** Interval handle for periodic saves. */
    private saveInterval;
    /** Current zone ID and spawn point for this room. */
    private currentZoneId;
    private respawnPoint;
    /** Zone connections for portal detection. */
    private zoneConnections;
    onCreate(): void;
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
     */
    private checkZoneTransitions;
    /**
     * Send combat events to all clients.
     */
    private broadcastCombatEvents;
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
//# sourceMappingURL=GameRoom.d.ts.map