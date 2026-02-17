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
  PLAYER_COLLISION_RADIUS,
  RESPAWN_TIME_MS,
  InputPayload,
  normalise,
  CLASS_TEMPLATES,
  ClassId,
  ITEM_CATALOG,
  RARITY_COLORS,
  INVENTORY_MAX_SLOTS,
  ItemId,
  EquipSlotType,
  computeDerivedStats,
  xpRequiredForLevel,
  MapDataPayload,
  TileLayerInfo,
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

  // Map data (received from server)
  private collisionGrid: number[] = [];
  private collisionMapW: number = 0;
  private collisionMapH: number = 0;
  private mapTileSize: number = 64;
  private mapWidthPx: number = 4096;
  private mapHeightPx: number = 4096;
  private tileSprites: Phaser.GameObjects.Sprite[] = [];
  private currentZoneId: string = 'grasslands';

  // Connection state
  private connected: boolean = false;
  private statusText!: Phaser.GameObjects.Text;

  // Class selection (passed from ClassSelectScene)
  private selectedClassId: string = 'warrior';

  // Inventory & Character Panel UI
  private inventoryOpen: boolean = false;
  private inventoryItems: { itemId: string; quantity: number }[] = [];
  private localEquipment: Record<string, string> = { weapon: '', helm: '', chest: '', legs: '', boots: '', ring: '' };
  private localXp: number = 0;
  private invContainer!: Phaser.GameObjects.Container;
  private panelGfx!: Phaser.GameObjects.Graphics;
  private invSlotTexts: Phaser.GameObjects.Text[] = [];
  private invQtyTexts: Phaser.GameObjects.Text[] = [];
  private invTooltipText!: Phaser.GameObjects.Text;
  private invTitleText!: Phaser.GameObjects.Text;
  // Character panel elements
  private charInfoText!: Phaser.GameObjects.Text;
  private charEquipTexts: Phaser.GameObjects.Text[] = [];
  private charStatTexts: Phaser.GameObjects.Text[] = [];
  private charBarsGfx!: Phaser.GameObjects.Graphics;

  // Drag-and-drop state
  private dragging: boolean = false;
  private dragSource: { type: 'inventory' | 'equipment'; index?: number; slotType?: string } | null = null;
  private dragGhost!: Phaser.GameObjects.Text;
  private dragGhostBg!: Phaser.GameObjects.Rectangle;
  // Cached panel positions for hit-testing
  private panelStartX: number = 0;
  private panelY: number = 0;
  private invX: number = 0;
  private charX: number = 0;
  private highlightGfx!: Phaser.GameObjects.Graphics;
  // Equipment slot Y positions for hit-testing (screen coords)
  private equipSlotYPositions: number[] = [];

  constructor() {
    super({ key: 'GameScene' });
  }

  private characterId: number = 0;
  private authToken: string = '';

  init(data?: { classId?: string; characterId?: number; token?: string }): void {
    if (data?.classId) {
      this.selectedClassId = data.classId;
    }
    if (data?.characterId) {
      this.characterId = data.characterId;
    }
    if (data?.token) {
      this.authToken = data.token;
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

    // Create inventory panel (hidden initially)
    this.createInventoryPanel();

    // Inventory toggle key (I)
    this.input.keyboard!.on('keydown-I', () => {
      this.toggleInventory();
    });

    // Setup network callbacks
    this.setupNetworkCallbacks();

    // Connect with auth token and character ID
    this.network.connect({
      characterId: this.characterId,
      token: this.authToken,
    }).catch((err) => {
      this.statusText.setText('Connection failed — is the server running?');
      console.error(err);
    });

    // Set world bounds (will be updated when map data arrives)
    this.cameras.main.setBounds(0, 0, this.mapWidthPx, this.mapHeightPx);
  }

  private setupNetworkCallbacks(): void {
    // Handle full map data (new multi-layer system)
    this.network.onMapData = (data: MapDataPayload) => {
      this.collisionGrid = data.collisionGrid;
      this.collisionMapW = data.width;
      this.collisionMapH = data.height;
      this.mapTileSize = data.tileSize;
      this.mapWidthPx = data.width * data.tileSize;
      this.mapHeightPx = data.height * data.tileSize;
      this.currentZoneId = data.zoneId;

      // Update camera bounds for the new map size
      this.cameras.main.setBounds(0, 0, this.mapWidthPx, this.mapHeightPx);

      this.buildTileMapFromData(data);
    };

    // Legacy fallback
    this.network.onCollisionGrid = (data) => {
      // Only use if onMapData hasn't already been handled
      if (this.collisionGrid.length === 0) {
        this.collisionGrid = data.grid;
        this.collisionMapW = data.width;
        this.collisionMapH = data.height;
        this.mapTileSize = data.tileSize;
        this.mapWidthPx = data.width * data.tileSize;
        this.mapHeightPx = data.height * data.tileSize;
        this.cameras.main.setBounds(0, 0, this.mapWidthPx, this.mapHeightPx);
        this.buildTileMapLegacy(data);
      }
    };

    // Handle zone change notifications
    this.network.onZoneChange = (data) => {
      console.log(`[GameScene] Zone change → ${data.zoneId} (spawn: ${data.spawnX}, ${data.spawnY})`);
      this.currentZoneId = data.zoneId;

      // Teleport the local player to the new spawn
      this.localX = data.spawnX;
      this.localY = data.spawnY;
      if (this.playerSprite) {
        this.playerSprite.setPosition(this.localX, this.localY);
      }

      // Clear pending inputs — server position is authoritative after zone change
      this.pendingInputs = [];
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
        this.localXp = player.xp ?? 0;
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
          player.characterName ?? '',
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
        this.localXp = player.xp ?? this.localXp;
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

    // Inventory sync
    this.network.onInventoryChange = (items: any[]) => {
      this.inventoryItems = items;
      if (this.inventoryOpen) {
        this.renderInventorySlots();
      }
    };

    // Equipment sync
    this.network.onEquipmentChange = (equipment: Record<string, string>) => {
      this.localEquipment = equipment;
      if (this.inventoryOpen) {
        this.renderCharacterPanel();
      }
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
  /** GID → texture key mapping for all tilesets. */
  private static readonly GID_TEXTURE_MAP: Record<number, string> = {
    // Default tileset (GID 1–10)
    1: 'tile_grass_light',
    2: 'tile_grass_dark',
    3: 'tile_dirt',
    4: 'tile_water',
    5: 'tile_stone_wall',
    6: 'tile_wood_fence',
    7: 'tile_tree',
    8: 'tile_stone_floor',
    9: 'tile_portal',
    10: 'tile_void',
    // Desert tileset (GID 11–20)
    11: 'tile_sand_light',
    12: 'tile_sand_dark',
    13: 'tile_sand_road',
    14: 'tile_oasis_water',
    15: 'tile_sandstone_wall',
    16: 'tile_cactus',
    17: 'tile_dead_tree',
    18: 'tile_desert_rock',
    19: 'tile_ruins_floor',
    20: 'tile_quicksand',
  };

  /**
   * Build multi-layer tile map from server MapDataPayload.
   * Renders each tile layer in order, with proper depth sorting.
   */
  private buildTileMapFromData(data: MapDataPayload): void {
    // Clear existing tiles
    for (const sprite of this.tileSprites) {
      sprite.destroy();
    }
    this.tileSprites = [];

    const { tileLayers, tileSize, width, height } = data;

    let layerDepth = 0;
    for (const layer of tileLayers) {
      if (!layer.visible) {
        layerDepth++;
        continue;
      }

      for (let y = 0; y < layer.height; y++) {
        for (let x = 0; x < layer.width; x++) {
          const gid = layer.data[y * layer.width + x];
          if (gid === 0) continue; // Empty tile, skip

          const textureKey = GameScene.GID_TEXTURE_MAP[gid] ?? 'tile_void';
          const sprite = this.add.sprite(
            x * tileSize + tileSize / 2,
            y * tileSize + tileSize / 2,
            textureKey,
          );
          sprite.setDepth(layerDepth);
          if (layer.opacity < 1) {
            sprite.setAlpha(layer.opacity);
          }
          this.tileSprites.push(sprite);
        }
      }
      layerDepth++;
    }
  }

  /**
   * Legacy tile map builder (old collision-grid-only format).
   */
  private buildTileMapLegacy(data: { grid: number[]; width: number; height: number; tileSize: number }): void {
    for (const sprite of this.tileSprites) {
      sprite.destroy();
    }
    this.tileSprites = [];

    const { grid, width, height, tileSize } = data;
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const isWall = grid[y * width + x] === 1;
        const sprite = this.add.sprite(
          x * tileSize + tileSize / 2,
          y * tileSize + tileSize / 2,
          isWall ? 'tile_wall' : 'tile_ground',
        );
        sprite.setDepth(isWall ? 1 : 0);
        this.tileSprites.push(sprite);
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

  // ── Inventory & Character Panel UI ───────────────────────

  // Layout constants
  private readonly INV_COLS = 8;
  private readonly INV_ROWS = 4;
  private readonly SLOT_SIZE = 48;
  private readonly SLOT_GAP = 4;
  private readonly CHAR_PANEL_W = 220;
  private readonly PANEL_GAP = 8;

  private get invPanelW(): number {
    return this.INV_COLS * (this.SLOT_SIZE + this.SLOT_GAP) + this.SLOT_GAP + 16;
  }

  private readonly PANEL_H = 340;

  private get invPanelH(): number {
    return this.PANEL_H;
  }

  private get totalPanelW(): number {
    return this.CHAR_PANEL_W + this.PANEL_GAP + this.invPanelW;
  }

  private createInventoryPanel(): void {
    const cam = this.cameras.main;
    this.invContainer = this.add.container(0, 0);
    this.invContainer.setScrollFactor(0);
    this.invContainer.setDepth(300);
    this.invContainer.setVisible(false);

    // Shared graphics layer for both panels
    this.panelGfx = this.add.graphics();
    this.charBarsGfx = this.add.graphics();

    // Fullscreen dim overlay
    const dimBg = this.add.rectangle(cam.width / 2, cam.height / 2, cam.width, cam.height, 0x000000, 0.4);
    this.invContainer.add(dimBg);

    // Panel positions — both centered together, using the taller panel for vertical centering
    const maxPanelH = Math.max(this.PANEL_H, this.invPanelH);
    const startX = Math.floor((cam.width - this.totalPanelW) / 2);
    const panelY = Math.floor((cam.height - maxPanelH) / 2);
    const charX = startX;
    const invX = startX + this.CHAR_PANEL_W + this.PANEL_GAP;

    // Cache for drag-and-drop hit testing
    this.panelStartX = startX;
    this.panelY = panelY;
    this.invX = invX;
    this.charX = charX;

    // ── Character Panel Background ──
    const charBg = this.add.rectangle(
      charX + this.CHAR_PANEL_W / 2, panelY + this.PANEL_H / 2,
      this.CHAR_PANEL_W, this.PANEL_H,
      0x1a1a2e, 0.95,
    );
    charBg.setStrokeStyle(2, 0x555588);
    this.invContainer.add(charBg);

    // ── Inventory Panel Background ──
    const invBg = this.add.rectangle(
      invX + this.invPanelW / 2, panelY + this.invPanelH / 2,
      this.invPanelW, this.invPanelH,
      0x1a1a2e, 0.95,
    );
    invBg.setStrokeStyle(2, 0x555588);
    this.invContainer.add(invBg);

    // Add graphics layers
    this.invContainer.add(this.panelGfx);
    this.invContainer.add(this.charBarsGfx);

    // ── Build Character Panel ──
    this.createCharacterPanelContent(charX, panelY);

    // ── Build Inventory Panel ──
    this.invTitleText = this.add.text(invX + 8, panelY + 8, 'Inventory', {
      fontSize: '14px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.invContainer.add(this.invTitleText);

    // Inventory slot texts (no zones — we use scene-level pointer hit testing)
    this.invSlotTexts = [];
    this.invQtyTexts = [];
    for (let i = 0; i < INVENTORY_MAX_SLOTS; i++) {
      const col = i % this.INV_COLS;
      const row = Math.floor(i / this.INV_COLS);
      const sx = invX + this.SLOT_GAP + 8 + col * (this.SLOT_SIZE + this.SLOT_GAP) + this.SLOT_SIZE / 2;
      const sy = panelY + 36 + this.SLOT_GAP + row * (this.SLOT_SIZE + this.SLOT_GAP) + this.SLOT_SIZE / 2;

      const nameText = this.add.text(sx, sy - 6, '', {
        fontSize: '10px',
        color: '#ffffff',
        stroke: '#000000',
        strokeThickness: 1,
        align: 'center',
      });
      nameText.setOrigin(0.5, 0.5);
      this.invContainer.add(nameText);
      this.invSlotTexts.push(nameText);

      const qtyText = this.add.text(sx + this.SLOT_SIZE / 2 - 6, sy + this.SLOT_SIZE / 2 - 10, '', {
        fontSize: '10px',
        color: '#ffff00',
        stroke: '#000000',
        strokeThickness: 2,
        align: 'right',
      });
      qtyText.setOrigin(1, 1);
      this.invContainer.add(qtyText);
      this.invQtyTexts.push(qtyText);
    }

    // Tooltip below both panels
    this.invTooltipText = this.add.text(startX, panelY + maxPanelH + 8, '', {
      fontSize: '12px',
      color: '#cccccc',
      stroke: '#000000',
      strokeThickness: 2,
      wordWrap: { width: this.totalPanelW },
    });
    this.invContainer.add(this.invTooltipText);

    // Highlight graphics for drag hover
    this.highlightGfx = this.add.graphics();
    this.invContainer.add(this.highlightGfx);

    // Drag ghost (hidden by default)
    this.dragGhostBg = this.add.rectangle(0, 0, 52, 28, 0x000000, 0.85);
    this.dragGhostBg.setStrokeStyle(1, 0xffcc00);
    this.dragGhostBg.setVisible(false);
    this.dragGhostBg.setDepth(999);
    this.invContainer.add(this.dragGhostBg);

    this.dragGhost = this.add.text(0, 0, '', {
      fontSize: '11px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.dragGhost.setOrigin(0.5, 0.5);
    this.dragGhost.setVisible(false);
    this.dragGhost.setDepth(1000);
    this.invContainer.add(this.dragGhost);

    // Setup drag-and-drop input handlers
    this.setupDragAndDrop();
  }

  private createCharacterPanelContent(charX: number, panelY: number): void {
    const x = charX + 10;
    let y = panelY + 8;

    // Title
    const titleText = this.add.text(x, y, 'Character', {
      fontSize: '14px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.invContainer.add(titleText);
    y += 22;

    // Class + Level + XP/HP/Mana info (one text block, updated dynamically)
    this.charInfoText = this.add.text(x, y, '', {
      fontSize: '11px',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 1,
      lineSpacing: 4,
    });
    this.invContainer.add(this.charInfoText);
    // charInfoText height depends on mana — reserve ~60px
    y += 60;

    // ── Equipment Section ──
    const equipLabel = this.add.text(x, y, '── Equipment ──', {
      fontSize: '10px',
      color: '#aaaacc',
      stroke: '#000000',
      strokeThickness: 1,
    });
    this.invContainer.add(equipLabel);
    y += 16;

    const slotLabels = ['Weapon', 'Helm', 'Chest', 'Legs', 'Boots', 'Ring'];
    this.charEquipTexts = [];
    this.equipSlotYPositions = [];
    for (let i = 0; i < slotLabels.length; i++) {
      const label = slotLabels[i];
      const slotLabel = this.add.text(x, y, `${label}:`, {
        fontSize: '10px',
        color: '#888888',
        stroke: '#000000',
        strokeThickness: 1,
      });
      this.invContainer.add(slotLabel);

      const itemText = this.add.text(x + 55, y, 'empty', {
        fontSize: '10px',
        color: '#555555',
        stroke: '#000000',
        strokeThickness: 1,
      });
      this.invContainer.add(itemText);
      this.charEquipTexts.push(itemText);

      // Track Y position for hit testing
      this.equipSlotYPositions.push(y);

      y += 14;
    }

    y += 6;

    // ── Stats Section ──
    const statsLabel = this.add.text(x, y, '── Stats ──', {
      fontSize: '10px',
      color: '#aaaacc',
      stroke: '#000000',
      strokeThickness: 1,
    });
    this.invContainer.add(statsLabel);
    y += 16;

    // 2-column layout: 7 rows of 2 stats each
    const statDefs: { label: string; key: string; pct?: boolean }[] = [
      { label: 'STR', key: 'strength' },
      { label: 'INT', key: 'intelligence' },
      { label: 'STA', key: 'stamina' },
      { label: 'WIS', key: 'wisdom' },
      { label: 'DEX', key: 'dexterity' },
      { label: 'P.Res', key: 'physicalResist' },
      { label: 'Crit', key: 'critChance', pct: true },
      { label: 'S.Res', key: 'spellResist' },
      { label: 'CDmg', key: 'critDamage', pct: true },
      { label: 'P.Def', key: 'physicalDefense' },
      { label: 'Block', key: 'blockRating', pct: true },
      { label: 'Dodge', key: 'dodgeRating', pct: true },
      { label: 'HP', key: 'hp' },
      { label: 'Mana', key: 'mana' },
    ];

    this.charStatTexts = [];
    for (let i = 0; i < statDefs.length; i += 2) {
      const col2X = x + 105;
      // Left column
      const leftText = this.add.text(x, y, '', {
        fontSize: '10px',
        color: '#cccccc',
        stroke: '#000000',
        strokeThickness: 1,
      });
      this.invContainer.add(leftText);
      this.charStatTexts.push(leftText);

      // Right column
      if (i + 1 < statDefs.length) {
        const rightText = this.add.text(col2X, y, '', {
          fontSize: '10px',
          color: '#cccccc',
          stroke: '#000000',
          strokeThickness: 1,
        });
        this.invContainer.add(rightText);
        this.charStatTexts.push(rightText);
      }

      y += 14;
    }
  }

  // ── Drag-and-Drop ─────────────────────────────────────────

  private setupDragAndDrop(): void {
    // All drag-and-drop uses scene-level pointer events with manual hit testing.
    // This avoids the Phaser container + zone interaction bug.

    // Pointerdown — start drag if clicking on an item slot
    this.input.on('pointerdown', (pointer: Phaser.Input.Pointer) => {
      if (!this.inventoryOpen) return;

      const px = pointer.x;
      const py = pointer.y;

      // Check inventory slot
      const invSlot = this.getInventorySlotAt(px, py);
      if (invSlot >= 0) {
        const item = this.inventoryItems[invSlot];
        if (item) {
          const template = ITEM_CATALOG[item.itemId as ItemId];
          if (template) {
            this.startDrag(
              { type: 'inventory', index: invSlot },
              template.name,
              RARITY_COLORS[template.rarity] ?? '#ffffff',
            );
            return;
          }
        }
      }

      // Check equipment slot
      const equipSlot = this.getEquipSlotAt(px, py);
      if (equipSlot) {
        const equippedId = this.localEquipment[equipSlot] || '';
        if (equippedId) {
          const template = ITEM_CATALOG[equippedId as ItemId];
          if (template) {
            this.startDrag(
              { type: 'equipment', slotType: equipSlot },
              template.name,
              RARITY_COLORS[template.rarity] ?? '#ffffff',
            );
          }
        }
      }
    });

    // Pointermove — update drag ghost position + hover tooltips
    this.input.on('pointermove', (pointer: Phaser.Input.Pointer) => {
      if (!this.inventoryOpen) return;

      const px = pointer.x;
      const py = pointer.y;

      if (this.dragging) {
        this.dragGhost.setPosition(px + 16, py);
        this.dragGhostBg.setPosition(px + 16, py);
        this.updateDragHighlight(px, py);
      } else {
        // Hover tooltip
        this.updateHoverTooltip(px, py);
      }
    });

    // Pointerup — handle drop
    this.input.on('pointerup', (pointer: Phaser.Input.Pointer) => {
      if (!this.dragging || !this.dragSource) return;
      this.handleDrop(pointer.x, pointer.y);
      this.endDrag();
    });
  }

  /**
   * Update tooltip text based on what slot the pointer is hovering.
   */
  private updateHoverTooltip(px: number, py: number): void {
    // Check inventory slot
    const invSlot = this.getInventorySlotAt(px, py);
    if (invSlot >= 0) {
      const item = this.inventoryItems[invSlot];
      if (item) {
        const t = ITEM_CATALOG[item.itemId as ItemId];
        if (t) {
          this.invTooltipText.setText(`${t.name} — ${t.description}`);
          return;
        }
      }
    }

    // Check equipment slot
    const equipSlot = this.getEquipSlotAt(px, py);
    if (equipSlot) {
      const equippedId = this.localEquipment[equipSlot] || '';
      if (equippedId) {
        const t = ITEM_CATALOG[equippedId as ItemId];
        if (t) {
          this.invTooltipText.setText(`${t.name} — ${t.description}`);
          return;
        }
      }
    }

    // Default tooltip
    if (this.inventoryItems.length === 0) {
      this.invTooltipText.setText('Your inventory is empty.');
    } else {
      this.invTooltipText.setText('Drag items to move, equip, or drop on ground.');
    }
  }

  private startDrag(
    source: { type: 'inventory' | 'equipment'; index?: number; slotType?: string },
    itemName: string,
    color: string,
  ): void {
    this.dragging = true;
    this.dragSource = source;

    const abbr = itemName.length > 8 ? itemName.slice(0, 7) + '.' : itemName;
    this.dragGhost.setText(abbr);
    this.dragGhost.setColor(color);
    this.dragGhost.setVisible(true);

    // Size the background to fit the text
    const textWidth = Math.max(this.dragGhost.width + 12, 52);
    this.dragGhostBg.setSize(textWidth, 22);
    this.dragGhostBg.setVisible(true);
  }

  private endDrag(): void {
    this.dragging = false;
    this.dragSource = null;
    this.dragGhost.setVisible(false);
    this.dragGhostBg.setVisible(false);
    this.highlightGfx.clear();
  }

  /**
   * Get the inventory slot index at a given screen position, or -1 if none.
   */
  private getInventorySlotAt(px: number, py: number): number {
    for (let i = 0; i < INVENTORY_MAX_SLOTS; i++) {
      const col = i % this.INV_COLS;
      const row = Math.floor(i / this.INV_COLS);
      const sx = this.invX + this.SLOT_GAP + 8 + col * (this.SLOT_SIZE + this.SLOT_GAP);
      const sy = this.panelY + 36 + this.SLOT_GAP + row * (this.SLOT_SIZE + this.SLOT_GAP);

      if (px >= sx && px <= sx + this.SLOT_SIZE && py >= sy && py <= sy + this.SLOT_SIZE) {
        return i;
      }
    }
    return -1;
  }

  /**
   * Get the equipment slot type at a given screen position, or null if none.
   */
  private getEquipSlotAt(px: number, py: number): string | null {
    const slotTypes = ['weapon', 'helm', 'chest', 'legs', 'boots', 'ring'];
    const rowH = 14;
    for (let i = 0; i < this.equipSlotYPositions.length; i++) {
      const slotY = this.equipSlotYPositions[i];
      if (px >= this.charX + 5 && px <= this.charX + this.CHAR_PANEL_W - 5 &&
          py >= slotY - 2 && py <= slotY + rowH) {
        return slotTypes[i];
      }
    }
    return null;
  }

  /**
   * Check if a point is inside either panel.
   */
  private isInsidePanels(px: number, py: number): boolean {
    // Character panel bounds
    if (px >= this.charX && px <= this.charX + this.CHAR_PANEL_W &&
        py >= this.panelY && py <= this.panelY + this.PANEL_H) {
      return true;
    }
    // Inventory panel bounds
    if (px >= this.invX && px <= this.invX + this.invPanelW &&
        py >= this.panelY && py <= this.panelY + this.invPanelH) {
      return true;
    }
    return false;
  }

  /**
   * Draw a highlight rectangle over the slot being hovered during drag.
   */
  private updateDragHighlight(px: number, py: number): void {
    this.highlightGfx.clear();

    // Check inventory slot hover
    const invSlot = this.getInventorySlotAt(px, py);
    if (invSlot >= 0) {
      const col = invSlot % this.INV_COLS;
      const row = Math.floor(invSlot / this.INV_COLS);
      const sx = this.invX + this.SLOT_GAP + 8 + col * (this.SLOT_SIZE + this.SLOT_GAP);
      const sy = this.panelY + 36 + this.SLOT_GAP + row * (this.SLOT_SIZE + this.SLOT_GAP);

      this.highlightGfx.lineStyle(2, 0xffcc00, 0.9);
      this.highlightGfx.strokeRect(sx, sy, this.SLOT_SIZE, this.SLOT_SIZE);
      this.highlightGfx.fillStyle(0xffcc00, 0.15);
      this.highlightGfx.fillRect(sx, sy, this.SLOT_SIZE, this.SLOT_SIZE);
      return;
    }

    // Check equip slot hover
    const equipSlot = this.getEquipSlotAt(px, py);
    if (equipSlot) {
      const slotTypes = ['weapon', 'helm', 'chest', 'legs', 'boots', 'ring'];
      const idx = slotTypes.indexOf(equipSlot);
      if (idx >= 0) {
        const slotY = this.equipSlotYPositions[idx];
        const rx = this.charX + 5;
        const ry = slotY - 2;
        const rw = this.CHAR_PANEL_W - 10;
        const rh = 14;
        this.highlightGfx.lineStyle(2, 0xffcc00, 0.9);
        this.highlightGfx.strokeRect(rx, ry, rw, rh);
        this.highlightGfx.fillStyle(0xffcc00, 0.15);
        this.highlightGfx.fillRect(rx, ry, rw, rh);
      }
      return;
    }

    // If outside panels — show red drop indicator
    if (!this.isInsidePanels(px, py)) {
      this.dragGhostBg.setStrokeStyle(1, 0xff4444);
    } else {
      this.dragGhostBg.setStrokeStyle(1, 0xffcc00);
    }
  }

  /**
   * Handle the drop action based on where the pointer was released.
   */
  private handleDrop(px: number, py: number): void {
    if (!this.dragSource) return;

    const targetInvSlot = this.getInventorySlotAt(px, py);
    const targetEquipSlot = this.getEquipSlotAt(px, py);
    const insidePanels = this.isInsidePanels(px, py);

    if (this.dragSource.type === 'inventory') {
      const fromIndex = this.dragSource.index!;

      if (targetEquipSlot) {
        // Inventory → Equipment: equip the item
        // Check if the item matches the target equip slot
        const item = this.inventoryItems[fromIndex];
        if (item) {
          const template = ITEM_CATALOG[item.itemId as ItemId];
          if (template?.equipSlot === targetEquipSlot) {
            this.network.sendEquipItem(fromIndex);
          }
        }
      } else if (targetInvSlot >= 0 && targetInvSlot !== fromIndex) {
        // Inventory → Inventory: swap slots
        this.network.sendSwapInventory(fromIndex, targetInvSlot);
      } else if (!insidePanels) {
        // Inventory → Ground: drop item
        this.network.sendDropItem('inventory', fromIndex);
      }
    } else if (this.dragSource.type === 'equipment') {
      const slotType = this.dragSource.slotType!;

      if (targetInvSlot >= 0) {
        // Equipment → Inventory: unequip to specific slot
        this.network.sendUnequipItem(slotType, targetInvSlot);
      } else if (!insidePanels) {
        // Equipment → Ground: drop equipped item
        this.network.sendDropItem('equipment', undefined, slotType);
      }
    }
  }

  private toggleInventory(): void {
    this.inventoryOpen = !this.inventoryOpen;
    this.invContainer.setVisible(this.inventoryOpen);
    if (this.inventoryOpen) {
      this.renderInventorySlots();
      this.renderCharacterPanel();
    }
  }

  private renderCharacterPanel(): void {
    // ── Player Info ──
    const template = CLASS_TEMPLATES[this.localClassId as ClassId];
    const className = template?.name ?? this.localClassId;
    const xpNeeded = xpRequiredForLevel(this.localLevel);
    const xpStr = xpNeeded === Infinity ? 'MAX' : `${this.localXp}/${xpNeeded}`;

    let info = `${className}  Lv.${this.localLevel}\n`;
    info += `XP: ${xpStr}\n`;
    info += `HP: ${this.localHp}/${this.localMaxHp}`;
    if (this.localMaxMana > 0) {
      info += `\nMP: ${this.localMana}/${this.localMaxMana}`;
    }
    this.charInfoText.setText(info);

    // ── Equipment Slots ──
    const slotKeys = ['weapon', 'helm', 'chest', 'legs', 'boots', 'ring'];
    for (let i = 0; i < slotKeys.length; i++) {
      const equippedId = this.localEquipment[slotKeys[i]] || '';
      if (equippedId) {
        const itemTemplate = ITEM_CATALOG[equippedId as ItemId];
        if (itemTemplate) {
          this.charEquipTexts[i].setText(itemTemplate.name);
          this.charEquipTexts[i].setColor(RARITY_COLORS[itemTemplate.rarity] ?? '#ffffff');
        } else {
          this.charEquipTexts[i].setText(equippedId);
          this.charEquipTexts[i].setColor('#ffffff');
        }
      } else {
        this.charEquipTexts[i].setText('empty');
        this.charEquipTexts[i].setColor('#555555');
      }
    }

    // ── Stats ──
    // Compute base stats from class + level
    const baseStats = computeDerivedStats(this.localClassId as ClassId, this.localLevel);

    // Add equipment bonuses (display-only — server is authoritative)
    const finalStats = { ...baseStats };
    for (const key of slotKeys) {
      const equippedId = this.localEquipment[key] || '';
      if (equippedId) {
        const itemTemplate = ITEM_CATALOG[equippedId as ItemId];
        if (itemTemplate?.statBonuses) {
          for (const [stat, bonus] of Object.entries(itemTemplate.statBonuses)) {
            if (stat in finalStats) {
              (finalStats as any)[stat] += bonus;
            }
          }
        }
      }
    }

    // Stat display definitions (must match createCharacterPanelContent order)
    const statDefs: { label: string; key: string; pct?: boolean }[] = [
      { label: 'STR', key: 'strength' },
      { label: 'INT', key: 'intelligence' },
      { label: 'STA', key: 'stamina' },
      { label: 'WIS', key: 'wisdom' },
      { label: 'DEX', key: 'dexterity' },
      { label: 'P.Res', key: 'physicalResist' },
      { label: 'Crit', key: 'critChance', pct: true },
      { label: 'S.Res', key: 'spellResist' },
      { label: 'CDmg', key: 'critDamage', pct: true },
      { label: 'P.Def', key: 'physicalDefense' },
      { label: 'Block', key: 'blockRating', pct: true },
      { label: 'Dodge', key: 'dodgeRating', pct: true },
      { label: 'HP', key: 'hp' },
      { label: 'Mana', key: 'mana' },
    ];

    for (let i = 0; i < statDefs.length; i++) {
      const def = statDefs[i];
      const val = (finalStats as any)[def.key] ?? 0;
      const baseVal = (baseStats as any)[def.key] ?? 0;
      const bonus = val - baseVal;

      let display: string;
      if (def.pct) {
        display = `${def.label}  ${(val * 100).toFixed(0)}%`;
      } else {
        display = `${def.label}  ${Math.round(val)}`;
      }

      // Show bonus in green if gear adds to this stat
      if (bonus > 0) {
        if (def.pct) {
          display += ` (+${(bonus * 100).toFixed(0)}%)`;
        } else {
          display += ` (+${Math.round(bonus)})`;
        }
        this.charStatTexts[i].setColor('#44ff44');
      } else {
        this.charStatTexts[i].setColor('#cccccc');
      }

      this.charStatTexts[i].setText(display);
    }
  }

  private renderInventorySlots(): void {
    // Use cached panel positions (set in createInventoryPanel)
    const invX = this.invX;
    const panelY = this.panelY;

    this.panelGfx.clear();

    for (let i = 0; i < INVENTORY_MAX_SLOTS; i++) {
      const col = i % this.INV_COLS;
      const row = Math.floor(i / this.INV_COLS);
      const sx = invX + this.SLOT_GAP + 8 + col * (this.SLOT_SIZE + this.SLOT_GAP);
      const sy = panelY + 36 + this.SLOT_GAP + row * (this.SLOT_SIZE + this.SLOT_GAP);

      const item = this.inventoryItems[i];
      const itemTemplate = item ? ITEM_CATALOG[item.itemId as ItemId] : null;

      // Slot background
      if (itemTemplate) {
        const rarityColor = RARITY_COLORS[itemTemplate.rarity] ?? '#333333';
        this.panelGfx.fillStyle(Phaser.Display.Color.HexStringToColor(rarityColor).color, 0.25);
      } else {
        this.panelGfx.fillStyle(0x222244, 0.6);
      }
      this.panelGfx.fillRect(sx, sy, this.SLOT_SIZE, this.SLOT_SIZE);

      // Slot border
      this.panelGfx.lineStyle(1, itemTemplate ? 0x888888 : 0x444466, 1);
      this.panelGfx.strokeRect(sx, sy, this.SLOT_SIZE, this.SLOT_SIZE);

      // Update text
      if (itemTemplate) {
        const abbr = itemTemplate.name.length > 8 ? itemTemplate.name.slice(0, 7) + '.' : itemTemplate.name;
        this.invSlotTexts[i].setText(abbr);
        this.invSlotTexts[i].setColor(RARITY_COLORS[itemTemplate.rarity] ?? '#ffffff');
        this.invQtyTexts[i].setText(item.quantity > 1 ? `${item.quantity}` : '');
      } else {
        this.invSlotTexts[i].setText('');
        this.invQtyTexts[i].setText('');
      }
    }

    // Tooltip — shows on hover via zones; default message
    if (this.inventoryItems.length === 0) {
      this.invTooltipText.setText('Your inventory is empty.');
    } else {
      this.invTooltipText.setText('Drag items to move, equip, or drop on ground.');
    }
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

    // ── Skip gameplay input while inventory is open ─────────
    if (!this.inventoryOpen) {
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

    const ts = this.mapTileSize;
    const isBlocked = (cx: number, cy: number): boolean => {
      const minTX = Math.floor((cx - radius) / ts);
      const maxTX = Math.floor((cx + radius) / ts);
      const minTY = Math.floor((cy - radius) / ts);
      const maxTY = Math.floor((cy + radius) / ts);

      for (let ty = minTY; ty <= maxTY; ty++) {
        for (let tx = minTX; tx <= maxTX; tx++) {
          if (tx < 0 || tx >= this.collisionMapW || ty < 0 || ty >= this.collisionMapH) return true;
          if (this.collisionGrid[ty * this.collisionMapW + tx] !== 1) continue;

          const tileLeft = tx * ts;
          const tileTop = ty * ts;
          const closestX = Math.max(tileLeft, Math.min(cx, tileLeft + ts));
          const closestY = Math.max(tileTop, Math.min(cy, tileTop + ts));
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
