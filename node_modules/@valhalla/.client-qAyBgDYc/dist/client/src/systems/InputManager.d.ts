import Phaser from 'phaser';
import { InputPayload } from '@valhalla/shared';
/**
 * Captures WASD keyboard input, mouse aim,
 * action bar keys (1-8), and UI toggle keys (K for skills pane).
 *
 * Auto-attack (melee/ranged) is now triggered via the action bar or
 * entity-click handlers in GameScene, which send START_AUTO_ATTACK
 * messages directly. This class no longer queues fire/melee flags.
 */
export declare class InputManager {
    private scene;
    private keys;
    private seq;
    /**
     * Set to true by entity click handlers (e.g. click-to-target) to prevent
     * the same left-click from also triggering auto-attack. Consumed on the
     * next pointerdown event.
     */
    suppressNextClick: boolean;
    /** True on the frame a left-click on the world (non-entity) was detected. */
    leftClickQueued: boolean;
    /** True on the frame a right-click on the world was detected. */
    rightClickQueued: boolean;
    private actionBarKeys;
    /** Callback when an action bar slot key (1-8) is pressed. */
    onActionBarKeyPressed: ((slotIndex: number) => void) | null;
    /** Callback when K is pressed to toggle skills pane. */
    onSkillsPaneToggle: (() => void) | null;
    constructor(scene: Phaser.Scene);
    /**
     * Sample the current input state and return an InputPayload.
     * Note: fire/melee flags removed — auto-attack is handled via messages now.
     */
    getInput(playerWorldX: number, playerWorldY: number): InputPayload;
    /** Drain the click queues without acting on them (call when UI panels block input). */
    clearFire(): void;
    /** Consume and return the left-click state. */
    consumeLeftClick(): boolean;
    /** Consume and return the right-click state. */
    consumeRightClick(): boolean;
    /** Check if any movement key is pressed. */
    isMoving(): boolean;
}
//# sourceMappingURL=InputManager.d.ts.map