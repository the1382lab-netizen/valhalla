import { Schema, MapSchema } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';
import { ProjectileState } from './ProjectileState.js';
import { SpellProjectileState } from './SpellProjectileState.js';
import { NPCState } from './NPCState.js';
import { LootBagState } from './LootBagState.js';
export declare class GameState extends Schema {
    players: MapSchema<PlayerState>;
    projectiles: MapSchema<ProjectileState>;
    spellProjectiles: MapSchema<SpellProjectileState>;
    npcs: MapSchema<NPCState>;
    lootBags: MapSchema<LootBagState>;
    constructor();
}
//# sourceMappingURL=GameState.d.ts.map