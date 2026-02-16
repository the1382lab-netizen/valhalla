import { Room, Client } from '@colyseus/core';
import { GameState } from '../schema/GameState.js';
import { PlayerState } from '../schema/PlayerState.js';
import { CollisionSystem } from '../systems/CollisionSystem.js';
import { MovementSystem } from '../systems/MovementSystem.js';
import {
  InputPayload,
  MessageType,
  TILE_SIZE,
  SERVER_TICK_RATE,
  PLAYER_SPEED,
} from '@valhalla/shared';

export class GameRoom extends Room<{ state: GameState }> {
  private collision!: CollisionSystem;
  private movement!: MovementSystem;
  private inputQueues: Map<string, InputPayload[]> = new Map();

  onCreate(): void {
    this.setState(new GameState());
    this.collision = new CollisionSystem();
    this.movement = new MovementSystem(this.collision);

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
   * Main server update loop — processes all queued inputs for every player.
   */
  private update(dt: number): void {
    const dtSec = dt / 1000;

    this.state.players.forEach((player, sessionId) => {
      const queue = this.inputQueues.get(sessionId);
      if (!queue || queue.length === 0) return;

      // Process all queued inputs for this tick
      for (const input of queue) {
        this.movement.processInput(player, input, dtSec);
      }

      // Clear the queue
      queue.length = 0;
    });
  }

  onDispose(): void {
    console.log('[GameRoom] Room disposed.');
  }
}
