import Phaser from 'phaser';
import { InputPayload } from '@valhalla/shared';
/**
 * Captures WASD keyboard input, mouse aim, and combat inputs (fire/melee).
 */
export declare class InputManager {
    private scene;
    private keys;
    private seq;
    private fireQueued;
    private meleeQueued;
    constructor(scene: Phaser.Scene);
    /**
     * Sample the current input state and return an InputPayload.
     */
    getInput(playerWorldX: number, playerWorldY: number): InputPayload;
    /** Check if any movement key is pressed. */
    isMoving(): boolean;
}
//# sourceMappingURL=InputManager.d.ts.map