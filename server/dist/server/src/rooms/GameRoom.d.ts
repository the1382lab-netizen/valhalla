import { Room, Client } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
export declare class GameRoom extends Room<{
    state: GameState;
}> {
    private collision;
    private movement;
    private combat;
    private visibility;
    private inputQueues;
    private visibilityAccumulator;
    private visibilityIntervalMs;
    onCreate(): void;
    onJoin(client: Client): void;
    onLeave(client: Client): void;
    /**
     * Main server update loop — processes inputs, combat, projectiles, respawns, visibility.
     */
    private update;
    /**
     * Compute and send visibility polygons to each connected client.
     * Also sends which entities are visible to each player.
     */
    private updateVisibility;
    /**
     * Send combat events to all clients.
     */
    private broadcastCombatEvents;
    onDispose(): void;
}
//# sourceMappingURL=GameRoom.d.ts.map