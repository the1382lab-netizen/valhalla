import { Schema, MapSchema, defineTypes } from '@colyseus/schema';
import { PlayerState } from './PlayerState.js';

export class GameState extends Schema {
  players!: MapSchema<PlayerState>;

  constructor() {
    super();
    this.players = new MapSchema<PlayerState>();
  }
}

defineTypes(GameState, {
  players: { map: PlayerState },
});
