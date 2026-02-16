import { Schema, defineTypes } from '@colyseus/schema';

export class ProjectileState extends Schema {
  id: string = '';
  ownerId: string = '';
  x: number = 0;
  y: number = 0;
  angle: number = 0;
  speed: number = 0;
  damage: number = 0;

  // Server-only: track distance travelled for max range despawn
  distanceTravelled: number = 0;
}

defineTypes(ProjectileState, {
  id: 'string',
  ownerId: 'string',
  x: 'float32',
  y: 'float32',
  angle: 'float32',
  speed: 'float32',
  damage: 'float32',
});
