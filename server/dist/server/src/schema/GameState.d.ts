import { Schema, MapSchema } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';
import { ProjectileState } from './ProjectileState.js';
export declare class GameState extends Schema {
    players: MapSchema<PlayerState>;
    projectiles: MapSchema<ProjectileState>;
    constructor();
}
//# sourceMappingURL=GameState.d.ts.map