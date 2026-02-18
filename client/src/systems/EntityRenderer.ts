import Phaser from 'phaser';
import {
  INTERPOLATION_BUFFER_MS,
  lerp,
  ClassId,
} from '@valhalla/shared';
import { ClientDataManager } from './ClientDataManager.js';

interface RemotePlayerData {
  sprite: Phaser.GameObjects.Sprite;
  nameText: Phaser.GameObjects.Text;
  nameBg: Phaser.GameObjects.Graphics;
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
  characterName: string;
}

interface NPCData {
  sprite: Phaser.GameObjects.Sprite;
  nameText: Phaser.GameObjects.Text;
  nameBg: Phaser.GameObjects.Graphics;
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
  npcType: string;
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
/** Spell projectile visual data (interpolated, direction-aware) */
interface SpellProjectileData {
  sprite: Phaser.GameObjects.Graphics;
  targetX: number;
  targetY: number;
  previousX: number;
  previousY: number;
  angle: number; // radians — used to orient the projectile gfx
  lastUpdateTime: number;
  skillId: string;
}

export class EntityRenderer {
  private scene: Phaser.Scene;
  private remotePlayers: Map<string, RemotePlayerData> = new Map();
  private npcs: Map<string, NPCData> = new Map();
  private projectiles: Map<string, ProjectileData> = new Map();
  private spellProjectiles: Map<string, SpellProjectileData> = new Map();

  constructor(scene: Phaser.Scene) {
    this.scene = scene;
  }

  // ── Remote Players ────────────────────────────────────────

  addRemotePlayer(sessionId: string, x: number, y: number, classId: string = 'warrior', level: number = 1, characterName: string = ''): void {
    const sprite = this.scene.add.sprite(x, y, 'remote_player');
    sprite.setDepth(5);

    // Tint sprite by class
    const color = ClientDataManager.instance.getClassColor(classId) ?? 0xffffff;
    sprite.setTint(color);

    // Name label: "CharName Lv.X" or fallback to "ClassName Lv.X"
    const displayName = characterName || (ClientDataManager.instance.getClass(classId)?.name ?? classId);
    const labelText = `${displayName} Lv.${level}`;

    // Nameplate background
    const nameBg = this.scene.add.graphics();
    nameBg.setDepth(6);

    const nameText = this.scene.add.text(x, y - 44, labelText, {
      fontSize: '12px',
      fontStyle: 'bold',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 3,
    });
    nameText.setOrigin(0.5, 1);
    nameText.setDepth(7);

    const hpBar = this.scene.add.graphics();
    hpBar.setDepth(8);

    this.remotePlayers.set(sessionId, {
      sprite,
      nameText,
      nameBg,
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
      characterName,
    });
  }

  removeRemotePlayer(sessionId: string): void {
    const data = this.remotePlayers.get(sessionId);
    if (data) {
      data.sprite.destroy();
      data.nameText.destroy();
      data.nameBg.destroy();
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
    data.nameBg.setVisible(alive);

    // Update level if changed
    if (level !== undefined && level !== data.level) {
      data.level = level;
      const displayName = data.characterName || (ClientDataManager.instance.getClass(data.classId)?.name ?? data.classId);
      data.nameText.setText(`${displayName} Lv.${level}`);
    }
  }

  // ── NPCs ─────────────────────────────────────────────────

  addNPC(
    id: string,
    x: number,
    y: number,
    name: string,
    level: number,
    npcType: string,
    spriteColor: number,
    spriteSize: number,
  ): void {
    const sprite = this.scene.add.sprite(x, y, 'npc_sprite');
    sprite.setDepth(4); // Below players (depth 5)
    sprite.setTint(spriteColor || 0xff4444);

    // Scale sprite by spriteSize (1 = default 24px, 2 = double, etc.)
    if (spriteSize > 1) {
      sprite.setScale(spriteSize);
    }

    // Nameplate: red for enemies, yellow for friendly NPCs
    const nameColor = npcType === 'enemy' ? '#ff6666' : '#ffee88';
    const labelText = `${name} Lv.${level}`;

    const nameBg = this.scene.add.graphics();
    nameBg.setDepth(6);

    const nameText = this.scene.add.text(x, y - 44, labelText, {
      fontSize: '12px',
      fontStyle: 'bold',
      color: nameColor,
      stroke: '#000000',
      strokeThickness: 3,
    });
    nameText.setOrigin(0.5, 1);
    nameText.setDepth(7);

    const hpBar = this.scene.add.graphics();
    hpBar.setDepth(8);

    this.npcs.set(id, {
      sprite,
      nameText,
      nameBg,
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
      npcType,
    });
  }

  removeNPC(id: string): void {
    const data = this.npcs.get(id);
    if (data) {
      data.sprite.destroy();
      data.nameText.destroy();
      data.nameBg.destroy();
      data.hpBar.destroy();
      this.npcs.delete(id);
    }
  }

  updateNPCTarget(id: string, x: number, y: number, aimAngle: number): void {
    const data = this.npcs.get(id);
    if (!data) return;

    data.previousX = data.sprite.x;
    data.previousY = data.sprite.y;
    data.targetX = x;
    data.targetY = y;
    data.targetAngle = aimAngle;
    data.lastUpdateTime = Date.now();
  }

  updateNPCHp(id: string, hp: number, maxHp: number, alive: boolean): void {
    const data = this.npcs.get(id);
    if (!data) return;

    data.hp = hp;
    data.maxHp = maxHp;
    data.alive = alive;
    data.sprite.setVisible(alive);
    data.nameText.setVisible(alive);
    data.nameBg.setVisible(alive);
  }

  hasNPC(id: string): boolean {
    return this.npcs.has(id);
  }

  getNPCPosition(id: string): { x: number; y: number } | null {
    const data = this.npcs.get(id);
    if (!data) return null;
    return { x: data.sprite.x, y: data.sprite.y };
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

  // ── Spell Projectiles (e.g. Fireball) ─────────────────────

  addSpellProjectile(id: string, x: number, y: number, targetX: number, targetY: number, skillId: string): void {
    // Draw at (0, 0) relative to the Graphics object, then position the object in world space.
    // This lets us move the fireball by setting gfx.x / gfx.y, and lets Phaser tweens
    // scale/rotate around the correct world-space origin.
    const gfx = this.drawFireball();
    gfx.x = x;
    gfx.y = y;

    this.spellProjectiles.set(id, {
      sprite: gfx,
      targetX: x,
      targetY: y,
      previousX: x,
      previousY: y,
      angle: Math.atan2(targetY - y, targetX - x),
      lastUpdateTime: Date.now(),
      skillId,
    });
  }

  updateSpellProjectileTarget(id: string, x: number, y: number): void {
    const data = this.spellProjectiles.get(id);
    if (!data) return;

    // sprite.x / sprite.y now correctly return the Graphics world position
    data.previousX = data.sprite.x;
    data.previousY = data.sprite.y;

    data.targetX = x;
    data.targetY = y;
    data.lastUpdateTime = Date.now();
  }

  removeSpellProjectile(id: string): void {
    const data = this.spellProjectiles.get(id);
    if (data) {
      data.sprite.destroy();
      this.spellProjectiles.delete(id);
    }
  }

  /**
   * Play a fireball explosion at the given world position.
   * Called when the server sends a SPELL_IMPACT event.
   *
   * Graphics are drawn at (0, 0) in local space; the objects are positioned
   * at (x, y) in world space. Phaser's tween then scales around (x, y),
   * not around the world origin.
   */
  showSpellImpact(x: number, y: number, radius: number, _skillId: string): void {
    // Outer blast ring — positioned at the detonation point
    const ring = this.scene.add.graphics();
    ring.setDepth(10);
    ring.x = x;
    ring.y = y;
    ring.lineStyle(4, 0xff6600, 1);
    ring.fillStyle(0xff4400, 0.55);
    ring.strokeCircle(0, 0, radius);
    ring.fillCircle(0, 0, radius);

    // Inner bright core — also centred on the detonation point
    const core = this.scene.add.graphics();
    core.setDepth(11);
    core.x = x;
    core.y = y;
    core.fillStyle(0xffee00, 0.9);
    core.fillCircle(0, 0, radius * 0.3);

    // Animate: expand + fade — scales around the Graphics origin, which is now (x, y)
    this.scene.tweens.add({
      targets: ring,
      alpha: 0,
      scaleX: 1.4,
      scaleY: 1.4,
      duration: 350,
      ease: 'Cubic.Out',
      onComplete: () => ring.destroy(),
    });

    this.scene.tweens.add({
      targets: core,
      alpha: 0,
      scaleX: 1.8,
      scaleY: 1.8,
      duration: 200,
      ease: 'Cubic.Out',
      onComplete: () => core.destroy(),
    });
  }

  /**
   * Draw the fireball visual at local origin (0, 0).
   * The caller is responsible for positioning the returned Graphics object
   * in world space by setting gfx.x / gfx.y.
   */
  private drawFireball(): Phaser.GameObjects.Graphics {
    const gfx = this.scene.add.graphics();
    gfx.setDepth(8);
    // Outer glow
    gfx.fillStyle(0xff6600, 0.5);
    gfx.fillCircle(0, 0, 14);
    // Core
    gfx.fillStyle(0xffdd00, 1);
    gfx.fillCircle(0, 0, 8);
    return gfx;
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
      data.nameText.y = data.sprite.y - 44;

      // Draw nameplate background
      data.nameBg.clear();
      const textW = data.nameText.width;
      const textH = data.nameText.height;
      const bgX = data.sprite.x - textW / 2 - 4;
      const bgY = data.sprite.y - 44 - textH - 1;
      data.nameBg.fillStyle(0x000000, 0.5);
      data.nameBg.fillRoundedRect(bgX, bgY, textW + 8, textH + 4, 3);

      // Draw HP bar
      this.drawHpBar(data.hpBar, data.sprite.x, data.sprite.y, data.hp, data.maxHp);
    });

    // NPCs — same interpolation as remote players
    this.npcs.forEach((data) => {
      if (!data.alive) {
        data.hpBar.clear();
        return;
      }

      const npcElapsed = now - data.lastUpdateTime;
      const nt = Math.min(npcElapsed / INTERPOLATION_BUFFER_MS, 1);

      data.sprite.x = lerp(data.previousX, data.targetX, nt);
      data.sprite.y = lerp(data.previousY, data.targetY, nt);
      data.sprite.rotation = data.targetAngle;

      data.nameText.x = data.sprite.x;
      data.nameText.y = data.sprite.y - 44;

      // Draw nameplate background
      data.nameBg.clear();
      const npcTextW = data.nameText.width;
      const npcTextH = data.nameText.height;
      const npcBgX = data.sprite.x - npcTextW / 2 - 4;
      const npcBgY = data.sprite.y - 44 - npcTextH - 1;
      data.nameBg.fillStyle(0x000000, 0.5);
      data.nameBg.fillRoundedRect(npcBgX, npcBgY, npcTextW + 8, npcTextH + 4, 3);

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

    // Spell projectiles — interpolate by moving the Graphics object's world position.
    // The circles are drawn at (0, 0) in local space, so moving .x/.y is all we need.
    this.spellProjectiles.forEach((data) => {
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
