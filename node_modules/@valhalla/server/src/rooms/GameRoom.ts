import { Room, Client } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
import { PlayerState } from '../schema/PlayerState.js';
import { CollisionSystem } from '../systems/CollisionSystem.js';
import { MovementSystem } from '../systems/MovementSystem.js';
import { CombatSystem, CombatEvent } from '../systems/CombatSystem.js';
import {
  InputPayload,
  MessageType,
  TILE_SIZE,
  SERVER_TICK_RATE,
  PLAYER_SPEED,
  PLAYER_MAX_HP,
} from '@valhalla/shared';

export class GameRoom extends Room<{ state: GameState }> {
  private collision!: CollisionSystem;
  private movement!: MovementSystem;
  private combat!: CombatSystem;
  private inputQueues: Map<string, InputPayload[]> = new Map();

  onCreate(): void {
    this.setState(new GameState());
    this.collision = new CollisionSystem();
    this.movement = new MovementSystem(this.collision);
    this.combat = new CombatSystem(this.collision);

    // Set the simulation interval (server tick)
    this.setSimulationInterval((dt) => this.update(dt), 1000 / SERVER_TICK_RATE);

    // Listen for player input messages
    this.onMessage(MessageType.INPUT, (client: Client, input: InputPayload) => {
      const queue = this.inputQueues.get(client.sessionId);
      if (queue) {
        // Cap the queue to prevent flooding
        if (queue.length < 30) {
          queue.push(input);
        }
      }
    });

    console.log(`[GameRoom] Room created. Tick rate: ${SERVER_TICK_RATE}Hz`);
  }

  onJoin(client: Client): void {
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
  }

  onLeave(client: Client): void {
    console.log(`[GameRoom] Player left: ${client.sessionId}`);
    this.state.players.delete(client.sessionId);
    this.inputQueues.delete(client.sessionId);
  }

  /**
   * Main server update loop — processes inputs, combat, projectiles, respawns.
   */
  private update(dt: number): void {
    const dtSec = dt / 1000;
    const now = Date.now();

    // 1. Process all queued inputs for every player
    this.state.players.forEach((player, sessionId) => {
      const queue = this.inputQueues.get(sessionId);
      if (!queue || queue.length === 0) return;

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
    const { toRemove, events } = this.combat.updateProjectiles(
      this.state.projectiles,
      this.state.players,
      dtSec,
      now,
    );

    // Remove destroyed projectiles
    for (const id of toRemove) {
      this.state.projectiles.delete(id);
    }

    // Broadcast combat events (hits, kills)
    this.broadcastCombatEvents(events);

    // 3. Check respawns
    const respawnEvents = this.combat.checkRespawns(this.state.players, now);
    this.broadcastCombatEvents(respawnEvents);
  }

  /**
   * Send combat events to all clients.
   */
  private broadcastCombatEvents(events: CombatEvent[]): void {
    for (const event of events) {
      this.broadcast(event.type, event.data);
    }
  }

  onDispose(): void {
    console.log('[GameRoom] Room disposed.');
  }
}
