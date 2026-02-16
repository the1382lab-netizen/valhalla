import { Schema } from '@colyseus/schema';
export declare class PlayerState extends Schema {
    id: string;
    x: number;
    y: number;
    aimAngle: number;
    speed: number;
    hp: number;
    maxHp: number;
    alive: boolean;
    inputSeq: number;
    fireCooldown: number;
    meleeCooldown: number;
    invulnerableUntil: number;
    respawnAt: number;
}
//# sourceMappingURL=PlayerState.d.ts.map