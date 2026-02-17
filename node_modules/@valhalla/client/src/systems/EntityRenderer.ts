import Phaser from 'phaser';
import {
  INTERPOLATION_BUFFER_MS,
  lerp,
  CLASS_TEMPLATES,
  CLASS_COLORS,
  ClassId,
} from '@valhalla/shared';

interface RemotePlayerData {
  sprite: Phaser.GameObjects.Sprite;
  nameText: Phaser.GameObjects.Text;
  hpBar: Phaser.GameObjects.Graphics;
  targetX: number;
  targetY: number;
  previousX: number;
  previousY: number;
  targetAngle: number;
  lastUpdateTime: number;
  hp: number;
  maxHp: number;
  alive: boolean;
  classId: string;
  level: number;
}

interface ProjectileData {
  sprite: Phaser.GameObjects.Sprite;
  targetX: number;
  targetY: number;
  previousX: number;
  previousY: number;
  lastUpdateTime: number;
}

/**
 * Manages rendering and interpolation for remote player entities,
 * projectiles, HP bars, class labels, and combat visual effects.
 */
export class EntityRenderer {
  private scene: Phaser.Scene;
  private remotePlayers: Map<string, RemotePlayerData> = new Map();
  private projectiles: Map<string, ProjectileData> = new Map();

  constructor(scene: Phaser.Scene) {
    this.scene = scene;
  }

  // ── Remote Players ────────────────────────────────────────

  addRemotePlayer(sessionId: string, x: number, y: number, classId: string = 'warrior', level: number = 1): void {
    const sprite = this.scene.add.sprite(x, y, 'remote_player');
    sprite.setDepth(5);

    // Tint sprite by class
    const color = CLASS_COLORS[classId as ClassId] ?? 0xffffff;
    sprite.setTint(color);

    // Name label: "ClassName Lv.X"
    const template = CLASS_TEMPLATES[classId as ClassId];
    const className = template?.name ?? classId;
    const labelText = `${className} Lv.${level}`;

    const nameText = this.scene.add.text(x, y - 38, labelText, {
      fontSize: '11px',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 2,
    });
    nameText.setOrigin(0.5, 1);
    nameText.setDepth(6);

    const hpBar = this.scene.add.graphics();
    hpBar.setDepth(7);

    this.remotePlayers.set(sessionId, {
      sprite,
      nameText,
      hpBar,
      targetX: x,
      targetY: y,
      previousX: x,
      previousY: y,
      targetAngle: 0,
      lastUpdateTime: Date.now(),
      hp: 100,
      maxHp: 100,
      alive: true,
      classId,
      level,
    });
  }

  removeRemotePlayer(sessionId: string): void {
    const data = this.remotePlayers.get(sessionId);
    if (data) {
      data.sprite.destroy();
      data.nameText.destroy();
      data.hpBar.destroy();
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

  updateRemotePlayerHp(sessionId: string, hp: number, maxHp: number, alive: boolean, level?: number): void {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return;

    data.hp = hp;
    data.maxHp = maxHp;
    data.alive = alive;
    data.sprite.setVisible(alive);
    data.nameText.setVisible(alive);

    // Update level if changed
    if (level !== undefined && level !== data.level) {
      data.level = level;
      const template = CLASS_TEMPLATES[data.classId as ClassId];
      const className = template?.name ?? data.classId;
      data.nameText.setText(`${className} Lv.${level}`);
    }
  }

  // ── Projectiles ───────────────────────────────────────────

  addProjectile(id: string, x: number, y: number): void {
    const sprite = this.scene.add.sprite(x, y, 'projectile');
    sprite.setDepth(8);

    this.projectiles.set(id, {
      sprite,
      targetX: x,
      targetY: y,
      previousX: x,
      previousY: y,
      lastUpdateTime: Date.now(),
    });
  }

  removeProjectile(id: string): void {
    const data = this.projectiles.get(id);
    if (data) {
      data.sprite.destroy();
      this.projectiles.delete(id);
    }
  }

  updateProjectileTarget(id: string, x: number, y: number): void {
    const data = this.projectiles.get(id);
    if (!data) return;

    data.previousX = data.sprite.x;
    data.previousY = data.sprite.y;
    data.targetX = x;
    data.targetY = y;
    data.lastUpdateTime = Date.now();
  }

  // ── Melee Visual Effect ───────────────────────────────────

  showMeleeSlash(x: number, y: number, angle: number): void {
    const slash = this.scene.add.sprite(
      x + Math.cos(angle) * 30,
      y + Math.sin(angle) * 30,
      'melee_slash',
    );
    slash.setDepth(9);
    slash.setRotation(angle);
    slash.setAlpha(0.9);

    // Fade out and destroy
    this.scene.tweens.add({
      targets: slash,
      alpha: 0,
      scaleX: 1.5,
      scaleY: 1.5,
      duration: 200,
      onComplete: () => slash.destroy(),
    });
  }

  // ── Damage Flash Effect ───────────────────────────────────

  showDamageFlash(x: number, y: number, damage: number, isCrit?: boolean): void {
    const text = isCrit ? `-${damage}!` : `-${damage}`;
    const fontSize = isCrit ? '22px' : '16px';
    const color = isCrit ? '#ffaa00' : '#ff4444';

    const dmgText = this.scene.add.text(x, y - 20, text, {
      fontSize,
      color,
      stroke: '#000000',
      strokeThickness: 3,
      fontStyle: 'bold',
    });
    dmgText.setOrigin(0.5, 0.5);
    dmgText.setDepth(50);

    this.scene.tweens.add({
      targets: dmgText,
      y: y - 60,
      alpha: 0,
      duration: isCrit ? 1000 : 800,
      ease: 'Power2',
      onComplete: () => dmgText.destroy(),
    });
  }

  // ── Combat Feedback Text (Miss / Dodge / Block) ──────────

  showCombatText(x: number, y: number, label: string, color: string): void {
    const txt = this.scene.add.text(x, y - 20, label, {
      fontSize: '14px',
      color,
      stroke: '#000000',
      strokeThickness: 3,
      fontStyle: 'bold',
    });
    txt.setOrigin(0.5, 0.5);
    txt.setDepth(50);

    this.scene.tweens.add({
      targets: txt,
      y: y - 55,
      alpha: 0,
      duration: 700,
      ease: 'Power2',
      onComplete: () => txt.destroy(),
    });
  }

  // ── HP Bar Drawing ────────────────────────────────────────

  private drawHpBar(gfx: Phaser.GameObjects.Graphics, x: number, y: number, hp: number, maxHp: number): void {
    gfx.clear();

    const barWidth = 40;
    const barHeight = 5;
    const barX = x - barWidth / 2;
    const barY = y - 34;

    // Background (dark)
    gfx.fillStyle(0x000000, 0.6);
    gfx.fillRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2);

    // HP fill
    const hpRatio = Math.max(0, hp / maxHp);
    const fillColor = hpRatio > 0.5 ? 0x44ff44 : hpRatio > 0.25 ? 0xffaa00 : 0xff4444;
    gfx.fillStyle(fillColor, 1);
    gfx.fillRect(barX, barY, barWidth * hpRatio, barHeight);
  }

  // ── Update Loop ───────────────────────────────────────────

  /**
   * Interpolate remote player and projectile positions each frame.
   */
  update(): void {
    const now = Date.now();

    // Remote players
    this.remotePlayers.forEach((data) => {
      if (!data.alive) {
        data.hpBar.clear();
        return;
      }

      const elapsed = now - data.lastUpdateTime;
      const t = Math.min(elapsed / INTERPOLATION_BUFFER_MS, 1);

      data.sprite.x = lerp(data.previousX, data.targetX, t);
      data.sprite.y = lerp(data.previousY, data.targetY, t);
      data.sprite.rotation = data.targetAngle;

      data.nameText.x = data.sprite.x;
      data.nameText.y = data.sprite.y - 38;

      // Draw HP bar
      this.drawHpBar(data.hpBar, data.sprite.x, data.sprite.y, data.hp, data.maxHp);
    });

    // Projectiles — interpolate positions
    this.projectiles.forEach((data) => {
      const elapsed = now - data.lastUpdateTime;
      const t = Math.min(elapsed / INTERPOLATION_BUFFER_MS, 1);

      data.sprite.x = lerp(data.previousX, data.targetX, t);
      data.sprite.y = lerp(data.previousY, data.targetY, t);
    });
  }

  hasPlayer(sessionId: string): boolean {
    return this.remotePlayers.has(sessionId);
  }

  getPlayerPosition(sessionId: string): { x: number; y: number } | null {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return null;
    return { x: data.sprite.x, y: data.sprite.y };
  }
}
