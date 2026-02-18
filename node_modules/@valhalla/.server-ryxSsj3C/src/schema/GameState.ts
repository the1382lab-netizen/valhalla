import { Schema, MapSchema, defineTypes } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';
import { ProjectileState } from './ProjectileState.js';
import { NPCState } from './NPCState.js';

export class GameState extends Schema {
  players!: MapSchema<PlayerState>;
  projectiles!: MapSchema<ProjectileState>;
  npcs!: MapSchema<NPCState>;

  constructor() {
    super();
    this.players = new MapSchema<PlayerState>();
    this.projectiles = new MapSchema<ProjectileState>();
    this.npcs = new MapSchema<NPCState>();
  }
}

defineTypes(GameState, {
  players: { map: PlayerState },
  projectiles: { map: ProjectileState },
  npcs: { map: NPCState },
});
