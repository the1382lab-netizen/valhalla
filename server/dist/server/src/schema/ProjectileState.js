import { Schema, defineTypes } from '@colyseus/schema';
export class ProjectileState extends Schema {
    constructor() {
        super(...arguments);
        this.id = '';
        this.ownerId = '';
        this.x = 0;
        this.y = 0;
        this.angle = 0;
        this.speed = 0;
        this.damage = 0;
        // Server-only: track distance travelled for max range despawn
        this.distanceTravelled = 0;
    }
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
//# sourceMappingURL=ProjectileState.js.map