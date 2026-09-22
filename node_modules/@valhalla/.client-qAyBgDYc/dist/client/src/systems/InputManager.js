import Phaser from 'phaser';
import { isoToOrtho } from '@valhalla/shared';
/**
 * Captures WASD keyboard input, mouse aim,
 * action bar keys (1-8), and UI toggle keys (K for skills pane).
 *
 * Auto-attack (melee/ranged) is now triggered via the action bar or
 * entity-click handlers in GameScene, which send START_AUTO_ATTACK
 * messages directly. This class no longer queues fire/melee flags.
 */
export class InputManager {
    scene;
    keys;
    seq = 0;
    /**
     * Set to true by entity click handlers (e.g. click-to-target) to prevent
     * the same left-click from also triggering auto-attack. Consumed on the
     * next pointerdown event.
     */
    suppressNextClick = false;
    /** True on the frame a left-click on the world (non-entity) was detected. */
    leftClickQueued = false;
    /** True on the frame a right-click on the world was detected. */
    rightClickQueued = false;
    // Action bar keys (1-8)
    actionBarKeys = [];
    /** Callback when an action bar slot key (1-8) is pressed. */
    onActionBarKeyPressed = null;
    /** Callback when K is pressed to toggle skills pane. */
    onSkillsPaneToggle = null;
    constructor(scene) {
        this.scene = scene;
        const kb = scene.input.keyboard;
        this.keys = {
            W: kb.addKey(Phaser.Input.Keyboard.KeyCodes.W),
            A: kb.addKey(Phaser.Input.Keyboard.KeyCodes.A),
            S: kb.addKey(Phaser.Input.Keyboard.KeyCodes.S),
            D: kb.addKey(Phaser.Input.Keyboard.KeyCodes.D),
            K: kb.addKey(Phaser.Input.Keyboard.KeyCodes.K),
        };
        // Prevent browser context menu on right-click so the game can handle it
        scene.input.mouse?.disableContextMenu();
        // Action bar: keys 1-8
        const numKeyCodes = [
            Phaser.Input.Keyboard.KeyCodes.ONE,
            Phaser.Input.Keyboard.KeyCodes.TWO,
            Phaser.Input.Keyboard.KeyCodes.THREE,
            Phaser.Input.Keyboard.KeyCodes.FOUR,
            Phaser.Input.Keyboard.KeyCodes.FIVE,
            Phaser.Input.Keyboard.KeyCodes.SIX,
            Phaser.Input.Keyboard.KeyCodes.SEVEN,
            Phaser.Input.Keyboard.KeyCodes.EIGHT,
        ];
        for (let i = 0; i < numKeyCodes.length; i++) {
            const key = kb.addKey(numKeyCodes[i]);
            this.actionBarKeys.push(key);
            key.on('down', () => {
                this.onActionBarKeyPressed?.(i);
            });
        }
        // K key for skills pane
        this.keys.K.on('down', () => {
            this.onSkillsPaneToggle?.();
        });
        // Left-click and right-click queue for auto-attack initiation
        // (GameScene reads these to decide whether to start auto-attack on a target)
        scene.input.on('pointerdown', (pointer) => {
            if (pointer.leftButtonDown()) {
                if (this.suppressNextClick) {
                    this.suppressNextClick = false;
                }
                else {
                    this.leftClickQueued = true;
                }
            }
            else if (pointer.rightButtonDown()) {
                this.rightClickQueued = true;
            }
        });
    }
    /**
     * Sample the current input state and return an InputPayload.
     * Note: fire/melee flags removed — auto-attack is handled via messages now.
     */
    getInput(playerWorldX, playerWorldY) {
        const pointer = this.scene.input.activePointer;
        // Convert pointer screen position to world position (ISO screen space),
        // then convert to orthogonal world space for aimAngle calculation.
        const worldPoint = this.scene.cameras.main.getWorldPoint(pointer.x, pointer.y);
        const orthoMouse = isoToOrtho(worldPoint.x, worldPoint.y);
        const aimAngle = Math.atan2(orthoMouse.y - playerWorldY, orthoMouse.x - playerWorldX);
        return {
            up: this.keys.W.isDown,
            down: this.keys.S.isDown,
            left: this.keys.A.isDown,
            right: this.keys.D.isDown,
            aimAngle,
            seq: ++this.seq,
        };
    }
    /** Drain the click queues without acting on them (call when UI panels block input). */
    clearFire() {
        this.leftClickQueued = false;
        this.rightClickQueued = false;
    }
    /** Consume and return the left-click state. */
    consumeLeftClick() {
        const val = this.leftClickQueued;
        this.leftClickQueued = false;
        return val;
    }
    /** Consume and return the right-click state. */
    consumeRightClick() {
        const val = this.rightClickQueued;
        this.rightClickQueued = false;
        return val;
    }
    /** Check if any movement key is pressed. */
    isMoving() {
        return this.keys.W.isDown || this.keys.A.isDown || this.keys.S.isDown || this.keys.D.isDown;
    }
}
//# sourceMappingURL=InputManager.js.map