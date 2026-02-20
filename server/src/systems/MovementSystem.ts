import { InputPayload, normalise } from '@valhalla/shared';
import { PlayerState } from '../schema/PlayerState.js';
import { CollisionSystem } from './CollisionSystem.js';

/**
 * Processes player input and updates authoritative positions.
 */
export class MovementSystem {
  private defaultCollision: CollisionSystem;

  constructor(collision: CollisionSystem) {
    this.defaultCollision = collision;
  }

  /**
   * Apply a single input to a player, advancing their position by dt seconds.
   * @param collision Optional override collision system (for multi-zone support).
   */
  processInput(player: PlayerState, input: InputPayload, dt: number, collision?: CollisionSystem): void {
    const col = collision ?? this.defaultCollision;

    // Build direction vector from input flags
    let mx = 0;
    let my = 0;
    if (input.up) my -= 1;
    if (input.down) my += 1;
    if (input.left) mx -= 1;
    if (input.right) mx += 1;

    // Rotate 45° CW so screen-relative WASD maps to orthogonal world directions.
    // In isometric view, "screen up" (W) is actually NW in world space.
    // Must match client's applyInputLocally exactly.
    const isoMx = mx + my;
    const isoMy = -mx + my;

    const dir = normalise(isoMx, isoMy);
    const dx = dir.x * player.speed * dt;
    const dy = dir.y * player.speed * dt;

    if (dx !== 0 || dy !== 0) {
      const resolved = col.resolveMovement(player.x, player.y, dx, dy);
      const clamped = col.clampToMap(resolved.x, resolved.y);
      player.x = clamped.x;
      player.y = clamped.y;
    }

    // Always update aim angle
    player.aimAngle = input.aimAngle;
    player.inputSeq = input.seq;
  }
}
