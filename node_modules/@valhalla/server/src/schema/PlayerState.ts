import { Schema, defineTypes } from '@colyseus/schema';

export class PlayerState extends Schema {
  id: string = '';
  x: number = 0;
  y: number = 0;
  aimAngle: number = 0;
  speed: number = 0;

  // Server-only (not synced) — last processed input sequence
  inputSeq: number = 0;
}

defineTypes(PlayerState, {
  id: 'string',
  x: 'float32',
  y: 'float32',
  aimAngle: 'float32',
  speed: 'float32',
});
