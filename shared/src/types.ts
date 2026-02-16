// ── Input ───────────────────────────────────────────────────
export interface InputPayload {
  /** Movement direction flags */
  up: boolean;
  down: boolean;
  left: boolean;
  right: boolean;
  /** Aim angle in radians (0 = right, PI/2 = down) */
  aimAngle: number;
  /** Monotonically increasing input sequence number */
  seq: number;
}

// ── Player State (mirrored in Colyseus schema) ─────────────
export interface IPlayerState {
  id: string;
  x: number;
  y: number;
  aimAngle: number;
  speed: number;
}

// ── Tile Map ────────────────────────────────────────────────
export interface TileMapData {
  width: number;
  height: number;
  tileSize: number;
  /** 1D collision grid: 0 = passable, 1 = blocked */
  collisionGrid: number[];
}

// ── Messages ────────────────────────────────────────────────
export enum MessageType {
  INPUT = 'input',
}
