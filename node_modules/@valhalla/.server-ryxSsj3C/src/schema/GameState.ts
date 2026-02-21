import { Schema, MapSchema, defineTypes } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';
import { ProjectileState } from './ProjectileState.js';
import { SpellProjectileState } from './SpellProjectileState.js';
import { NPCState } from './NPCState.js';
import { LootBagState } from './LootBagState.js';

export class GameState extends Schema {
  players!: MapSchema<PlayerState>;
  projectiles!: MapSchema<ProjectileState>;
  spellProjectiles!: MapSchema<SpellProjectileState>;
  npcs!: MapSchema<NPCState>;
  lootBags!: MapSchema<LootBagState>;

  constructor() {
    super();
    this.players = new MapSchema<PlayerState>();
    this.projectiles = new MapSchema<ProjectileState>();
    this.spellProjectiles = new MapSchema<SpellProjectileState>();
    this.npcs = new MapSchema<NPCState>();
    this.lootBags = new MapSchema<LootBagState>();
  }
}

defineTypes(GameState, {
  players: { map: PlayerState },
  projectiles: { map: ProjectileState },
  spellProjectiles: { map: SpellProjectileState },
  npcs: { map: NPCState },
  lootBags: { map: LootBagState },
});
