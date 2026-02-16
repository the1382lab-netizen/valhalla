import { Room } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
import { PlayerState } from '../schema/PlayerState.js';
import { CollisionSystem } from '../systems/CollisionSystem.js';
import { MovementSystem } from '../systems/MovementSystem.js';
import { CombatSystem } from '../systems/CombatSystem.js';
import { VisibilitySystem } from '../systems/VisibilitySystem.js';
import { MessageType, TILE_SIZE, SERVER_TICK_RATE, PLAYER_SPEED, PLAYER_MAX_HP, VISION_RADIUS, VISIBILITY_UPDATE_RATE, PLAYER_COLLISION_RADIUS, } from '@valhalla/shared';
export class GameRoom extends Room {
    constructor() {
        super(...arguments);
        this.inputQueues = new Map();
        // Visibility update throttle
        this.visibilityAccumulator = 0;
        this.visibilityIntervalMs = 1000 / VISIBILITY_UPDATE_RATE;
    }
    onCreate() {
        this.setState(new GameState());
        this.collision = new CollisionSystem();
        this.movement = new MovementSystem(this.collision);
        this.combat = new CombatSystem(this.collision);
        this.visibility = new VisibilitySystem(this.collision.getGrid());
        // Set the simulation interval (server tick)
        this.setSimulationInterval((dt) => this.update(dt), 1000 / SERVER_TICK_RATE);
        // Listen for player input messages
        this.onMessage(MessageType.INPUT, (client, input) => {
            const queue = this.inputQueues.get(client.sessionId);
            if (queue) {
                // Cap the queue to prevent flooding
                if (queue.length < 30) {
                    queue.push(input);
                }
            }
        });
        console.log(`[GameRoom] Room created. Tick rate: ${SERVER_TICK_RATE}Hz, Vis rate: ${VISIBILITY_UPDATE_RATE}Hz`);
    }
    onJoin(client) {
        console.log(`[GameRoom] Player joined: ${client.sessionId}`);
        const player = new PlayerState();
        player.id = client.sessionId;
        // Spawn in the middle of the map (avoiding walls)
        player.x = 5 * TILE_SIZE + TILE_SIZE / 2;
        player.y = 5 * TILE_SIZE + TILE_SIZE / 2;
        player.speed = PLAYER_SPEED;
        player.hp = PLAYER_MAX_HP;
        player.maxHp = PLAYER_MAX_HP;
        player.alive = true;
        this.state.players.set(client.sessionId, player);
        this.inputQueues.set(client.sessionId, []);
        // Send collision grid to the client so it can do client-side prediction
        client.send('collisionGrid', {
            grid: this.collision.getGrid(),
            width: 32,
            height: 32,
            tileSize: TILE_SIZE,
        });
        // Send initial visibility polygon (facing right by default)
        const polygon = this.visibility.computeVisibilityPolygon(player.x, player.y, player.aimAngle, VISION_RADIUS);
        client.send('visibility', { polygon, visiblePlayers: [], visibleProjectiles: [] });
    }
    onLeave(client) {
        console.log(`[GameRoom] Player left: ${client.sessionId}`);
        this.state.players.delete(client.sessionId);
        this.inputQueues.delete(client.sessionId);
    }
    /**
     * Main server update loop — processes inputs, combat, projectiles, respawns, visibility.
     */
    update(dt) {
        const dtSec = dt / 1000;
        const now = Date.now();
        // 1. Process all queued inputs for every player
        this.state.players.forEach((player, sessionId) => {
            const queue = this.inputQueues.get(sessionId);
            if (!queue || queue.length === 0)
                return;
            for (const input of queue) {
                // Only process movement if alive
                if (player.alive) {
                    this.movement.processInput(player, input, dtSec);
                }
                // Handle fire input
                if (input.fire && player.alive) {
                    const proj = this.combat.tryFire(player, now);
                    if (proj) {
                        this.state.projectiles.set(proj.id, proj);
                    }
                }
                // Handle melee input
                if (input.melee && player.alive) {
                    const events = this.combat.tryMelee(player, this.state.players, now);
                    this.broadcastCombatEvents(events);
                }
            }
            // Clear the queue
            queue.length = 0;
        });
        // 2. Update projectiles (movement + collision)
        const { toRemove, events } = this.combat.updateProjectiles(this.state.projectiles, this.state.players, dtSec, now);
        // Remove destroyed projectiles
        for (const id of toRemove) {
            this.state.projectiles.delete(id);
        }
        // Broadcast combat events (hits, kills)
        this.broadcastCombatEvents(events);
        // 3. Check respawns
        const respawnEvents = this.combat.checkRespawns(this.state.players, now);
        this.broadcastCombatEvents(respawnEvents);
        // 4. Visibility updates (throttled to VISIBILITY_UPDATE_RATE)
        this.visibilityAccumulator += dt;
        if (this.visibilityAccumulator >= this.visibilityIntervalMs) {
            this.visibilityAccumulator -= this.visibilityIntervalMs;
            this.updateVisibility();
        }
    }
    /**
     * Compute and send visibility polygons to each connected client.
     * Also sends which entities are visible to each player.
     */
    updateVisibility() {
        this.state.players.forEach((player, sessionId) => {
            if (!player.alive)
                return;
            // Compute visibility polygon for this player (cone follows aim direction)
            const polygon = this.visibility.computeVisibilityPolygon(player.x, player.y, player.aimAngle, VISION_RADIUS);
            // Determine which other players are visible
            const visiblePlayers = [];
            this.state.players.forEach((other, otherId) => {
                if (otherId === sessionId)
                    return; // always see yourself
                if (!other.alive)
                    return;
                if (this.visibility.isCircleVisible(polygon, other.x, other.y, PLAYER_COLLISION_RADIUS)) {
                    visiblePlayers.push(otherId);
                }
            });
            // Determine which projectiles are visible
            const visibleProjectiles = [];
            this.state.projectiles.forEach((proj, projId) => {
                if (this.visibility.isCircleVisible(polygon, proj.x, proj.y, 6)) {
                    visibleProjectiles.push(projId);
                }
            });
            // Send visibility data to the client
            const client = this.clients.find((c) => c.sessionId === sessionId);
            if (client) {
                client.send('visibility', {
                    polygon,
                    visiblePlayers,
                    visibleProjectiles,
                });
            }
        });
    }
    /**
     * Send combat events to all clients.
     */
    broadcastCombatEvents(events) {
        for (const event of events) {
            this.broadcast(event.type, event.data);
        }
    }
    onDispose() {
        console.log('[GameRoom] Room disposed.');
    }
}
//# sourceMappingURL=GameRoom.js.map