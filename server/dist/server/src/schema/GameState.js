import { Schema, MapSchema, defineTypes } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';
import { ProjectileState } from './ProjectileState.js';
import { SpellProjectileState } from './SpellProjectileState.js';
import { NPCState } from './NPCState.js';
import { LootBagState } from './LootBagState.js';
export class GameState extends Schema {
    constructor() {
        super();
        this.players = new MapSchema();
        this.projectiles = new MapSchema();
        this.spellProjectiles = new MapSchema();
        this.npcs = new MapSchema();
        this.lootBags = new MapSchema();
    }
}
defineTypes(GameState, {
    players: { map: PlayerState },
    projectiles: { map: ProjectileState },
    spellProjectiles: { map: SpellProjectileState },
    npcs: { map: NPCState },
    lootBags: { map: LootBagState },
});
//# sourceMappingURL=GameState.js.map