import { Schema, MapSchema, defineTypes } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';
import { ProjectileState } from './ProjectileState.js';

export class GameState extends Schema {
  players!: MapSchema<PlayerState>;
  projectiles!: MapSchema<ProjectileState>;

  constructor() {
    super();
    this.players = new MapSchema<PlayerState>();
    this.projectiles = new MapSchema<ProjectileState>();
  }
}

defineTypes(GameState, {
  players: { map: PlayerState },
  projectiles: { map: ProjectileState },
});
