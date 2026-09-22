export declare const DEFAULT_TILE_SIZE = 64;
/** @deprecated Maps now define their own dimensions. Use ParsedMapData.tileSize instead. */
export declare const TILE_SIZE = 64;
/** @deprecated Maps now define their own dimensions. Use ParsedMapData.width instead. */
export declare const MAP_WIDTH_TILES = 64;
/** @deprecated Maps now define their own dimensions. Use ParsedMapData.height instead. */
export declare const MAP_HEIGHT_TILES = 64;
export declare const MAP_WIDTH_PX: number;
export declare const MAP_HEIGHT_PX: number;
export declare const SERVER_TICK_RATE = 60;
export declare const SERVER_TICK_MS: number;
export declare const SERVER_PORT = 2567;
/** @deprecated Use CLASS_TEMPLATES[classId].baseSpeed instead for class-aware speed */
export declare const PLAYER_SPEED = 160;
export declare const PLAYER_SIZE = 48;
export declare const PLAYER_COLLISION_RADIUS = 20;
/** @deprecated Use computeDerivedStats() for class-aware max HP */
export declare const PLAYER_MAX_HP = 100;
export declare const PROJECTILE_SPEED = 400;
export declare const PROJECTILE_RADIUS = 6;
export declare const PROJECTILE_MAX_RANGE = 600;
export declare const PROJECTILE_DAMAGE = 15;
/** @deprecated Auto-attack system now uses weapon/class attack speed + dex scaling. */
export declare const FIRE_COOLDOWN_MS = 300;
export declare const INVULNERABILITY_MS = 500;
export declare const RESPAWN_TIME_MS = 3000;
export declare const MELEE_DAMAGE = 25;
export declare const MELEE_RANGE = 60;
export declare const MELEE_ARC: number;
/** @deprecated Auto-attack system now uses weapon/class attack speed + dex scaling. */
export declare const MELEE_COOLDOWN_MS = 600;
/** Travel speed of the Fireball projectile (px/s) */
export declare const FIREBALL_PROJECTILE_SPEED = 350;
/** Blast radius for Fireball AoE damage (px) */
export declare const FIREBALL_AOE_RADIUS = 96;
/** Collision body radius of the Fireball while in flight (px) */
export declare const FIREBALL_PROJECTILE_RADIUS = 10;
/** Minimum damage multiplier at the outer edge of the blast radius (0–1) */
export declare const FIREBALL_DAMAGE_FALLOFF_MIN = 0.35;
export declare const INTERPOLATION_BUFFER_MS = 100;
export declare const SERVER_URL: string;
export declare const ROOM_NAME = "game_room";
export declare const SAVE_INTERVAL_MS = 30000;
export declare const JWT_EXPIRY = "24h";
export declare const MIN_USERNAME_LENGTH = 3;
export declare const MAX_USERNAME_LENGTH = 20;
export declare const MIN_PASSWORD_LENGTH = 6;
export declare const MAX_CHARACTERS_PER_USER = 4;
//# sourceMappingURL=constants.d.ts.map