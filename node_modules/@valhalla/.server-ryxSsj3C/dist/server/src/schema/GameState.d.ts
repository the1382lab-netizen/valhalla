import { Schema, MapSchema } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';
import { ProjectileState } from './ProjectileState.js';
import { NPCState } from './NPCState.js';
export declare class GameState extends Schema {
    players: MapSchema<PlayerState>;
    projectiles: MapSchema<ProjectileState>;
    npcs: MapSchema<NPCState>;
    constructor();
}
//# sourceMappingURL=GameState.d.ts.map