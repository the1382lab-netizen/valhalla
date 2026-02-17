import {
  TILE_SIZE,
  MAP_WIDTH_TILES,
  MAP_HEIGHT_TILES,
  MAP_WIDTH_PX,
  MAP_HEIGHT_PX,
  PLAYER_COLLISION_RADIUS,
} from '@valhalla/shared';

/**
 * Server-side collision system.
 * Loads a collision grid and provides circle-vs-tile collision resolution.
 */
export class CollisionSystem {
  private grid: number[];
  private mapW: number;
  private mapH: number;

  constructor() {
    this.mapW = MAP_WIDTH_TILES;
    this.mapH = MAP_HEIGHT_TILES;
    this.grid = this.buildDefaultCollisionGrid();
  }

  /**
   * Build a simple collision grid: walls around the perimeter,
   * plus a few interior walls for testing.
   */
  private buildDefaultCollisionGrid(): number[] {
    const g = new Array(this.mapW * this.mapH).fill(0);

    for (let x = 0; x < this.mapW; x++) {
      for (let y = 0; y < this.mapH; y++) {
        // Perimeter walls
        if (x === 0 || x === this.mapW - 1 || y === 0 || y === this.mapH - 1) {
          g[y * this.mapW + x] = 1;
        }
      }
    }

    // A few interior walls for testing
    // Horizontal wall
    for (let x = 5; x <= 12; x++) {
      g[10 * this.mapW + x] = 1;
    }
    // Vertical wall
    for (let y = 14; y <= 22; y++) {
      g[y * this.mapW + 20] = 1;
    }
    // Small room
    for (let x = 24; x <= 28; x++) {
      g[5 * this.mapW + x] = 1;
      g[9 * this.mapW + x] = 1;
    }
    for (let y = 5; y <= 9; y++) {
      g[y * this.mapW + 24] = 1;
      g[y * this.mapW + 28] = 1;
    }
    // Doorway
    g[7 * this.mapW + 24] = 0;

    return g;
  }

  /** Get the collision grid (for sharing with clients). */
  getGrid(): number[] {
    return this.grid;
  }

  /**
   * Check if a circle at (cx, cy) with the given radius overlaps any wall tile.
   * Uses AABB of the circle to determine which tiles to check, then
   * does circle-vs-AABB for each wall tile.
   */
  isCircleBlocked(cx: number, cy: number, radius: number = PLAYER_COLLISION_RADIUS): boolean {
    const minTX = Math.floor((cx - radius) / TILE_SIZE);
    const maxTX = Math.floor((cx + radius) / TILE_SIZE);
    const minTY = Math.floor((cy - radius) / TILE_SIZE);
    const maxTY = Math.floor((cy + radius) / TILE_SIZE);

    for (let ty = minTY; ty <= maxTY; ty++) {
      for (let tx = minTX; tx <= maxTX; tx++) {
        if (tx < 0 || tx >= this.mapW || ty < 0 || ty >= this.mapH) return true;
        if (this.grid[ty * this.mapW + tx] !== 1) continue;

        // Circle vs AABB check
        const tileLeft = tx * TILE_SIZE;
        const tileTop = ty * TILE_SIZE;
        const tileRight = tileLeft + TILE_SIZE;
        const tileBottom = tileTop + TILE_SIZE;

        const closestX = Math.max(tileLeft, Math.min(cx, tileRight));
        const closestY = Math.max(tileTop, Math.min(cy, tileBottom));

        const dx = cx - closestX;
        const dy = cy - closestY;
        if (dx * dx + dy * dy < radius * radius) return true;
      }
    }
    return false;
  }

  /**
   * Try to move from (x,y) by (dx,dy). Resolves collisions per-axis
   * so the player slides along walls instead of stopping dead.
   */
  resolveMovement(
    x: number,
    y: number,
    dx: number,
    dy: number,
    radius: number = PLAYER_COLLISION_RADIUS,
  ): { x: number; y: number } {
    let newX = x + dx;
    let newY = y + dy;

    // Try full movement
    if (!this.isCircleBlocked(newX, newY, radius)) {
      return { x: newX, y: newY };
    }

    // Try X only
    newX = x + dx;
    newY = y;
    const canMoveX = !this.isCircleBlocked(newX, newY, radius);

    // Try Y only
    newX = x;
    newY = y + dy;
    const canMoveY = !this.isCircleBlocked(newX, newY, radius);

    if (canMoveX && canMoveY) {
      // Both axes work individually — prefer the one with larger movement
      if (Math.abs(dx) >= Math.abs(dy)) {
        return { x: x + dx, y };
      }
      return { x, y: y + dy };
    }
    if (canMoveX) return { x: x + dx, y };
    if (canMoveY) return { x, y: y + dy };

    return { x, y }; // stuck
  }

  /**
   * Clamp a position to be within the map bounds (accounting for player radius).
   */
  clampToMap(x: number, y: number, radius: number = PLAYER_COLLISION_RADIUS): { x: number; y: number } {
    return {
      x: Math.max(radius, Math.min(MAP_WIDTH_PX - radius, x)),
      y: Math.max(radius, Math.min(MAP_HEIGHT_PX - radius, y)),
    };
  }
}
