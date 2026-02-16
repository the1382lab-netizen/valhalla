// ── World & Tiles ──────────────────────────────────────────
export const TILE_SIZE = 64;
export const MAP_WIDTH_TILES = 32;
export const MAP_HEIGHT_TILES = 32;
export const MAP_WIDTH_PX = MAP_WIDTH_TILES * TILE_SIZE;
export const MAP_HEIGHT_PX = MAP_HEIGHT_TILES * TILE_SIZE;

// ── Server ─────────────────────────────────────────────────
export const SERVER_TICK_RATE = 60;
export const SERVER_TICK_MS = 1000 / SERVER_TICK_RATE;
export const SERVER_PORT = 2567;

// ── Player ─────────────────────────────────────────────────
export const PLAYER_SPEED = 200;          // pixels per second
export const PLAYER_SIZE = 48;            // sprite size in pixels
export const PLAYER_COLLISION_RADIUS = 20; // collision circle radius

// ── Network ────────────────────────────────────────────────
export const INTERPOLATION_BUFFER_MS = 100; // ms of interpolation delay for remote entities
export const SERVER_URL = 'http://localhost:2567';
export const ROOM_NAME = 'game_room';
