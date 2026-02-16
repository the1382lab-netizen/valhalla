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
export const PLAYER_SPEED = 200; // pixels per second
export const PLAYER_SIZE = 48; // sprite size in pixels
export const PLAYER_COLLISION_RADIUS = 20; // collision circle radius
// ── Combat ─────────────────────────────────────────────────
export const PLAYER_MAX_HP = 100;
export const PROJECTILE_SPEED = 400; // pixels per second
export const PROJECTILE_RADIUS = 6;
export const PROJECTILE_MAX_RANGE = 600; // pixels before despawning
export const PROJECTILE_DAMAGE = 15;
export const FIRE_COOLDOWN_MS = 300; // minimum ms between shots
export const INVULNERABILITY_MS = 500; // i-frames after being hit
export const RESPAWN_TIME_MS = 3000; // ms before respawn
export const MELEE_DAMAGE = 25;
export const MELEE_RANGE = 60; // pixels
export const MELEE_ARC = Math.PI / 2; // 90 degree arc
export const MELEE_COOLDOWN_MS = 600;
// ── Visibility / Field of Vision ─────────────────────────
export const VISION_RADIUS = 500; // max sight distance in pixels
export const VISION_CONE_ANGLE = (100 / 180) * Math.PI; // 100° field of vision cone
export const VISIBILITY_UPDATE_RATE = 15; // how many times per second to recompute visibility
export const FOG_EXPLORED_ALPHA = 0.65; // alpha for explored-but-not-visible fog
export const FOG_HIDDEN_ALPHA = 0.95; // alpha for never-seen fog
// ── Network ────────────────────────────────────────────────
export const INTERPOLATION_BUFFER_MS = 100; // ms of interpolation delay for remote entities
// Dynamically resolve the game server URL so LAN clients connect to the right host.
// In a browser, window.location.hostname gives us the IP/hostname used to load the page.
// On the server side (Node), fall back to localhost.
export const SERVER_URL = typeof window !== 'undefined'
    ? `http://${window.location.hostname}:2567`
    : 'http://localhost:2567';
export const ROOM_NAME = 'game_room';
//# sourceMappingURL=constants.js.map