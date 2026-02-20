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
  ClassId,
  RARITY_COLORS,
  INVENTORY_MAX_SLOTS,
  ItemId,
  EquipSlotType,
  computeDerivedStats,
  xpRequiredForLevel,
  MapDataPayload,
  TileLayerInfo,
  SkillId,
  SkillTemplate,
  ACTION_BAR_SLOTS,
  getAvailableSkills,
  ChatMessagePayload,
  orthoToIso,
  isoToOrtho,
  tileToIso,
  ORTHO_TILE_SIZE,
} from '@valhalla/shared';
import { ClientDataManager } from '../systems/ClientDataManager.js';
import { PlayerTargetInfo, NpcTargetInfo, ENTITY_DEPTH_BASE, UI_DEPTH_BASE } from '../systems/EntityRenderer.js';

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
  /** True when the current zone uses isometric rendering. */
  private isIso: boolean = false;
  /** Last facing direction for idle animation. */
  private lastFacingDir: string = 'down';

  // Local player vitals
  private localHp: number = 100;
  private localMaxHp: number = 100;
  private localMana: number = 0;
  private localMaxMana: number = 0;
  private localAlive: boolean = true;
  private localSpeed: number = 200;
  private localClassId: string = 'warrior';
  private localLevel: number = 1;
  private localCharacterName: string = '';
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
  // Zone-filtering: track every remote player's zone and last-known data
  // so we only render players in the same zone as us.
  private remotePlayerZones: Map<string, string> = new Map();
  private remotePlayerCache: Map<string, any> = new Map();
  // Zone-filtering for projectiles: track which projectile IDs are currently
  // rendered (i.e. their owner is in our zone).
  private visibleProjectiles: Set<string> = new Set();
  private visibleSpellProjectiles: Set<string> = new Set();
  // Zone-filtering for NPCs: track each NPC's zone and cache data so we can
  // re-render them when the local player changes zones.
  private npcZones: Map<string, string> = new Map();
  private npcCache: Map<string, any> = new Map();
  private visibleNpcs: Set<string> = new Set();

  // ── Draggable HUD panels ──────────────────────────────────
  // Per-panel offsets from their default computed positions (pixels).
  private chatOffset:      { x: number; y: number } = { x: 0, y: 0 };
  private actionBarOffset: { x: number; y: number } = { x: 0, y: 0 };
  private invOffset:       { x: number; y: number } = { x: 0, y: 0 };
  private skillsOffset:    { x: number; y: number } = { x: 0, y: 0 };
  // Active drag state
  private hudDragTarget: 'chat' | 'actionBar' | 'inventory' | 'skills' | 'target' | null = null;
  private hudDragStartMouse:  { x: number; y: number } = { x: 0, y: 0 };
  private hudDragStartOffset: { x: number; y: number } = { x: 0, y: 0 };
  // Cached screen rects for the action bar and skills pane (updated each draw).
  private actionBarScreenRect: { x: number; y: number; w: number; h: number } = { x: 0, y: 0, w: 0, h: 0 };
  private skillsPaneScreenRect: { x: number; y: number; w: number; h: number } = { x: 0, y: 0, w: 420, h: 360 };

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
  private invDimBg!: Phaser.GameObjects.Rectangle;    // fixed dim overlay, NOT inside invContainer
  private skillsDimBg!: Phaser.GameObjects.Rectangle; // fixed dim overlay, NOT inside skillsPaneContainer
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

  // ── Skill System UI State ────────────────────────────────
  private actionBar: string[] = ['', '', '', '', '', '', '', ''];
  private actionBarContainer!: Phaser.GameObjects.Container;
  private actionBarGfx!: Phaser.GameObjects.Graphics;
  private actionBarSlotTexts: Phaser.GameObjects.Text[] = [];
  private actionBarKeyTexts: Phaser.GameObjects.Text[] = [];
  private actionBarCooldownGfx!: Phaser.GameObjects.Graphics;
  private actionBarCooldownTexts: Phaser.GameObjects.Text[] = [];

  // Cast bar
  private castBarContainer!: Phaser.GameObjects.Container;
  private castBarGfx!: Phaser.GameObjects.Graphics;
  private castBarText!: Phaser.GameObjects.Text;
  private localCastingSkillId: string = '';
  private localCastingStartedAt: number = 0;
  private localCastingDurationMs: number = 0;

  // Energy
  private localEnergy: number = 0;
  private localMaxEnergy: number = 0;

  // Skills pane
  private skillsPaneOpen: boolean = false;
  private skillsPaneContainer!: Phaser.GameObjects.Container;
  private skillsPaneGfx!: Phaser.GameObjects.Graphics;
  private skillsPaneSkillRows: Phaser.GameObjects.Text[] = [];

  // Skills pane drag state
  private skillDragging: boolean = false;
  private skillDragId: string = '';
  private skillDragGhost!: Phaser.GameObjects.Text;
  private skillDragGhostBg!: Phaser.GameObjects.Rectangle;

  // Skills pane tooltip
  private skillTooltipContainer!: Phaser.GameObjects.Container;
  private skillTooltipBg!: Phaser.GameObjects.Graphics;
  private skillTooltipText!: Phaser.GameObjects.Text;

  // Cooldown tracking (local display)
  private skillCooldowns: Map<string, { expiresAt: number; durationMs: number }> = new Map();

  // ── Chat System ──────────────────────────────────────────
  private chatContainer!: Phaser.GameObjects.Container;
  private chatBgGfx!: Phaser.GameObjects.Graphics;
  private chatInputBgGfx!: Phaser.GameObjects.Graphics;
  private chatMessageTexts: Phaser.GameObjects.Text[] = [];
  private chatInputDisplay!: Phaser.GameObjects.Text;
  private chatChannelLabel!: Phaser.GameObjects.Text;
  /** Full message history (newest last). */
  private chatMessages: Array<{ text: string; color: string }> = [];
  private chatInputActive: boolean = false;
  private chatInputText: string = '';
  /** Default send channel. Whispers are always triggered by /w prefix. */
  private chatCurrentChannel: 'general' | 'world' = 'general';

  // ── Targeting System ─────────────────────────────────────
  /** ID of the currently targeted entity (player sessionId or NPC id), or null. */
  private currentTargetId: string | null = null;
  /** Type of the currently targeted entity. */
  private currentTargetType: 'player' | 'npc' | null = null;
  /**
   * Set to true when an entity sprite was just clicked, so the global
   * pointer-down handler (which moves the player) can skip that frame.
   */
  private entityClickConsumed: boolean = false;

  // Target nameplate panel
  private targetOffset: { x: number; y: number } = { x: 0, y: 0 };
  private targetNameplateContainer!: Phaser.GameObjects.Container;
  private targetNameplateNameText!: Phaser.GameObjects.Text;
  private targetNameplateLevelText!: Phaser.GameObjects.Text;
  private targetNameplateHpBar!: Phaser.GameObjects.Graphics;
  private targetNameplateHpText!: Phaser.GameObjects.Text;
  private targetNameplateTitleHandle!: { x: number; y: number; w: number; h: number };

  // Layout constants
  private readonly CHAT_MAX_W = 360;   // maximum panel width
  private readonly CHAT_H = 170;
  private readonly CHAT_BOTTOM_MARGIN = 8;
  private readonly CHAT_LINE_H = 15;
  private readonly CHAT_PAD = 6;
  private readonly CHAT_INPUT_H = 18;
  private readonly CHAT_MAX_MESSAGES = 50;
  private readonly CHAT_VISIBLE_LINES = 9; // (CHAT_H - header - input) / CHAT_LINE_H
  /** Left edge of chat panel — 12px gap right of the HP/mana bar (barX=16 + barWidth=200 + border=4 + gap=12) */
  private readonly CHAT_LEFT_X = 232;
  /** Tracks current effective width so word-wrap is only recalculated on change */
  private chatEffectiveW = 200;

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
    this.statusText.setDepth(UI_DEPTH_BASE);

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
    this.deathOverlay.setDepth(UI_DEPTH_BASE + 100);
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
    this.deathText.setDepth(UI_DEPTH_BASE + 101);
    this.deathText.setVisible(false);

    // Local HP/Mana bar (HUD)
    this.hpBarGfx = this.add.graphics();
    this.hpBarGfx.setScrollFactor(0);
    this.hpBarGfx.setDepth(UI_DEPTH_BASE);

    // Class + Level HUD
    this.classHudText = this.add.text(16, this.cameras.main.height - 58, '', {
      fontSize: '12px',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.classHudText.setScrollFactor(0);
    this.classHudText.setDepth(UI_DEPTH_BASE);

    // Create inventory panel (hidden initially)
    this.createInventoryPanel();

    // Create skill system UI
    this.createActionBar();
    this.createCastBar();
    this.createSkillsPane();

    // Create chat panel
    this.createChatPanel();

    // Create target nameplate panel (initially hidden)
    this.createTargetNameplate();

    // Load persisted HUD layout, then wire up panel drag handlers
    this.loadHudLayout();
    this.setupHudDragHandlers();

    // Inventory toggle key (I)
    this.input.keyboard!.on('keydown-I', () => {
      if (this.chatInputActive) return;
      this.toggleInventory();
    });

    // Chat: Enter opens/sends, Escape cancels
    this.input.keyboard!.on('keydown-ENTER', () => {
      if (this.chatInputActive) {
        this.submitChatInput();
      } else if (!this.inventoryOpen && !this.skillsPaneOpen) {
        this.activateChatInput();
      }
    });

    this.input.keyboard!.on('keydown-ESC', () => {
      if (this.chatInputActive) {
        this.cancelChatInput();
      }
    });

    // Keyboard listener that routes to chat when active
    this.input.keyboard!.on('keydown', (event: KeyboardEvent) => {
      if (!this.chatInputActive) return;

      const key = event.key;
      if (key === 'Backspace') {
        this.chatInputText = this.chatInputText.slice(0, -1);
      } else if (key.length === 1) {
        // Printable character
        if (this.chatInputText.length < 200) {
          this.chatInputText += key;
        }
      }
      // Enter and Escape are handled by the dedicated listeners above
    });

    // Setup network callbacks
    this.setupNetworkCallbacks();

    // Wire up action bar key presses (1-8)
    this.inputManager.onActionBarKeyPressed = (slotIndex: number) => {
      if (this.chatInputActive || this.inventoryOpen || this.skillsPaneOpen) return;
      const skillId = this.actionBar[slotIndex];
      if (!skillId) return;

      const skill = ClientDataManager.instance.getSkill(skillId);

      // Single-target skills require a target to be selected
      if (skill?.targetType === 'singleEnemy' || skill?.targetType === 'singleAlly') {
        if (!this.currentTargetId) {
          const pos = this.network.sessionId ? this.getCombatTextPosition(this.network.sessionId) : null;
          if (pos) this.entityRenderer.showCombatText(pos.x, pos.y - 30, 'No target', '#ff8844');
          return;
        }
        this.network.sendCastSkill(skillId, this.currentTargetId);
      // AOE_GROUND skills (Fireball, Meteor) require a ground target position.
      // Use the current mouse/cursor world position as the target.
      } else if (skill?.targetType === 'aoeGround') {
        const worldPoint = this.cameras.main.getWorldPoint(this.input.activePointer.x, this.input.activePointer.y);
        let targetX = worldPoint.x;
        let targetY = worldPoint.y;

        // Convert from ISO screen coords to ortho world coords if isometric
        if (this.isIso) {
          const orthoCoords = isoToOrtho(worldPoint.x, worldPoint.y);
          targetX = orthoCoords.x;
          targetY = orthoCoords.y;
        }

        this.network.sendCastSkill(skillId, undefined, targetX, targetY);
      } else {
        this.network.sendCastSkill(skillId);
      }
    };

    // Wire up skills pane toggle (K)
    this.inputManager.onSkillsPaneToggle = () => {
      if (this.chatInputActive || this.inventoryOpen) return;
      this.toggleSkillsPane();
    };

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
    // ── Entity click-to-target callbacks ─────────────────────
    this.entityRenderer.onPlayerClick = (sessionId: string) => {
      this.entityClickConsumed = true;
      this.inputManager.suppressNextMelee = true; // prevent melee attack on targeting click
      if (this.currentTargetId === sessionId && this.currentTargetType === 'player') {
        this.clearTarget(); // click same target again = deselect
      } else {
        this.setTarget(sessionId, 'player');
      }
    };
    this.entityRenderer.onNpcClick = (npcId: string) => {
      this.entityClickConsumed = true;
      this.inputManager.suppressNextMelee = true; // prevent melee attack on targeting click
      if (this.currentTargetId === npcId && this.currentTargetType === 'npc') {
        this.clearTarget(); // click same target again = deselect
      } else {
        this.setTarget(npcId, 'npc');
      }
    };

    // Handle full map data (new multi-layer system)
    this.network.onMapData = (data: MapDataPayload) => {
      this.collisionGrid = data.collisionGrid;
      this.collisionMapW = data.width;
      this.collisionMapH = data.height;
      this.mapTileSize = data.tileSize;
      this.mapWidthPx = data.width * data.tileSize;
      this.mapHeightPx = data.height * data.tileSize;
      this.currentZoneId = data.zoneId;
      this.isIso = data.orientation === 'isometric';

      // Reconcile NPC visibility — fixes the race where onNpcAdd fires before
      // currentZoneId is set, causing NPCs from other zones to ghost in.
      for (const [nid, npcZone] of this.npcZones) {
        const visible = this.visibleNpcs.has(nid);
        const shouldBeVisible = npcZone === this.currentZoneId;
        if (visible && !shouldBeVisible) {
          this.entityRenderer.removeNPC(nid);
          this.visibleNpcs.delete(nid);
        } else if (!visible && shouldBeVisible) {
          const cached = this.npcCache.get(nid);
          if (cached) {
            this.entityRenderer.addNPC(nid, cached.x, cached.y, cached.name ?? 'NPC', cached.level ?? 1, cached.npcType ?? 'enemy', cached.spriteColor ?? 0xff4444, cached.spriteSize ?? 1);
            this.entityRenderer.updateNPCHp(nid, cached.hp, cached.maxHp, cached.alive);
            this.visibleNpcs.add(nid);
          }
        }
      }

      // Update camera bounds for the new map size
      // For ISO maps the screen footprint is much wider/taller than ortho pixel dims.
      if (this.isIso) {
        // ISO diamond bounds: the map spans roughly ±(width+height)*tileSize/2 in X,
        // and 0..(width+height)*tileSize/2 in Y. Add generous padding.
        const totalTiles = data.width + data.height;
        const halfW = totalTiles * data.tileSize;
        const halfH = totalTiles * data.tileSize / 2;
        this.cameras.main.setBounds(-halfW, -halfH / 2, halfW * 2, halfH * 2);
      } else {
        this.cameras.main.setBounds(0, 0, this.mapWidthPx, this.mapHeightPx);
      }

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

      // 0. Clear targeting — target may not exist in new zone
      this.clearTarget();

      // 1. Remove all currently-rendered remote players, projectiles, and NPCs
      for (const sid of this.remotePlayerZones.keys()) {
        this.entityRenderer.removeRemotePlayer(sid);
      }
      for (const pid of this.visibleProjectiles) {
        this.entityRenderer.removeProjectile(pid);
      }
      this.visibleProjectiles.clear();
      for (const sid of this.visibleSpellProjectiles) {
        this.entityRenderer.removeSpellProjectile(sid);
      }
      this.visibleSpellProjectiles.clear();
      for (const nid of this.visibleNpcs) {
        this.entityRenderer.removeNPC(nid);
      }
      this.visibleNpcs.clear();

      // 2. Switch zone
      this.currentZoneId = data.zoneId;

      // 3. Re-add any cached remote players that are already in our new zone
      for (const [sid, cached] of this.remotePlayerCache) {
        if ((cached.zoneId ?? 'grasslands') === this.currentZoneId) {
          this.entityRenderer.addRemotePlayer(
            sid,
            cached.x,
            cached.y,
            cached.classId ?? 'warrior',
            cached.level ?? 1,
            cached.characterName ?? '',
          );
        }
      }

      // 4. Re-add NPCs that are in our new zone
      for (const [nid, npcZone] of this.npcZones) {
        if (npcZone === this.currentZoneId) {
          const cached = this.npcCache.get(nid);
          if (cached) {
            this.entityRenderer.addNPC(
              nid,
              cached.x, cached.y,
              cached.name ?? 'NPC',
              cached.level ?? 1,
              cached.npcType ?? 'enemy',
              cached.spriteColor ?? 0xff4444,
              cached.spriteSize ?? 1,
            );
            this.entityRenderer.updateNPCHp(nid, cached.hp, cached.maxHp, cached.alive);
            this.visibleNpcs.add(nid);
          }
        }
      }

      // 4. Teleport the local player to the new spawn
      this.localX = data.spawnX;
      this.localY = data.spawnY;
      if (this.playerSprite) {
        const playerIso = this.isIso ? orthoToIso(this.localX, this.localY) : { x: this.localX, y: this.localY };
        this.playerSprite.setPosition(playerIso.x, playerIso.y);
        this.lastFacingDir = 'down';
        this.playerSprite.play('idle_down', true);
      }

      // 5. Clear pending inputs — server position is authoritative after zone change
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
        this.localCharacterName = player.characterName ?? '';
        // Set zone early so onNpcAdd / onPlayerAdd for others filters correctly
        this.currentZoneId = player.zoneId ?? 'grasslands';
        this.createLocalPlayer();
        this.connected = true;
        this.statusText.setText('WASD move | Mouse aim | Left-click melee | Right-click ranged (Ranger)');
        this.time.delayedCall(4000, () => this.statusText.setVisible(false));
      } else {
        // Remote player — track zone and cache data for all players in the room,
        // but only render those who share our current zone.
        const theirZone = player.zoneId ?? 'grasslands';
        this.remotePlayerZones.set(sessionId, theirZone);
        this.remotePlayerCache.set(sessionId, {
          x: player.x, y: player.y,
          classId: player.classId ?? 'warrior',
          level: player.level ?? 1,
          characterName: player.characterName ?? '',
          zoneId: theirZone,
          aimAngle: player.aimAngle ?? 0,
          hp: player.hp ?? 100,
          maxHp: player.maxHp ?? 100,
          alive: player.alive ?? true,
        });
        if (theirZone === this.currentZoneId) {
          this.entityRenderer.addRemotePlayer(
            sessionId,
            player.x,
            player.y,
            player.classId ?? 'warrior',
            player.level ?? 1,
            player.characterName ?? '',
          );
        }
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
        this.localEnergy = player.energy ?? 0;
        this.localMaxEnergy = player.maxEnergy ?? 0;

        // Track casting state from server
        if (player.castingSkillId && player.castingDurationMs > 0) {
          this.localCastingSkillId = player.castingSkillId;
          this.localCastingStartedAt = player.castingStartedAt;
          this.localCastingDurationMs = player.castingDurationMs;
        } else if (this.localCastingSkillId && !player.castingSkillId) {
          this.localCastingSkillId = '';
          this.localCastingDurationMs = 0;
        }

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
        const prevZone = this.remotePlayerZones.get(sessionId) ?? 'grasslands';
        const newZone  = player.zoneId ?? prevZone;
        const wasInOurZone = prevZone === this.currentZoneId;
        const isInOurZone  = newZone  === this.currentZoneId;

        // Always keep zone + cache up to date
        this.remotePlayerZones.set(sessionId, newZone);
        const cached = this.remotePlayerCache.get(sessionId) ?? {};
        this.remotePlayerCache.set(sessionId, {
          ...cached,
          x: player.x, y: player.y,
          zoneId: newZone,
          aimAngle: player.aimAngle ?? cached.aimAngle ?? 0,
          hp: player.hp ?? cached.hp,
          maxHp: player.maxHp ?? cached.maxHp,
          alive: player.alive ?? cached.alive,
          level: player.level ?? cached.level ?? 1,
          classId: player.classId ?? cached.classId ?? 'warrior',
          characterName: player.characterName ?? cached.characterName ?? '',
        });

        if (wasInOurZone && !isInOurZone) {
          // Player left our zone — remove their sprite
          this.entityRenderer.removeRemotePlayer(sessionId);
        } else if (!wasInOurZone && isInOurZone) {
          // Player entered our zone — add their sprite
          this.entityRenderer.addRemotePlayer(
            sessionId,
            player.x,
            player.y,
            player.classId ?? 'warrior',
            player.level ?? 1,
            player.characterName ?? '',
          );
        } else if (isInOurZone) {
          // Same zone — normal position/HP update
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
        // Different zone (and wasn't in ours) → nothing to do
      }
    };

    this.network.onPlayerRemove = (sessionId: string) => {
      if (this.currentTargetId === sessionId) this.clearTarget();
      this.entityRenderer.removeRemotePlayer(sessionId);
      this.remotePlayerZones.delete(sessionId);
      this.remotePlayerCache.delete(sessionId);
    };

    // Projectile callbacks — only render projectiles whose owner is in our zone.
    // Projectiles have no zoneId, so we derive zone from their ownerId:
    //   - our own projectiles: always in our zone
    //   - remote projectiles: look up owner in remotePlayerZones
    this.network.onProjectileAdd = (proj: any, id: string) => {
      const ownerZone = proj.ownerId === this.network.sessionId
        ? this.currentZoneId
        : (this.remotePlayerZones.get(proj.ownerId) ?? null);
      if (ownerZone === this.currentZoneId) {
        this.entityRenderer.addProjectile(id, proj.x, proj.y);
        this.visibleProjectiles.add(id);
      }
    };

    this.network.onProjectileRemove = (id: string) => {
      if (this.visibleProjectiles.has(id)) {
        this.entityRenderer.removeProjectile(id);
        this.visibleProjectiles.delete(id);
      }
    };

    this.network.onProjectileChange = (proj: any, id: string) => {
      if (this.visibleProjectiles.has(id)) {
        this.entityRenderer.updateProjectileTarget(id, proj.x, proj.y);
      }
    };

    // Spell projectile callbacks (Fireball, etc.)
    // Like basic projectiles we derive zone from ownerId; zoneId is stored server-only.
    this.network.onSpellProjectileAdd = (proj: any, id: string) => {
      const ownerZone = proj.ownerId === this.network.sessionId
        ? this.currentZoneId
        : (this.remotePlayerZones.get(proj.ownerId) ?? null);
      if (ownerZone === this.currentZoneId) {
        this.entityRenderer.addSpellProjectile(id, proj.x, proj.y, proj.targetX, proj.targetY, proj.skillId);
        this.visibleSpellProjectiles.add(id);
      }
    };

    this.network.onSpellProjectileRemove = (id: string) => {
      if (this.visibleSpellProjectiles.has(id)) {
        this.entityRenderer.removeSpellProjectile(id);
        this.visibleSpellProjectiles.delete(id);
      }
    };

    this.network.onSpellProjectileChange = (proj: any, id: string) => {
      if (this.visibleSpellProjectiles.has(id)) {
        this.entityRenderer.updateSpellProjectileTarget(id, proj.x, proj.y);
      }
    };

    // Spell impact — play explosion VFX at the detonation point
    // data.x/y are orthogonal world coords from the server; convert to ISO screen coords before rendering
    this.network.onSpellImpact = (data) => {
      const isoPos = orthoToIso(data.x, data.y);
      this.entityRenderer.showSpellImpact(isoPos.x, isoPos.y, data.radius, data.skillId);
    };

    // NPC callbacks — NPCs have a zoneId, so we filter by zone like remote players.
    this.network.onNpcAdd = (npc: any, id: string) => {
      const npcZone = npc.zoneId ?? 'grasslands';
      this.npcZones.set(id, npcZone);
      this.npcCache.set(id, {
        x: npc.x, y: npc.y,
        name: npc.name, level: npc.level,
        npcType: npc.npcType, spriteColor: npc.spriteColor,
        spriteSize: npc.spriteSize,
        hp: npc.hp, maxHp: npc.maxHp, alive: npc.alive,
      });

      if (npcZone === this.currentZoneId) {
        this.entityRenderer.addNPC(
          id, npc.x, npc.y,
          npc.name ?? 'NPC',
          npc.level ?? 1,
          npc.npcType ?? 'enemy',
          npc.spriteColor ?? 0xff4444,
          npc.spriteSize ?? 1,
        );
        this.entityRenderer.updateNPCHp(id, npc.hp, npc.maxHp, npc.alive);
        this.visibleNpcs.add(id);
      }
    };

    this.network.onNpcChange = (npc: any, id: string) => {
      const prevZone = this.npcZones.get(id) ?? 'grasslands';
      const newZone = npc.zoneId ?? prevZone;
      const wasVisible = this.visibleNpcs.has(id);
      const shouldBeVisible = newZone === this.currentZoneId;

      // Keep zone + cache up to date
      this.npcZones.set(id, newZone);
      this.npcCache.set(id, {
        x: npc.x, y: npc.y,
        name: npc.name, level: npc.level,
        npcType: npc.npcType, spriteColor: npc.spriteColor,
        spriteSize: npc.spriteSize,
        hp: npc.hp, maxHp: npc.maxHp, alive: npc.alive,
      });

      // Handle zone transitions
      if (wasVisible && !shouldBeVisible) {
        this.entityRenderer.removeNPC(id);
        this.visibleNpcs.delete(id);
      } else if (!wasVisible && shouldBeVisible) {
        this.entityRenderer.addNPC(
          id, npc.x, npc.y,
          npc.name ?? 'NPC',
          npc.level ?? 1,
          npc.npcType ?? 'enemy',
          npc.spriteColor ?? 0xff4444,
          npc.spriteSize ?? 1,
        );
        this.visibleNpcs.add(id);
      }

      // Update rendering if currently visible
      if (this.visibleNpcs.has(id)) {
        this.entityRenderer.updateNPCTarget(id, npc.x, npc.y, npc.aimAngle ?? 0);
        this.entityRenderer.updateNPCHp(id, npc.hp, npc.maxHp, npc.alive);
      }
    };

    this.network.onNpcRemove = (id: string) => {
      if (this.currentTargetId === id) this.clearTarget();
      if (this.visibleNpcs.has(id)) {
        this.entityRenderer.removeNPC(id);
        this.visibleNpcs.delete(id);
      }
      this.npcZones.delete(id);
      this.npcCache.delete(id);
    };

    // Combat events
    this.network.onPlayerHit = (data: PlayerHitData) => {
      if (data.targetId === this.network.sessionId) {
        // We got hit — show damage with crit indicator
        const dmgText = data.isCrit ? `${data.damage}!` : `${data.damage}`;
        const pos = this.getCombatTextPosition(data.targetId);
        if (pos) this.entityRenderer.showDamageFlash(pos.x, pos.y, data.damage, data.isCrit);
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
        const pos = this.getCombatTextPosition(data.attackerId);
        if (pos) this.entityRenderer.showMeleeSlash(pos.x, pos.y, data.angle, this.isIso);
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

    // NPC combat events
    this.network.onNpcHit = (data: any) => {
      const pos = this.entityRenderer.getNPCPosition(data.targetId);
      if (pos) {
        this.entityRenderer.showDamageFlash(pos.x, pos.y, data.damage, data.isCrit);

        // Magic Missile VFX — bolt from caster to NPC target
        if (data.skillId === SkillId.WIZARD_MAGIC_MISSILE && data.casterId) {
          const casterPos = this.getCombatTextPosition(data.casterId);
          if (casterPos) {
            this.entityRenderer.showMagicMissileVFX(casterPos.x, casterPos.y, pos.x, pos.y);
          }
        }
      }
    };

    this.network.onNpcDied = (data) => {
      console.log(`[Combat] NPC ${data.targetId} was killed by ${data.killerId.slice(0, 6)}, +${data.xpReward} XP`);
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

    // ── Skill System Callbacks ──

    this.network.onActionBarData = (data: { slots: string[] }) => {
      if (data.slots) {
        this.actionBar = [...data.slots];
        while (this.actionBar.length < ACTION_BAR_SLOTS) this.actionBar.push('');
      }
    };

    this.network.onSkillStarted = (data: { casterId: string; skillId: string; castTimeMs: number }) => {
      if (data.casterId === this.network.sessionId) {
        if (data.castTimeMs > 0) {
          // Cast-time spell — show cast bar
          this.localCastingSkillId = data.skillId;
          this.localCastingStartedAt = Date.now();
          this.localCastingDurationMs = data.castTimeMs;
        } else {
          // Instant cast — start cooldown immediately
          const skill = ClientDataManager.instance.getSkill(data.skillId);
          if (skill && skill.cooldownMs > 0) {
            this.skillCooldowns.set(data.skillId, {
              expiresAt: Date.now() + skill.cooldownMs,
              durationMs: skill.cooldownMs,
            });
          }
        }
      }
    };

    this.network.onSkillEffect = (data: any) => {
      // Start cooldown when a cast-time spell completes (effect fires)
      if (data.casterId === this.network.sessionId && data.skillId) {
        const skill = ClientDataManager.instance.getSkill(data.skillId);
        if (skill && skill.cooldownMs > 0 && skill.castTimeMs > 0) {
          // Only set if not already tracked (instant casts are handled in onSkillStarted)
          if (!this.skillCooldowns.has(data.skillId) || this.skillCooldowns.get(data.skillId)!.expiresAt < Date.now()) {
            this.skillCooldowns.set(data.skillId, {
              expiresAt: Date.now() + skill.cooldownMs,
              durationMs: skill.cooldownMs,
            });
          }
        }
      }

      // Show damage/heal numbers via entity renderer
      if (data.type === 'damage' && data.targetId) {
        const pos = this.getCombatTextPosition(data.targetId);
        if (pos) {
          this.entityRenderer.showDamageFlash(pos.x, pos.y, data.damage, data.isCrit);
        }

        // Magic Missile VFX: bolt from caster to target
        if (data.skillId === SkillId.WIZARD_MAGIC_MISSILE) {
          const casterPos = this.getCombatTextPosition(data.casterId);
          const targetPos = this.getCombatTextPosition(data.targetId);
          if (casterPos && targetPos) {
            this.entityRenderer.showMagicMissileVFX(casterPos.x, casterPos.y, targetPos.x, targetPos.y);
          }
        }
      } else if (data.type === 'heal' && data.targetId) {
        const pos = this.getCombatTextPosition(data.targetId);
        if (pos) {
          this.entityRenderer.showCombatText(pos.x, pos.y, `+${data.amount}`, '#44ff44');
        }
      }
    };

    this.network.onSkillFailed = (data: { reason: string }) => {
      // Show error text above player
      const pos = this.network.sessionId ? this.getCombatTextPosition(this.network.sessionId) : null;
      if (pos) {
        const offsetPos = { x: pos.x, y: pos.y - 30 };
        this.entityRenderer.showCombatText(offsetPos.x, offsetPos.y, data.reason, '#ff6666');
      }
    };

    this.network.onSkillInterrupted = (data: { casterId: string; skillId: string }) => {
      if (data.casterId === this.network.sessionId) {
        this.localCastingSkillId = '';
        this.localCastingDurationMs = 0;
        this.entityRenderer.showCombatText(this.localX, this.localY - 30, 'Interrupted!', '#ff8888');
      }
    };

    this.network.onBuffApplied = (data: { targetId: string; skillId: string; durationMs: number }) => {
      const skill = ClientDataManager.instance.getSkill(data.skillId);
      if (skill && data.targetId === this.network.sessionId) {
        this.entityRenderer.showCombatText(this.localX, this.localY - 30, `+${skill.name}`, '#88ccff');
      }
    };

    this.network.onBuffRemoved = (data: { targetId: string; skillId: string }) => {
      const skill = ClientDataManager.instance.getSkill(data.skillId);
      if (skill && data.targetId === this.network.sessionId) {
        this.entityRenderer.showCombatText(this.localX, this.localY - 30, `-${skill.name}`, '#888888');
      }
    };

    // ── Chat ──
    this.network.onChatMessage = (data: ChatMessagePayload) => {
      this.receiveChatMessage(data);
    };
  }

  /**
   * Get the world position for a given player (local or remote).
   */
  private getCombatTextPosition(entityId: string): { x: number; y: number } | null {
    if (entityId === this.network.sessionId) {
      if (this.isIso) {
        return orthoToIso(this.localX, this.localY);
      }
      return { x: this.localX, y: this.localY };
    }
    return this.entityRenderer.getPlayerPosition(entityId)
      ?? this.entityRenderer.getNPCPosition(entityId);
  }

  private showRemoteDamageFlash(targetId: string, damage: number, isCrit?: boolean): void {
    const pos = this.entityRenderer.getPlayerPosition(targetId);
    if (pos) this.entityRenderer.showDamageFlash(pos.x, pos.y, damage, isCrit);
  }

  private showRemoteMeleeSlash(attackerId: string, angle: number): void {
    const pos = this.entityRenderer.getPlayerPosition(attackerId);
    if (pos) this.entityRenderer.showMeleeSlash(pos.x, pos.y, angle, this.isIso);
  }

  private createLocalPlayer(): void {
    const playerIso = this.isIso ? orthoToIso(this.localX, this.localY) : { x: this.localX, y: this.localY };
    // Use sprite sheet — frame 18 = row 2 (down), col 0 = idle facing down
    this.playerSprite = this.add.sprite(playerIso.x, playerIso.y, 'player_walk', 18);
    this.playerSprite.setDepth(ENTITY_DEPTH_BASE);
    this.playerSprite.rotation = 0;

    // Aim indicator line
    this.aimLine = this.add.graphics();
    this.aimLine.setDepth(ENTITY_DEPTH_BASE - 1);

    // Camera follows player
    this.cameras.main.startFollow(this.playerSprite, true, 0.1, 0.1);
    this.cameras.main.setZoom(1);
  }

  /**
   * Determine the 4-direction movement direction string from WASD input.
   * Maps directly from world-space movement intent (W=up, S=down, A=left, D=right)
   * so that the sprite row matches the key pressed, regardless of ISO projection.
   * For diagonals the vertical axis takes priority (up/down over left/right).
   * Returns null if not moving.
   */
  private getMovementDir(input: InputPayload): string | null {
    const mx = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    const my = (input.down ? 1 : 0) - (input.up ? 1 : 0);
    if (mx === 0 && my === 0) return null;

    // Cardinal directions first
    if (my < 0 && mx === 0) return 'up';
    if (my > 0 && mx === 0) return 'down';
    if (mx < 0 && my === 0) return 'left';
    if (mx > 0 && my === 0) return 'right';

    // Diagonals — vertical axis takes priority (feels most natural in ISO)
    if (my < 0) return 'up';
    return 'down';
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
  /** Depth stride between tile layers for painter's-algorithm sorting. */
  private static readonly LAYER_DEPTH_STRIDE = 100_000;

  private buildTileMapFromData(data: MapDataPayload): void {
    // Clear existing tiles
    for (const sprite of this.tileSprites) {
      sprite.destroy();
    }
    this.tileSprites = [];

    const { tileLayers, tileSize } = data;
    const isIso = data.orientation === 'isometric';

    let layerIndex = 0;
    for (const layer of tileLayers) {
      if (!layer.visible) {
        layerIndex++;
        continue;
      }

      for (let y = 0; y < layer.height; y++) {
        for (let x = 0; x < layer.width; x++) {
          const gid = layer.data[y * layer.width + x];
          if (gid === 0) continue; // Empty tile, skip

          const textureKey = GameScene.GID_TEXTURE_MAP[gid] ?? 'tile_void';

          let screenX: number;
          let screenY: number;
          let depth: number;

          if (isIso) {
            // Isometric: project tile grid → screen via orthoToIso
            const pos = tileToIso(x, y);
            screenX = pos.x;
            screenY = pos.y;
            // Depth sorting: layer stride + row+col sum for painter's algorithm
            depth = layerIndex * GameScene.LAYER_DEPTH_STRIDE + (x + y);
          } else {
            // Orthogonal: simple grid placement
            screenX = x * tileSize + tileSize / 2;
            screenY = y * tileSize + tileSize / 2;
            depth = layerIndex;
          }

          const sprite = this.add.sprite(screenX, screenY, textureKey);
          sprite.setDepth(depth);
          if (layer.opacity < 1) {
            sprite.setAlpha(layer.opacity);
          }
          this.tileSprites.push(sprite);
        }
      }
      layerIndex++;
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

    // Energy bar (only for non-caster classes)
    if (this.localMaxEnergy > 0) {
      const energyBarY = hpBarY - barHeight - 6;
      this.hpBarGfx.fillStyle(0x000000, 0.7);
      this.hpBarGfx.fillRect(barX - 2, energyBarY - 2, barWidth + 4, barHeight + 4);

      const energyRatio = Math.max(0, this.localEnergy / this.localMaxEnergy);
      this.hpBarGfx.fillStyle(0xddaa00, 1); // Yellow/orange for energy
      this.hpBarGfx.fillRect(barX, energyBarY, barWidth * energyRatio, barHeight);

      this.hpBarGfx.lineStyle(1, 0xffffff, 0.5);
      this.hpBarGfx.strokeRect(barX - 2, energyBarY - 2, barWidth + 4, barHeight + 4);
    }

    // Name + Level label
    const displayName = this.localCharacterName || (ClientDataManager.instance.getClass(this.localClassId)?.name ?? this.localClassId);
    const resourceStr = this.localMaxMana > 0
      ? `  MP: ${this.localMana}/${this.localMaxMana}`
      : this.localMaxEnergy > 0
        ? `  EP: ${Math.floor(this.localEnergy)}/${this.localMaxEnergy}`
        : '';
    this.classHudText.setText(`${displayName} Lv.${this.localLevel}  HP: ${this.localHp}/${this.localMaxHp}${resourceStr}`);

    // Position class HUD above bars
    const hasResourceBar = this.localMaxMana > 0 || this.localMaxEnergy > 0;
    const topBarY = hasResourceBar
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

  // ═══════════════════════════════════════════════════════════
  // HUD PANEL DRAG + LAYOUT PERSISTENCE
  // ═══════════════════════════════════════════════════════════

  private loadHudLayout(): void {
    try {
      const raw = localStorage.getItem(`valhalla_hud_${this.characterId}`);
      if (!raw) return;
      const saved = JSON.parse(raw);
      if (saved.chat)      this.chatOffset      = saved.chat;
      if (saved.actionBar) this.actionBarOffset = saved.actionBar;
      if (saved.inv)       this.invOffset       = saved.inv;
      if (saved.skills)    this.skillsOffset    = saved.skills;
      if (saved.target)    this.targetOffset    = saved.target;
    } catch { /* ignore malformed data */ }
  }

  private saveHudLayout(): void {
    try {
      localStorage.setItem(`valhalla_hud_${this.characterId}`, JSON.stringify({
        chat:      this.chatOffset,
        actionBar: this.actionBarOffset,
        inv:       this.invOffset,
        skills:    this.skillsOffset,
        target:    this.targetOffset,
      }));
    } catch { /* ignore */ }
  }

  /**
   * Single global pointer handler that detects drags on each panel's title bar
   * and moves the panel by updating its offset. Registered once in create().
   */
  private setupHudDragHandlers(): void {
    const TITLE_H = 22; // height of the draggable title/header strip on each panel

    const hitRect = (rect: { x: number; y: number; w: number; h: number }, px: number, py: number) =>
      px >= rect.x && px <= rect.x + rect.w && py >= rect.y && py <= rect.y + rect.h;

    // ── Compute current screen rects for each panel's drag handle ──────────
    const getChatHandleRect = () => {
      const cam = this.cameras.main;
      const hudRight = this.classHudText ? this.classHudText.x + this.classHudText.width + 10 : this.CHAT_LEFT_X;
      const px = Math.max(this.CHAT_LEFT_X, hudRight) + this.chatOffset.x;
      const py = cam.height - this.CHAT_H - this.CHAT_BOTTOM_MARGIN + this.chatOffset.y;
      return { x: px, y: py, w: this.chatEffectiveW, h: TITLE_H };
    };

    const getActionBarHandleRect = () => {
      const r = this.actionBarScreenRect;
      return { x: r.x, y: r.y, w: r.w, h: r.h }; // full bar is the handle
    };

    const getInvHandleRect = () => {
      // Top strip of the combined character+inventory panel block
      return {
        x: this.panelStartX + this.invOffset.x,
        y: this.panelY      + this.invOffset.y,
        w: this.totalPanelW,
        h: TITLE_H,
      };
    };

    const getSkillsHandleRect = () => {
      const r = this.skillsPaneScreenRect;
      return { x: r.x + this.skillsOffset.x, y: r.y + this.skillsOffset.y, w: r.w, h: TITLE_H };
    };

    this.input.on('pointerdown', (pointer: Phaser.Input.Pointer) => {
      if (!pointer.leftButtonDown()) return;
      const px = pointer.x;
      const py = pointer.y;

      // Chat drag handle
      if (hitRect(getChatHandleRect(), px, py)) {
        this.hudDragTarget      = 'chat';
        this.hudDragStartMouse  = { x: px, y: py };
        this.hudDragStartOffset = { ...this.chatOffset };
        return;
      }
      // Action bar drag handle
      if (hitRect(getActionBarHandleRect(), px, py)) {
        // Only start drag on the padding area (not on a slot) to avoid blocking skill drops
        const slotHit = this.getActionBarSlotAt(px, py);
        if (slotHit === -1) {
          this.hudDragTarget      = 'actionBar';
          this.hudDragStartMouse  = { x: px, y: py };
          this.hudDragStartOffset = { ...this.actionBarOffset };
          return;
        }
      }
      // Inventory panel drag handle (only when open)
      if (this.inventoryOpen && hitRect(getInvHandleRect(), px, py)) {
        this.hudDragTarget      = 'inventory';
        this.hudDragStartMouse  = { x: px, y: py };
        this.hudDragStartOffset = { ...this.invOffset };
        return;
      }
      // Skills pane drag handle (only when open)
      if (this.skillsPaneOpen && hitRect(getSkillsHandleRect(), px, py)) {
        this.hudDragTarget      = 'skills';
        this.hudDragStartMouse  = { x: px, y: py };
        this.hudDragStartOffset = { ...this.skillsOffset };
        return;
      }
      // Target nameplate drag handle (only when a target is selected)
      if (this.currentTargetId && this.targetNameplateTitleHandle &&
          hitRect(this.targetNameplateTitleHandle, px, py)) {
        this.hudDragTarget      = 'target';
        this.hudDragStartMouse  = { x: px, y: py };
        this.hudDragStartOffset = { ...this.targetOffset };
        return;
      }
    });

    this.input.on('pointermove', (pointer: Phaser.Input.Pointer) => {
      if (!this.hudDragTarget) return;
      const dx = pointer.x - this.hudDragStartMouse.x;
      const dy = pointer.y - this.hudDragStartMouse.y;
      const newOffset = {
        x: this.hudDragStartOffset.x + dx,
        y: this.hudDragStartOffset.y + dy,
      };

      switch (this.hudDragTarget) {
        case 'chat':
          this.chatOffset = newOffset;
          break;
        case 'actionBar':
          this.actionBarOffset = newOffset;
          break;
        case 'inventory':
          this.invOffset = newOffset;
          this.invContainer.setPosition(newOffset.x, newOffset.y);
          break;
        case 'skills':
          this.skillsOffset = newOffset;
          this.skillsPaneContainer.setPosition(newOffset.x, newOffset.y);
          break;
        case 'target':
          this.targetOffset = newOffset;
          this.targetNameplateContainer.setPosition(
            this.getTargetNameplateDefaultX() + newOffset.x,
            this.getTargetNameplateDefaultY() + newOffset.y,
          );
          break;
      }
    });

    this.input.on('pointerup', () => {
      if (!this.hudDragTarget) return;
      this.hudDragTarget = null;
      this.saveHudLayout();
    });
  }

  // ── Targeting Methods ─────────────────────────────────────

  private getTargetNameplateDefaultX(): number {
    return this.cameras.main.width - 222;
  }

  private getTargetNameplateDefaultY(): number {
    return 100;
  }

  private createTargetNameplate(): void {
    const cam = this.cameras.main;
    const NW = 210;  // nameplate width
    const NH = 72;   // nameplate height
    const TH = 22;   // title strip height

    const defaultX = cam.width - NW - 12;
    const defaultY = 100;

    this.targetNameplateContainer = this.add.container(
      defaultX + this.targetOffset.x,
      defaultY + this.targetOffset.y,
    );
    this.targetNameplateContainer.setScrollFactor(0);
    this.targetNameplateContainer.setDepth(UI_DEPTH_BASE + 70);
    this.targetNameplateContainer.setVisible(false);

    // ── Background panel ──
    const bg = this.add.graphics();
    // Main body
    bg.fillStyle(0x12122a, 0.92);
    bg.fillRoundedRect(0, 0, NW, NH, 4);
    bg.lineStyle(1, 0x44446a, 1);
    bg.strokeRoundedRect(0, 0, NW, NH, 4);
    // Title strip
    bg.fillStyle(0x22225a, 0.98);
    bg.fillRoundedRect(0, 0, NW, TH, { tl: 4, tr: 4, bl: 0, br: 0 });

    // ── "Target" label in title strip ──
    const titleText = this.add.text(NW / 2, TH / 2, 'Target', {
      fontSize: '11px',
      color: '#8888bb',
      fontStyle: 'bold',
    });
    titleText.setOrigin(0.5, 0.5);

    // ── Target name ──
    this.targetNameplateNameText = this.add.text(8, TH + 5, '', {
      fontSize: '13px',
      color: '#ffffff',
      fontStyle: 'bold',
    });
    this.targetNameplateNameText.setOrigin(0, 0);

    // ── Level label ──
    this.targetNameplateLevelText = this.add.text(NW - 6, TH + 5, '', {
      fontSize: '11px',
      color: '#aaaaaa',
    });
    this.targetNameplateLevelText.setOrigin(1, 0);

    // ── HP bar (redrawn each frame) ──
    this.targetNameplateHpBar = this.add.graphics();

    // ── HP value text ──
    this.targetNameplateHpText = this.add.text(NW / 2, TH + 44, '', {
      fontSize: '10px',
      color: '#aaaaaa',
    });
    this.targetNameplateHpText.setOrigin(0.5, 0);

    this.targetNameplateContainer.add([
      bg,
      titleText,
      this.targetNameplateNameText,
      this.targetNameplateLevelText,
      this.targetNameplateHpBar,
      this.targetNameplateHpText,
    ]);

    // Cache the title-strip screen rect for drag hit-testing (updated in updateTargetNameplate)
    this.targetNameplateTitleHandle = { x: defaultX, y: defaultY, w: NW, h: TH };
  }

  private updateTargetNameplate(): void {
    if (!this.currentTargetId || !this.currentTargetType) {
      this.targetNameplateContainer.setVisible(false);
      return;
    }

    const NW = 210;
    const TH = 22;

    let name = '';
    let level = 1;
    let hp = 0;
    let maxHp = 1;
    let isEnemy = false;

    if (this.currentTargetType === 'player') {
      const info: PlayerTargetInfo | null = this.entityRenderer.getPlayerTargetInfo(this.currentTargetId);
      if (!info) { this.clearTarget(); return; }
      name = info.characterName || info.classId;
      level = info.level;
      hp = info.hp;
      maxHp = info.maxHp;
      isEnemy = false;
    } else {
      const info: NpcTargetInfo | null = this.entityRenderer.getNpcTargetInfo(this.currentTargetId);
      if (!info) { this.clearTarget(); return; }
      name = info.name;
      level = info.level;
      hp = info.hp;
      maxHp = info.maxHp;
      isEnemy = info.npcType === 'enemy';
    }

    this.targetNameplateContainer.setVisible(true);

    // Update title-strip drag handle screen rect
    const cx = this.targetNameplateContainer.x;
    const cy = this.targetNameplateContainer.y;
    this.targetNameplateTitleHandle = { x: cx, y: cy, w: NW, h: TH };

    // Name (truncate if too long) — color red for enemies, yellow for NPCs, white for players
    const maxLen = 18;
    const displayName = name.length > maxLen ? name.slice(0, maxLen) + '…' : name;
    const nameColor = this.currentTargetType === 'npc'
      ? (isEnemy ? '#ff8888' : '#ffee88')
      : '#ffffff';
    this.targetNameplateNameText.setText(displayName);
    this.targetNameplateNameText.setColor(nameColor);

    // Level
    this.targetNameplateLevelText.setText(`Lv.${level}`);

    // HP bar
    const hpRatio = maxHp > 0 ? Math.max(0, hp / maxHp) : 0;
    const fillColor = hpRatio > 0.5 ? 0x44ee44 : hpRatio > 0.25 ? 0xffaa00 : 0xff4444;
    const barX = 8;
    const barY = TH + 24;
    const barW = NW - 16;
    const barH = 10;

    this.targetNameplateHpBar.clear();
    this.targetNameplateHpBar.fillStyle(0x000000, 0.6);
    this.targetNameplateHpBar.fillRect(barX - 1, barY - 1, barW + 2, barH + 2);
    this.targetNameplateHpBar.fillStyle(fillColor, 1);
    this.targetNameplateHpBar.fillRect(barX, barY, Math.max(0, barW * hpRatio), barH);

    // HP text
    this.targetNameplateHpText.setText(`${Math.round(hp)} / ${Math.round(maxHp)}`);
  }

  private setTarget(id: string, type: 'player' | 'npc'): void {
    this.currentTargetId = id;
    this.currentTargetType = type;
    this.targetNameplateContainer.setPosition(
      this.getTargetNameplateDefaultX() + this.targetOffset.x,
      this.getTargetNameplateDefaultY() + this.targetOffset.y,
    );
  }

  private clearTarget(): void {
    this.currentTargetId = null;
    this.currentTargetType = null;
    this.targetNameplateContainer?.setVisible(false);
  }

  private createInventoryPanel(): void {
    const cam = this.cameras.main;
    this.invContainer = this.add.container(0, 0);
    this.invContainer.setScrollFactor(0);
    this.invContainer.setDepth(UI_DEPTH_BASE + 200);
    this.invContainer.setVisible(false);

    // Shared graphics layer for both panels
    this.panelGfx = this.add.graphics();
    this.charBarsGfx = this.add.graphics();

    // Fullscreen dim overlay — kept OUTSIDE invContainer so it doesn't move when the panel is dragged
    this.invDimBg = this.add.rectangle(cam.width / 2, cam.height / 2, cam.width, cam.height, 0x000000, 0.4);
    this.invDimBg.setScrollFactor(0);
    this.invDimBg.setDepth(UI_DEPTH_BASE + 199); // just below invContainer (UI_DEPTH_BASE + 200)
    this.invDimBg.setVisible(false);

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
    this.dragGhostBg.setDepth(UI_DEPTH_BASE + 899);
    this.invContainer.add(this.dragGhostBg);

    this.dragGhost = this.add.text(0, 0, '', {
      fontSize: '11px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.dragGhost.setOrigin(0.5, 0.5);
    this.dragGhost.setVisible(false);
    this.dragGhost.setDepth(UI_DEPTH_BASE + 900);
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
      if (this.hudDragTarget !== null) return; // panel drag takes priority

      // Adjust for the container's current drag offset
      const px = pointer.x - this.invOffset.x;
      const py = pointer.y - this.invOffset.y;

      // Check inventory slot
      const invSlot = this.getInventorySlotAt(px, py);
      if (invSlot >= 0) {
        const item = this.inventoryItems[invSlot];
        if (item) {
          const template = ClientDataManager.instance.getItem(item.itemId);
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
          const template = ClientDataManager.instance.getItem(equippedId);
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
      if (this.hudDragTarget !== null) return;

      // Adjust for container offset
      const px = pointer.x - this.invOffset.x;
      const py = pointer.y - this.invOffset.y;

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
      const px = pointer.x - this.invOffset.x;
      const py = pointer.y - this.invOffset.y;
      this.handleDrop(px, py);
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
        const t = ClientDataManager.instance.getItem(item.itemId);
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
        const t = ClientDataManager.instance.getItem(equippedId);
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
          const template = ClientDataManager.instance.getItem(item.itemId);
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
    this.invDimBg.setVisible(this.inventoryOpen);
    this.invContainer.setVisible(this.inventoryOpen);
    if (this.inventoryOpen) {
      this.invContainer.setPosition(this.invOffset.x, this.invOffset.y);
      this.renderInventorySlots();
      this.renderCharacterPanel();
    }
  }

  private renderCharacterPanel(): void {
    // ── Player Info ──
    const template = ClientDataManager.instance.getClass(this.localClassId);
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
        const itemTemplate = ClientDataManager.instance.getItem(equippedId);
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
        const itemTemplate = ClientDataManager.instance.getItem(equippedId);
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
      const itemTemplate = item ? ClientDataManager.instance.getItem(item.itemId) : null;

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

    // ── Skip gameplay input while any UI panel or chat is open ─
    if (this.inventoryOpen || this.skillsPaneOpen || this.chatInputActive) {
      this.inputManager.clearFire();
    } else if (!this.inventoryOpen && !this.skillsPaneOpen && !this.chatInputActive) {
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
        const playerIso = this.isIso ? orthoToIso(this.localX, this.localY) : { x: this.localX, y: this.localY };
        this.playerSprite.x = playerIso.x;
        this.playerSprite.y = playerIso.y;
        this.playerSprite.setDepth(ENTITY_DEPTH_BASE + Math.floor(this.localX / 64) + Math.floor(this.localY / 64));

        // Directional sprite animation based on movement
        const moveDir = this.getMovementDir(input);
        this.playerSprite.rotation = 0;
        if (moveDir) {
          this.lastFacingDir = moveDir;
          if (this.playerSprite.anims.getName() !== `walk_${moveDir}` || !this.playerSprite.anims.isPlaying) {
            this.playerSprite.play(`walk_${moveDir}`, true);
          }
        } else {
          const idleKey = `idle_${this.lastFacingDir}`;
          if (this.playerSprite.anims.getName() !== idleKey || !this.playerSprite.anims.isPlaying) {
            this.playerSprite.play(idleKey, true);
          }
        }

        // ── Draw aim line ─────────────────────────────────────
        this.drawAimLine(input.aimAngle);
      }
    }

    // ── Draw HUD ────────────────────────────────────────────
    this.drawLocalHud();
    this.drawActionBar();
    this.drawCastBar();
    this.drawChatPanel();
    this.updateTargetNameplate();

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

    // Apply 45° rotation for isometric movement
    if (this.isIso) {
      const isoMx = mx + my;
      const isoMy = -mx + my;
      mx = isoMx;
      my = isoMy;
    }

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

    if (this.isIso) {
      const isoStart = orthoToIso(startX, startY);
      const isoEnd = orthoToIso(endX, endY);
      this.aimLine.beginPath();
      this.aimLine.moveTo(isoStart.x, isoStart.y);
      this.aimLine.lineTo(isoEnd.x, isoEnd.y);
      this.aimLine.strokePath();
    } else {
      this.aimLine.beginPath();
      this.aimLine.moveTo(startX, startY);
      this.aimLine.lineTo(endX, endY);
      this.aimLine.strokePath();
    }
  }

  // ═══════════════════════════════════════════════════════════
  // CHAT SYSTEM
  // ═══════════════════════════════════════════════════════════

  private createChatPanel(): void {
    const cam = this.cameras.main;

    this.chatContainer = this.add.container(0, 0);
    this.chatContainer.setScrollFactor(0);
    this.chatContainer.setDepth(UI_DEPTH_BASE + 20);

    // Background drawn via graphics (redrawn every frame in drawChatPanel)
    this.chatBgGfx = this.add.graphics();
    this.chatContainer.add(this.chatBgGfx);

    this.chatInputBgGfx = this.add.graphics();
    this.chatContainer.add(this.chatInputBgGfx);

    // Channel label (e.g. "[General]")
    this.chatChannelLabel = this.add.text(0, 0, '[General]', {
      fontSize: '10px',
      color: '#aaaacc',
      stroke: '#000000',
      strokeThickness: 1,
    });
    this.chatChannelLabel.setScrollFactor(0);
    this.chatChannelLabel.setDepth(UI_DEPTH_BASE + 21);
    this.chatContainer.add(this.chatChannelLabel);

    // Pre-allocate message text objects
    for (let i = 0; i < this.CHAT_VISIBLE_LINES; i++) {
      const t = this.add.text(0, 0, '', {
        fontSize: '11px',
        color: '#ffffff',
        stroke: '#000000',
        strokeThickness: 1,
        wordWrap: { width: this.chatEffectiveW - this.CHAT_PAD * 2 },
      });
      t.setScrollFactor(0);
      t.setDepth(UI_DEPTH_BASE + 21);
      t.setVisible(false);
      this.chatContainer.add(t);
      this.chatMessageTexts.push(t);
    }

    // Input display text
    this.chatInputDisplay = this.add.text(0, 0, '', {
      fontSize: '11px',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 1,
    });
    this.chatInputDisplay.setScrollFactor(0);
    this.chatInputDisplay.setDepth(UI_DEPTH_BASE + 21);
    this.chatInputDisplay.setVisible(false);
    this.chatContainer.add(this.chatInputDisplay);

    // Position all elements
    this.repositionChatPanel(cam.width, cam.height);
  }

  private repositionChatPanel(camW: number, camH: number): void {
    const hudTextRight = this.classHudText ? this.classHudText.x + this.classHudText.width + 10 : 0;
    const panelX = Math.max(this.CHAT_LEFT_X, hudTextRight);
    const panelY = camH - this.CHAT_H - this.CHAT_BOTTOM_MARGIN;

    this.chatBgGfx.setPosition(panelX, panelY);
    this.chatChannelLabel.setPosition(panelX + this.CHAT_PAD, panelY + 3);

    const msgAreaY = panelY + 18;
    for (let i = 0; i < this.chatMessageTexts.length; i++) {
      this.chatMessageTexts[i].setPosition(
        panelX + this.CHAT_PAD,
        msgAreaY + i * this.CHAT_LINE_H,
      );
    }

    const inputY = panelY + this.CHAT_H - this.CHAT_INPUT_H - 2;
    this.chatInputBgGfx.setPosition(panelX, inputY);
    this.chatInputDisplay.setPosition(panelX + this.CHAT_PAD + 14, inputY + 3);
  }

  private drawChatPanel(): void {
    const cam = this.cameras.main;
    // Dynamically place the panel just to the right of the HUD text (class/level/HP/mana),
    // with a small gap, so it never overlaps regardless of text content length.
    const hudTextRight = this.classHudText.x + this.classHudText.width + 10;
    const panelX = Math.max(this.CHAT_LEFT_X, hudTextRight) + this.chatOffset.x;
    const panelY = cam.height - this.CHAT_H - this.CHAT_BOTTOM_MARGIN + this.chatOffset.y;

    // Compute effective width: fill the gap between the HUD text and the action bar
    const actionBarTotalW = ACTION_BAR_SLOTS * (this.AB_SLOT_SIZE + this.AB_SLOT_GAP)
      - this.AB_SLOT_GAP + this.AB_PADDING * 2;
    const actionBarStartX = Math.floor((cam.width - actionBarTotalW) / 2);
    const newW = Math.min(this.CHAT_MAX_W, Math.max(180, actionBarStartX - panelX - 8));

    // Update word-wrap on text objects only when width actually changes
    if (newW !== this.chatEffectiveW) {
      this.chatEffectiveW = newW;
      const wrapW = newW - this.CHAT_PAD * 2;
      for (const t of this.chatMessageTexts) {
        t.setWordWrapWidth(wrapW);
      }
    }

    // Panel background — position the graphics object at the panel's screen coords
    this.chatBgGfx.setPosition(panelX, panelY);
    this.chatBgGfx.clear();
    this.chatBgGfx.fillStyle(0x0a0a1a, 0.72);
    this.chatBgGfx.fillRect(0, 0, this.chatEffectiveW, this.CHAT_H);
    this.chatBgGfx.lineStyle(1, 0x333355, 0.9);
    this.chatBgGfx.strokeRect(0, 0, this.chatEffectiveW, this.CHAT_H);

    // Channel label
    const channelLabel = this.chatCurrentChannel === 'general' ? '[General]' : '[World]';
    this.chatChannelLabel.setText(channelLabel);
    this.chatChannelLabel.setPosition(panelX + this.CHAT_PAD, panelY + 3);

    // Messages — show most recent CHAT_VISIBLE_LINES from history
    const msgAreaTopY = panelY + 18;
    const start = Math.max(0, this.chatMessages.length - this.CHAT_VISIBLE_LINES);
    for (let i = 0; i < this.CHAT_VISIBLE_LINES; i++) {
      const t = this.chatMessageTexts[i];
      const msgIndex = start + i;
      if (msgIndex < this.chatMessages.length) {
        const msg = this.chatMessages[msgIndex];
        t.setText(msg.text);
        t.setColor(msg.color);
        t.setPosition(panelX + this.CHAT_PAD, msgAreaTopY + i * this.CHAT_LINE_H);
        t.setVisible(true);
      } else {
        t.setVisible(false);
      }
    }

    // Input area
    const inputY = panelY + this.CHAT_H - this.CHAT_INPUT_H - 2;
    this.chatInputBgGfx.clear();
    this.chatInputBgGfx.setPosition(panelX, inputY);
    if (this.chatInputActive) {
      this.chatInputBgGfx.fillStyle(0x1a1a3a, 0.95);
      this.chatInputBgGfx.fillRect(0, 0, this.chatEffectiveW, this.CHAT_INPUT_H);
      this.chatInputBgGfx.lineStyle(1, 0x5555aa, 1);
      this.chatInputBgGfx.strokeRect(0, 0, this.chatEffectiveW, this.CHAT_INPUT_H);

      // Blinking cursor
      const cursorChar = Math.floor(Date.now() / 500) % 2 === 0 ? '|' : '';
      this.chatInputDisplay.setText(`> ${this.chatInputText}${cursorChar}`);
      this.chatInputDisplay.setPosition(panelX + this.CHAT_PAD, inputY + 3);
      this.chatInputDisplay.setVisible(true);
    } else {
      this.chatInputDisplay.setVisible(false);
    }
  }

  private activateChatInput(): void {
    this.chatInputActive = true;
    this.chatInputText = '';
  }

  private cancelChatInput(): void {
    this.chatInputActive = false;
    this.chatInputText = '';
  }

  private submitChatInput(): void {
    const raw = this.chatInputText.trim();
    this.chatInputActive = false;
    this.chatInputText = '';

    if (raw.length === 0) return;

    // Parse command prefixes
    const lc = raw.toLowerCase();

    if (lc.startsWith('/general ') || lc.startsWith('/g ')) {
      const msg = raw.slice(lc.startsWith('/g ') ? 3 : 9);
      if (msg.trim().length === 0) return;
      this.chatCurrentChannel = 'general';
      this.network.sendChatMessage('general', msg.trim());

    } else if (lc.startsWith('/world ') || lc.startsWith('/y ')) {
      const msg = raw.slice(lc.startsWith('/y ') ? 3 : 7);
      if (msg.trim().length === 0) return;
      this.chatCurrentChannel = 'world';
      this.network.sendChatMessage('world', msg.trim());

    } else if (lc.startsWith('/whisper ') || lc.startsWith('/w ')) {
      const rest = raw.slice(lc.startsWith('/w ') ? 3 : 9);
      const spaceIdx = rest.indexOf(' ');
      if (spaceIdx < 1) {
        this.pushSystemChat('Usage: /w PlayerName message');
        return;
      }
      const targetName = rest.slice(0, spaceIdx);
      const msg = rest.slice(spaceIdx + 1).trim();
      if (msg.length === 0) {
        this.pushSystemChat('Usage: /w PlayerName message');
        return;
      }
      this.network.sendChatMessage('whisper', msg, targetName);

    } else if (raw.startsWith('/')) {
      this.pushSystemChat(`Unknown command: ${raw.split(' ')[0]}`);

    } else {
      // No prefix — send on current channel
      this.network.sendChatMessage(this.chatCurrentChannel, raw);
    }
  }

  private receiveChatMessage(data: ChatMessagePayload): void {
    if (data.channel === 'general') {
      const text = `[G] ${data.senderName}: ${data.message}`;
      this.pushChat(text, '#ffffff');

    } else if (data.channel === 'world') {
      const text = `[W] ${data.senderName}: ${data.message}`;
      this.pushChat(text, '#ffdd00');

    } else if (data.channel === 'whisper') {
      // Determine direction: from me → "To X", to me → "From X"
      const isSender = data.senderName === this.localCharacterName;
      const prefix = isSender
        ? `[To ${data.targetName}]`
        : `[From ${data.senderName}]`;
      this.pushChat(`${prefix}: ${data.message}`, '#ff88cc');

    } else if (data.channel === 'system') {
      this.pushSystemChat(data.message);
    }
  }

  private pushChat(text: string, color: string): void {
    this.chatMessages.push({ text, color });
    if (this.chatMessages.length > this.CHAT_MAX_MESSAGES) {
      this.chatMessages.shift();
    }
  }

  private pushSystemChat(message: string): void {
    this.pushChat(message, '#ffaa44');
  }

  // ═══════════════════════════════════════════════════════════
  // ACTION BAR (8 slots at bottom center)
  // ═══════════════════════════════════════════════════════════

  private readonly AB_SLOT_SIZE = 44;
  private readonly AB_SLOT_GAP = 4;
  private readonly AB_PADDING = 6;

  private createActionBar(): void {
    const cam = this.cameras.main;
    this.actionBarContainer = this.add.container(0, 0);
    this.actionBarContainer.setScrollFactor(0);
    this.actionBarContainer.setDepth(UI_DEPTH_BASE + 50);

    this.actionBarGfx = this.add.graphics();
    this.actionBarContainer.add(this.actionBarGfx);

    this.actionBarCooldownGfx = this.add.graphics();
    this.actionBarContainer.add(this.actionBarCooldownGfx);

    // Create slot label texts
    for (let i = 0; i < ACTION_BAR_SLOTS; i++) {
      // Skill abbreviation
      const slotText = this.add.text(0, 0, '', {
        fontSize: '10px',
        fontStyle: 'bold',
        color: '#ffffff',
        stroke: '#000000',
        strokeThickness: 2,
      });
      slotText.setOrigin(0.5, 0.5);
      this.actionBarContainer.add(slotText);
      this.actionBarSlotTexts.push(slotText);

      // Key number label
      const keyText = this.add.text(0, 0, `${i + 1}`, {
        fontSize: '9px',
        color: '#cccccc',
        stroke: '#000000',
        strokeThickness: 1,
      });
      this.actionBarContainer.add(keyText);
      this.actionBarKeyTexts.push(keyText);

      // Cooldown timer text (centered on slot)
      const cdText = this.add.text(0, 0, '', {
        fontSize: '14px',
        fontStyle: 'bold',
        color: '#ffffff',
        stroke: '#000000',
        strokeThickness: 3,
      });
      cdText.setOrigin(0.5, 0.5);
      cdText.setVisible(false);
      this.actionBarContainer.add(cdText);
      this.actionBarCooldownTexts.push(cdText);
    }
  }

  private drawActionBar(): void {
    const cam = this.cameras.main;
    const totalWidth = ACTION_BAR_SLOTS * (this.AB_SLOT_SIZE + this.AB_SLOT_GAP) - this.AB_SLOT_GAP + this.AB_PADDING * 2;
    const barX = Math.floor((cam.width - totalWidth) / 2) + this.actionBarOffset.x;
    const barY = cam.height - this.AB_SLOT_SIZE - this.AB_PADDING - 8 + this.actionBarOffset.y;
    const now = Date.now();

    // Cache screen rect for drag hit-testing
    this.actionBarScreenRect = { x: barX, y: barY - this.AB_PADDING, w: totalWidth, h: this.AB_SLOT_SIZE + this.AB_PADDING * 2 };

    this.actionBarGfx.clear();
    this.actionBarCooldownGfx.clear();

    // Background
    this.actionBarGfx.fillStyle(0x111122, 0.85);
    this.actionBarGfx.fillRoundedRect(barX, barY - this.AB_PADDING, totalWidth, this.AB_SLOT_SIZE + this.AB_PADDING * 2, 6);
    this.actionBarGfx.lineStyle(1, 0x555588, 0.8);
    this.actionBarGfx.strokeRoundedRect(barX, barY - this.AB_PADDING, totalWidth, this.AB_SLOT_SIZE + this.AB_PADDING * 2, 6);

    // Drag grip indicator (left side of bar)
    this.actionBarGfx.fillStyle(0x8888aa, 0.5);
    for (let dot = 0; dot < 3; dot++) {
      this.actionBarGfx.fillRect(barX + 3, barY + 4 + dot * 12, 3, 8);
    }

    for (let i = 0; i < ACTION_BAR_SLOTS; i++) {
      const sx = barX + this.AB_PADDING + i * (this.AB_SLOT_SIZE + this.AB_SLOT_GAP);
      const sy = barY;
      const skillId = this.actionBar[i];
      const skill = skillId ? ClientDataManager.instance.getSkill(skillId) : null;

      // Slot background
      if (skill) {
        this.actionBarGfx.fillStyle(skill.iconColor, 0.5);
      } else {
        this.actionBarGfx.fillStyle(0x222244, 0.6);
      }
      this.actionBarGfx.fillRect(sx, sy, this.AB_SLOT_SIZE, this.AB_SLOT_SIZE);

      // Slot border
      this.actionBarGfx.lineStyle(1, skill ? 0x888888 : 0x444466, 1);
      this.actionBarGfx.strokeRect(sx, sy, this.AB_SLOT_SIZE, this.AB_SLOT_SIZE);

      // Skill text
      if (skill) {
        this.actionBarSlotTexts[i].setText(skill.iconAbbrev);
        this.actionBarSlotTexts[i].setPosition(sx + this.AB_SLOT_SIZE / 2, sy + this.AB_SLOT_SIZE / 2);
        this.actionBarSlotTexts[i].setVisible(true);
      } else {
        this.actionBarSlotTexts[i].setVisible(false);
      }

      // Key number
      this.actionBarKeyTexts[i].setPosition(sx + 2, sy + 1);

      // Cooldown overlay + timer number
      if (skill) {
        const cd = this.skillCooldowns.get(skillId);
        if (cd && now < cd.expiresAt) {
          const remaining = cd.expiresAt - now;
          const ratio = remaining / cd.durationMs;
          const fillHeight = Math.floor(this.AB_SLOT_SIZE * ratio);
          this.actionBarCooldownGfx.fillStyle(0x000000, 0.6);
          this.actionBarCooldownGfx.fillRect(sx, sy, this.AB_SLOT_SIZE, fillHeight);

          // Show remaining seconds as overlay text
          const secs = Math.ceil(remaining / 1000);
          this.actionBarCooldownTexts[i].setText(`${secs}`);
          this.actionBarCooldownTexts[i].setPosition(sx + this.AB_SLOT_SIZE / 2, sy + this.AB_SLOT_SIZE / 2);
          this.actionBarCooldownTexts[i].setVisible(true);
        } else {
          this.actionBarCooldownTexts[i].setVisible(false);
        }
      } else {
        this.actionBarCooldownTexts[i].setVisible(false);
      }
    }
  }

  /**
   * Get the action bar slot index at a given screen position, or -1.
   */
  private getActionBarSlotAt(px: number, py: number): number {
    const cam = this.cameras.main;
    const totalWidth = ACTION_BAR_SLOTS * (this.AB_SLOT_SIZE + this.AB_SLOT_GAP) - this.AB_SLOT_GAP + this.AB_PADDING * 2;
    const barX = Math.floor((cam.width - totalWidth) / 2) + this.actionBarOffset.x;
    const barY = cam.height - this.AB_SLOT_SIZE - this.AB_PADDING - 8 + this.actionBarOffset.y;

    for (let i = 0; i < ACTION_BAR_SLOTS; i++) {
      const sx = barX + this.AB_PADDING + i * (this.AB_SLOT_SIZE + this.AB_SLOT_GAP);
      const sy = barY;
      if (px >= sx && px <= sx + this.AB_SLOT_SIZE && py >= sy && py <= sy + this.AB_SLOT_SIZE) {
        return i;
      }
    }
    return -1;
  }

  // ═══════════════════════════════════════════════════════════
  // CAST BAR
  // ═══════════════════════════════════════════════════════════

  private createCastBar(): void {
    this.castBarContainer = this.add.container(0, 0);
    this.castBarContainer.setScrollFactor(0);
    this.castBarContainer.setDepth(UI_DEPTH_BASE + 60);
    this.castBarContainer.setVisible(false);

    this.castBarGfx = this.add.graphics();
    this.castBarContainer.add(this.castBarGfx);

    this.castBarText = this.add.text(0, 0, '', {
      fontSize: '12px',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.castBarText.setOrigin(0.5, 0.5);
    this.castBarContainer.add(this.castBarText);
  }

  private drawCastBar(): void {
    if (!this.localCastingSkillId || this.localCastingDurationMs <= 0) {
      this.castBarContainer.setVisible(false);
      return;
    }

    const cam = this.cameras.main;
    const barWidth = 260;
    const barHeight = 16;
    const barX = Math.floor((cam.width - barWidth) / 2);
    const barY = cam.height - this.AB_SLOT_SIZE - this.AB_PADDING - 40;

    const now = Date.now();
    const elapsed = now - this.localCastingStartedAt;
    const progress = Math.min(1, elapsed / this.localCastingDurationMs);

    if (progress >= 1) {
      this.localCastingSkillId = '';
      this.localCastingDurationMs = 0;
      this.castBarContainer.setVisible(false);
      return;
    }

    this.castBarContainer.setVisible(true);
    this.castBarGfx.clear();

    // Background
    this.castBarGfx.fillStyle(0x000000, 0.8);
    this.castBarGfx.fillRoundedRect(barX - 2, barY - 2, barWidth + 4, barHeight + 4, 4);

    // Progress fill
    this.castBarGfx.fillStyle(0xffaa00, 0.9);
    this.castBarGfx.fillRect(barX, barY, barWidth * progress, barHeight);

    // Border
    this.castBarGfx.lineStyle(1, 0xffffff, 0.5);
    this.castBarGfx.strokeRoundedRect(barX - 2, barY - 2, barWidth + 4, barHeight + 4, 4);

    // Skill name
    const skill = ClientDataManager.instance.getSkill(this.localCastingSkillId);
    this.castBarText.setText(skill?.name ?? 'Casting...');
    this.castBarText.setPosition(barX + barWidth / 2, barY + barHeight / 2);
  }

  // ═══════════════════════════════════════════════════════════
  // SKILLS PANE (K key toggle)
  // ═══════════════════════════════════════════════════════════

  private createSkillsPane(): void {
    const cam = this.cameras.main;
    this.skillsPaneContainer = this.add.container(0, 0);
    this.skillsPaneContainer.setScrollFactor(0);
    this.skillsPaneContainer.setDepth(UI_DEPTH_BASE + 210);
    this.skillsPaneContainer.setVisible(false);

    // Fullscreen dim overlay — kept OUTSIDE skillsPaneContainer so it doesn't move when the pane is dragged
    this.skillsDimBg = this.add.rectangle(cam.width / 2, cam.height / 2, cam.width, cam.height, 0x000000, 0.5);
    this.skillsDimBg.setScrollFactor(0);
    this.skillsDimBg.setDepth(UI_DEPTH_BASE + 209); // just below skillsPaneContainer (UI_DEPTH_BASE + 210)
    this.skillsDimBg.setVisible(false);

    this.skillsPaneGfx = this.add.graphics();
    this.skillsPaneContainer.add(this.skillsPaneGfx);

    // Title
    const paneW = 420;
    const paneH = 360;
    const paneX = Math.floor((cam.width - paneW) / 2);
    // Store default rect for drag hit-testing (offset applied at runtime)
    this.skillsPaneScreenRect = { x: paneX, y: Math.floor((cam.height - paneH) / 2), w: paneW, h: paneH };
    const paneY = Math.floor((cam.height - paneH) / 2);

    const paneBg = this.add.rectangle(paneX + paneW / 2, paneY + paneH / 2, paneW, paneH, 0x1a1a2e, 0.95);
    paneBg.setStrokeStyle(2, 0x555588);
    this.skillsPaneContainer.add(paneBg);

    const titleText = this.add.text(paneX + 10, paneY + 8, 'Skills (drag to action bar)', {
      fontSize: '14px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.skillsPaneContainer.add(titleText);

    // Drag ghost for skills pane
    this.skillDragGhostBg = this.add.rectangle(0, 0, 52, 22, 0x000000, 0.85);
    this.skillDragGhostBg.setStrokeStyle(1, 0xffcc00);
    this.skillDragGhostBg.setVisible(false);
    this.skillDragGhostBg.setDepth(UI_DEPTH_BASE + 899);
    this.skillsPaneContainer.add(this.skillDragGhostBg);

    this.skillDragGhost = this.add.text(0, 0, '', {
      fontSize: '11px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.skillDragGhost.setOrigin(0.5, 0.5);
    this.skillDragGhost.setVisible(false);
    this.skillDragGhost.setDepth(UI_DEPTH_BASE + 900);
    this.skillsPaneContainer.add(this.skillDragGhost);

    // Skill tooltip (shown on hover)
    this.skillTooltipContainer = this.add.container(0, 0);
    this.skillTooltipContainer.setDepth(UI_DEPTH_BASE + 1000);
    this.skillTooltipContainer.setVisible(false);
    this.skillsPaneContainer.add(this.skillTooltipContainer);

    this.skillTooltipBg = this.add.graphics();
    this.skillTooltipContainer.add(this.skillTooltipBg);

    this.skillTooltipText = this.add.text(0, 0, '', {
      fontSize: '11px',
      color: '#dddddd',
      stroke: '#000000',
      strokeThickness: 1,
      wordWrap: { width: 240 },
      lineSpacing: 3,
    });
    this.skillTooltipContainer.add(this.skillTooltipText);
  }

  private toggleSkillsPane(): void {
    this.skillsPaneOpen = !this.skillsPaneOpen;
    this.skillsDimBg.setVisible(this.skillsPaneOpen);
    this.skillsPaneContainer.setVisible(this.skillsPaneOpen);

    if (this.skillsPaneOpen) {
      this.skillsPaneContainer.setPosition(this.skillsOffset.x, this.skillsOffset.y);
      this.renderSkillsPane();
      this.setupSkillsPaneDrag();
    } else {
      this.hideSkillTooltip();
    }
  }

  private renderSkillsPane(): void {
    const cam = this.cameras.main;
    const paneW = 420;
    const paneH = 360;
    const paneX = Math.floor((cam.width - paneW) / 2);
    const paneY = Math.floor((cam.height - paneH) / 2);

    // Clear old rows
    for (const t of this.skillsPaneSkillRows) {
      t.destroy();
    }
    this.skillsPaneSkillRows = [];

    this.skillsPaneGfx.clear();

    const classSkills = ClientDataManager.instance.getClassSkills(this.localClassId) ?? [];
    let y = paneY + 30;
    const rowH = 28;

    // Header
    const headerText = this.add.text(paneX + 10, y, 'Skill             Lvl  Cost    Cast     CD', {
      fontSize: '10px',
      color: '#aaaacc',
      stroke: '#000000',
      strokeThickness: 1,
    });
    this.skillsPaneContainer.add(headerText);
    this.skillsPaneSkillRows.push(headerText);
    y += 18;

    for (const skillId of classSkills) {
      const skill = ClientDataManager.instance.getSkill(skillId);
      if (!skill) continue;

      const available = this.localLevel >= skill.levelRequired;
      const color = available ? '#ffffff' : '#666666';

      // Icon color square
      if (available) {
        this.skillsPaneGfx.fillStyle(skill.iconColor, 0.6);
      } else {
        this.skillsPaneGfx.fillStyle(0x333333, 0.4);
      }
      this.skillsPaneGfx.fillRect(paneX + 10, y + 2, 16, 16);
      this.skillsPaneGfx.lineStyle(1, 0x555555, 0.5);
      this.skillsPaneGfx.strokeRect(paneX + 10, y + 2, 16, 16);

      // Abbreviation in icon
      const abbrText = this.add.text(paneX + 18, y + 10, skill.iconAbbrev, {
        fontSize: '8px',
        fontStyle: 'bold',
        color: available ? '#ffffff' : '#888888',
        stroke: '#000000',
        strokeThickness: 1,
      });
      abbrText.setOrigin(0.5, 0.5);
      this.skillsPaneContainer.add(abbrText);
      this.skillsPaneSkillRows.push(abbrText);

      // Skill name + details
      const castStr = skill.castTimeMs === 0 ? 'Inst' : `${(skill.castTimeMs / 1000).toFixed(1)}s`;
      const cdStr = `${(skill.cooldownMs / 1000).toFixed(0)}s`;
      const costStr = skill.resourceCost > 0 ? `${skill.resourceCost}${skill.resourceType === 'mana' ? 'MP' : 'EP'}` : 'Free';

      const rowText = this.add.text(paneX + 32, y + 2, `${skill.name.padEnd(18)}${String(skill.levelRequired).padStart(3)}  ${costStr.padStart(6)}  ${castStr.padStart(6)}  ${cdStr.padStart(5)}`, {
        fontSize: '11px',
        color,
        stroke: '#000000',
        strokeThickness: 1,
        fontFamily: 'monospace',
      });
      this.skillsPaneContainer.add(rowText);
      this.skillsPaneSkillRows.push(rowText);

      // Store data attribute for drag detection
      (rowText as any)._skillId = skillId;
      (rowText as any)._skillY = y;

      y += rowH;
    }
  }

  private showSkillTooltip(skill: SkillTemplate, px: number, py: number): void {
    const cam = this.cameras.main;
    const tooltipW = 260;
    const pad = 10;

    // Build tooltip content — description and effect notes only
    let lines = skill.description;
    if (skill.effectNotes) {
      lines += `\n${skill.effectNotes}`;
    }

    this.skillTooltipText.setText(lines);
    this.skillTooltipText.setPosition(0, 0);

    // Measure text bounds
    const textW = Math.min(tooltipW, this.skillTooltipText.width + pad * 2);
    const textH = this.skillTooltipText.height + pad * 2;

    // Position tooltip to the right of the cursor, clamped to screen
    let tx = px + 20;
    let ty = py - 10;
    if (tx + textW > cam.width - 10) tx = px - textW - 10;
    if (ty + textH > cam.height - 10) ty = cam.height - textH - 10;
    if (ty < 10) ty = 10;

    this.skillTooltipContainer.setPosition(tx, ty);
    this.skillTooltipText.setPosition(pad, pad);

    // Draw background
    this.skillTooltipBg.clear();
    this.skillTooltipBg.fillStyle(0x0a0a1a, 0.95);
    this.skillTooltipBg.fillRoundedRect(0, 0, this.skillTooltipText.width + pad * 2, textH, 6);
    this.skillTooltipBg.lineStyle(1, 0x888888, 0.8);
    this.skillTooltipBg.strokeRoundedRect(0, 0, this.skillTooltipText.width + pad * 2, textH, 6);

    this.skillTooltipContainer.setVisible(true);
  }

  private hideSkillTooltip(): void {
    this.skillTooltipContainer.setVisible(false);
  }

  private setupSkillsPaneDrag(): void {
    // We use the existing scene pointer handlers — check if skills pane is open

    const onPointerDown = (pointer: Phaser.Input.Pointer) => {
      if (!this.skillsPaneOpen) return;
      if (this.hudDragTarget !== null) return; // panel drag takes priority

      // Adjust for the container's drag offset
      const px = pointer.x - this.skillsOffset.x;
      const py = pointer.y - this.skillsOffset.y;

      // Check if clicking on a skill row
      for (const row of this.skillsPaneSkillRows) {
        const sid = (row as any)._skillId;
        const sy = (row as any)._skillY;
        if (!sid || sy === undefined) continue;

        const skill = ClientDataManager.instance.getSkill(sid);
        if (!skill || this.localLevel < skill.levelRequired) continue;

        const cam = this.cameras.main;
        const paneW = 420;
        const paneX = Math.floor((cam.width - paneW) / 2);

        if (px >= paneX + 10 && px <= paneX + paneW - 10 && py >= sy && py <= sy + 26) {
          this.skillDragging = true;
          this.skillDragId = sid;
          this.skillDragGhost.setText(skill.iconAbbrev);
          this.skillDragGhost.setVisible(true);
          this.skillDragGhostBg.setVisible(true);
          this.skillDragGhost.setPosition(px + 16, py);
          this.skillDragGhostBg.setPosition(px + 16, py);
          return;
        }
      }
    };

    const onPointerMove = (pointer: Phaser.Input.Pointer) => {
      if (this.skillDragging) {
        this.skillDragGhost.setPosition(pointer.x + 16, pointer.y);
        this.skillDragGhostBg.setPosition(pointer.x + 16, pointer.y);
        this.hideSkillTooltip();
        return;
      }

      // Hover tooltip — check if hovering a skill row
      if (!this.skillsPaneOpen) return;
      const px = pointer.x - this.skillsOffset.x;
      const py = pointer.y - this.skillsOffset.y;
      const cam = this.cameras.main;
      const paneW = 420;
      const paneX = Math.floor((cam.width - paneW) / 2);

      for (const row of this.skillsPaneSkillRows) {
        const sid = (row as any)._skillId;
        const sy = (row as any)._skillY;
        if (!sid || sy === undefined) continue;

        const skill = ClientDataManager.instance.getSkill(sid);
        if (!skill) continue;

        if (px >= paneX + 10 && px <= paneX + paneW - 10 && py >= sy && py <= sy + 26) {
          this.showSkillTooltip(skill, px, py);
          return;
        }
      }
      this.hideSkillTooltip();
    };

    const onPointerUp = (pointer: Phaser.Input.Pointer) => {
      if (!this.skillDragging) return;

      // Check if dropped on action bar slot
      const slot = this.getActionBarSlotAt(pointer.x, pointer.y);
      if (slot >= 0 && this.skillDragId) {
        this.actionBar[slot] = this.skillDragId;
        this.network.sendSetActionBar(this.actionBar);
      }

      this.skillDragging = false;
      this.skillDragId = '';
      this.skillDragGhost.setVisible(false);
      this.skillDragGhostBg.setVisible(false);
    };

    // Remove any previous skill pane listeners to avoid stacking
    this.input.off('pointerdown', onPointerDown);
    this.input.off('pointermove', onPointerMove);
    this.input.off('pointerup', onPointerUp);

    this.input.on('pointerdown', onPointerDown);
    this.input.on('pointermove', onPointerMove);
    this.input.on('pointerup', onPointerUp);
  }
}
