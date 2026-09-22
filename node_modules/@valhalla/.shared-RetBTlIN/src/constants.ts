// ── World & Tiles ──────────────────────────────────────────
export const DEFAULT_TILE_SIZE = 64;
/** @deprecated Maps now define their own dimensions. Use ParsedMapData.tileSize instead. */
export const TILE_SIZE = 64;
/** @deprecated Maps now define their own dimensions. Use ParsedMapData.width instead. */
export const MAP_WIDTH_TILES = 64;
/** @deprecated Maps now define their own dimensions. Use ParsedMapData.height instead. */
export const MAP_HEIGHT_TILES = 64;
export const MAP_WIDTH_PX = MAP_WIDTH_TILES * TILE_SIZE;
export const MAP_HEIGHT_PX = MAP_HEIGHT_TILES * TILE_SIZE;

// ── Server ─────────────────────────────────────────────────
export const SERVER_TICK_RATE = 60;
export const SERVER_TICK_MS = 1000 / SERVER_TICK_RATE;
export const SERVER_PORT = 2567;

// ── Player ─────────────────────────────────────────────────
/** @deprecated Use CLASS_TEMPLATES[classId].baseSpeed instead for class-aware speed */
export const PLAYER_SPEED = 160;          // pixels per second (legacy fallback)
export const PLAYER_SIZE = 48;            // sprite size in pixels
export const PLAYER_COLLISION_RADIUS = 20; // collision circle radius

// ── Combat ─────────────────────────────────────────────────
/** @deprecated Use computeDerivedStats() for class-aware max HP */
export const PLAYER_MAX_HP = 100;         // legacy fallback
export const PROJECTILE_SPEED = 400;        // pixels per second
export const PROJECTILE_RADIUS = 6;
export const PROJECTILE_MAX_RANGE = 600;    // pixels before despawning
export const PROJECTILE_DAMAGE = 15;
/** @deprecated Auto-attack system now uses weapon/class attack speed + dex scaling. */
export const FIRE_COOLDOWN_MS = 300;        // minimum ms between shots
export const INVULNERABILITY_MS = 500;      // i-frames after being hit
export const RESPAWN_TIME_MS = 3000;        // ms before respawn
export const MELEE_DAMAGE = 25;
export const MELEE_RANGE = 60;              // pixels
export const MELEE_ARC = Math.PI / 2;       // 90 degree arc
/** @deprecated Auto-attack system now uses weapon/class attack speed + dex scaling. */
export const MELEE_COOLDOWN_MS = 600;

// ── Spell Projectiles ───────────────────────────────────────
/** Travel speed of the Fireball projectile (px/s) */
export const FIREBALL_PROJECTILE_SPEED = 350;
/** Blast radius for Fireball AoE damage (px) */
export const FIREBALL_AOE_RADIUS = 96;
/** Collision body radius of the Fireball while in flight (px) */
export const FIREBALL_PROJECTILE_RADIUS = 10;
/** Minimum damage multiplier at the outer edge of the blast radius (0–1) */
export const FIREBALL_DAMAGE_FALLOFF_MIN = 0.35;

// ── Network ────────────────────────────────────────────────
export const INTERPOLATION_BUFFER_MS = 100; // ms of interpolation delay for remote entities
// Dynamically resolve the game server URL so LAN clients connect to the right host.
// In a browser, window.location.hostname gives us the IP/hostname used to load the page.
// On the server side (Node), fall back to localhost.
export const SERVER_URL =
  typeof window !== 'undefined'
    ? `http://${window.location.hostname}:2567`
    : 'http://localhost:2567';
export const ROOM_NAME = 'game_room';

// ── Persistence & Auth ────────────────────────────────────
export const SAVE_INTERVAL_MS = 30_000;    // auto-save every 30 seconds
export const JWT_EXPIRY = '24h';           // token lifetime
export const MIN_USERNAME_LENGTH = 3;
export const MAX_USERNAME_LENGTH = 20;
export const MIN_PASSWORD_LENGTH = 6;
export const MAX_CHARACTERS_PER_USER = 4;
