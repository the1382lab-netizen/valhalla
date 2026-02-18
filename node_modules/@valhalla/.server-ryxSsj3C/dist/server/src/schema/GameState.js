import { Schema, MapSchema, defineTypes } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';
import { ProjectileState } from './ProjectileState.js';
import { NPCState } from './NPCState.js';
export class GameState extends Schema {
    constructor() {
        super();
        this.players = new MapSchema();
        this.projectiles = new MapSchema();
        this.npcs = new MapSchema();
    }
}
defineTypes(GameState, {
    players: { map: PlayerState },
    projectiles: { map: ProjectileState },
    npcs: { map: NPCState },
});
//# sourceMappingURL=GameState.js.map