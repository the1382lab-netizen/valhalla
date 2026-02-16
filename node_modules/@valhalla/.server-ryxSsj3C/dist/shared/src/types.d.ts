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
    /** Fire a ranged projectile */
    fire: boolean;
    /** Melee attack */
    melee: boolean;
}
export interface IPlayerState {
    id: string;
    x: number;
    y: number;
    aimAngle: number;
    speed: number;
    hp: number;
    maxHp: number;
    alive: boolean;
    inputSeq: number;
}
export interface TileMapData {
    width: number;
    height: number;
    tileSize: number;
    /** 1D collision grid: 0 = passable, 1 = blocked */
    collisionGrid: number[];
}
export interface IProjectileState {
    id: string;
    ownerId: string;
    x: number;
    y: number;
    angle: number;
    speed: number;
    damage: number;
}
export declare enum MessageType {
    INPUT = "input",
    PLAYER_HIT = "playerHit",
    PLAYER_DIED = "playerDied",
    PLAYER_RESPAWNED = "playerRespawned",
    MELEE_ATTACK = "meleeAttack"
}
//# sourceMappingURL=types.d.ts.map