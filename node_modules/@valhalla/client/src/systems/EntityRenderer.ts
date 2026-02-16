import Phaser from 'phaser';
import { INTERPOLATION_BUFFER_MS, lerp } from '@valhalla/shared';

interface RemotePlayerData {
  sprite: Phaser.GameObjects.Sprite;
  nameText: Phaser.GameObjects.Text;
  targetX: number;
  targetY: number;
  previousX: number;
  previousY: number;
  targetAngle: number;
  lastUpdateTime: number;
}

/**
 * Manages rendering and interpolation for remote player entities.
 */
export class EntityRenderer {
  private scene: Phaser.Scene;
  private remotePlayers: Map<string, RemotePlayerData> = new Map();

  constructor(scene: Phaser.Scene) {
    this.scene = scene;
  }

  addRemotePlayer(sessionId: string, x: number, y: number): void {
    const sprite = this.scene.add.sprite(x, y, 'remote_player');
    sprite.setDepth(5);

    const nameText = this.scene.add.text(x, y - 32, sessionId.slice(0, 6), {
      fontSize: '12px',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 2,
    });
    nameText.setOrigin(0.5, 1);
    nameText.setDepth(6);

    this.remotePlayers.set(sessionId, {
      sprite,
      nameText,
      targetX: x,
      targetY: y,
      previousX: x,
      previousY: y,
      targetAngle: 0,
      lastUpdateTime: Date.now(),
    });
  }

  removeRemotePlayer(sessionId: string): void {
    const data = this.remotePlayers.get(sessionId);
    if (data) {
      data.sprite.destroy();
      data.nameText.destroy();
      this.remotePlayers.delete(sessionId);
    }
  }

  updateRemotePlayerTarget(sessionId: string, x: number, y: number, aimAngle: number): void {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return;

    data.previousX = data.sprite.x;
    data.previousY = data.sprite.y;
    data.targetX = x;
    data.targetY = y;
    data.targetAngle = aimAngle;
    data.lastUpdateTime = Date.now();
  }

  /**
   * Interpolate remote player positions each frame for smooth rendering.
   */
  update(): void {
    const now = Date.now();

    this.remotePlayers.forEach((data) => {
      const elapsed = now - data.lastUpdateTime;
      const t = Math.min(elapsed / INTERPOLATION_BUFFER_MS, 1);

      data.sprite.x = lerp(data.previousX, data.targetX, t);
      data.sprite.y = lerp(data.previousY, data.targetY, t);
      data.sprite.rotation = data.targetAngle;

      data.nameText.x = data.sprite.x;
      data.nameText.y = data.sprite.y - 32;
    });
  }

  hasPlayer(sessionId: string): boolean {
    return this.remotePlayers.has(sessionId);
  }
}
