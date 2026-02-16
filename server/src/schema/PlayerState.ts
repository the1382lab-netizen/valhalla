import { Schema, defineTypes } from '@colyseus/schema';
import { PLAYER_MAX_HP } from '@valhalla/shared';

export class PlayerState extends Schema {
  id: string = '';
  x: number = 0;
  y: number = 0;
  aimAngle: number = 0;
  speed: number = 0;
  hp: number = PLAYER_MAX_HP;
  maxHp: number = PLAYER_MAX_HP;
  alive: boolean = true;

  // Server-only (not synced) — last processed input sequence
  inputSeq: number = 0;
  // Combat cooldowns (server-only, timestamps in ms)
  fireCooldown: number = 0;
  meleeCooldown: number = 0;
  invulnerableUntil: number = 0;
  respawnAt: number = 0;
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
