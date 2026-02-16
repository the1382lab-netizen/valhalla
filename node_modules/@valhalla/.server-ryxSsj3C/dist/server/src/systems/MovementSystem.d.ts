import { InputPayload } from '@valhalla/shared';
import { PlayerState } from '../schema/PlayerState.js';
import { CollisionSystem } from './CollisionSystem.js';
/**
 * Processes player input and updates authoritative positions.
 */
export declare class MovementSystem {
    private collision;
    constructor(collision: CollisionSystem);
    /**
     * Apply a single input to a player, advancing their position by dt seconds.
     */
    processInput(player: PlayerState, input: InputPayload, dt: number): void;
}
//# sourceMappingURL=MovementSystem.d.ts.map