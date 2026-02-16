import Phaser from 'phaser';
import { InputPayload } from '@valhalla/shared';

/**
 * Captures WASD keyboard input and mouse aim.
 */
export class InputManager {
  private scene: Phaser.Scene;
  private keys: {
    W: Phaser.Input.Keyboard.Key;
    A: Phaser.Input.Keyboard.Key;
    S: Phaser.Input.Keyboard.Key;
    D: Phaser.Input.Keyboard.Key;
  };
  private seq: number = 0;

  constructor(scene: Phaser.Scene) {
    this.scene = scene;
    const kb = scene.input.keyboard!;
    this.keys = {
      W: kb.addKey(Phaser.Input.Keyboard.KeyCodes.W),
      A: kb.addKey(Phaser.Input.Keyboard.KeyCodes.A),
      S: kb.addKey(Phaser.Input.Keyboard.KeyCodes.S),
      D: kb.addKey(Phaser.Input.Keyboard.KeyCodes.D),
    };
  }

  /**
   * Sample the current input state and return an InputPayload.
   */
  getInput(playerWorldX: number, playerWorldY: number): InputPayload {
    const pointer = this.scene.input.activePointer;

    // Convert pointer screen position to world position
    const worldPoint = this.scene.cameras.main.getWorldPoint(pointer.x, pointer.y);
    const aimAngle = Math.atan2(worldPoint.y - playerWorldY, worldPoint.x - playerWorldX);

    return {
      up: this.keys.W.isDown,
      down: this.keys.S.isDown,
      left: this.keys.A.isDown,
      right: this.keys.D.isDown,
      aimAngle,
      seq: ++this.seq,
    };
  }

  /** Check if any movement key is pressed. */
  isMoving(): boolean {
    return this.keys.W.isDown || this.keys.A.isDown || this.keys.S.isDown || this.keys.D.isDown;
  }
}
