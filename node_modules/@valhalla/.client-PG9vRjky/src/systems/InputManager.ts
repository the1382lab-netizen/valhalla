import Phaser from 'phaser';
import { InputPayload } from '@valhalla/shared';

/**
 * Captures WASD keyboard input, mouse aim, and combat inputs (fire/melee).
 */
export class InputManager {
  private scene: Phaser.Scene;
  private keys: {
    W: Phaser.Input.Keyboard.Key;
    A: Phaser.Input.Keyboard.Key;
    S: Phaser.Input.Keyboard.Key;
    D: Phaser.Input.Keyboard.Key;
    SPACE: Phaser.Input.Keyboard.Key;
  };
  private seq: number = 0;

  // Fire state: true on the frame the left mouse button was pressed
  private fireQueued: boolean = false;
  // Melee state: true on the frame spacebar was pressed
  private meleeQueued: boolean = false;

  constructor(scene: Phaser.Scene) {
    this.scene = scene;
    const kb = scene.input.keyboard!;
    this.keys = {
      W: kb.addKey(Phaser.Input.Keyboard.KeyCodes.W),
      A: kb.addKey(Phaser.Input.Keyboard.KeyCodes.A),
      S: kb.addKey(Phaser.Input.Keyboard.KeyCodes.S),
      D: kb.addKey(Phaser.Input.Keyboard.KeyCodes.D),
      SPACE: kb.addKey(Phaser.Input.Keyboard.KeyCodes.SPACE),
    };

    // Left click to fire
    scene.input.on('pointerdown', (pointer: Phaser.Input.Pointer) => {
      if (pointer.leftButtonDown()) {
        this.fireQueued = true;
      }
    });

    // Spacebar to melee (JustDown check below)
  }

  /**
   * Sample the current input state and return an InputPayload.
   */
  getInput(playerWorldX: number, playerWorldY: number): InputPayload {
    const pointer = this.scene.input.activePointer;

    // Convert pointer screen position to world position
    const worldPoint = this.scene.cameras.main.getWorldPoint(pointer.x, pointer.y);
    const aimAngle = Math.atan2(worldPoint.y - playerWorldY, worldPoint.x - playerWorldX);

    const fire = this.fireQueued;
    this.fireQueued = false;

    const melee = Phaser.Input.Keyboard.JustDown(this.keys.SPACE) || this.meleeQueued;
    this.meleeQueued = false;

    return {
      up: this.keys.W.isDown,
      down: this.keys.S.isDown,
      left: this.keys.A.isDown,
      right: this.keys.D.isDown,
      aimAngle,
      seq: ++this.seq,
      fire,
      melee,
    };
  }

  /** Check if any movement key is pressed. */
  isMoving(): boolean {
    return this.keys.W.isDown || this.keys.A.isDown || this.keys.S.isDown || this.keys.D.isDown;
  }
}
