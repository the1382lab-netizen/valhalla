import { InputPayload, PLAYER_SPEED, normalise } from '@valhalla/shared';
import { PlayerState } from '../schema/PlayerState.js';
import { CollisionSystem } from './CollisionSystem.js';

/**
 * Processes player input and updates authoritative positions.
 */
export class MovementSystem {
  private collision: CollisionSystem;

  constructor(collision: CollisionSystem) {
    this.collision = collision;
  }

  /**
   * Apply a single input to a player, advancing their position by dt seconds.
   */
  processInput(player: PlayerState, input: InputPayload, dt: number): void {
    // Build direction vector from input flags
    let mx = 0;
    let my = 0;
    if (input.up) my -= 1;
    if (input.down) my += 1;
    if (input.left) mx -= 1;
    if (input.right) mx += 1;

    const dir = normalise(mx, my);
    const dx = dir.x * PLAYER_SPEED * dt;
    const dy = dir.y * PLAYER_SPEED * dt;

    if (dx !== 0 || dy !== 0) {
      const resolved = this.collision.resolveMovement(player.x, player.y, dx, dy);
      const clamped = this.collision.clampToMap(resolved.x, resolved.y);
      player.x = clamped.x;
      player.y = clamped.y;
    }

    // Always update aim angle
    player.aimAngle = input.aimAngle;
    player.inputSeq = input.seq;
  }
}
