import Phaser from 'phaser';
import {
  NetworkClient,
  CollisionGridData,
  PlayerHitData,
  PlayerDiedData,
  PlayerRespawnedData,
  MeleeAttackData,
  CombatFeedbackData,
} from '../systems/NetworkClient.js';
import { InputManager } from '../systems/InputManager.js';
import { EntityRenderer } from '../systems/EntityRenderer.js';
import {
  TILE_SIZE,
  PLAYER_COLLISION_RADIUS,
  MAP_WIDTH_PX,
  MAP_HEIGHT_PX,
  RESPAWN_TIME_MS,
  InputPayload,
  normalise,
  CLASS_TEMPLATES,
  ClassId,
} from '@valhalla/shared';

interface PendingInput {
  input: InputPayload;
  dt: number;
}

/**
 * Main gameplay scene.
 * Handles tile map rendering, local player with client-side prediction,
 * remote player interpolation, projectile rendering, HP, mana, death/respawn.
 */
export class GameScene extends Phaser.Scene {
  private network!: NetworkClient;
  private inputManager!: InputManager;
  private entityRenderer!: EntityRenderer;

  // Local player
  private playerSprite!: Phaser.GameObjects.Sprite;
  private aimLine!: Phaser.GameObjects.Graphics;
  private localX: number = 0;
  private localY: number = 0;

  // Local player vitals
  private localHp: number = 100;
  private localMaxHp: number = 100;
  private localMana: number = 0;
  private localMaxMana: number = 0;
  private localAlive: boolean = true;
  private localSpeed: number = 200;
  private localClassId: string = 'warrior';
  private localLevel: number = 1;
  private hpBarGfx!: Phaser.GameObjects.Graphics;
  private classHudText!: Phaser.GameObjects.Text;

  // Death overlay
  private deathOverlay!: Phaser.GameObjects.Rectangle;
  private deathText!: Phaser.GameObjects.Text;
  private respawnTimer: number = 0;

  // Client-side prediction
  private pendingInputs: PendingInput[] = [];
  private lastServerSeq: number = 0;

  // Collision (received from server)
  private collisionGrid: number[] = [];
  private collisionMapW: number = 0;
  private collisionMapH: number = 0;

  // Connection state
  private connected: boolean = false;
  private statusText!: Phaser.GameObjects.Text;

  // Class selection (passed from ClassSelectScene)
  private selectedClassId: string = 'warrior';

  constructor() {
    super({ key: 'GameScene' });
  }

  init(data?: { classId?: string }): void {
    if (data?.classId) {
      this.selectedClassId = data.classId;
    }
  }

  create(): void {
    this.inputManager = new InputManager(this);
    this.entityRenderer = new EntityRenderer(this);
    this.network = new NetworkClient();

    // Status text
    this.statusText = this.add.text(16, 16, 'Connecting...', {
      fontSize: '16px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 3,
    });
    this.statusText.setScrollFactor(0);
    this.statusText.setDepth(100);

    // Death overlay (hidden initially)
    this.deathOverlay = this.add.rectangle(
      this.cameras.main.width / 2,
      this.cameras.main.height / 2,
      this.cameras.main.width,
      this.cameras.main.height,
      0x000000,
      0.7,
    );
    this.deathOverlay.setScrollFactor(0);
    this.deathOverlay.setDepth(200);
    this.deathOverlay.setVisible(false);

    this.deathText = this.add.text(
      this.cameras.main.width / 2,
      this.cameras.main.height / 2,
      'YOU DIED\nRespawning...',
      {
        fontSize: '32px',
        color: '#ff4444',
        stroke: '#000000',
        strokeThickness: 4,
        align: 'center',
      },
    );
    this.deathText.setOrigin(0.5, 0.5);
    this.deathText.setScrollFactor(0);
    this.deathText.setDepth(201);
    this.deathText.setVisible(false);

    // Local HP/Mana bar (HUD)
    this.hpBarGfx = this.add.graphics();
    this.hpBarGfx.setScrollFactor(0);
    this.hpBarGfx.setDepth(100);

    // Class + Level HUD
    this.classHudText = this.add.text(16, this.cameras.main.height - 58, '', {
      fontSize: '12px',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.classHudText.setScrollFactor(0);
    this.classHudText.setDepth(100);

    // Setup network callbacks
    this.setupNetworkCallbacks();

    // Connect with class selection
    this.network.connect({ classId: this.selectedClassId }).catch((err) => {
      this.statusText.setText('Connection failed — is the server running?');
      console.error(err);
    });

    // Set world bounds
    this.cameras.main.setBounds(0, 0, MAP_WIDTH_PX, MAP_HEIGHT_PX);
  }

  private setupNetworkCallbacks(): void {
    this.network.onCollisionGrid = (data: CollisionGridData) => {
      this.collisionGrid = data.grid;
      this.collisionMapW = data.width;
      this.collisionMapH = data.height;
      this.buildTileMap(data);
    };

    this.network.onPlayerAdd = (player: any, sessionId: string) => {
      if (sessionId === this.network.sessionId) {
        // This is us — set up local player
        this.localX = player.x;
        this.localY = player.y;
        this.localHp = player.hp;
        this.localMaxHp = player.maxHp;
        this.localMana = player.mana ?? 0;
        this.localMaxMana = player.maxMana ?? 0;
        this.localAlive = player.alive;
        this.localSpeed = player.speed ?? 200;
        this.localClassId = player.classId ?? 'warrior';
        this.localLevel = player.level ?? 1;
        this.createLocalPlayer();
        this.connected = true;
        this.statusText.setText('WASD move | Mouse aim | Left-click shoot | Space melee');
        this.time.delayedCall(4000, () => this.statusText.setVisible(false));
      } else {
        // Remote player
        this.entityRenderer.addRemotePlayer(
          sessionId,
          player.x,
          player.y,
          player.classId ?? 'warrior',
          player.level ?? 1,
        );
      }
    };

    this.network.onPlayerChange = (player: any, sessionId: string) => {
      if (sessionId === this.network.sessionId) {
        // Update local state from server
        this.localHp = player.hp;
        this.localMaxHp = player.maxHp;
        this.localMana = player.mana ?? 0;
        this.localMaxMana = player.maxMana ?? 0;
        this.localSpeed = player.speed ?? this.localSpeed;
        this.localLevel = player.level ?? this.localLevel;
        const wasAlive = this.localAlive;
        this.localAlive = player.alive;

        // Handle death transition
        if (wasAlive && !this.localAlive) {
          this.showDeathScreen();
        } else if (!wasAlive && this.localAlive) {
          this.hideDeathScreen();
        }

        // Server reconciliation — reapply unacknowledged inputs
        this.reconcile(player.x, player.y, player.inputSeq);
      } else {
        this.entityRenderer.updateRemotePlayerTarget(
          sessionId,
          player.x,
          player.y,
          player.aimAngle,
        );
        this.entityRenderer.updateRemotePlayerHp(
          sessionId,
          player.hp,
          player.maxHp,
          player.alive,
          player.level ?? 1,
        );
      }
    };

    this.network.onPlayerRemove = (sessionId: string) => {
      this.entityRenderer.removeRemotePlayer(sessionId);
    };

    // Projectile callbacks
    this.network.onProjectileAdd = (proj: any, id: string) => {
      this.entityRenderer.addProjectile(id, proj.x, proj.y);
    };

    this.network.onProjectileRemove = (id: string) => {
      this.entityRenderer.removeProjectile(id);
    };

    this.network.onProjectileChange = (proj: any, id: string) => {
      this.entityRenderer.updateProjectileTarget(id, proj.x, proj.y);
    };

    // Combat events
    this.network.onPlayerHit = (data: PlayerHitData) => {
      if (data.targetId === this.network.sessionId) {
        // We got hit — show damage with crit indicator
        const dmgText = data.isCrit ? `${data.damage}!` : `${data.damage}`;
        this.entityRenderer.showDamageFlash(this.localX, this.localY, data.damage, data.isCrit);
        this.cameras.main.shake(100, data.isCrit ? 0.01 : 0.005);
      } else {
        this.showRemoteDamageFlash(data.targetId, data.damage, data.isCrit);
      }
    };

    this.network.onPlayerDied = (data: PlayerDiedData) => {
      if (data.targetId !== this.network.sessionId) {
        console.log(`[Combat] Player ${data.targetId.slice(0, 6)} was killed by ${data.killerId.slice(0, 6)}`);
      }
    };

    this.network.onPlayerRespawned = (data: PlayerRespawnedData) => {
      if (data.playerId === this.network.sessionId) {
        console.log('[Combat] You respawned!');
      }
    };

    this.network.onMeleeAttack = (data: MeleeAttackData) => {
      if (data.attackerId === this.network.sessionId) {
        this.entityRenderer.showMeleeSlash(this.localX, this.localY, data.angle);
      } else {
        this.showRemoteMeleeSlash(data.attackerId, data.angle);
      }
    };

    // Combat feedback: miss / dodge / block
    this.network.onMissed = (data: CombatFeedbackData) => {
      const pos = this.getCombatTextPosition(data.targetId);
      if (pos) this.entityRenderer.showCombatText(pos.x, pos.y, 'MISS', '#999999');
    };

    this.network.onDodged = (data: CombatFeedbackData) => {
      const pos = this.getCombatTextPosition(data.targetId);
      if (pos) this.entityRenderer.showCombatText(pos.x, pos.y, 'DODGE', '#ffffff');
    };

    this.network.onBlocked = (data: CombatFeedbackData) => {
      const pos = this.getCombatTextPosition(data.targetId);
      if (pos) this.entityRenderer.showCombatText(pos.x, pos.y, 'BLOCK', '#4488ff');
    };
  }

  /**
   * Get the world position for a given player (local or remote).
   */
  private getCombatTextPosition(playerId: string): { x: number; y: number } | null {
    if (playerId === this.network.sessionId) {
      return { x: this.localX, y: this.localY };
    }
    return this.entityRenderer.getPlayerPosition(playerId);
  }

  private showRemoteDamageFlash(targetId: string, damage: number, isCrit?: boolean): void {
    const pos = this.entityRenderer.getPlayerPosition(targetId);
    if (pos) this.entityRenderer.showDamageFlash(pos.x, pos.y, damage, isCrit);
  }

  private showRemoteMeleeSlash(attackerId: string, angle: number): void {
    const pos = this.entityRenderer.getPlayerPosition(attackerId);
    if (pos) this.entityRenderer.showMeleeSlash(pos.x, pos.y, angle);
  }

  private createLocalPlayer(): void {
    this.playerSprite = this.add.sprite(this.localX, this.localY, 'player');
    this.playerSprite.setDepth(10);

    // Tint by class
    const template = CLASS_TEMPLATES[this.localClassId as ClassId];
    if (template) {
      // Import CLASS_COLORS if we want to tint, but for local player keep it subtle
    }

    // Aim indicator line
    this.aimLine = this.add.graphics();
    this.aimLine.setDepth(9);

    // Camera follows player
    this.cameras.main.startFollow(this.playerSprite, true, 0.1, 0.1);
    this.cameras.main.setZoom(1);
  }

  /**
   * Build the visual tilemap from the collision grid data.
   */
  private buildTileMap(data: CollisionGridData): void {
    const { grid, width, height, tileSize } = data;

    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const isWall = grid[y * width + x] === 1;
        const tile = this.add.sprite(
          x * tileSize + tileSize / 2,
          y * tileSize + tileSize / 2,
          isWall ? 'tile_wall' : 'tile_ground',
        );
        tile.setDepth(isWall ? 1 : 0);
      }
    }
  }

  // ── Death / Respawn UI ──────────────────────────────────────

  private showDeathScreen(): void {
    this.deathOverlay.setVisible(true);
    this.deathText.setVisible(true);
    this.respawnTimer = RESPAWN_TIME_MS;

    if (this.playerSprite) {
      this.playerSprite.setVisible(false);
    }
    if (this.aimLine) {
      this.aimLine.clear();
    }
  }

  private hideDeathScreen(): void {
    this.deathOverlay.setVisible(false);
    this.deathText.setVisible(false);

    if (this.playerSprite) {
      this.playerSprite.setVisible(true);
    }
  }

  // ── HUD ───────────────────────────────────────────────────

  private drawLocalHud(): void {
    this.hpBarGfx.clear();

    const barWidth = 200;
    const barHeight = 12;
    const barX = 16;
    const hpBarY = this.cameras.main.height - 36;

    // HP bar
    this.hpBarGfx.fillStyle(0x000000, 0.7);
    this.hpBarGfx.fillRect(barX - 2, hpBarY - 2, barWidth + 4, barHeight + 4);

    const hpRatio = Math.max(0, this.localHp / this.localMaxHp);
    const hpColor = hpRatio > 0.5 ? 0x44ff44 : hpRatio > 0.25 ? 0xffaa00 : 0xff4444;
    this.hpBarGfx.fillStyle(hpColor, 1);
    this.hpBarGfx.fillRect(barX, hpBarY, barWidth * hpRatio, barHeight);

    this.hpBarGfx.lineStyle(1, 0xffffff, 0.5);
    this.hpBarGfx.strokeRect(barX - 2, hpBarY - 2, barWidth + 4, barHeight + 4);

    // HP text
    this.hpBarGfx.fillStyle(0xffffff, 1);

    // Mana bar (only for mana-using classes)
    if (this.localMaxMana > 0) {
      const manaBarY = hpBarY - barHeight - 6;
      this.hpBarGfx.fillStyle(0x000000, 0.7);
      this.hpBarGfx.fillRect(barX - 2, manaBarY - 2, barWidth + 4, barHeight + 4);

      const manaRatio = Math.max(0, this.localMana / this.localMaxMana);
      this.hpBarGfx.fillStyle(0x4488ff, 1);
      this.hpBarGfx.fillRect(barX, manaBarY, barWidth * manaRatio, barHeight);

      this.hpBarGfx.lineStyle(1, 0xffffff, 0.5);
      this.hpBarGfx.strokeRect(barX - 2, manaBarY - 2, barWidth + 4, barHeight + 4);
    }

    // Class + Level label
    const template = CLASS_TEMPLATES[this.localClassId as ClassId];
    const className = template?.name ?? this.localClassId;
    this.classHudText.setText(`${className} Lv.${this.localLevel}  HP: ${this.localHp}/${this.localMaxHp}${this.localMaxMana > 0 ? `  MP: ${this.localMana}/${this.localMaxMana}` : ''}`);

    // Position class HUD above bars
    const topBarY = this.localMaxMana > 0
      ? this.cameras.main.height - 36 - barHeight - 6 - 18
      : this.cameras.main.height - 56;
    this.classHudText.setY(topBarY);
  }

  update(_time: number, delta: number): void {
    if (!this.connected || !this.playerSprite) return;

    const dt = delta / 1000;

    // ── Update death timer ───────────────────────────────────
    if (!this.localAlive && this.respawnTimer > 0) {
      this.respawnTimer -= delta;
      const secs = Math.max(0, Math.ceil(this.respawnTimer / 1000));
      this.deathText.setText(`YOU DIED\nRespawning in ${secs}...`);
    }

    // ── Sample input ────────────────────────────────────────
    const input = this.inputManager.getInput(this.localX, this.localY);

    // ── Client-side prediction (only if alive) ──────────────
    if (this.localAlive) {
      this.applyInputLocally(input, dt);
    }

    // ── Send to server (always, so server knows aim angle) ──
    this.network.sendInput(input);

    // ── Store for reconciliation ────────────────────────────
    this.pendingInputs.push({ input, dt });

    // ── Update local sprite ─────────────────────────────────
    if (this.localAlive) {
      this.playerSprite.x = this.localX;
      this.playerSprite.y = this.localY;
      this.playerSprite.rotation = input.aimAngle;

      // ── Draw aim line ─────────────────────────────────────
      this.drawAimLine(input.aimAngle);
    }

    // ── Draw HUD ────────────────────────────────────────────
    this.drawLocalHud();

    // ── Interpolate remote players + projectiles ────────────
    this.entityRenderer.update();
  }

  /**
   * Apply input locally for client-side prediction.
   * Uses the synced speed value from the server (class-specific).
   */
  private applyInputLocally(input: InputPayload, dt: number): void {
    let mx = 0;
    let my = 0;
    if (input.up) my -= 1;
    if (input.down) my += 1;
    if (input.left) mx -= 1;
    if (input.right) mx += 1;

    const dir = normalise(mx, my);
    const dx = dir.x * this.localSpeed * dt;
    const dy = dir.y * this.localSpeed * dt;

    if (dx !== 0 || dy !== 0) {
      const resolved = this.resolveCollisionLocal(this.localX, this.localY, dx, dy);
      this.localX = resolved.x;
      this.localY = resolved.y;
    }
  }

  /**
   * Client-side collision resolution (mirrors server logic).
   */
  private resolveCollisionLocal(
    x: number,
    y: number,
    dx: number,
    dy: number,
  ): { x: number; y: number } {
    const radius = PLAYER_COLLISION_RADIUS;

    const isBlocked = (cx: number, cy: number): boolean => {
      const minTX = Math.floor((cx - radius) / TILE_SIZE);
      const maxTX = Math.floor((cx + radius) / TILE_SIZE);
      const minTY = Math.floor((cy - radius) / TILE_SIZE);
      const maxTY = Math.floor((cy + radius) / TILE_SIZE);

      for (let ty = minTY; ty <= maxTY; ty++) {
        for (let tx = minTX; tx <= maxTX; tx++) {
          if (tx < 0 || tx >= this.collisionMapW || ty < 0 || ty >= this.collisionMapH) return true;
          if (this.collisionGrid[ty * this.collisionMapW + tx] !== 1) continue;

          const tileLeft = tx * TILE_SIZE;
          const tileTop = ty * TILE_SIZE;
          const closestX = Math.max(tileLeft, Math.min(cx, tileLeft + TILE_SIZE));
          const closestY = Math.max(tileTop, Math.min(cy, tileTop + TILE_SIZE));
          const ddx = cx - closestX;
          const ddy = cy - closestY;
          if (ddx * ddx + ddy * ddy < radius * radius) return true;
        }
      }
      return false;
    };

    // Try full movement
    let newX = x + dx;
    let newY = y + dy;
    if (!isBlocked(newX, newY)) return { x: newX, y: newY };

    // Slide along axes
    if (!isBlocked(x + dx, y)) return { x: x + dx, y };
    if (!isBlocked(x, y + dy)) return { x, y: y + dy };

    return { x, y };
  }

  /**
   * Server reconciliation: when we get authoritative state, reapply
   * any inputs the server hasn't processed yet.
   */
  private reconcile(serverX: number, serverY: number, serverSeq: number): void {
    // Drop all inputs that have been acknowledged by the server
    this.pendingInputs = this.pendingInputs.filter((p) => p.input.seq > serverSeq);

    // Start from authoritative position
    this.localX = serverX;
    this.localY = serverY;

    // Re-apply unacknowledged inputs
    for (const pending of this.pendingInputs) {
      this.applyInputLocally(pending.input, pending.dt);
    }
  }

  /**
   * Draw a short aim line from the player in the aim direction.
   */
  private drawAimLine(angle: number): void {
    this.aimLine.clear();
    this.aimLine.lineStyle(2, 0xff4444, 0.7);

    const len = 40;
    const startX = this.localX + Math.cos(angle) * 20;
    const startY = this.localY + Math.sin(angle) * 20;
    const endX = this.localX + Math.cos(angle) * (20 + len);
    const endY = this.localY + Math.sin(angle) * (20 + len);

    this.aimLine.beginPath();
    this.aimLine.moveTo(startX, startY);
    this.aimLine.lineTo(endX, endY);
    this.aimLine.strokePath();
  }
}
