import Phaser from 'phaser';
import {
  INTERPOLATION_BUFFER_MS,
  lerp,
  ClassId,
  orthoToIso,
  isoToOrtho,
  ORTHO_TILE_SIZE,
} from '@valhalla/shared';
import { ClientDataManager } from './ClientDataManager.js';

export const ENTITY_DEPTH_BASE = 600_000;
export const NAMEPLATE_DEPTH   = 800_000;
export const UI_DEPTH_BASE     = 1_000_000;

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
  lastDir: string;
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
  name: string;
  level: number;
}

// ── Public Target Info Types ──────────────────────────────

export interface PlayerTargetInfo {
  characterName: string;
  classId: string;
  level: number;
  hp: number;
  maxHp: number;
  alive: boolean;
}

export interface NpcTargetInfo {
  name: string;
  level: number;
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

  /** Called when a remote player sprite is left-clicked. */
  public onPlayerClick?: (sessionId: string) => void;
  /** Called when an NPC sprite is left-clicked. */
  public onNpcClick?: (npcId: string) => void;

  constructor(scene: Phaser.Scene) {
    this.scene = scene;
  }

  // ── Remote Players ────────────────────────────────────────

  addRemotePlayer(sessionId: string, x: number, y: number, classId: string = 'warrior', level: number = 1, characterName: string = ''): void {
    // Convert ortho world coords to ISO screen coords
    const isoPos = orthoToIso(x, y);
    // Use sprite sheet — frame 18 = row 2 (down), col 0 = idle facing down
    const sprite = this.scene.add.sprite(isoPos.x, isoPos.y, 'player_walk', 18);
    sprite.setDepth(ENTITY_DEPTH_BASE);
    sprite.rotation = 0;

    // Make sprite clickable for targeting
    sprite.setInteractive({ useHandCursor: false });
    sprite.on('pointerdown', () => {
      this.onPlayerClick?.(sessionId);
    });

    // Name label: "CharName Lv.X" or fallback to "ClassName Lv.X"
    const displayName = characterName || (ClientDataManager.instance.getClass(classId)?.name ?? classId);
    const labelText = `${displayName} Lv.${level}`;

    // Nameplate background
    const nameBg = this.scene.add.graphics();
    nameBg.setDepth(NAMEPLATE_DEPTH);

    const nameText = this.scene.add.text(isoPos.x, isoPos.y - 44, labelText, {
      fontSize: '12px',
      fontStyle: 'bold',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 3,
    });
    nameText.setOrigin(0.5, 1);
    nameText.setDepth(NAMEPLATE_DEPTH + 1);

    const hpBar = this.scene.add.graphics();
    hpBar.setDepth(NAMEPLATE_DEPTH + 2);

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
      lastDir: 'down',
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

    // Snapshot current ISO sprite position and convert back to ortho
    const orthoPos = isoToOrtho(data.sprite.x, data.sprite.y);
    data.previousX = orthoPos.x;
    data.previousY = orthoPos.y;
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
    // Convert ortho world coords to ISO screen coords
    const isoPos = orthoToIso(x, y);
    const sprite = this.scene.add.sprite(isoPos.x, isoPos.y, 'npc_sprite');
    sprite.setDepth(ENTITY_DEPTH_BASE);
    sprite.setTint(spriteColor || 0xff4444);

    // Make sprite clickable for targeting
    sprite.setInteractive({ useHandCursor: false });
    sprite.on('pointerdown', () => {
      this.onNpcClick?.(id);
    });

    // Scale sprite by spriteSize (1 = default 24px, 2 = double, etc.)
    if (spriteSize > 1) {
      sprite.setScale(spriteSize);
    }

    // Nameplate: red for enemies, yellow for friendly NPCs
    const nameColor = npcType === 'enemy' ? '#ff6666' : '#ffee88';
    const labelText = `${name} Lv.${level}`;

    const nameBg = this.scene.add.graphics();
    nameBg.setDepth(NAMEPLATE_DEPTH);

    const nameText = this.scene.add.text(isoPos.x, isoPos.y - 44, labelText, {
      fontSize: '12px',
      fontStyle: 'bold',
      color: nameColor,
      stroke: '#000000',
      strokeThickness: 3,
    });
    nameText.setOrigin(0.5, 1);
    nameText.setDepth(NAMEPLATE_DEPTH + 1);

    const hpBar = this.scene.add.graphics();
    hpBar.setDepth(NAMEPLATE_DEPTH + 2);

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
      name,
      level,
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

    // Snapshot current ISO sprite position and convert back to ortho
    const orthoPos = isoToOrtho(data.sprite.x, data.sprite.y);
    data.previousX = orthoPos.x;
    data.previousY = orthoPos.y;
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
    // Convert ortho world coords to ISO screen coords
    const isoPos = orthoToIso(x, y);
    const sprite = this.scene.add.sprite(isoPos.x, isoPos.y, 'projectile');
    sprite.setDepth(ENTITY_DEPTH_BASE + 50);

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

    // Snapshot current ISO sprite position and convert back to ortho
    const orthoPos = isoToOrtho(data.sprite.x, data.sprite.y);
    data.previousX = orthoPos.x;
    data.previousY = orthoPos.y;
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
    // Convert ortho world coords to ISO screen coords
    const isoPos = orthoToIso(x, y);
    gfx.x = isoPos.x;
    gfx.y = isoPos.y;

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

    // sprite.x / sprite.y now correctly return the Graphics world position (ISO screen coords)
    // Convert back to ortho for storage
    const orthoPos = isoToOrtho(data.sprite.x, data.sprite.y);
    data.previousX = orthoPos.x;
    data.previousY = orthoPos.y;

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
    ring.setDepth(ENTITY_DEPTH_BASE + 10);
    ring.x = x;
    ring.y = y;
    ring.lineStyle(4, 0xff6600, 1);
    ring.fillStyle(0xff4400, 0.55);
    ring.strokeCircle(0, 0, radius);
    ring.fillCircle(0, 0, radius);

    // Inner bright core — also centred on the detonation point
    const core = this.scene.add.graphics();
    core.setDepth(ENTITY_DEPTH_BASE + 11);
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
    gfx.setDepth(ENTITY_DEPTH_BASE + 50);
    // Outer glow
    gfx.fillStyle(0xff6600, 0.5);
    gfx.fillCircle(0, 0, 14);
    // Core
    gfx.fillStyle(0xffdd00, 1);
    gfx.fillCircle(0, 0, 8);
    return gfx;
  }

  // ── Melee Visual Effect ───────────────────────────────────

  showMeleeSlash(x: number, y: number, angle: number, isIso: boolean = false): void {
    // x,y are in screen/sprite space (ISO screen coords when isIso, ortho world coords otherwise).
    // `angle` is always in orthogonal world space (calculated from mouse vs player in ortho).
    // For ISO we must convert the ortho direction vector into ISO screen space so the
    // offset and rotation match the direction the player is actually facing on screen.
    let offsetX: number;
    let offsetY: number;
    let rotation: number;

    if (isIso) {
      // Transform ortho unit vector → ISO screen direction
      // orthoToIso formula: isoX = orthoX - orthoY,  isoY = (orthoX + orthoY) / 2
      const cosA = Math.cos(angle);
      const sinA = Math.sin(angle);
      const isoDx = cosA - sinA;
      const isoDy = (cosA + sinA) / 2;
      // Normalise so the visual offset is a consistent 30 px regardless of angle
      const isoLen = Math.hypot(isoDx, isoDy) || 1;
      offsetX = (isoDx / isoLen) * 30;
      offsetY = (isoDy / isoLen) * 30;
      rotation = Math.atan2(isoDy, isoDx);
    } else {
      offsetX = Math.cos(angle) * 30;
      offsetY = Math.sin(angle) * 30;
      rotation = angle;
    }

    const slash = this.scene.add.sprite(x + offsetX, y + offsetY, 'melee_slash');
    slash.setDepth(ENTITY_DEPTH_BASE + 9);
    slash.setRotation(rotation);
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
    dmgText.setDepth(NAMEPLATE_DEPTH + 50);

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
    txt.setDepth(NAMEPLATE_DEPTH + 50);

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

      // Lerp in ortho space
      const orthoX = lerp(data.previousX, data.targetX, t);
      const orthoY = lerp(data.previousY, data.targetY, t);

      // Convert lerped ortho result to ISO for sprite position
      const isoPos = orthoToIso(orthoX, orthoY);
      data.sprite.x = isoPos.x;
      data.sprite.y = isoPos.y;
      data.sprite.rotation = 0;

      // Directional animation based on raw ortho movement delta
      const dx = data.targetX - data.previousX;
      const dy = data.targetY - data.previousY;
      if (Math.abs(dx) > 0.5 || Math.abs(dy) > 0.5) {
        // Moving — map ortho delta directly to sprite direction
        // Vertical axis takes priority for diagonals
        let dir: string;
        if (Math.abs(dy) >= Math.abs(dx)) {
          dir = dy < 0 ? 'up' : 'down';
        } else {
          dir = dx < 0 ? 'left' : 'right';
        }
        data.lastDir = dir;
        if (data.sprite.anims.getName() !== `walk_${dir}` || !data.sprite.anims.isPlaying) {
          data.sprite.play(`walk_${dir}`, true);
        }
      } else {
        // Idle
        const idleKey = `idle_${data.lastDir}`;
        if (data.sprite.anims.getName() !== idleKey || !data.sprite.anims.isPlaying) {
          data.sprite.play(idleKey, true);
        }
      }

      // Dynamic depth based on ortho position
      data.sprite.setDepth(ENTITY_DEPTH_BASE + Math.floor(orthoX / ORTHO_TILE_SIZE) + Math.floor(orthoY / ORTHO_TILE_SIZE));

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

      // Lerp in ortho space
      const orthoX = lerp(data.previousX, data.targetX, nt);
      const orthoY = lerp(data.previousY, data.targetY, nt);

      // Convert lerped ortho result to ISO for sprite position
      const isoPos = orthoToIso(orthoX, orthoY);
      data.sprite.x = isoPos.x;
      data.sprite.y = isoPos.y;
      data.sprite.rotation = data.targetAngle;

      // Dynamic depth based on ortho position
      data.sprite.setDepth(ENTITY_DEPTH_BASE + Math.floor(orthoX / ORTHO_TILE_SIZE) + Math.floor(orthoY / ORTHO_TILE_SIZE));

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

    // Projectiles — interpolate positions in ortho space, convert to ISO
    this.projectiles.forEach((data) => {
      const elapsed = now - data.lastUpdateTime;
      const t = Math.min(elapsed / INTERPOLATION_BUFFER_MS, 1);

      // Lerp in ortho space
      const orthoX = lerp(data.previousX, data.targetX, t);
      const orthoY = lerp(data.previousY, data.targetY, t);

      // Convert lerped ortho result to ISO for sprite position
      const isoPos = orthoToIso(orthoX, orthoY);
      data.sprite.x = isoPos.x;
      data.sprite.y = isoPos.y;
    });

    // Spell projectiles — interpolate by moving the Graphics object's world position.
    // The circles are drawn at (0, 0) in local space, so moving .x/.y is all we need.
    // Interpolate in ortho space, convert to ISO.
    this.spellProjectiles.forEach((data) => {
      const elapsed = now - data.lastUpdateTime;
      const t = Math.min(elapsed / INTERPOLATION_BUFFER_MS, 1);

      // Lerp in ortho space
      const orthoX = lerp(data.previousX, data.targetX, t);
      const orthoY = lerp(data.previousY, data.targetY, t);

      // Convert lerped ortho result to ISO for sprite position
      const isoPos = orthoToIso(orthoX, orthoY);
      data.sprite.x = isoPos.x;
      data.sprite.y = isoPos.y;
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

  // ── Target Info Getters (for the targeting nameplate) ─────

  getPlayerTargetInfo(sessionId: string): PlayerTargetInfo | null {
    const data = this.remotePlayers.get(sessionId);
    if (!data) return null;
    return {
      characterName: data.characterName,
      classId: data.classId,
      level: data.level,
      hp: data.hp,
      maxHp: data.maxHp,
      alive: data.alive,
    };
  }

  getNpcTargetInfo(id: string): NpcTargetInfo | null {
    const data = this.npcs.get(id);
    if (!data) return null;
    return {
      name: data.name,
      level: data.level,
      hp: data.hp,
      maxHp: data.maxHp,
      alive: data.alive,
      npcType: data.npcType,
    };
  }

  // ── Magic Missile VFX ─────────────────────────────────────

  /**
   * Play the Magic Missile visual effect:
   *   1. A bright arcane burst at the caster's feet (launch flash).
   *   2. A glowing blue-white bolt that travels to the target.
   *   3. An arcane impact burst at the target position.
   *
   * All effects are purely cosmetic Phaser Graphics tweens — no game state
   * is modified here.
   */
  showMagicMissileVFX(fromX: number, fromY: number, toX: number, toY: number): void {
    // ── 1. Cast flash at caster ──
    const castFlash = this.scene.add.graphics();
    castFlash.setDepth(ENTITY_DEPTH_BASE + 12);
    castFlash.x = fromX;
    castFlash.y = fromY;
    // Outer glow
    castFlash.fillStyle(0x6699ff, 0.4);
    castFlash.fillCircle(0, 0, 18);
    // Inner bright core
    castFlash.fillStyle(0xddeeff, 0.95);
    castFlash.fillCircle(0, 0, 8);

    this.scene.tweens.add({
      targets: castFlash,
      alpha: 0,
      scaleX: 2.2,
      scaleY: 2.2,
      duration: 220,
      ease: 'Cubic.Out',
      onComplete: () => castFlash.destroy(),
    });

    // ── 2. Bolt that travels from caster to target ──
    const bolt = this.scene.add.graphics();
    bolt.setDepth(ENTITY_DEPTH_BASE + 12);
    bolt.x = fromX;
    bolt.y = fromY;
    // Outer soft glow
    bolt.fillStyle(0x88aaff, 0.6);
    bolt.fillCircle(0, 0, 7);
    // Bright core
    bolt.fillStyle(0xffffff, 1.0);
    bolt.fillCircle(0, 0, 4);

    // Travel duration is proportional to distance but capped so it never drags
    const dx = toX - fromX;
    const dy = toY - fromY;
    const dist = Math.sqrt(dx * dx + dy * dy);
    const travelMs = Math.min(180, Math.max(60, dist * 0.35));

    this.scene.tweens.add({
      targets: bolt,
      x: toX,
      y: toY,
      duration: travelMs,
      ease: 'Linear',
      onComplete: () => {
        bolt.destroy();

        // ── 3. Impact flash at target ──
        const impact = this.scene.add.graphics();
        impact.setDepth(ENTITY_DEPTH_BASE + 12);
        impact.x = toX;
        impact.y = toY;
        // Outer burst ring
        impact.lineStyle(3, 0x6699ff, 0.9);
        impact.strokeCircle(0, 0, 14);
        // Fill
        impact.fillStyle(0x88ccff, 0.55);
        impact.fillCircle(0, 0, 14);
        // Bright centre
        impact.fillStyle(0xffffff, 0.9);
        impact.fillCircle(0, 0, 6);

        this.scene.tweens.add({
          targets: impact,
          alpha: 0,
          scaleX: 2.4,
          scaleY: 2.4,
          duration: 280,
          ease: 'Cubic.Out',
          onComplete: () => impact.destroy(),
        });
      },
    });
  }
}
