import { Schema, defineTypes } from '@colyseus/schema';
import { PLAYER_MAX_HP } from '@valhalla/shared';
export class PlayerState extends Schema {
    constructor() {
        super(...arguments);
        this.id = '';
        this.x = 0;
        this.y = 0;
        this.aimAngle = 0;
        this.speed = 0;
        this.hp = PLAYER_MAX_HP;
        this.maxHp = PLAYER_MAX_HP;
        this.alive = true;
        // Server-only (not synced) — last processed input sequence
        this.inputSeq = 0;
        // Combat cooldowns (server-only, timestamps in ms)
        this.fireCooldown = 0;
        this.meleeCooldown = 0;
        this.invulnerableUntil = 0;
        this.respawnAt = 0;
    }
}
defineTypes(PlayerState, {
    id: 'string',
    x: 'float32',
    y: 'float32',
    aimAngle: 'float32',
    speed: 'float32',
    hp: 'int16',
    maxHp: 'int16',
    alive: 'boolean',
    inputSeq: 'uint32',
});
//# sourceMappingURL=PlayerState.js.map