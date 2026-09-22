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
  PartyMemberInfo,
  orthoToIso,
  isoToOrtho,
  tileToIso,
  ORTHO_TILE_SIZE,
  LOOT_BAG_PICKUP_RANGE,
  dirFromOrthoVector,
  dirFromScreenVector,
  EQUIP_SLOTS,
  attackAnimFor,
} from '@valhalla/shared';
import type { PaperdollAnim, PaperdollDir } from '@valhalla/shared';
import { ClientDataManager } from '../systems/ClientDataManager.js';
import { AuthClient } from '../systems/AuthClient.js';
import { PlayerTargetInfo, NpcTargetInfo, NpcBuffData, ENTITY_DEPTH_BASE, UI_DEPTH_BASE,
         createCharacterSprite, characterAnimKey, playCharacterAnim, toPaperdollAnim } from '../systems/EntityRenderer.js';

interface PendingInput {
  input: InputPayload;
  dt: number;
}

/**
 * Main gameplay scene.
 * Handles tile map rendering, local player with client-side prediction,
 * remote player interpolation, projectile rendering, HP, mana, death/respawn.
 */

/** `weapon` -> `equipWeapon`, the flat field name on PlayerState. */
function equipField(slot: string): string {
  return `equip${slot.charAt(0).toUpperCase()}${slot.slice(1)}`;
}

/** Read the flat `equip*` fields off a player state (or cache) into a slot map. */
function playerEquipmentMap(src: any, fallback?: any): Record<string, string> {
  const out: Record<string, string> = {};
  for (const slot of EQUIP_SLOTS) {
    const f = equipField(slot);
    out[slot] = src?.[f] ?? fallback?.[f] ?? '';
  }
  return out;
}

/** The same values keyed by field name, for the remote-player cache. */
function equipFields(src: any, fallback?: any): Record<string, string> {
  const out: Record<string, string> = {};
  for (const slot of EQUIP_SLOTS) {
    const f = equipField(slot);
    out[f] = src?.[f] ?? fallback?.[f] ?? '';
  }
  return out;
}

export class GameScene extends Phaser.Scene {
  private network!: NetworkClient;
  private inputManager!: InputManager;
  private entityRenderer!: EntityRenderer;

  // Local player
  private playerSprite!: Phaser.GameObjects.Sprite;
  private aimLine!: Phaser.GameObjects.Graphics;
  private localX: number = 0;
  private localY: number = 0;
  /** Visual-only position that lerps toward localX/Y each frame to absorb reconcile jitter. */
  private renderX: number = 0;
  private renderY: number = 0;
  /** True when the current zone uses isometric rendering. */
  private isIso: boolean = false;
  /** Last facing direction for idle animation. */
  private lastFacingDir: string = 's';
  /** True while a one-shot or looping action animation is playing on the local player. */
  private isPlayingActionAnim: boolean = false;

  // Local player vitals
  private localHp: number = 100;
  private localMaxHp: number = 100;
  private localShieldHp: number = 0;
  private localMana: number = 0;
  private localMaxMana: number = 0;
  private localAlive: boolean = true;
  private localSpeed: number = 200;
  private localClassId: string = 'warrior';
  /** Paperdoll base body for the local character. */
  private localBodyId: string = '';
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
  /** Last inputSeq value acknowledged by the server — used to skip redundant reconciles. */
  private _lastServerSeq: number = -1;
  private lastServerSeq: number = 0;

  // Map data (received from server)
  private collisionGrid: number[] = [];
  private collisionMapW: number = 0;
  private collisionMapH: number = 0;
  private mapTileSize: number = 64;
  private mapWidthPx: number = 4096;
  private mapHeightPx: number = 4096;
  private tileSprites: Phaser.GameObjects.Sprite[] = [];
  /**
   * Tile sprites bucketed into fixed squares of the tile grid, with the screen
   * rectangle each bucket covers. A 128x128 map is ~22k tile sprites; asking
   * Phaser to consider every one of them each frame is what makes a big map
   * feel heavy, so whole buckets are switched off when they fall outside the
   * camera. Buckets, not individual sprites, because the visibility test then
   * runs a few hundred times per frame instead of tens of thousands.
   */
  private tileChunks: {
    left: number; top: number; right: number; bottom: number;
    sprites: Phaser.GameObjects.Sprite[]; shown: boolean;
  }[] = [];
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
  // Zone-filtering for loot bags
  private lootBagZones: Map<string, string> = new Map();
  private lootBagCache: Map<string, any> = new Map();
  private visibleLootBags: Set<string> = new Set();

  // ── Loot Panel State ───────────────────────────────────────
  private lootPanelOpen: boolean = false;
  private currentLootBagId: string | null = null;
  private lootPanelContainer!: Phaser.GameObjects.Container;
  private lootPanelGfx!: Phaser.GameObjects.Graphics;
  private lootPanelSlotTexts: Phaser.GameObjects.Text[] = [];
  private lootPanelQtyTexts: Phaser.GameObjects.Text[] = [];
  private lootPanelSlotIcons: (Phaser.GameObjects.Image | null)[] = [];
  private lootPanelTitleText!: Phaser.GameObjects.Text;
  /** Cached loot panel items from the latest server state. */
  private lootPanelItems: { itemId: string; quantity: number }[] = [];
  // Loot panel layout constants
  private readonly LOOT_COLS = 6;
  private readonly LOOT_ROWS = 3;
  private readonly LOOT_SLOT_SIZE = 48;
  private readonly LOOT_SLOT_GAP = 4;
  private readonly LOOT_PANEL_PAD = 10;

  // ── Draggable HUD panels ──────────────────────────────────
  // Per-panel offsets from their default computed positions (pixels).
  private chatOffset:      { x: number; y: number } = { x: 0, y: 0 };
  private actionBarOffset: { x: number; y: number } = { x: 0, y: 0 };
  private invOffset:       { x: number; y: number } = { x: 0, y: 0 };
  private skillsOffset:    { x: number; y: number } = { x: 0, y: 0 };
  private combatLogOffset:  { x: number; y: number } = { x: 0, y: 0 };
  // Active drag state
  private hudDragTarget: 'chat' | 'actionBar' | 'inventory' | 'skills' | 'target' | 'party' | 'buffs' | 'combatLog' | null = null;
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
  private localEquipment: Record<string, string> =
    Object.fromEntries(EQUIP_SLOTS.map(s => [s, ''])) as Record<string, string>;
  /** Overlay sprites for local player equipped items (managed by EntityRenderer helpers). */
  private localEquipOverlays: { slot: string; itemId: string; sprite: Phaser.GameObjects.Sprite }[] = [];
  private localXp: number = 0;
  private invContainer!: Phaser.GameObjects.Container;
  private invDimBg!: Phaser.GameObjects.Rectangle;    // fixed dim overlay, NOT inside invContainer
  private skillsDimBg!: Phaser.GameObjects.Rectangle; // fixed dim overlay, NOT inside skillsPaneContainer
  private panelGfx!: Phaser.GameObjects.Graphics;
  private invSlotTexts: Phaser.GameObjects.Text[] = [];
  private invSlotIcons: (Phaser.GameObjects.Image | null)[] = [];
  private invQtyTexts: Phaser.GameObjects.Text[] = [];
  private invTooltipText!: Phaser.GameObjects.Text;
  private invTitleText!: Phaser.GameObjects.Text;
  // Character panel elements
  private charInfoText!: Phaser.GameObjects.Text;
  private charEquipTexts: Phaser.GameObjects.Text[] = [];
  private charStatTexts: Phaser.GameObjects.Text[] = [];
  private charBarsGfx!: Phaser.GameObjects.Graphics;

  // Item Detail Panel
  private itemDetailPanelOpen: boolean = false;
  private itemDetailContainer!: Phaser.GameObjects.Container;
  private itemDetailGfx!: Phaser.GameObjects.Graphics;
  private _itemDetailDynamic: Phaser.GameObjects.GameObject[] = [];

  // Drag-and-drop state
  private dragging: boolean = false;
  private dragSource: { type: 'inventory' | 'equipment' | 'lootBag'; index?: number; slotType?: string; bagId?: string; bagSlotIndex?: number } | null = null;
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

  // Auto-attack state (synced from server)
  private localAutoAttackActive: boolean = false;
  private localAutoAttackSkillId: string = '';

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

  // Action bar slot drag state (drag skill off bar to remove it)
  private abDragging: boolean = false;
  private abDragSourceSlot: number = -1;
  private abDragGhost!: Phaser.GameObjects.Text;
  private abDragGhostBg!: Phaser.GameObjects.Rectangle;

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
  private chatScrollOffset: number = 0; // 0 = bottom (newest)
  private chatScrollGfx!: Phaser.GameObjects.Graphics;

  // ── Targeting System ─────────────────────────────────────
  /** ID of the currently targeted entity (player sessionId or NPC id), or null. */
  private currentTargetId: string | null = null;
  /** Type of the currently targeted entity. */
  private currentTargetType: 'player' | 'npc' | 'self' | null = null;
  /**
   * Set to true when an entity sprite was just clicked, so the global
   * pointer-down handler (which moves the player) can skip that frame.
   */
  private entityClickConsumed: boolean = false;

  // Target nameplate panel
  private targetOffset: { x: number; y: number } = { x: 0, y: 0 };
  private targetNameplateContainer!: Phaser.GameObjects.Container;
  private targetNameplateBg!: Phaser.GameObjects.Graphics;
  private targetNameplateTitleText!: Phaser.GameObjects.Text;
  private targetNameplateNameText!: Phaser.GameObjects.Text;
  private targetNameplateLevelText!: Phaser.GameObjects.Text;
  private targetNameplateHpBar!: Phaser.GameObjects.Graphics;
  private targetNameplateHpText!: Phaser.GameObjects.Text;
  private targetNameplateBuffsGfx!: Phaser.GameObjects.Graphics;
  private targetNameplateBuffsText!: Phaser.GameObjects.Text;
  private targetNameplateTitleHandle!: { x: number; y: number; w: number; h: number };

  // ── Party System ──────────────────────────────────────────
  private partyMembers: PartyMemberInfo[] = [];
  private partyMemberData: Map<string, { hp: number; maxHp: number; mana: number; maxMana: number; level: number; alive: boolean; shieldHp: number }> = new Map();
  private remotePlayerShieldHp: Map<string, number> = new Map();

  // Buffs panel
  private localBuffs: Map<string, { skillId: string; appliedAt: number; expiresAt: number }> = new Map();
  private buffsOffset: { x: number; y: number } = { x: 0, y: 0 };
  private buffsPanelContainer!: Phaser.GameObjects.Container;
  private buffsPanelBg!: Phaser.GameObjects.Graphics;
  private buffsPanelRowGfx: Phaser.GameObjects.Graphics[] = [];
  private buffsPanelAbbrTexts: Phaser.GameObjects.Text[] = [];
  private buffsPanelNameTexts: Phaser.GameObjects.Text[] = [];
  private buffsPanelTimerTexts: Phaser.GameObjects.Text[] = [];
  private buffsPanelTitleHandle: { x: number; y: number; w: number; h: number } = { x: 0, y: 0, w: 0, h: 0 };
  private buffsPanelTitleText!: Phaser.GameObjects.Text;
  private partyOffset: { x: number; y: number } = { x: 0, y: 0 };
  private partyPanelContainer!: Phaser.GameObjects.Container;
  private partyPanelBg!: Phaser.GameObjects.Graphics;
  private partyPanelTexts: Phaser.GameObjects.Text[] = [];
  private partyPanelHpBars: Phaser.GameObjects.Graphics[] = [];
  private partyPanelManaBars: Phaser.GameObjects.Graphics[] = [];
  private partyPanelTitleHandle: { x: number; y: number; w: number; h: number } = { x: 0, y: 0, w: 0, h: 0 };
  private partySlotSessionIds: (string | null)[] = [null, null, null, null];

  // ── Options Menu ─────────────────────────────────────────
  private optionsMenuOpen: boolean = false;
  private optionsMenuContainer!: Phaser.GameObjects.Container;
  private optionsDimBg!: Phaser.GameObjects.Rectangle;
  private optionsButtons: { bg: Phaser.GameObjects.Rectangle; text: Phaser.GameObjects.Text; action: string }[] = [];
  private optionsCloseText!: Phaser.GameObjects.Text;
  /** Index of the currently hovered options button (-1 = none, -2 = close btn). */
  private optionsHoveredIdx: number = -1;

  // ── HUD Edit Mode ──────────────────────────────────────────
  private hudEditMode: boolean = false;
  private hudEditLabel!: Phaser.GameObjects.Text;
  private hudEditSubLabel!: Phaser.GameObjects.Text;
  /** Per-panel visibility (used in normal gameplay, toggled in edit mode). */
  private panelVisibility: Record<string, boolean> = {
    chat: true, actionBar: true, target: true, party: true, buffs: true,
  };
  /** Per-panel opacity multiplier (0.5, 0.75, 1.0). */
  private panelOpacity: Record<string, number> = {
    chat: 1.0, actionBar: 1.0, target: 1.0, party: 1.0, buffs: 1.0,
  };
  /** Per-panel scale (0.8, 1.0, 1.2). */
  private panelScale: Record<string, number> = {
    chat: 1.0, actionBar: 1.0, target: 1.0, party: 1.0, buffs: 1.0,
  };
  private hudEditPanelControls: Phaser.GameObjects.GameObject[] = [];

  // Layout constants
  private readonly CHAT_MAX_W = 360;   // maximum panel width
  private readonly CHAT_H = 170;
  private readonly CHAT_BOTTOM_MARGIN = 8;
  private readonly CHAT_LINE_H = 15;
  private readonly CHAT_PAD = 6;
  private readonly CHAT_INPUT_H = 18;
  private readonly CHAT_MAX_MESSAGES = 200;
  private readonly CHAT_VISIBLE_LINES = 9; // (CHAT_H - header - input) / CHAT_LINE_H
  private readonly CHAT_SCROLLBAR_W = 8;
  /** Left edge of chat panel — 12px gap right of the HP/mana bar (barX=16 + barWidth=200 + border=4 + gap=12) */
  private readonly CHAT_LEFT_X = 232;
  /** Tracks current effective width so word-wrap is only recalculated on change */
  private chatEffectiveW = 200;

  // ── Combat Log Panel ────────────────────────────────────
  private readonly CL_W = 320;
  private readonly CL_H = 200;
  private readonly CL_LINE_H = 14;
  private readonly CL_PAD = 6;
  private readonly CL_TITLE_H = 18;
  private readonly CL_SCROLLBAR_W = 8;
  private readonly CL_MAX_MESSAGES = 200;
  private CL_VISIBLE_LINES = 12;
  private combatLogContainer!: Phaser.GameObjects.Container;
  private combatLogBgGfx!: Phaser.GameObjects.Graphics;
  private combatLogScrollGfx!: Phaser.GameObjects.Graphics;
  private combatLogTitleText!: Phaser.GameObjects.Text;
  private combatLogMessageTexts: Phaser.GameObjects.Text[] = [];
  private combatLogMessages: { text: string; color: string; type: string }[] = [];
  private combatLogScrollOffset: number = 0; // 0 = bottom (newest)
  private combatLogScrollDragging: boolean = false;
  private combatLogScrollDragStartY: number = 0;
  private combatLogScrollDragStartOffset: number = 0;
  private combatLogFilters: Record<string, boolean> = {
    outDmg: true,      // Your damage dealt
    inDmg: true,       // Damage taken
    misses: true,      // Misses (both directions)
    dodges: true,      // Dodges
    blocks: true,      // Blocks
    deaths: true,      // Deaths
    heals: true,       // Healing
    buffs: true,       // Buffs & debuffs
    xp: true,          // XP rewards
    party: true,       // Party member combat
  };
  private combatLogContextMenuOpen: boolean = false;
  private combatLogContextMenuPos: { x: number; y: number } = { x: 0, y: 0 };
  private combatLogContextGfx!: Phaser.GameObjects.Graphics;
  private combatLogContextTexts: Phaser.GameObjects.Text[] = [];

  constructor() {
    super({ key: 'GameScene' });
  }

  private characterId: number = 0;
  private authToken: string = '';

  init(data?: { classId?: string; characterId?: number; token?: string }): void {
    if (data?.classId)     this.selectedClassId = data.classId;
    if (data?.characterId) this.characterId     = data.characterId;
    if (data?.token)       this.authToken       = data.token;

    // ── Full state reset ────────────────────────────────────────────────────
    // Phaser reuses the same scene instance on scene.start(), so any fields
    // initialised as class-level defaults carry over from the previous session.
    // Everything below must be reset here (before create() runs) so that a
    // second login always starts clean.

    // Local player vitals
    this.localX             = 0;
    this.localY             = 0;
    this.renderX            = 0;
    this.renderY            = 0;
    this.localHp            = 100;
    this.localMaxHp         = 100;
    this.localShieldHp      = 0;
    this.localMana          = 0;
    this.localMaxMana       = 0;
    this.localEnergy        = 0;
    this.localMaxEnergy     = 0;
    this.localAlive         = true;
    this.localSpeed         = 200;
    this.localClassId       = 'warrior';
    this.localBodyId        = '';
    this.localLevel         = 1;
    this.localCharacterName = '';
    this.lastFacingDir      = 's';
    this.isIso              = false;
    this.respawnTimer       = 0;

    // Client-side prediction
    this.pendingInputs  = [];
    this._lastServerSeq = -1;
    this.lastServerSeq  = 0;

    // Zone / entity tracking
    this.currentZoneId           = 'grasslands';
    this.remotePlayerZones       = new Map();
    this.remotePlayerCache       = new Map();
    this.npcZones                = new Map();
    this.npcCache                = new Map();
    this.lootBagZones            = new Map();
    this.lootBagCache            = new Map();
    this.visibleProjectiles      = new Set();
    this.visibleSpellProjectiles = new Set();
    this.visibleNpcs             = new Set();
    this.visibleLootBags         = new Set();

    // Skill / action bar
    this.actionBar      = ['', '', '', '', '', '', '', ''];
    this.skillCooldowns = new Map();
    this.localCastingSkillId     = '';
    this.localCastingStartedAt   = 0;
    this.localCastingDurationMs  = 0;

    // Targeting & combat
    this.currentTargetId   = null;
    this.currentTargetType = null;
    this.localBuffs        = new Map();

    // Combat Log
    this.combatLogMessages      = [];
    this.combatLogScrollOffset  = 0;
    this.combatLogContextMenuOpen = false;
    this.combatLogScrollDragging = false;

    // Party
    this.partyMembers        = [];
    this.partyMemberData     = new Map();
    this.remotePlayerShieldHp = new Map();

    // Inventory / equipment
    this.inventoryItems  = [];
    this.localEquipment  = Object.fromEntries(EQUIP_SLOTS.map(s => [s, ''])) as Record<string, string>;
    this.localEquipOverlays = [];
    this.localXp         = 0;

    // Loot
    this.lootPanelItems  = [];
    this.currentLootBagId = null;

    // Chat
    this.chatMessages       = [];
    this.chatInputText      = '';
    this.chatInputActive    = false;
    this.chatCurrentChannel = 'general';
    this.chatScrollOffset   = 0;

    // UI open states (panels are recreated in create(), but booleans persist)
    this.inventoryOpen      = false;
    this.skillsPaneOpen     = false;
    this.itemDetailPanelOpen = false;
    this.lootPanelOpen      = false;
    this.optionsMenuOpen    = false;
    this.hudEditMode        = false;
    this.hudDragTarget      = null;
    this.dragging           = false;
    this.dragSource         = null;
    this.skillDragging      = false;
    this.skillDragId        = '';
    this.abDragging         = false;
    this.abDragSourceSlot   = -1;
    this.entityClickConsumed = false;
    this.connected          = false;
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

    // Create combat log panel
    this.createCombatLogPanel();

    // Create target nameplate panel (initially hidden)
    this.createTargetNameplate();

    // Create party panel (initially hidden)
    this.createPartyPanel();

    // Create buffs panel (initially hidden)
    this.createBuffsPanel();

    // Create loot panel (hidden initially)
    this.createLootPanel();

    // Create item detail panel (hidden initially)
    this.createItemDetailPanel();

    // Create options menu (hidden initially)
    this.createOptionsMenu();

    // Create HUD edit mode labels (hidden initially)
    this.createHudEditModeUI();

    // Suppress browser context menu so right-click works in-game
    this.game.canvas.addEventListener('contextmenu', (e) => e.preventDefault());

    // Load persisted HUD layout, then wire up panel drag handlers
    this.loadHudLayout();
    this.setupHudDragHandlers();

    // Inventory toggle key (I)
    this.input.keyboard!.on('keydown-I', () => {
      if (this.chatInputActive || this.optionsMenuOpen || this.hudEditMode) return;
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
      } else if (this.lootPanelOpen) {
        // Close loot panel and the inventory that was auto-opened with it
        this.closeLootPanel();
        if (this.inventoryOpen) {
          this.toggleInventory();
        }
      } else if (this.inventoryOpen) {
        this.toggleInventory();
      } else if (this.skillsPaneOpen) {
        this.toggleSkillsPane();
      } else if (this.hudEditMode) {
        this.exitHudEditMode();
      } else if (this.currentTargetId) {
        // Clear target (also stops auto-attack via clearTarget)
        this.clearTarget();
      } else {
        this.toggleOptionsMenu();
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
      if (this.chatInputActive || this.inventoryOpen || this.skillsPaneOpen || this.optionsMenuOpen || this.hudEditMode) return;
      const skillId = this.actionBar[slotIndex];
      if (!skillId) return;

      const skill = ClientDataManager.instance.getSkill(skillId);

      // Auto-attack skills: toggle on/off
      if (skill?.isAutoAttack) {
        // If this exact auto-attack is already active, pressing the key again stops it
        if (this.localAutoAttackActive && this.localAutoAttackSkillId === skillId) {
          this.network.sendStopAutoAttack();
          return;
        }
        const showErr = (msg: string) => {
          const pos = this.network.sessionId ? this.getCombatTextPosition(this.network.sessionId) : null;
          if (pos) this.entityRenderer.showCombatText(pos.x, pos.y - 30, msg, '#ff8844');
        };
        if (!this.currentTargetId || this.currentTargetType !== 'npc') {
          showErr('No target');
          return;
        }
        this.network.sendStartAutoAttack(skillId, this.currentTargetId);
        return;
      }

      // Single-target skills require a target to be selected, and the target
      // must match the skill's target class (ally = player, enemy = NPC).
      if (skill?.targetType === 'singleEnemy' || skill?.targetType === 'singleAlly') {
        const showErr = (msg: string) => {
          const pos = this.network.sessionId ? this.getCombatTextPosition(this.network.sessionId) : null;
          if (pos) this.entityRenderer.showCombatText(pos.x, pos.y - 30, msg, '#ff8844');
        };
        if (!this.currentTargetId) {
          showErr('No target');
          return;
        }
        if (skill.targetType === 'singleAlly' && this.currentTargetType !== 'player' && this.currentTargetType !== 'self') {
          showErr('Invalid target');
          return;
        }
        if (skill.targetType === 'singleEnemy' && this.currentTargetType !== 'npc') {
          showErr('Invalid target');
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
      if (this.chatInputActive || this.inventoryOpen || this.optionsMenuOpen || this.hudEditMode) return;
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
      this.inputManager.suppressNextClick = true; // prevent melee attack on targeting click
      if (this.currentTargetId === sessionId && this.currentTargetType === 'player') {
        this.clearTarget(); // click same target again = deselect
      } else {
        this.setTarget(sessionId, 'player');
      }
    };
    this.entityRenderer.onNpcClick = (npcId: string) => {
      this.entityClickConsumed = true;
      this.inputManager.suppressNextClick = true;
      if (this.currentTargetId === npcId && this.currentTargetType === 'npc') {
        this.clearTarget(); // click same target again = deselect
      } else {
        this.setTarget(npcId, 'npc');
      }
    };
    // Right-click on NPC → set as target (auto-attack must be triggered from action bar)
    this.entityRenderer.onNpcRightClick = (npcId: string) => {
      this.entityClickConsumed = true;
      this.inputManager.suppressNextClick = true;
      this.setTarget(npcId, 'npc');
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

      // Reconcile remote player visibility — fixes the same race for players:
      // when the local player changes zones, remove sprites from the old zone
      // and add sprites for players already in the new zone.
      for (const [pid, playerZone] of this.remotePlayerZones) {
        const isRendered = this.entityRenderer.hasPlayer(pid);
        const shouldBeVisible = playerZone === this.currentZoneId;
        if (isRendered && !shouldBeVisible) {
          this.entityRenderer.removeRemotePlayer(pid);
          // Clear stale target if it was pointing at a ghost
          if (this.currentTargetId === pid) this.clearTarget();
        } else if (!isRendered && shouldBeVisible) {
          const cached = this.remotePlayerCache.get(pid);
          if (cached) {
            this.entityRenderer.addRemotePlayer(pid, cached.x, cached.y, cached.classId, cached.level, cached.characterName);
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
      for (const bid of this.visibleLootBags) {
        this.entityRenderer.removeLootBag(bid);
      }
      this.visibleLootBags.clear();
      // Close loot panel on zone change
      if (this.lootPanelOpen) this.closeLootPanel();

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
            cached.bodyId ?? '',
          );
          this.entityRenderer.updateRemotePlayerEquipment(sid, playerEquipmentMap(cached));
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

      // 5. Re-add loot bags that are in our new zone
      for (const [bid, bagZone] of this.lootBagZones) {
        if (bagZone === this.currentZoneId) {
          const cached = this.lootBagCache.get(bid);
          if (cached) {
            this.entityRenderer.addLootBag(bid, cached.x, cached.y, bagZone);
            this.visibleLootBags.add(bid);
          }
        }
      }

      // 6. Teleport the local player to the new spawn
      this.localX = data.spawnX;
      this.localY = data.spawnY;
      this.renderX = data.spawnX;
      this.renderY = data.spawnY;
      if (this.playerSprite) {
        const playerIso = this.isIso ? orthoToIso(this.localX, this.localY) : { x: this.localX, y: this.localY };
        this.playerSprite.setPosition(playerIso.x, playerIso.y);
        this.lastFacingDir = 's';
        playCharacterAnim(this, this.playerSprite, this.localClassId, 'idle', 's', true);
      }

      // 5. Clear pending inputs — server position is authoritative after zone change
      this.pendingInputs = [];
      this._lastServerSeq = -1;
    };

    this.network.onPlayerAdd = (player: any, sessionId: string) => {
      if (sessionId === this.network.sessionId) {
        // This is us — set up local player
        this.localX = player.x;
        this.localY = player.y;
        this.renderX = player.x;
        this.renderY = player.y;
        this.localHp = player.hp;
        this.localMaxHp = player.maxHp;
        this.localShieldHp = player.shieldHp ?? 0;
        this.localMana = player.mana ?? 0;
        this.localMaxMana = player.maxMana ?? 0;
        this.localAlive = player.alive;
        this.localSpeed = player.speed ?? 200;
        this.localClassId = player.classId ?? 'warrior';
        this.localBodyId = (player as { bodyId?: string }).bodyId ?? '';
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
          mana: player.mana ?? 0,
          maxMana: player.maxMana ?? 0,
          shieldHp: player.shieldHp ?? 0,
          bodyId: player.bodyId ?? '',
          ...equipFields(player),
        });
        if (theirZone === this.currentZoneId) {
          this.entityRenderer.addRemotePlayer(
            sessionId,
            player.x,
            player.y,
            player.classId ?? 'warrior',
            player.level ?? 1,
            player.characterName ?? '',
            player.bodyId ?? '',
          );
          this.entityRenderer.updateRemotePlayerEquipment(sessionId, playerEquipmentMap(player));
        }
      }
    };

    this.network.onPlayerChange = (player: any, sessionId: string) => {
      if (sessionId === this.network.sessionId) {
        // Update local state from server
        this.localHp = player.hp;
        this.localMaxHp = player.maxHp;
        this.localShieldHp = player.shieldHp ?? 0;
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

        // Track auto-attack state from server
        this.localAutoAttackActive = player.autoAttackActive ?? false;
        this.localAutoAttackSkillId = player.autoAttackSkillId ?? '';

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
          mana: player.mana ?? cached.mana ?? 0,
          maxMana: player.maxMana ?? cached.maxMana ?? 0,
          shieldHp: player.shieldHp ?? cached.shieldHp ?? 0,
          bodyId: player.bodyId ?? cached.bodyId ?? '',
          ...equipFields(player, cached),
        });

        if (wasInOurZone && !isInOurZone) {
          // Player left our zone — remove their sprite
          this.entityRenderer.removeRemotePlayer(sessionId);
        } else if (!wasInOurZone && isInOurZone) {
          // Player entered our zone — add their sprite and sync equipment immediately
          this.entityRenderer.addRemotePlayer(
            sessionId,
            player.x,
            player.y,
            player.classId ?? 'warrior',
            player.level ?? 1,
            player.characterName ?? '',
            player.bodyId ?? '',
          );
          this.entityRenderer.updateRemotePlayerEquipment(sessionId, playerEquipmentMap(player));
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
          // Sync equipment overlays for remote player
          this.entityRenderer.updateRemotePlayerEquipment(sessionId, playerEquipmentMap(player));
        }
        // Different zone (and wasn't in ours) → nothing to do

        // Track shield for target pane (remote players only)
        this.remotePlayerShieldHp.set(sessionId, player.shieldHp ?? 0);
      }

      // Update party member data cache (fires for ALL players, not just our zone)
      if (this.partyMembers.some(m => m.sessionId === sessionId)) {
        this.partyMemberData.set(sessionId, {
          hp: player.hp ?? 0,
          maxHp: player.maxHp ?? 1,
          mana: player.mana ?? 0,
          maxMana: player.maxMana ?? 0,
          level: player.level ?? 1,
          alive: player.alive ?? true,
          shieldHp: player.shieldHp ?? 0,
        });
      }
    };

    this.network.onPlayerRemove = (sessionId: string) => {
      if (this.currentTargetId === sessionId) this.clearTarget();
      this.entityRenderer.removeRemotePlayer(sessionId);
      this.remotePlayerZones.delete(sessionId);
      this.remotePlayerCache.delete(sessionId);
      this.remotePlayerShieldHp.delete(sessionId);
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

        // Sync active buffs from syncedBuffs schema
        if (npc.syncedBuffs) {
          const buffs: NpcBuffData[] = [];
          npc.syncedBuffs.forEach((b: any) => {
            buffs.push({ skillId: b.skillId, expiresAt: b.expiresAt, dotDamagePerSec: b.dotDamagePerSec ?? 0 });
          });
          this.entityRenderer.updateNPCBuffs(id, buffs);
        }
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
        // Combat log — incoming damage
        const atkName = this.getCombatEntityName(data.attackerId);
        const critTag = data.isCrit ? ' (Critical!)' : '';
        this.pushCombatLog(`${atkName} hits you for ${data.damage} damage${critTag}`, '#ff6644', 'inDmg');
      } else {
        this.showRemoteDamageFlash(data.targetId, data.damage, data.isCrit);
        // Combat log — party member taking damage
        if (this.isLocalOrParty(data.targetId)) {
          const tgtName = this.getCombatEntityName(data.targetId);
          const atkName = this.getCombatEntityName(data.attackerId);
          const critTag = data.isCrit ? ' (Critical!)' : '';
          this.pushCombatLog(`${atkName} hits ${tgtName} for ${data.damage}${critTag}`, '#cc8866', 'party');
        }
      }
    };

    this.network.onPlayerDied = (data: PlayerDiedData) => {
      if (data.targetId === this.network.sessionId) {
        const killerName = this.getCombatEntityName(data.killerId);
        this.pushCombatLog(`You were killed by ${killerName}`, '#ff4444', 'deaths');
      } else if (this.isLocalOrParty(data.targetId)) {
        const tgtName = this.getCombatEntityName(data.targetId);
        const killerName = this.getCombatEntityName(data.killerId);
        this.pushCombatLog(`${tgtName} was killed by ${killerName}`, '#ff6666', 'deaths');
      }
    };

    this.network.onPlayerRespawned = (data: PlayerRespawnedData) => {
      if (data.playerId === this.network.sessionId) {
        this.pushCombatLog('You have respawned', '#44ff44', 'deaths');
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
      // Combat log
      if (data.attackerId === this.network.sessionId) {
        const tgtName = this.getCombatEntityName(data.targetId);
        this.pushCombatLog(`Your attack missed ${tgtName}`, '#999999', 'misses');
      } else if (data.targetId === this.network.sessionId) {
        const atkName = this.getCombatEntityName(data.attackerId);
        this.pushCombatLog(`${atkName}'s attack missed you`, '#999999', 'misses');
      } else if (this.isLocalOrParty(data.attackerId) || this.isLocalOrParty(data.targetId)) {
        const atkName = this.getCombatEntityName(data.attackerId);
        const tgtName = this.getCombatEntityName(data.targetId);
        this.pushCombatLog(`${atkName}'s attack missed ${tgtName}`, '#888888', 'party');
      }
    };

    this.network.onDodged = (data: CombatFeedbackData) => {
      const pos = this.getCombatTextPosition(data.targetId);
      if (pos) this.entityRenderer.showCombatText(pos.x, pos.y, 'DODGE', '#ffffff');
      if (data.targetId === this.network.sessionId) {
        const atkName = this.getCombatEntityName(data.attackerId);
        this.pushCombatLog(`You dodged ${atkName}'s attack`, '#ffffff', 'dodges');
      } else if (data.attackerId === this.network.sessionId) {
        const tgtName = this.getCombatEntityName(data.targetId);
        this.pushCombatLog(`${tgtName} dodged your attack`, '#cccccc', 'dodges');
      } else if (this.isLocalOrParty(data.attackerId) || this.isLocalOrParty(data.targetId)) {
        const atkName = this.getCombatEntityName(data.attackerId);
        const tgtName = this.getCombatEntityName(data.targetId);
        this.pushCombatLog(`${tgtName} dodged ${atkName}'s attack`, '#aaaaaa', 'party');
      }
    };

    this.network.onBlocked = (data: CombatFeedbackData) => {
      const pos = this.getCombatTextPosition(data.targetId);
      if (pos) this.entityRenderer.showCombatText(pos.x, pos.y, 'BLOCK', '#4488ff');
      if (data.targetId === this.network.sessionId) {
        const atkName = this.getCombatEntityName(data.attackerId);
        this.pushCombatLog(`You blocked ${atkName}'s attack`, '#4488ff', 'blocks');
      } else if (data.attackerId === this.network.sessionId) {
        const tgtName = this.getCombatEntityName(data.targetId);
        this.pushCombatLog(`${tgtName} blocked your attack`, '#6688cc', 'blocks');
      } else if (this.isLocalOrParty(data.attackerId) || this.isLocalOrParty(data.targetId)) {
        const atkName = this.getCombatEntityName(data.attackerId);
        const tgtName = this.getCombatEntityName(data.targetId);
        this.pushCombatLog(`${tgtName} blocked ${atkName}'s attack`, '#5577aa', 'party');
      }
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

        // ── Action animation on hit ──
        const hitAttackerId = data.casterId || data.attackerId;
        if (hitAttackerId) {
          const animType = data.skillId === 'melee_attack' ? 'melee'
            : data.skillId === 'ranged_attack' ? 'ranged'
            : null; // named skills use cast animation triggered by onSkillStarted
          if (animType) {
            const dir = this.getDirectionToTarget(hitAttackerId, pos.x, pos.y);
            if (hitAttackerId === this.network.sessionId) {
              this.playActionAnimation(animType, dir);
            } else {
              this.entityRenderer.playRemoteActionAnim(hitAttackerId, animType, dir);
            }
          }
        }
      }
      // Combat log — outgoing damage (you or party hitting an NPC)
      const casterId = data.casterId || data.attackerId;
      if (casterId && this.isLocalOrParty(casterId)) {
        const atkName = this.getCombatEntityName(casterId);
        const tgtName = this.getCombatEntityName(data.targetId);
        const critTag = data.isCrit ? ' (Critical!)' : '';
        let skillLabel = '';
        if (data.skillId === 'melee_attack') {
          skillLabel = ' [Melee]';
        } else if (data.skillId === 'ranged_attack') {
          skillLabel = ' [Ranged]';
        } else if (data.skillId) {
          skillLabel = ` [${ClientDataManager.instance.getSkill(data.skillId)?.name ?? data.skillId}]`;
        }
        if (casterId === this.network.sessionId) {
          this.pushCombatLog(`You hit ${tgtName} for ${data.damage}${critTag}${skillLabel}`, '#ffcc44', 'outDmg');
        } else {
          this.pushCombatLog(`${atkName} hits ${tgtName} for ${data.damage}${critTag}${skillLabel}`, '#aabb88', 'party');
        }
      }
    };

    this.network.onNpcDied = (data) => {
      const tgtName = this.getCombatEntityName(data.targetId);
      if (data.killerId === this.network.sessionId) {
        this.pushCombatLog(`You killed ${tgtName}`, '#44ff44', 'deaths');
      } else if (this.isLocalOrParty(data.killerId)) {
        const killerName = this.getCombatEntityName(data.killerId);
        this.pushCombatLog(`${killerName} killed ${tgtName}`, '#88cc88', 'party');
      }
    };

    this.network.onXpGained = (amount: number) => {
      if (amount > 0) {
        this.pushCombatLog(`+${amount} XP`, '#ffaa00', 'xp');
      }
    };

    // Inventory sync
    this.network.onInventoryChange = (items: any[]) => {
      this.inventoryItems = items;
      // Always refresh the render so looted/moved items appear immediately,
      // regardless of how the panel was opened.
      this.renderInventorySlots();
    };

    // Equipment sync
    this.network.onEquipmentChange = (equipment: Record<string, string>) => {
      this.localEquipment = equipment;
      // Sync equipment overlay sprites on local player
      if (this.playerSprite) {
        this.entityRenderer.syncOverlays(this.localEquipOverlays, equipment, this.playerSprite);
      }
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
          // Cast-time spell — show cast bar + cast animation
          this.localCastingSkillId = data.skillId;
          this.localCastingStartedAt = Date.now();
          this.localCastingDurationMs = data.castTimeMs;
          this.playActionAnimation('cast', this.lastFacingDir);
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
      } else {
        // Remote player started casting
        if (data.castTimeMs > 0) {
          this.entityRenderer.playRemoteActionAnim(data.casterId, 'cast');
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
        // Stop cast animation when the spell finishes
        this.stopActionAnimation();
      } else if (data.casterId) {
        // Stop remote player's cast animation
        this.entityRenderer.stopRemoteActionAnim(data.casterId);
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
        // Combat log — healing
        if (this.isLocalOrParty(data.targetId) || this.isLocalOrParty(data.casterId)) {
          const casterName = this.getCombatEntityName(data.casterId);
          const tgtName = this.getCombatEntityName(data.targetId);
          const skillName = data.skillId ? (ClientDataManager.instance.getSkill(data.skillId)?.name ?? data.skillId) : 'heal';
          if (data.casterId === data.targetId) {
            this.pushCombatLog(`${casterName} healed self for ${data.amount} [${skillName}]`, '#44ff44', 'heals');
          } else {
            this.pushCombatLog(`${casterName} healed ${tgtName} for ${data.amount} [${skillName}]`, '#44ff44', 'heals');
          }
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
        this.stopActionAnimation();
        this.entityRenderer.showCombatText(this.localX, this.localY - 30, 'Interrupted!', '#ff8888');
      } else {
        this.entityRenderer.stopRemoteActionAnim(data.casterId);
      }
    };

    this.network.onBuffApplied = (data: { targetId: string; skillId: string; durationMs: number }) => {
      const skill = ClientDataManager.instance.getSkill(data.skillId);
      if (data.targetId === this.network.sessionId) {
        if (skill) {
          const pos = this.getCombatTextPosition(this.network.sessionId);
          if (pos) this.entityRenderer.showCombatText(pos.x, pos.y - 30, `+${skill.name}`, '#88ccff');
          // Combat log
          const durSec = (data.durationMs / 1000).toFixed(0);
          this.pushCombatLog(`+${skill.name} applied (${durSec}s)`, '#88ccff', 'buffs');
        }
        // Track in local buff list for the buffs panel
        const now = Date.now();
        this.localBuffs.set(data.skillId, {
          skillId: data.skillId,
          appliedAt: now,
          expiresAt: now + data.durationMs,
        });
      } else {
        // Debuff applied to an NPC — show a floating label above the target
        const npcPos = this.entityRenderer.getNPCPosition(data.targetId);
        if (npcPos && skill) {
          const label = skill.category === 'debuff' ? `☠ ${skill.name}` : `✦ ${skill.name}`;
          const color = skill.category === 'debuff' ? '#88ff44' : '#88ccff';
          this.entityRenderer.showCombatText(npcPos.x, npcPos.y - 30, label, color);
          // Combat log — debuff on NPC
          const tgtName = this.getCombatEntityName(data.targetId);
          this.pushCombatLog(`${skill.name} applied to ${tgtName}`, '#88ff44', 'buffs');
        }
      }
    };

    this.network.onBuffRemoved = (data: { targetId: string; skillId: string }) => {
      const skill = ClientDataManager.instance.getSkill(data.skillId);
      if (data.targetId === this.network.sessionId) {
        if (skill) {
          const pos = this.getCombatTextPosition(this.network.sessionId);
          if (pos) this.entityRenderer.showCombatText(pos.x, pos.y - 30, `-${skill.name}`, '#888888');
          this.pushCombatLog(`-${skill.name} faded`, '#888888', 'buffs');
        }
        this.localBuffs.delete(data.skillId);
      }
    };

    // ── Chat ──
    this.network.onChatMessage = (data: ChatMessagePayload) => {
      this.receiveChatMessage(data);
    };

    // ── Loot Bags ──
    this.network.onLootBagAdd = (bag: any, bagId: string) => {
      const bagZone = bag.zoneId ?? 'grasslands';
      this.lootBagZones.set(bagId, bagZone);
      this.lootBagCache.set(bagId, { x: bag.x, y: bag.y, zoneId: bagZone, items: bag.items });

      if (bagZone === this.currentZoneId) {
        this.entityRenderer.addLootBag(bagId, bag.x, bag.y, bagZone);
        this.visibleLootBags.add(bagId);
      }
    };

    this.network.onLootBagChange = (bag: any, bagId: string) => {
      this.lootBagCache.set(bagId, { x: bag.x, y: bag.y, zoneId: bag.zoneId, items: bag.items });

      // Live-update loot panel if this bag is currently open
      if (this.lootPanelOpen && this.currentLootBagId === bagId) {
        this.refreshLootPanelItems(bag);
      }
    };

    this.network.onLootBagRemove = (bagId: string) => {
      // Close loot panel if viewing this bag
      if (this.lootPanelOpen && this.currentLootBagId === bagId) {
        this.closeLootPanel();
      }
      if (this.visibleLootBags.has(bagId)) {
        this.entityRenderer.removeLootBag(bagId);
        this.visibleLootBags.delete(bagId);
      }
      this.lootBagZones.delete(bagId);
      this.lootBagCache.delete(bagId);
    };

    // Direct server confirmation that a loot action succeeded.
    // By the time this fires, the Colyseus state patch has already been applied,
    // so we can safely read the live schema state to force-refresh both panels.
    this.network.onLootSuccess = (bagId: string) => {
      // Refresh loot panel from live bag schema (bypasses onLootBagChange callback)
      if (this.lootPanelOpen && this.currentLootBagId === bagId) {
        const cached = this.lootBagCache.get(bagId);
        if (cached) {
          this.refreshLootPanelItems(cached);
        }
      }
      // Force-rebuild inventory from live player schema (bypasses onInventoryChange callback)
      this.network.refreshInventory();
    };

    // Level-up notification — show a prominent gold message on screen
    this.network.onLevelUp = (newLevel: number) => {
      // Large gold combat text over the player
      const pos = this.network.sessionId ? this.getCombatTextPosition(this.network.sessionId) : null;
      if (pos) {
        this.entityRenderer.showCombatText(pos.x, pos.y - 50, `LEVEL UP! (${newLevel})`, '#ffdd44');
      }
      // Also announce in chat as a system message
      this.pushSystemChat(`You have reached level ${newLevel}!`);
    };

    // ── Party system callbacks ──
    this.network.onPartyUpdate = (members: PartyMemberInfo[]) => {
      this.partyMembers = members;

      // Backfill partyMemberData immediately for any new member whose state
      // was already received via onPlayerChange before they joined the party.
      // Without this, bars stay blank until the next state-change delta arrives.
      for (const member of members) {
        if (member.sessionId === this.network.sessionId) continue;
        if (!this.partyMemberData.has(member.sessionId)) {
          const cached = this.remotePlayerCache.get(member.sessionId);
          if (cached) {
            this.partyMemberData.set(member.sessionId, {
              hp:      cached.hp      ?? 0,
              maxHp:   cached.maxHp   ?? 1,
              mana:    cached.mana    ?? 0,
              maxMana: cached.maxMana ?? 0,
              level:   cached.level   ?? 1,
              alive:   cached.alive   ?? true,
              shieldHp: cached.shieldHp ?? 0,
            });
          }
        }
      }

      // Prune stale data for members no longer in party
      const memberSet = new Set(members.map(m => m.sessionId));
      for (const key of this.partyMemberData.keys()) {
        if (!memberSet.has(key)) this.partyMemberData.delete(key);
      }
    };

    // Wire up bag click handler on entity renderer
    this.entityRenderer.onBagClick = (bagId: string, button: number) => {
      this.entityClickConsumed = true;
      if (button === 2) {
        // Right-click → open loot panel
        this.openLootPanel(bagId);
      }
    };
  }

  /**
   * Get the world position for a given player (local or remote).
   */
  private getCombatTextPosition(entityId: string): { x: number; y: number } | null {
    if (entityId === this.network.sessionId) {
      if (this.isIso) {
        return orthoToIso(this.renderX, this.renderY);
      }
      return { x: this.renderX, y: this.renderY };
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
    this.playerSprite = createCharacterSprite(this, playerIso.x, playerIso.y, this.localClassId, this.localBodyId);
    this.playerSprite.setDepth(ENTITY_DEPTH_BASE);
    this.playerSprite.rotation = 0;

    // Allow clicking the local player sprite to self-target
    this.playerSprite.setInteractive({ useHandCursor: false });
    this.playerSprite.on('pointerdown', () => {
      this.entityClickConsumed = true;
      this.inputManager.suppressNextClick = true;
      const selfId = this.network.sessionId;
      if (!selfId) return;
      if (this.currentTargetId === selfId && this.currentTargetType === 'self') {
        this.clearTarget(); // click self again = deselect
      } else {
        this.setTarget(selfId, 'self');
      }
    });

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

    // WASD is rotated 45 degrees into the world before it moves the player
    // (see applyInputLocally / MovementSystem.processInput). Facing has to use
    // the same rotated vector, otherwise the local player faces one way on
    // their own screen and another on everyone else's.
    const orthoMx = mx + my;
    const orthoMy = -mx + my;
    return dirFromOrthoVector(orthoMx, orthoMy);
  }

  // ── Action Animation Helpers ───────────────────────────────

  /**
   * Play an action animation (melee, ranged, cast) on the local player sprite.
   * Melee/ranged play once then return to idle; cast loops until stopped.
   */
  private playActionAnimation(animType: string, direction?: string): void {
    if (!this.playerSprite) return;
    const dir = (direction ?? this.lastFacingDir) as PaperdollDir;
    const anim = toPaperdollAnim(animType);
    const animKey = characterAnimKey(this, this.playerSprite, this.localClassId, anim, dir);
    if (!animKey) return;

    this.isPlayingActionAnim = true;
    this.playerSprite.play(animKey, true);

    if (anim === 'attack' || anim === 'shoot') {
      // One-shot animation — return to idle when complete
      this.playerSprite.once('animationcomplete', () => {
        this.isPlayingActionAnim = false;
        if (this.playerSprite) {
          playCharacterAnim(this, this.playerSprite, this.localClassId, 'idle',
                            this.lastFacingDir as PaperdollDir, true);
        }
      });
    }
    // 'cast' loops indefinitely — stopped by stopActionAnimation()
  }

  /** Stop any looping action animation (e.g. cast) and return to idle. */
  private stopActionAnimation(): void {
    this.isPlayingActionAnim = false;
    if (this.playerSprite) {
      playCharacterAnim(this, this.playerSprite, this.localClassId, 'idle',
                        this.lastFacingDir as PaperdollDir, true);
    }
  }

  /**
   * Get the facing direction from an attacker toward a target screen position.
   * Returns 'up', 'down', 'left', or 'right'.
   */
  private getDirectionToTarget(attackerId: string, targetScreenX: number, targetScreenY: number): string {
    let attackerX: number;
    let attackerY: number;

    if (attackerId === this.network.sessionId) {
      // Local player
      const playerIso = this.isIso
        ? orthoToIso(this.renderX, this.renderY)
        : { x: this.renderX, y: this.renderY };
      attackerX = playerIso.x;
      attackerY = playerIso.y;
    } else {
      // Remote player
      const pos = this.entityRenderer.getPlayerPosition(attackerId);
      if (!pos) return this.lastFacingDir;
      attackerX = pos.x;
      attackerY = pos.y;
    }

    // Already in ISO screen space, so this is a screen-vector lookup
    return dirFromScreenVector(targetScreenX - attackerX, targetScreenY - attackerY)
      ?? this.lastFacingDir;
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
    // Town / farm / cave tileset (GID 21-40)
    21: 'tile_road_cobble',
    22: 'tile_plaster_wall',
    23: 'tile_timber_wall',
    24: 'tile_roof_thatch',
    25: 'tile_roof_tile',
    26: 'tile_door_wood',
    27: 'tile_window_lit',
    28: 'tile_wood_floor',
    29: 'tile_signpost',
    30: 'tile_well',
    31: 'tile_market_stall',
    32: 'tile_crop_field',
    33: 'tile_bridge_wood',
    34: 'tile_cave_mouth',
    35: 'tile_cave_floor',
    36: 'tile_cave_wall',
    37: 'tile_rock',
    38: 'tile_flowers',
    39: 'tile_hedge',
    40: 'tile_campfire',
  };

  /**
   * Build multi-layer tile map from server MapDataPayload.
   * Renders each tile layer in order, with proper depth sorting.
   */
  /** Depth stride between tile layers for painter's-algorithm sorting. */
  private static readonly LAYER_DEPTH_STRIDE = 100_000;

  /** Tile-grid squares per cull bucket. */
  private static readonly CULL_CHUNK = 8;

  private buildTileMapFromData(data: MapDataPayload): void {
    // Clear existing tiles
    for (const sprite of this.tileSprites) {
      sprite.destroy();
    }
    this.tileSprites = [];
    this.tileChunks = [];
    const chunks = new Map<string, Phaser.GameObjects.Sprite[]>();

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

          const key = `${(x / GameScene.CULL_CHUNK) | 0},${(y / GameScene.CULL_CHUNK) | 0}`;
          const bucket = chunks.get(key);
          if (bucket) bucket.push(sprite);
          else chunks.set(key, [sprite]);
        }
      }
      layerIndex++;
    }

    // Measure each bucket from the sprites it actually holds rather than from
    // the tile maths: a tile texture may be taller than its cell (a tree, a
    // roof), and a bucket sized from the grid would pop those off early.
    for (const sprites of chunks.values()) {
      let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity;
      for (const sp of sprites) {
        const b = sp.getBounds();
        if (b.left < left) left = b.left;
        if (b.top < top) top = b.top;
        if (b.right > right) right = b.right;
        if (b.bottom > bottom) bottom = b.bottom;
      }
      this.tileChunks.push({ left, top, right, bottom, sprites, shown: true });
    }
    this.cullTiles(true);
  }

  /**
   * Show only the buckets the camera can see. Called every frame; it early-outs
   * on the common case where nothing changed, so the cost is one rectangle test
   * per bucket.
   */
  private cullTiles(force = false): void {
    if (this.tileChunks.length === 0) return;
    const view = this.cameras.main.worldView;
    // A margin of one bucket, so a chunk is already on when it slides in.
    const margin = GameScene.CULL_CHUNK * 128;
    const left = view.x - margin;
    const right = view.right + margin;
    const top = view.y - margin;
    const bottom = view.bottom + margin;

    for (const chunk of this.tileChunks) {
      const visible =
        chunk.right >= left && chunk.left <= right &&
        chunk.bottom >= top && chunk.top <= bottom;
      if (!force && visible === chunk.shown) continue;
      chunk.shown = visible;
      for (const sp of chunk.sprites) sp.setVisible(visible);
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
    this.tileChunks = [];

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

    // Shield overlay — cyan tint on HP bar when Shield of Faith is active
    if (this.localShieldHp > 0 && this.localMaxHp > 0) {
      const shieldRatio = Math.min(1, this.localShieldHp / this.localMaxHp);
      this.hpBarGfx.fillStyle(0x00ccff, 0.45);
      this.hpBarGfx.fillRect(barX, hpBarY, barWidth * shieldRatio, barHeight);
    }

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
    const shieldStr = this.localShieldHp > 0 ? `  Shield: ${this.localShieldHp}` : '';
    this.classHudText.setText(`${displayName} Lv.${this.localLevel}  HP: ${this.localHp}/${this.localMaxHp}${resourceStr}${shieldStr}`);

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

  private readonly PANEL_H = 386;   // 9 equipment rows + stats

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
      if (saved.party)     this.partyOffset     = saved.party;
      if (saved.buffs)     this.buffsOffset     = saved.buffs;
      if (saved.combatLog) this.combatLogOffset = saved.combatLog;
      if (saved.combatLogFilters) Object.assign(this.combatLogFilters, saved.combatLogFilters);
      if (saved.panelVisibility) Object.assign(this.panelVisibility, saved.panelVisibility);
      if (saved.panelOpacity)    Object.assign(this.panelOpacity, saved.panelOpacity);
      if (saved.panelScale)      Object.assign(this.panelScale, saved.panelScale);
      this.applyPanelCustomisation();
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
        party:     this.partyOffset,
        buffs:     this.buffsOffset,
        combatLog: this.combatLogOffset,
        combatLogFilters: this.combatLogFilters,
        panelVisibility: this.panelVisibility,
        panelOpacity:    this.panelOpacity,
        panelScale:      this.panelScale,
      }));
    } catch { /* ignore */ }
  }

  // ══════════════════════════════════════════════════════════
  // ██  OPTIONS MENU
  // ══════════════════════════════════════════════════════════

  // Options menu layout constants (shared between create and hit-testing)
  private readonly OPTS_PANEL_W = 320;
  private readonly OPTS_PANEL_H = 300;
  private readonly OPTS_BTN_W = 240;
  private readonly OPTS_BTN_H = 34;
  private readonly OPTS_BTN_GAP = 8;
  private readonly OPTS_TITLE_H = 22;
  private readonly OPTS_BUTTONS: { label: string; action: string; enabled: boolean }[] = [
    { label: 'Edit HUD',                   action: 'editHud',    enabled: true },
    { label: 'Reset HUD Layout',           action: 'resetHud',   enabled: true },
    { label: 'Toggle Sound (Coming Soon)', action: 'sound',      enabled: false },
    { label: 'Logout',                     action: 'logout',     enabled: true },
  ];

  /** Return the screen rect for the options panel. */
  private getOptionsPanelRect(): { x: number; y: number; w: number; h: number } {
    const cam = this.cameras.main;
    return {
      x: cam.width / 2 - this.OPTS_PANEL_W / 2,
      y: cam.height / 2 - this.OPTS_PANEL_H / 2,
      w: this.OPTS_PANEL_W,
      h: this.OPTS_PANEL_H,
    };
  }

  /** Return the screen rect for a specific options button by index. */
  private getOptionsButtonRect(index: number): { x: number; y: number; w: number; h: number } {
    const pr = this.getOptionsPanelRect();
    const startY = pr.y + this.OPTS_TITLE_H + 24;
    const by = startY + index * (this.OPTS_BTN_H + this.OPTS_BTN_GAP);
    return {
      x: pr.x + this.OPTS_PANEL_W / 2 - this.OPTS_BTN_W / 2,
      y: by,
      w: this.OPTS_BTN_W,
      h: this.OPTS_BTN_H,
    };
  }

  /** Return the screen rect for the close (✕) button. */
  private getOptionsCloseRect(): { x: number; y: number; w: number; h: number } {
    const pr = this.getOptionsPanelRect();
    return { x: pr.x + this.OPTS_PANEL_W - 28, y: pr.y + 2, w: 24, h: 24 };
  }

  private createOptionsMenu(): void {
    const cam = this.cameras.main;

    // Dim overlay — visual only, not interactive.
    // Click-outside and button clicks are handled via manual hit-rect testing
    // in the global pointerdown/pointermove handlers (same pattern as all other panels).
    this.optionsDimBg = this.add.rectangle(cam.width / 2, cam.height / 2, cam.width, cam.height, 0x000000, 0.5);
    this.optionsDimBg.setScrollFactor(0);
    this.optionsDimBg.setDepth(UI_DEPTH_BASE + 299);
    this.optionsDimBg.setVisible(false);

    // Container — holds all visual elements, toggled as a group
    this.optionsMenuContainer = this.add.container(0, 0);
    this.optionsMenuContainer.setScrollFactor(0);
    this.optionsMenuContainer.setDepth(UI_DEPTH_BASE + 300);
    this.optionsMenuContainer.setVisible(false);

    this.rebuildOptionsMenuVisuals();
  }

  /**
   * (Re)build the visual elements inside the options menu container.
   * Called once at creation and can be called again on resize.
   */
  private rebuildOptionsMenuVisuals(): void {
    // Clear previous contents
    this.optionsMenuContainer.removeAll(true);
    this.optionsButtons = [];

    const pr = this.getOptionsPanelRect();

    // Background
    const bg = this.add.rectangle(
      pr.x + this.OPTS_PANEL_W / 2, pr.y + this.OPTS_PANEL_H / 2,
      this.OPTS_PANEL_W, this.OPTS_PANEL_H, 0x1a1a2e, 0.95,
    );
    bg.setStrokeStyle(2, 0x555588);
    this.optionsMenuContainer.add(bg);

    // Title
    const title = this.add.text(pr.x + this.OPTS_PANEL_W / 2, pr.y + this.OPTS_TITLE_H / 2 + 4, 'Options', {
      fontSize: '15px', color: '#ffcc00', stroke: '#000000', strokeThickness: 2, fontStyle: 'bold',
    }).setOrigin(0.5, 0.5);
    this.optionsMenuContainer.add(title);

    // Separator
    const sep = this.add.rectangle(
      pr.x + this.OPTS_PANEL_W / 2, pr.y + this.OPTS_TITLE_H + 6,
      this.OPTS_PANEL_W - 20, 1, 0x555588, 0.6,
    );
    this.optionsMenuContainer.add(sep);

    // Buttons (visual only — interaction handled via global pointer handlers)
    for (let i = 0; i < this.OPTS_BUTTONS.length; i++) {
      const btn = this.OPTS_BUTTONS[i];
      const br = this.getOptionsButtonRect(i);
      const bx = br.x + br.w / 2;
      const by = br.y + br.h / 2;

      const btnBg = this.add.rectangle(bx, by, this.OPTS_BTN_W, this.OPTS_BTN_H, btn.enabled ? 0x2a2a3e : 0x1e1e2e, 1);
      btnBg.setStrokeStyle(1, 0x444466);
      this.optionsMenuContainer.add(btnBg);

      const textColor = btn.enabled ? '#cccccc' : '#555555';
      const btnText = this.add.text(bx, by, btn.label, {
        fontSize: '13px', color: textColor, stroke: '#000000', strokeThickness: 1,
      }).setOrigin(0.5, 0.5);
      this.optionsMenuContainer.add(btnText);

      this.optionsButtons.push({ bg: btnBg, text: btnText, action: btn.action });
    }

    // Close button (✕)
    const cr = this.getOptionsCloseRect();
    const closeBtn = this.add.text(cr.x + cr.w / 2, cr.y + cr.h / 2, '✕', {
      fontSize: '14px', color: '#888888', stroke: '#000000', strokeThickness: 1,
    }).setOrigin(0.5, 0.5);
    this.optionsMenuContainer.add(closeBtn);
    // Store ref for hover styling
    this.optionsCloseText = closeBtn;

    // Footer
    const footer = this.add.text(pr.x + this.OPTS_PANEL_W / 2, pr.y + this.OPTS_PANEL_H - 16, '1382 Labs — Valhalla', {
      fontSize: '10px', color: '#444466',
    }).setOrigin(0.5, 0.5);
    this.optionsMenuContainer.add(footer);
  }

  private toggleOptionsMenu(): void {
    this.optionsMenuOpen = !this.optionsMenuOpen;
    this.optionsDimBg.setVisible(this.optionsMenuOpen);
    this.optionsMenuContainer.setVisible(this.optionsMenuOpen);
  }

  private handleOptionsAction(action: string): void {
    switch (action) {
      case 'editHud':
        this.toggleOptionsMenu();
        this.enterHudEditMode();
        break;
      case 'resetHud':
        this.resetHudLayout();
        break;
      case 'logout':
        this.logoutToLoginScreen();
        break;
      case 'sound':
        // Placeholder for future audio system
        break;
    }
  }

  private returnToCharacterSelect(): void {
    this.saveHudLayout();
    // Await the full WebSocket leave so the server processes the disconnect
    // before we attempt a new joinOrCreate from the character select screen.
    this.network.disconnect()
      .then(() => AuthClient.getCharactersWithUsername())
      .then(({ characters, username }) => {
        this.scene.start('CharacterSelectScene', {
          characters,
          username,
          token: this.authToken,
        });
      })
      .catch(() => {
        // If the fetch fails (e.g. network error), fall back to LoginScene
        AuthClient.clearToken();
        this.scene.start('LoginScene');
      });
  }

  private logoutToLoginScreen(): void {
    this.saveHudLayout();
    // Release all Phaser keyboard captures before leaving — addKey() registers
    // WASD in the global KeyboardManager capture list (which calls preventDefault),
    // so without this, WASD won't type in HTML <input> fields after the scene ends.
    this.input.keyboard?.clearCaptures();
    this.network.disconnect().then(() => {
      AuthClient.clearToken();
      this.scene.start('LoginScene');
    }).catch(() => {
      AuthClient.clearToken();
      this.scene.start('LoginScene');
    });
  }

  private resetHudLayout(): void {
    this.chatOffset      = { x: 0, y: 0 };
    this.actionBarOffset = { x: 0, y: 0 };
    this.invOffset       = { x: 0, y: 0 };
    this.skillsOffset    = { x: 0, y: 0 };
    this.targetOffset    = { x: 0, y: 0 };
    this.partyOffset     = { x: 0, y: 0 };
    this.buffsOffset     = { x: 0, y: 0 };
    this.combatLogOffset = { x: 0, y: 0 };
    this.panelVisibility = { chat: true, actionBar: true, target: true, party: true, buffs: true };
    this.panelOpacity    = { chat: 1.0, actionBar: 1.0, target: 1.0, party: 1.0, buffs: 1.0 };
    this.panelScale      = { chat: 1.0, actionBar: 1.0, target: 1.0, party: 1.0, buffs: 1.0 };
    try { localStorage.removeItem(`valhalla_hud_${this.characterId}`); } catch { /* ignore */ }
    this.applyPanelCustomisation();
    this.saveHudLayout();
  }

  // ══════════════════════════════════════════════════════════
  // ██  HUD EDIT MODE
  // ══════════════════════════════════════════════════════════

  private createHudEditModeUI(): void {
    const cam = this.cameras.main;
    this.hudEditLabel = this.add.text(cam.width / 2, 20, 'HUD EDIT MODE', {
      fontSize: '18px', color: '#ffcc00', stroke: '#000000', strokeThickness: 3, fontStyle: 'bold',
    }).setOrigin(0.5, 0).setScrollFactor(0).setDepth(UI_DEPTH_BASE + 310).setVisible(false);

    this.hudEditSubLabel = this.add.text(cam.width / 2, 42, 'Drag panels to reposition  •  Click controls to customise  •  Press Esc to exit', {
      fontSize: '11px', color: '#aaaacc', stroke: '#000000', strokeThickness: 1,
    }).setOrigin(0.5, 0).setScrollFactor(0).setDepth(UI_DEPTH_BASE + 310).setVisible(false);
  }

  /** Map panel keys to their Phaser containers. */
  private getPanelContainerMap(): Record<string, Phaser.GameObjects.Container> {
    return {
      chat:      this.chatContainer,
      actionBar: this.actionBarContainer,
      target:    this.targetNameplateContainer,
      party:     this.partyPanelContainer,
      buffs:     this.buffsPanelContainer,
    };
  }

  private enterHudEditMode(): void {
    this.hudEditMode = true;
    this.hudEditLabel.setVisible(true);
    this.hudEditSubLabel.setVisible(true);

    // Force show all customisable panels so the user can drag them
    const containers = this.getPanelContainerMap();
    for (const key of Object.keys(containers)) {
      containers[key].setVisible(true);
    }

    // Build per-panel control strips
    this.buildHudEditControls();
  }

  private exitHudEditMode(): void {
    this.hudEditMode = false;
    this.hudEditLabel.setVisible(false);
    this.hudEditSubLabel.setVisible(false);

    // Destroy edit mode controls
    for (const obj of this.hudEditPanelControls) {
      obj.destroy();
    }
    this.hudEditPanelControls = [];

    // Apply visibility/opacity/scale settings and hide panels that should be hidden
    this.applyPanelCustomisation();
    this.saveHudLayout();
  }

  /**
   * Apply the stored panelVisibility, panelOpacity and panelScale to the actual containers.
   * Called when loading layout, exiting edit mode, or resetting layout.
   */
  private applyPanelCustomisation(): void {
    const containers = this.getPanelContainerMap();
    for (const key of Object.keys(containers)) {
      const c = containers[key];
      const scale = this.panelScale[key] ?? 1.0;
      c.setScale(scale);
      c.setAlpha(this.panelOpacity[key] ?? 1.0);
      // Don't force-hide panels that have contextual visibility (target, party).
      // Those are managed by the game logic. We just store the user preference.
    }
  }

  /**
   * Build floating control strips for each panel during HUD edit mode.
   * Each panel gets: [Eye] toggle visibility, [O] cycle opacity, [S] cycle scale
   */
  private buildHudEditControls(): void {
    // Clean up any previous controls
    for (const obj of this.hudEditPanelControls) { obj.destroy(); }
    this.hudEditPanelControls = [];

    const containers = this.getPanelContainerMap();
    const cam = this.cameras.main;
    const CTRL_H = 22;
    const CTRL_W = 120;

    // Panel-specific screen rect functions (approximate top-left of each panel)
    const panelRects: Record<string, () => { x: number; y: number }> = {
      chat: () => ({
        x: this.CHAT_LEFT_X + this.chatOffset.x,
        y: cam.height - this.CHAT_H - this.CHAT_BOTTOM_MARGIN + this.chatOffset.y,
      }),
      actionBar: () => ({
        x: this.actionBarScreenRect.x + this.actionBarOffset.x,
        y: this.actionBarScreenRect.y + this.actionBarOffset.y,
      }),
      target: () => ({
        x: 16 + this.targetOffset.x,
        y: 60 + this.targetOffset.y,
      }),
      party: () => ({
        x: (this.partyPanelTitleHandle?.x ?? 0) + this.partyOffset.x,
        y: (this.partyPanelTitleHandle?.y ?? 80) + this.partyOffset.y,
      }),
      buffs: () => ({
        x: (this.buffsPanelTitleHandle?.x ?? cam.width - 180) + this.buffsOffset.x,
        y: (this.buffsPanelTitleHandle?.y ?? 60) + this.buffsOffset.y,
      }),
    };

    for (const key of Object.keys(containers)) {
      const rect = panelRects[key]?.();
      if (!rect) continue;
      const ctrlX = rect.x;
      const ctrlY = rect.y - CTRL_H - 4; // above the panel

      // Background strip
      const stripBg = this.add.rectangle(ctrlX + CTRL_W / 2, ctrlY + CTRL_H / 2, CTRL_W, CTRL_H, 0x1a1a2e, 0.9)
        .setStrokeStyle(1, 0x555588).setScrollFactor(0).setDepth(UI_DEPTH_BASE + 311);
      this.hudEditPanelControls.push(stripBg);

      // [Eye] Visibility toggle
      const vis = this.panelVisibility[key] ?? true;
      const eyeBtn = this.add.text(ctrlX + 8, ctrlY + 4, vis ? '👁' : '👁‍🗨', {
        fontSize: '13px', color: vis ? '#44ff44' : '#ff4444',
      }).setScrollFactor(0).setDepth(UI_DEPTH_BASE + 312).setInteractive({ useHandCursor: true });
      eyeBtn.on('pointerdown', () => {
        this.panelVisibility[key] = !this.panelVisibility[key];
        eyeBtn.setText(this.panelVisibility[key] ? '👁' : '👁‍🗨');
        eyeBtn.setColor(this.panelVisibility[key] ? '#44ff44' : '#ff4444');
        // In edit mode, keep showing the panel even if toggled off (greyed out)
        containers[key].setAlpha(this.panelVisibility[key] ? (this.panelOpacity[key] ?? 1) : 0.3);
      });
      this.hudEditPanelControls.push(eyeBtn);

      // [O] Opacity cycle: 50% → 75% → 100%
      const opacitySteps = [0.5, 0.75, 1.0];
      const opLabel = this.add.text(ctrlX + 34, ctrlY + 4, `O:${Math.round((this.panelOpacity[key] ?? 1) * 100)}%`, {
        fontSize: '11px', color: '#aaaacc',
      }).setScrollFactor(0).setDepth(UI_DEPTH_BASE + 312).setInteractive({ useHandCursor: true });
      opLabel.on('pointerdown', () => {
        const curIdx = opacitySteps.indexOf(this.panelOpacity[key] ?? 1.0);
        const nextIdx = (curIdx + 1) % opacitySteps.length;
        this.panelOpacity[key] = opacitySteps[nextIdx];
        opLabel.setText(`O:${Math.round(opacitySteps[nextIdx] * 100)}%`);
        if (this.panelVisibility[key]) {
          containers[key].setAlpha(opacitySteps[nextIdx]);
        }
      });
      this.hudEditPanelControls.push(opLabel);

      // [S] Scale cycle: 0.8x → 1.0x → 1.2x
      const scaleSteps = [0.8, 1.0, 1.2];
      const scLabel = this.add.text(ctrlX + 82, ctrlY + 4, `S:${this.panelScale[key] ?? 1.0}x`, {
        fontSize: '11px', color: '#aaaacc',
      }).setScrollFactor(0).setDepth(UI_DEPTH_BASE + 312).setInteractive({ useHandCursor: true });
      scLabel.on('pointerdown', () => {
        const curIdx = scaleSteps.indexOf(this.panelScale[key] ?? 1.0);
        const nextIdx = (curIdx + 1) % scaleSteps.length;
        this.panelScale[key] = scaleSteps[nextIdx];
        scLabel.setText(`S:${scaleSteps[nextIdx]}x`);
        containers[key].setScale(scaleSteps[nextIdx]);
      });
      this.hudEditPanelControls.push(scLabel);
    }
  }

  /**
   * Phaser lifecycle — called when the scene is stopped or replaced (e.g. logging out,
   * returning to character select). Saves HUD layout as a safety net so positions are
   * never lost even if the player leaves without completing a drag gesture.
   */
  shutdown(): void {
    this.saveHudLayout();
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
      const px = this.CHAT_LEFT_X + this.chatOffset.x;
      const py = cam.height - this.CHAT_H - this.CHAT_BOTTOM_MARGIN + this.chatOffset.y;
      return { x: px, y: py, w: this.chatEffectiveW, h: TITLE_H };
    };

    const getActionBarHandleRect = () => {
      const r = this.actionBarScreenRect;
      return { x: r.x, y: r.y, w: r.w, h: r.h }; // full bar is the handle
    };

    const getCombatLogHandleRect = () => {
      const r = this.getCombatLogPanelRect();
      return { x: r.x, y: r.y, w: r.w, h: this.CL_TITLE_H };
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

      // ── Options menu interaction (manual hit-rect, same pattern as all panels) ──
      if (this.optionsMenuOpen) {
        // Close button (✕)
        if (hitRect(this.getOptionsCloseRect(), px, py)) {
          this.toggleOptionsMenu();
          return;
        }
        // Button clicks
        for (let i = 0; i < this.OPTS_BUTTONS.length; i++) {
          if (!this.OPTS_BUTTONS[i].enabled) continue;
          if (hitRect(this.getOptionsButtonRect(i), px, py)) {
            this.handleOptionsAction(this.OPTS_BUTTONS[i].action);
            return;
          }
        }
        // Click outside panel → close
        if (!hitRect(this.getOptionsPanelRect(), px, py)) {
          this.toggleOptionsMenu();
        }
        return; // don't start panel drags while options menu is open
      }

      // Chat drag handle
      if (hitRect(getChatHandleRect(), px, py)) {
        this.hudDragTarget      = 'chat';
        this.hudDragStartMouse  = { x: px, y: py };
        this.hudDragStartOffset = { ...this.chatOffset };
        return;
      }
      // Action bar drag handle
      if (hitRect(getActionBarHandleRect(), px, py)) {
        const slotHit = this.getActionBarSlotAt(px, py);
        if (slotHit === -1) {
          // Padding area → reposition the whole bar
          this.hudDragTarget      = 'actionBar';
          this.hudDragStartMouse  = { x: px, y: py };
          this.hudDragStartOffset = { ...this.actionBarOffset };
          return;
        } else {
          // Slot area → start skill drag if the slot is occupied
          const skillId = this.actionBar[slotHit];
          if (skillId) {
            const skill = ClientDataManager.instance.getSkill(skillId);
            this.abDragging = true;
            this.abDragSourceSlot = slotHit;
            this.abDragGhost.setText(skill?.iconAbbrev ?? skillId.slice(0, 4));
            this.abDragGhost.setPosition(px + 16, py);
            this.abDragGhostBg.setPosition(px + 16, py);
            this.abDragGhost.setVisible(true);
            this.abDragGhostBg.setVisible(true);
            return;
          }
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
      // Party panel drag handle (only when party is visible)
      if (this.partyMembers.length > 0 && hitRect(this.partyPanelTitleHandle, px, py)) {
        this.hudDragTarget      = 'party';
        this.hudDragStartMouse  = { x: px, y: py };
        this.hudDragStartOffset = { ...this.partyOffset };
        return;
      }
      // Buffs panel drag handle (only when at least one buff is active)
      if (this.localBuffs.size > 0 && hitRect(this.buffsPanelTitleHandle, px, py)) {
        this.hudDragTarget      = 'buffs';
        this.hudDragStartMouse  = { x: px, y: py };
        this.hudDragStartOffset = { ...this.buffsOffset };
        return;
      }
      // Combat log: close context menu on left click, or start drag
      if (this.combatLogContextMenuOpen) {
        this.handleCombatLogContextClick(px, py);
        return;
      }
      if (hitRect(getCombatLogHandleRect(), px, py)) {
        this.hudDragTarget      = 'combatLog';
        this.hudDragStartMouse  = { x: px, y: py };
        this.hudDragStartOffset = { ...this.combatLogOffset };
        return;
      }
    });

    // ── Combat log: right-click opens context menu ──
    this.input.on('pointerdown', (pointer: Phaser.Input.Pointer) => {
      if (!pointer.rightButtonDown()) return;
      const px = pointer.x;
      const py = pointer.y;
      const r = this.getCombatLogPanelRect();
      if (px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h) {
        this.openCombatLogContextMenu(px, py);
      } else if (this.combatLogContextMenuOpen) {
        this.closeCombatLogContextMenu();
      }
    });

    // ── Chat + Combat log: mouse wheel scroll ──
    this.input.on('wheel', (_pointer: Phaser.Input.Pointer, _gos: any[], _dx: number, dy: number) => {
      const px = _pointer.x;
      const py = _pointer.y;

      // Chat scroll
      const cr = this.getChatPanelRect();
      if (px >= cr.x && px <= cr.x + cr.w && py >= cr.y && py <= cr.y + cr.h) {
        const maxScroll = Math.max(0, this.chatMessages.length - this.CHAT_VISIBLE_LINES);
        if (dy < 0) {
          this.chatScrollOffset = Math.min(maxScroll, this.chatScrollOffset + 3);
        } else {
          this.chatScrollOffset = Math.max(0, this.chatScrollOffset - 3);
        }
        return;
      }

      // Combat log scroll
      const r = this.getCombatLogPanelRect();
      if (px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h) {
        const filtered = this.getFilteredCombatLogMessages();
        const maxScroll = Math.max(0, filtered.length - this.CL_VISIBLE_LINES);
        if (dy < 0) {
          // Scroll up (show older messages)
          this.combatLogScrollOffset = Math.min(maxScroll, this.combatLogScrollOffset + 3);
        } else {
          // Scroll down (show newer messages)
          this.combatLogScrollOffset = Math.max(0, this.combatLogScrollOffset - 3);
        }
      }
    });

    this.input.on('pointermove', (pointer: Phaser.Input.Pointer) => {
      // ── Options menu hover effects ──
      if (this.optionsMenuOpen) {
        const mx = pointer.x;
        const my = pointer.y;
        let newHover = -1;
        for (let i = 0; i < this.OPTS_BUTTONS.length; i++) {
          if (!this.OPTS_BUTTONS[i].enabled) continue;
          const br = this.getOptionsButtonRect(i);
          if (mx >= br.x && mx <= br.x + br.w && my >= br.y && my <= br.y + br.h) {
            newHover = i;
            break;
          }
        }
        // Check close button
        const cr = this.getOptionsCloseRect();
        const overClose = mx >= cr.x && mx <= cr.x + cr.w && my >= cr.y && my <= cr.y + cr.h;

        if (newHover !== this.optionsHoveredIdx) {
          // Un-hover previous
          if (this.optionsHoveredIdx >= 0 && this.optionsHoveredIdx < this.optionsButtons.length) {
            this.optionsButtons[this.optionsHoveredIdx].bg.setFillStyle(0x2a2a3e);
            this.optionsButtons[this.optionsHoveredIdx].text.setColor('#cccccc');
          }
          // Hover new
          if (newHover >= 0) {
            this.optionsButtons[newHover].bg.setFillStyle(0x3a3a4e);
            this.optionsButtons[newHover].text.setColor('#ffcc00');
          }
          this.optionsHoveredIdx = newHover;
        }
        // Close btn hover
        if (this.optionsCloseText) {
          this.optionsCloseText.setColor(overClose ? '#ff6666' : '#888888');
        }
        return; // don't process drag while options menu is open
      }

      // Action-bar slot drag ghost
      if (this.abDragging) {
        this.abDragGhost.setPosition(pointer.x + 16, pointer.y);
        this.abDragGhostBg.setPosition(pointer.x + 16, pointer.y);
        return;
      }

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
        case 'party':
          this.partyOffset = newOffset;
          break;
        case 'buffs':
          this.buffsOffset = newOffset;
          break;
        case 'combatLog':
          this.combatLogOffset = newOffset;
          break;
      }
    });

    this.input.on('pointerup', (pointer: Phaser.Input.Pointer) => {
      // ── Action-bar skill drag drop ──────────────────────────
      if (this.abDragging) {
        const src = this.abDragSourceSlot;
        const dest = this.getActionBarSlotAt(pointer.x, pointer.y);
        if (dest >= 0 && dest !== src) {
          // Dropped on another slot → swap
          const tmp = this.actionBar[dest];
          this.actionBar[dest] = this.actionBar[src];
          this.actionBar[src] = tmp;
          this.network.sendSetActionBar(this.actionBar);
        } else if (dest === -1) {
          // Dropped outside the action bar → remove from source slot
          this.actionBar[src] = '';
          this.network.sendSetActionBar(this.actionBar);
        }
        // dest === src → no-op (dropped back on same slot)
        this.abDragging = false;
        this.abDragSourceSlot = -1;
        this.abDragGhost.setVisible(false);
        this.abDragGhostBg.setVisible(false);
        return;
      }

      // ── HUD panel repositioning drag end ────────────────────
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
    const TH = 22;   // title strip height (fixed)

    const defaultX = cam.width - 222;
    const defaultY = 100;

    this.targetNameplateContainer = this.add.container(
      defaultX + this.targetOffset.x,
      defaultY + this.targetOffset.y,
    );
    this.targetNameplateContainer.setScrollFactor(0);
    this.targetNameplateContainer.setDepth(UI_DEPTH_BASE + 70);
    this.targetNameplateContainer.setVisible(false);

    // ── Background panel (redrawn with dynamic width each frame) ──
    this.targetNameplateBg = this.add.graphics();

    // ── "Target" label in title strip ──
    this.targetNameplateTitleText = this.add.text(0, TH / 2, 'Target', {
      fontSize: '11px',
      color: '#8888bb',
      fontStyle: 'bold',
    });
    this.targetNameplateTitleText.setOrigin(0.5, 0.5);

    // ── Target name ──
    this.targetNameplateNameText = this.add.text(8, TH + 5, '', {
      fontSize: '13px',
      color: '#ffffff',
      fontStyle: 'bold',
    });
    this.targetNameplateNameText.setOrigin(0, 0);

    // ── Level label ──
    this.targetNameplateLevelText = this.add.text(0, TH + 5, '', {
      fontSize: '11px',
      color: '#aaaaaa',
    });
    this.targetNameplateLevelText.setOrigin(1, 0);

    // ── HP bar (redrawn each frame) ──
    this.targetNameplateHpBar = this.add.graphics();

    // ── HP value text ── (y = barTop + barHeight + gap = (TH+24) + 10 + 4 = TH+38)
    this.targetNameplateHpText = this.add.text(0, TH + 38, '', {
      fontSize: '10px',
      color: '#aaaaaa',
    });
    this.targetNameplateHpText.setOrigin(0.5, 0);

    // ── Buff pills (drawn each frame for NPC targets) ──
    this.targetNameplateBuffsGfx = this.add.graphics();

    // Single text element that renders buff pill labels (e.g. "☠ Poison 4s")
    this.targetNameplateBuffsText = this.add.text(0, 0, '', {
      fontSize: '10px',
      color: '#88ff88',
      fontStyle: 'bold',
    });
    this.targetNameplateBuffsText.setOrigin(0, 0.5);
    this.targetNameplateBuffsText.setVisible(false);

    this.targetNameplateContainer.add([
      this.targetNameplateBg,
      this.targetNameplateTitleText,
      this.targetNameplateNameText,
      this.targetNameplateLevelText,
      this.targetNameplateHpBar,
      this.targetNameplateHpText,
      this.targetNameplateBuffsGfx,
      this.targetNameplateBuffsText,
    ]);

    // Cache the title-strip screen rect for drag hit-testing (updated in updateTargetNameplate)
    this.targetNameplateTitleHandle = { x: defaultX, y: defaultY, w: 210, h: TH };
  }

  // ── Party Panel ─────────────────────────────────────────────

  private createPartyPanel(): void {
    const PW = 160;  // panel width
    const TH = 20;   // title strip height

    const defaultX = 12;
    const defaultY = 200;

    this.partyPanelContainer = this.add.container(
      defaultX + this.partyOffset.x,
      defaultY + this.partyOffset.y,
    );
    this.partyPanelContainer.setScrollFactor(0);
    this.partyPanelContainer.setDepth(UI_DEPTH_BASE + 75);
    this.partyPanelContainer.setVisible(false);

    // Background (redrawn dynamically based on member count)
    this.partyPanelBg = this.add.graphics();
    this.partyPanelContainer.add(this.partyPanelBg);

    // Title text
    const titleText = this.add.text(PW / 2, TH / 2, 'Party', {
      fontSize: '11px',
      color: '#8888bb',
      fontStyle: 'bold',
    });
    titleText.setOrigin(0.5, 0.5);
    this.partyPanelContainer.add(titleText);

    // Pre-create slots for up to 4 members (name text + HP bar + mana bar each)
    for (let i = 0; i < 4; i++) {
      const nameText = this.add.text(8, TH + 4 + i * 38, '', {
        fontSize: '11px',
        color: '#ffffff',
        fontStyle: 'bold',
      });
      nameText.setOrigin(0, 0);
      // Make the full row clickable for targeting
      nameText.setInteractive({
        hitArea: new Phaser.Geom.Rectangle(-8, -2, PW, 34),
        hitAreaCallback: Phaser.Geom.Rectangle.Contains,
        useHandCursor: true,
      });
      const slotIndex = i;
      nameText.on('pointerdown', () => {
        const sid = this.partySlotSessionIds[slotIndex];
        if (!sid) return;
        this.entityClickConsumed = true;
        this.inputManager.suppressNextClick = true;
        if (this.currentTargetId === sid && this.currentTargetType === 'player') {
          this.clearTarget();
        } else {
          this.setTarget(sid, 'player');
        }
      });
      this.partyPanelTexts.push(nameText);
      this.partyPanelContainer.add(nameText);

      const hpBar = this.add.graphics();
      this.partyPanelHpBars.push(hpBar);
      this.partyPanelContainer.add(hpBar);

      const manaBar = this.add.graphics();
      this.partyPanelManaBars.push(manaBar);
      this.partyPanelContainer.add(manaBar);
    }

    // Cache initial title handle rect
    this.partyPanelTitleHandle = { x: defaultX, y: defaultY, w: PW, h: TH };
  }

  // ── Buffs Panel ──────────────────────────────────────────────

  private createBuffsPanel(): void {
    const MAX_BUFFS = 8;
    const PW = 180;
    const TH = 20;

    this.buffsPanelContainer = this.add.container(0, 0);
    this.buffsPanelContainer.setScrollFactor(0);
    this.buffsPanelContainer.setDepth(UI_DEPTH_BASE + 76);
    this.buffsPanelContainer.setVisible(false);

    // Background graphics (redrawn each frame based on buff count)
    this.buffsPanelBg = this.add.graphics();
    this.buffsPanelContainer.add(this.buffsPanelBg);

    // Title text — x is updated each frame when panel width changes
    this.buffsPanelTitleText = this.add.text(PW / 2, TH / 2, 'Buffs', {
      fontSize: '11px',
      color: '#8888bb',
      fontStyle: 'bold',
    }).setOrigin(0.5, 0.5);
    this.buffsPanelContainer.add(this.buffsPanelTitleText);

    // Pre-create one slot per possible displayed buff
    for (let i = 0; i < MAX_BUFFS; i++) {
      // Graphics: icon box fill + timer progress bar
      const rowGfx = this.add.graphics();
      this.buffsPanelRowGfx.push(rowGfx);
      this.buffsPanelContainer.add(rowGfx);

      // Abbreviation text, centered over the icon box
      const abbrText = this.add.text(0, 0, '', {
        fontSize: '8px',
        fontFamily: 'monospace',
        color: '#ffffff',
        fontStyle: 'bold',
      }).setOrigin(0.5, 0.5);
      this.buffsPanelAbbrTexts.push(abbrText);
      this.buffsPanelContainer.add(abbrText);

      // Buff name
      const nameText = this.add.text(0, 0, '', {
        fontSize: '9px',
        fontFamily: 'monospace',
        color: '#dddddd',
      }).setOrigin(0, 0);
      this.buffsPanelNameTexts.push(nameText);
      this.buffsPanelContainer.add(nameText);

      // Countdown timer (right-aligned)
      const timerText = this.add.text(0, 0, '', {
        fontSize: '9px',
        fontFamily: 'monospace',
        color: '#aaaaaa',
      }).setOrigin(1, 0);
      this.buffsPanelTimerTexts.push(timerText);
      this.buffsPanelContainer.add(timerText);
    }
  }

  private updateBuffsPanel(): void {
    const MAX_BUFFS  = 8;
    const MIN_PW     = 150;   // minimum panel width in px
    const TH         = 20;    // title bar height
    const ROW_H      = 28;    // height per buff row
    const ICON_X     = 8;     // left padding / icon left edge
    const ICON_SIZE  = 16;
    const NAME_GAP   = 4;     // gap between icon right edge and name text
    const TIMER_GAP  = 6;     // gap between name text right edge and timer
    const RIGHT_PAD  = 8;     // right margin inside panel
    const BAR_H      = 4;
    const now = Date.now();

    // Prune expired buffs
    for (const [skillId, buff] of this.localBuffs) {
      if (now >= buff.expiresAt) this.localBuffs.delete(skillId);
    }

    const buffs = Array.from(this.localBuffs.values());

    if (buffs.length === 0) {
      this.buffsPanelContainer.setVisible(false);
      return;
    }

    this.buffsPanelContainer.setVisible(true);

    const cam = this.cameras.main;
    const displayCount = Math.min(buffs.length, MAX_BUFFS);

    // ── Pass 1: set text content so Phaser can report measured widths ──
    for (let i = 0; i < displayCount; i++) {
      const buff = buffs[i];
      const skill = ClientDataManager.instance.getSkill(buff.skillId);
      const skillName = skill?.name ?? buff.skillId;
      const secsLeft  = Math.ceil(Math.max(0, buff.expiresAt - now) / 1000);

      this.buffsPanelNameTexts[i].setText(skillName);
      this.buffsPanelTimerTexts[i].setText(`${secsLeft}s`);
    }

    // ── Measure and compute dynamic panel width ──
    let maxNameW  = 0;
    let maxTimerW = 0;
    for (let i = 0; i < displayCount; i++) {
      maxNameW  = Math.max(maxNameW,  this.buffsPanelNameTexts[i].width);
      maxTimerW = Math.max(maxTimerW, this.buffsPanelTimerTexts[i].width);
    }

    const nameStartX = ICON_X + ICON_SIZE + NAME_GAP;
    const PW = Math.max(MIN_PW, nameStartX + maxNameW + TIMER_GAP + maxTimerW + RIGHT_PAD);
    const panelH = TH + displayCount * ROW_H + 4;

    // ── Position panel (re-centred on new width) ──
    const defaultX = Math.round(cam.width / 2 - PW / 2);
    const defaultY = cam.height - 72 - panelH;
    const px = defaultX + this.buffsOffset.x;
    const py = defaultY + this.buffsOffset.y;
    this.buffsPanelContainer.setPosition(px, py);
    this.buffsPanelTitleHandle = { x: px, y: py, w: PW, h: TH };

    // Re-centre title text horizontally when PW changes
    this.buffsPanelTitleText.setX(PW / 2);

    // ── Redraw background ──
    this.buffsPanelBg.clear();
    this.buffsPanelBg.fillStyle(0x12122a, 0.92);
    this.buffsPanelBg.fillRoundedRect(0, 0, PW, panelH, 4);
    this.buffsPanelBg.lineStyle(1, 0x44446a, 1);
    this.buffsPanelBg.strokeRoundedRect(0, 0, PW, panelH, 4);
    this.buffsPanelBg.fillStyle(0x22225a, 0.98);
    this.buffsPanelBg.fillRoundedRect(0, 0, PW, TH, { tl: 4, tr: 4, bl: 0, br: 0 });

    // ── Pass 2: position and draw each slot ──
    for (let i = 0; i < MAX_BUFFS; i++) {
      if (i < displayCount) {
        const buff = buffs[i];
        const skill = ClientDataManager.instance.getSkill(buff.skillId);
        const iconColor = skill?.iconColor ?? 0x555577;
        const abbrev    = skill?.iconAbbrev ?? '??';

        const rowY = TH + 4 + i * ROW_H;
        const iconY = rowY + 2;
        const barY  = rowY + ICON_SIZE + 6;
        const barW  = PW - ICON_X - RIGHT_PAD;

        // Duration progress (1 → 0 as buff counts down)
        const total   = buff.expiresAt - buff.appliedAt;
        const elapsed = now - buff.appliedAt;
        const ratio   = total > 0 ? Math.max(0, 1 - elapsed / total) : 0;

        // Icon box
        this.buffsPanelRowGfx[i].clear();
        this.buffsPanelRowGfx[i].fillStyle(iconColor, 1);
        this.buffsPanelRowGfx[i].fillRect(ICON_X, iconY, ICON_SIZE, ICON_SIZE);
        this.buffsPanelRowGfx[i].lineStyle(1, 0xffffff, 0.4);
        this.buffsPanelRowGfx[i].strokeRect(ICON_X, iconY, ICON_SIZE, ICON_SIZE);

        // Timer progress bar (spans full inner width)
        this.buffsPanelRowGfx[i].fillStyle(0x1a1a2e, 1);
        this.buffsPanelRowGfx[i].fillRect(ICON_X, barY, barW, BAR_H);
        this.buffsPanelRowGfx[i].fillStyle(iconColor, 0.85);
        this.buffsPanelRowGfx[i].fillRect(ICON_X, barY, Math.floor(barW * ratio), BAR_H);
        this.buffsPanelRowGfx[i].setVisible(true);

        // Abbreviation (centred in icon)
        this.buffsPanelAbbrTexts[i].setText(abbrev);
        this.buffsPanelAbbrTexts[i].setPosition(ICON_X + ICON_SIZE / 2, iconY + ICON_SIZE / 2);
        this.buffsPanelAbbrTexts[i].setVisible(true);

        // Buff name (full — panel was sized to fit, no truncation)
        this.buffsPanelNameTexts[i].setPosition(nameStartX, iconY);
        this.buffsPanelNameTexts[i].setVisible(true);
        // text was already set in pass 1

        // Countdown timer (right-aligned to panel edge)
        this.buffsPanelTimerTexts[i].setPosition(PW - RIGHT_PAD, iconY);
        this.buffsPanelTimerTexts[i].setVisible(true);
        // text was already set in pass 1

      } else {
        this.buffsPanelRowGfx[i].clear();
        this.buffsPanelRowGfx[i].setVisible(false);
        this.buffsPanelAbbrTexts[i].setVisible(false);
        this.buffsPanelNameTexts[i].setVisible(false);
        this.buffsPanelTimerTexts[i].setVisible(false);
      }
    }
  }

  private updatePartyPanel(): void {
    // Filter out self from display list
    const displayMembers = this.partyMembers.filter(m => m.sessionId !== this.network.sessionId);

    if (displayMembers.length === 0) {
      this.partyPanelContainer.setVisible(false);
      return;
    }

    this.partyPanelContainer.setVisible(true);

    const PW = 160;
    const TH = 20;
    const ROW_H = 38;
    const BAR_H = 7;
    const BAR_W = PW - 16;
    const panelH = TH + displayMembers.length * ROW_H + 4;

    const defaultX = 12;
    const defaultY = 200;
    const px = defaultX + this.partyOffset.x;
    const py = defaultY + this.partyOffset.y;

    this.partyPanelContainer.setPosition(px, py);
    this.partyPanelTitleHandle = { x: px, y: py, w: PW, h: TH };

    // Redraw background
    this.partyPanelBg.clear();
    this.partyPanelBg.fillStyle(0x12122a, 0.92);
    this.partyPanelBg.fillRoundedRect(0, 0, PW, panelH, 4);
    this.partyPanelBg.lineStyle(1, 0x44446a, 1);
    this.partyPanelBg.strokeRoundedRect(0, 0, PW, panelH, 4);
    // Title strip
    this.partyPanelBg.fillStyle(0x22225a, 0.98);
    this.partyPanelBg.fillRoundedRect(0, 0, PW, TH, { tl: 4, tr: 4, bl: 0, br: 0 });
    // Title text
    this.partyPanelBg.fillStyle(0x8888bb, 1);
    // We'll just render the title via a small text approach by overlaying it on the bg
    // Actually, let's use the first draw to place it. We need a text object for the title.
    // For simplicity, use the bg graphics and manually draw the title each frame via an inline text reuse.

    // Update member rows
    for (let i = 0; i < 4; i++) {
      if (i < displayMembers.length) {
        const member = displayMembers[i];
        const data = this.partyMemberData.get(member.sessionId);
        const hp = data?.hp ?? 0;
        const maxHp = data?.maxHp ?? 1;
        const mana = data?.mana ?? 0;
        const maxMana = data?.maxMana ?? 1;
        const alive = data?.alive ?? true;

        const rowY = TH + 4 + i * ROW_H;

        // Name
        this.partySlotSessionIds[i] = member.sessionId;
        const displayName = member.characterName.length > 14
          ? member.characterName.slice(0, 14) + '…'
          : member.characterName;
        this.partyPanelTexts[i].setText(displayName);
        this.partyPanelTexts[i].setPosition(8, rowY);
        const isTargeted = this.currentTargetId === member.sessionId && this.currentTargetType === 'player';
        this.partyPanelTexts[i].setColor(
          isTargeted ? '#ffdd55' : (alive ? '#ffffff' : '#666666'),
        );
        this.partyPanelTexts[i].setVisible(true);

        // HP bar
        const hpBarY = rowY + 16;
        const hpRatio = maxHp > 0 ? Math.max(0, hp / maxHp) : 0;
        this.partyPanelHpBars[i].clear();
        // Background
        this.partyPanelHpBars[i].fillStyle(0x1a1a1a, 1);
        this.partyPanelHpBars[i].fillRect(8, hpBarY, BAR_W, BAR_H);
        // Fill
        const hpColor = hpRatio > 0.5 ? 0x44bb44 : hpRatio > 0.25 ? 0xdddd44 : 0xdd4444;
        this.partyPanelHpBars[i].fillStyle(hpColor, 1);
        this.partyPanelHpBars[i].fillRect(8, hpBarY, Math.floor(BAR_W * hpRatio), BAR_H);
        // Shield overlay (Shield of Faith) — cyan tint proportional to remaining absorption
        const memberShieldHp = data?.shieldHp ?? 0;
        if (memberShieldHp > 0 && maxHp > 0) {
          const shieldRatio = Math.min(1, memberShieldHp / maxHp);
          this.partyPanelHpBars[i].fillStyle(0x00ccff, 0.45);
          this.partyPanelHpBars[i].fillRect(8, hpBarY, Math.floor(BAR_W * shieldRatio), BAR_H);
        }
        this.partyPanelHpBars[i].setVisible(true);

        // Mana bar
        const manaBarY = hpBarY + BAR_H + 2;
        const manaRatio = maxMana > 0 ? Math.max(0, mana / maxMana) : 0;
        this.partyPanelManaBars[i].clear();
        this.partyPanelManaBars[i].fillStyle(0x1a1a1a, 1);
        this.partyPanelManaBars[i].fillRect(8, manaBarY, BAR_W, BAR_H);
        this.partyPanelManaBars[i].fillStyle(0x4466dd, 1);
        this.partyPanelManaBars[i].fillRect(8, manaBarY, Math.floor(BAR_W * manaRatio), BAR_H);
        this.partyPanelManaBars[i].setVisible(true);
      } else {
        // Hide unused slots
        this.partySlotSessionIds[i] = null;
        this.partyPanelTexts[i].setVisible(false);
        this.partyPanelHpBars[i].clear();
        this.partyPanelHpBars[i].setVisible(false);
        this.partyPanelManaBars[i].clear();
        this.partyPanelManaBars[i].setVisible(false);
      }
    }
  }

  private updateTargetNameplate(): void {
    if (!this.currentTargetId || !this.currentTargetType) {
      this.targetNameplateContainer.setVisible(false);
      this.targetNameplateBuffsGfx.clear();
      return;
    }

    const TH           = 22;             // title strip height
    const BAR_Y        = TH + 24;        // 46 — top of HP bar
    const BAR_H        = 10;
    const HP_TEXT_Y    = BAR_Y + BAR_H + 4; // 60 — 4 px gap below bar
    const BUFF_Y       = HP_TEXT_Y + 14 + 6; // 80 — top of buff pill row
    const PILL_H       = 16;             // pill height
    const PILL_SPACING = 4;             // gap between pills
    const MIN_NW = 160;
    const LEFT_PAD = 8;
    const RIGHT_PAD = 10;
    const NAME_LEVEL_GAP = 10;

    let name = '';
    let level = 1;
    let hp = 0;
    let maxHp = 1;
    let isEnemy = false;
    let shieldHp = 0;
    let npcBuffs: NpcBuffData[] = [];

    if (this.currentTargetType === 'player') {
      const info: PlayerTargetInfo | null = this.entityRenderer.getPlayerTargetInfo(this.currentTargetId);
      if (!info) { this.clearTarget(); return; }
      name = info.characterName || info.classId;
      level = info.level;
      hp = info.hp;
      maxHp = info.maxHp;
      isEnemy = false;
      shieldHp = this.remotePlayerShieldHp.get(this.currentTargetId) ?? 0;
    } else if (this.currentTargetType === 'self') {
      // Local player — read directly from scene fields (always up to date)
      name = this.localCharacterName || (ClientDataManager.instance.getClass(this.localClassId)?.name ?? this.localClassId);
      level = this.localLevel;
      hp = this.localHp;
      maxHp = this.localMaxHp;
      isEnemy = false;
      shieldHp = this.localShieldHp;
    } else {
      const info: NpcTargetInfo | null = this.entityRenderer.getNpcTargetInfo(this.currentTargetId);
      if (!info) { this.clearTarget(); return; }
      name = info.name;
      level = info.level;
      hp = info.hp;
      maxHp = info.maxHp;
      isEnemy = info.npcType === 'enemy';
      npcBuffs = info.buffs ?? [];
    }

    // ── Compute panel height (extends if NPC has active buffs) ─
    const now = Date.now();
    const activeBuffs = npcBuffs.filter(b => b.expiresAt > now);
    const NH_BASE = HP_TEXT_Y + 14 + 5; // 79 — base height (no buffs)
    const NH = activeBuffs.length > 0
      ? NH_BASE + PILL_H + PILL_SPACING + 4
      : NH_BASE;

    this.targetNameplateContainer.setVisible(true);

    // ── Pass 1: set text so Phaser can measure widths ──────────
    const nameColor = this.currentTargetType === 'npc'
      ? (isEnemy ? '#ff8888' : '#ffee88')
      : '#ffffff';
    this.targetNameplateNameText.setText(name);
    this.targetNameplateNameText.setColor(nameColor);
    this.targetNameplateLevelText.setText(`Lv.${level}`);

    // ── Pass 2: compute dynamic panel width ────────────────────
    const nameW = this.targetNameplateNameText.width;
    const levelW = this.targetNameplateLevelText.width;
    const NW = Math.max(MIN_NW, LEFT_PAD + nameW + NAME_LEVEL_GAP + levelW + RIGHT_PAD);

    // ── Reposition right-anchored and centred elements ─────────
    this.targetNameplateLevelText.setX(NW - RIGHT_PAD + 4);
    this.targetNameplateTitleText.setX(NW / 2);
    this.targetNameplateHpText.setX(NW / 2);
    this.targetNameplateHpText.setY(HP_TEXT_Y);

    // ── Reposition container so panel stays right-anchored ─────
    const camW = this.cameras.main.width;
    this.targetNameplateContainer.setPosition(
      (camW - NW - 12) + this.targetOffset.x,
      this.getTargetNameplateDefaultY() + this.targetOffset.y,
    );

    // ── Redraw background at new width ─────────────────────────
    this.targetNameplateBg.clear();
    this.targetNameplateBg.fillStyle(0x12122a, 0.92);
    this.targetNameplateBg.fillRoundedRect(0, 0, NW, NH, 4);
    this.targetNameplateBg.lineStyle(1, 0x44446a, 1);
    this.targetNameplateBg.strokeRoundedRect(0, 0, NW, NH, 4);
    this.targetNameplateBg.fillStyle(0x22225a, 0.98);
    this.targetNameplateBg.fillRoundedRect(0, 0, NW, TH, { tl: 4, tr: 4, bl: 0, br: 0 });

    // ── Update drag handle ─────────────────────────────────────
    const cx = this.targetNameplateContainer.x;
    const cy = this.targetNameplateContainer.y;
    this.targetNameplateTitleHandle = { x: cx, y: cy, w: NW, h: TH };

    // ── HP bar ─────────────────────────────────────────────────
    const hpRatio = maxHp > 0 ? Math.max(0, hp / maxHp) : 0;
    const fillColor = hpRatio > 0.5 ? 0x44ee44 : hpRatio > 0.25 ? 0xffaa00 : 0xff4444;
    const barX = LEFT_PAD;
    const barW = NW - LEFT_PAD * 2;

    this.targetNameplateHpBar.clear();
    this.targetNameplateHpBar.fillStyle(0x000000, 0.6);
    this.targetNameplateHpBar.fillRect(barX - 1, BAR_Y - 1, barW + 2, BAR_H + 2);
    this.targetNameplateHpBar.fillStyle(fillColor, 1);
    this.targetNameplateHpBar.fillRect(barX, BAR_Y, Math.max(0, barW * hpRatio), BAR_H);

    // Shield overlay (Shield of Faith) — cyan tint proportional to remaining absorption
    if (shieldHp > 0 && maxHp > 0) {
      const shieldRatio = Math.min(1, shieldHp / maxHp);
      this.targetNameplateHpBar.fillStyle(0x00ccff, 0.45);
      this.targetNameplateHpBar.fillRect(barX, BAR_Y, Math.max(0, barW * shieldRatio), BAR_H);
    }

    // ── HP text ────────────────────────────────────────────────
    this.targetNameplateHpText.setText(`${Math.round(hp)} / ${Math.round(maxHp)}`);

    // ── Buff pills (NPC targets only) ──────────────────────────
    this.targetNameplateBuffsGfx.clear();
    if (activeBuffs.length > 0) {
      let pillX = LEFT_PAD;
      for (const buff of activeBuffs) {
        // Remaining seconds, clamped to 0
        const remainSec = Math.max(0, Math.ceil((buff.expiresAt - now) / 1000));

        // Skill short name (e.g. "Poison" from "rogue_poison_blade")
        const skillTemplate = ClientDataManager.instance.getSkill(buff.skillId);
        const skillName = skillTemplate?.name ?? buff.skillId;
        // Shorten to first word for compact pill
        const shortName = skillName.split(' ')[0];
        const label = `\u2620 ${shortName} ${remainSec}s`;

        // Pill color: green for DoT poison, purple fallback
        const pillColor = buff.dotDamagePerSec > 0 ? 0x1a3a1a : 0x2a1a3a;
        const pillBorder = buff.dotDamagePerSec > 0 ? 0x44bb44 : 0x9955cc;
        const textColor = buff.dotDamagePerSec > 0 ? '#88ff88' : '#cc88ff';

        // Approximate pill width (8px per char is a safe estimate for 10px font)
        const pillW = Math.max(50, label.length * 7 + 8);

        // Pill background + border
        this.targetNameplateBuffsGfx.fillStyle(pillColor, 0.95);
        this.targetNameplateBuffsGfx.fillRoundedRect(pillX, BUFF_Y, pillW, PILL_H, 3);
        this.targetNameplateBuffsGfx.lineStyle(1, pillBorder, 0.9);
        this.targetNameplateBuffsGfx.strokeRoundedRect(pillX, BUFF_Y, pillW, PILL_H, 3);

        // Position text label inside first pill
        // (Only one Text object — show the first buff label; additional buffs shown as extra pills)
        if (pillX === LEFT_PAD) {
          this.targetNameplateBuffsText.setText(label);
          this.targetNameplateBuffsText.setColor(textColor);
          this.targetNameplateBuffsText.setPosition(pillX + 4, BUFF_Y + PILL_H / 2);
          this.targetNameplateBuffsText.setVisible(true);
        }

        pillX += pillW + PILL_SPACING;
        // Stop drawing more pills if we'd exceed the panel width
        if (pillX > NW - RIGHT_PAD) break;
      }
      // If only additional pills beyond the first exist (no label shown), still show the text
    } else {
      this.targetNameplateBuffsText.setVisible(false);
    }
  }

  private setTarget(id: string, type: 'player' | 'npc' | 'self'): void {
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
    // Stop auto-attack when target is cleared
    this.network.sendStopAutoAttack();
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

    // Inventory slot texts and icon images (no zones — we use scene-level pointer hit testing)
    this.invSlotTexts = [];
    this.invSlotIcons = [];
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

      // Placeholder for inventory icon image (created on demand in renderInventorySlots)
      this.invSlotIcons.push(null);

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

    const slotLabels = EQUIP_SLOTS.map(sl => sl.charAt(0).toUpperCase() + sl.slice(1));
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

      // Item detail panel close button (must be checked before drag logic)
      if (this.itemDetailPanelOpen && this.isItemDetailCloseBtn(pointer.x, pointer.y)) {
        this.closeItemDetailPanel();
        return;
      }

      // Adjust for the container's current drag offset
      const px = pointer.x - this.invOffset.x;
      const py = pointer.y - this.invOffset.y;

      const isShift = (pointer.event as MouseEvent).shiftKey;

      // Check inventory slot
      const invSlot = this.getInventorySlotAt(px, py);
      if (invSlot >= 0) {
        const item = this.inventoryItems[invSlot];
        if (item) {
          // Shift+click: open item detail pane instead of dragging
          if (isShift) {
            this.showItemDetail(item.itemId);
            return;
          }
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
          // Shift+click on equipped item: open item detail pane
          if (isShift) {
            this.showItemDetail(equippedId);
            return;
          }
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

      // Check loot panel slot (left-click drag from loot panel)
      if (this.lootPanelOpen && this.currentLootBagId && pointer.button === 0) {
        const lootSlot = this.getLootSlotAt(pointer.x, pointer.y);
        if (lootSlot >= 0) {
          const item = this.lootPanelItems[lootSlot];
          if (item) {
            // Shift+click: inspect item before looting
            if (isShift) {
              this.showItemDetail(item.itemId);
              return;
            }
            const template = ClientDataManager.instance.getItem(item.itemId);
            if (template) {
              this.startDrag(
                { type: 'lootBag', bagId: this.currentLootBagId, bagSlotIndex: lootSlot },
                template.name,
                RARITY_COLORS[template.rarity] ?? '#ffffff',
              );
            }
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
    source: { type: 'inventory' | 'equipment' | 'lootBag'; index?: number; slotType?: string; bagId?: string; bagSlotIndex?: number },
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
    const slotTypes = EQUIP_SLOTS;
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
    // Loot panel bounds
    if (this.lootPanelOpen && this.isInsideLootPanel(px, py)) {
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
      const idx = (EQUIP_SLOTS as string[]).indexOf(equipSlot);
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
    } else if (this.dragSource.type === 'lootBag') {
      // Loot bag → Inventory: loot the item
      const bagId = this.dragSource.bagId!;
      const bagSlotIndex = this.dragSource.bagSlotIndex!;
      const lootItem = this.lootPanelItems[bagSlotIndex];
      if (lootItem && (targetInvSlot >= 0 || insidePanels)) {
        this.network.sendLootItem(bagId, bagSlotIndex, lootItem.quantity);
      }
      // Dropping outside panels from loot panel = do nothing
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
    } else {
      this.closeItemDetailPanel();
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
    const slotKeys = EQUIP_SLOTS;
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

      // Update icon / text
      if (itemTemplate) {
        const iconKey = itemTemplate.inventoryIcon ? `icon_${itemTemplate.inventoryIcon}` : null;
        const hasIcon = iconKey && this.textures.exists(iconKey);

        if (hasIcon) {
          // Show icon image, hide text abbreviation
          this.invSlotTexts[i].setText('');
          if (!this.invSlotIcons[i] || this.invSlotIcons[i]!.texture.key !== iconKey) {
            // Destroy old icon if different
            if (this.invSlotIcons[i]) {
              this.invSlotIcons[i]!.destroy();
            }
            const iconImg = this.add.image(sx + this.SLOT_SIZE / 2, sy + this.SLOT_SIZE / 2, iconKey);
            // Scale icon to fit slot (leave a 2px border)
            const maxDim = this.SLOT_SIZE - 4;
            const scale = Math.min(maxDim / iconImg.width, maxDim / iconImg.height);
            iconImg.setScale(scale);
            this.invContainer.add(iconImg);
            this.invSlotIcons[i] = iconImg;
          }
          this.invSlotIcons[i]!.setVisible(true);
          this.invSlotIcons[i]!.setPosition(sx + this.SLOT_SIZE / 2, sy + this.SLOT_SIZE / 2);
        } else {
          // No icon — use text abbreviation
          const abbr = itemTemplate.name.length > 8 ? itemTemplate.name.slice(0, 7) + '.' : itemTemplate.name;
          this.invSlotTexts[i].setText(abbr);
          this.invSlotTexts[i].setColor(RARITY_COLORS[itemTemplate.rarity] ?? '#ffffff');
          if (this.invSlotIcons[i]) {
            this.invSlotIcons[i]!.setVisible(false);
          }
        }
        this.invQtyTexts[i].setText(item.quantity > 1 ? `${item.quantity}` : '');
      } else {
        this.invSlotTexts[i].setText('');
        this.invQtyTexts[i].setText('');
        if (this.invSlotIcons[i]) {
          this.invSlotIcons[i]!.setVisible(false);
        }
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
    // Culling runs before the early-out: the camera can move (and the map
    // therefore needs re-culling) while the player is dead or still connecting.
    this.cullTiles();

    if (!this.connected || !this.playerSprite) return;

    const dt = delta / 1000;

    // ── Update death timer ───────────────────────────────────
    if (!this.localAlive && this.respawnTimer > 0) {
      this.respawnTimer -= delta;
      const secs = Math.max(0, Math.ceil(this.respawnTimer / 1000));
      this.deathText.setText(`YOU DIED\nRespawning in ${secs}...`);
    }

    // ── Skip gameplay input while any UI panel or chat is open ─
    if (this.inventoryOpen || this.skillsPaneOpen || this.chatInputActive || this.optionsMenuOpen || this.hudEditMode) {
      this.inputManager.clearFire();
    } else if (!this.inventoryOpen && !this.skillsPaneOpen && !this.chatInputActive && !this.optionsMenuOpen && !this.hudEditMode) {
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

      // ── Update local sprite (via render interpolation) ──────
      if (this.localAlive) {
        // Smooth renderX/Y toward the authoritative localX/Y each frame.
        // This absorbs any micro-corrections from server reconciliation
        // without adding perceptible input lag (~2 px at 200 speed).
        const rdx = this.localX - this.renderX;
        const rdy = this.localY - this.renderY;
        const rDistSq = rdx * rdx + rdy * rdy;
        if (rDistSq < 0.25) {
          // Sub-pixel — snap to avoid endless chase
          this.renderX = this.localX;
          this.renderY = this.localY;
        } else if (rDistSq > 2500) {
          // > 50 px — teleport / zone change, snap immediately
          this.renderX = this.localX;
          this.renderY = this.localY;
        } else {
          const smoothT = 1 - Math.exp(-25 * dt);
          this.renderX += rdx * smoothT;
          this.renderY += rdy * smoothT;
        }

        const playerIso = this.isIso ? orthoToIso(this.renderX, this.renderY) : { x: this.renderX, y: this.renderY };
        this.playerSprite.x = playerIso.x;
        this.playerSprite.y = playerIso.y;
        this.playerSprite.setDepth(ENTITY_DEPTH_BASE + Math.floor(this.renderX / 64) + Math.floor(this.renderY / 64));

        // Directional sprite animation based on movement
        const moveDir = this.getMovementDir(input);
        this.playerSprite.rotation = 0;

        // Movement cancels action animations
        if (this.isPlayingActionAnim && moveDir) {
          this.stopActionAnimation();
        }

        if (this.isPlayingActionAnim) {
          // Let action animation play through
        } else if (moveDir) {
          this.lastFacingDir = moveDir;
          playCharacterAnim(this, this.playerSprite, this.localClassId, 'walk', moveDir as PaperdollDir);
        } else {
          playCharacterAnim(this, this.playerSprite, this.localClassId, 'idle',
                            this.lastFacingDir as PaperdollDir);
        }

        // ── Draw aim line ─────────────────────────────────────
        this.drawAimLine(input.aimAngle);

        // ── Sync equipment overlay positions ─────────────────
        this.entityRenderer.updateOverlayPositions(this.localEquipOverlays, this.playerSprite);
      }
    }

    // ── Auto-close loot panel if too far ────────────────────
    if (this.lootPanelOpen && this.currentLootBagId) {
      const bagOrtho = this.entityRenderer.getLootBagOrthoPosition(this.currentLootBagId);
      if (bagOrtho) {
        const dx = this.localX - bagOrtho.x;
        const dy = this.localY - bagOrtho.y;
        if (dx * dx + dy * dy > LOOT_BAG_PICKUP_RANGE * LOOT_BAG_PICKUP_RANGE) {
          this.closeLootPanel();
        }
      } else {
        // Bag no longer exists
        this.closeLootPanel();
      }
    }

    // ── Draw HUD ────────────────────────────────────────────
    this.drawLocalHud();
    this.drawActionBar();
    this.drawCastBar();
    this.drawChatPanel();
    this.drawCombatLogPanel();
    this.updateTargetNameplate();
    this.updatePartyPanel();
    this.updateBuffsPanel();

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
    // If the server hasn't acknowledged any new input since the last reconcile,
    // no positions will have changed on the server side — skip the work entirely.
    if (serverSeq === this._lastServerSeq) return;
    this._lastServerSeq = serverSeq;

    // Drop all inputs the server has already processed.
    this.pendingInputs = this.pendingInputs.filter((p) => p.input.seq > serverSeq);

    // Save the current client-predicted position so we can measure error below.
    const predictedX = this.localX;
    const predictedY = this.localY;

    // Compute the server-authoritative position by re-applying every unacknowledged input.
    this.localX = serverX;
    this.localY = serverY;
    for (const pending of this.pendingInputs) {
      this.applyInputLocally(pending.input, pending.dt);
    }

    // ── Smooth error correction ───────────────────────────────
    // Instead of hard-snapping, blend small prediction errors so micro-corrections
    // don't produce visible pops.  Large errors (genuine desyncs) still snap immediately.
    const errX  = this.localX - predictedX;
    const errY  = this.localY - predictedY;
    const errSq = errX * errX + errY * errY;

    if (errSq < 1) {
      // Sub-pixel error — discard entirely and trust the client prediction.
      this.localX = predictedX;
      this.localY = predictedY;
    } else if (errSq < 144) {
      // Small error (< 12 px) — blend 35 % of the way toward the correction.
      // This spreads the adjustment over ~3 server patches (~150 ms) invisibly.
      this.localX = predictedX + errX * 0.35;
      this.localY = predictedY + errY * 0.35;
    }
    // errSq >= 144 (>= 12 px) — genuine desync, hard-snap (position already set).
  }

  /**
   * Draw a short aim line from the player in the aim direction.
   */
  private drawAimLine(angle: number): void {
    this.aimLine.clear();
    this.aimLine.lineStyle(2, 0xff4444, 0.7);

    const len = 40;
    const startX = this.renderX + Math.cos(angle) * 20;
    const startY = this.renderY + Math.sin(angle) * 20;
    const endX = this.renderX + Math.cos(angle) * (20 + len);
    const endY = this.renderY + Math.sin(angle) * (20 + len);

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

    // Scrollbar graphics (drawn on top of background)
    this.chatScrollGfx = this.add.graphics();
    this.chatScrollGfx.setScrollFactor(0);
    this.chatScrollGfx.setDepth(UI_DEPTH_BASE + 22);
    this.chatContainer.add(this.chatScrollGfx);

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
        wordWrap: { width: this.chatEffectiveW - this.CHAT_PAD * 2 - this.CHAT_SCROLLBAR_W },
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
    const panelX = this.CHAT_LEFT_X;
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

  // ══════════════════════════════════════════════════════════
  // ██  COMBAT LOG PANEL
  // ══════════════════════════════════════════════════════════

  private createCombatLogPanel(): void {
    const cam = this.cameras.main;
    this.CL_VISIBLE_LINES = Math.floor((this.CL_H - this.CL_TITLE_H - this.CL_PAD) / this.CL_LINE_H);

    this.combatLogContainer = this.add.container(0, 0);
    this.combatLogContainer.setScrollFactor(0);
    this.combatLogContainer.setDepth(UI_DEPTH_BASE + 22);

    // Background
    this.combatLogBgGfx = this.add.graphics();
    this.combatLogContainer.add(this.combatLogBgGfx);

    // Scrollbar
    this.combatLogScrollGfx = this.add.graphics();
    this.combatLogContainer.add(this.combatLogScrollGfx);

    // Title text
    this.combatLogTitleText = this.add.text(0, 0, 'Combat Log', {
      fontSize: '10px',
      color: '#cc8844',
      stroke: '#000000',
      strokeThickness: 1,
    });
    this.combatLogTitleText.setScrollFactor(0);
    this.combatLogTitleText.setDepth(UI_DEPTH_BASE + 23);
    this.combatLogContainer.add(this.combatLogTitleText);

    // Pre-allocate message text objects
    const textW = this.CL_W - this.CL_PAD * 2 - this.CL_SCROLLBAR_W;
    for (let i = 0; i < this.CL_VISIBLE_LINES; i++) {
      const t = this.add.text(0, 0, '', {
        fontSize: '10px',
        color: '#ffffff',
        stroke: '#000000',
        strokeThickness: 1,
        wordWrap: { width: textW },
      });
      t.setScrollFactor(0);
      t.setDepth(UI_DEPTH_BASE + 23);
      t.setVisible(false);
      this.combatLogContainer.add(t);
      this.combatLogMessageTexts.push(t);
    }

    // Context menu graphics (starts hidden, positioned on right-click)
    this.combatLogContextGfx = this.add.graphics();
    this.combatLogContextGfx.setScrollFactor(0);
    this.combatLogContextGfx.setDepth(UI_DEPTH_BASE + 310);
    this.combatLogContextGfx.setVisible(false);
  }

  /** Get the default screen position of the combat log panel (top-right area). */
  private getCombatLogDefaultPos(): { x: number; y: number } {
    const cam = this.cameras.main;
    return { x: cam.width - this.CL_W - 12, y: 12 };
  }

  /** Get the chat panel rect (for scroll / drag hit-testing). */
  private getChatPanelRect(): { x: number; y: number; w: number; h: number } {
    const cam = this.cameras.main;
    return {
      x: this.CHAT_LEFT_X + this.chatOffset.x,
      y: cam.height - this.CHAT_H - this.CHAT_BOTTOM_MARGIN + this.chatOffset.y,
      w: this.chatEffectiveW,
      h: this.CHAT_H,
    };
  }

  /** Get the combat log panel rect (for drag detection). */
  private getCombatLogPanelRect(): { x: number; y: number; w: number; h: number } {
    const def = this.getCombatLogDefaultPos();
    return {
      x: def.x + this.combatLogOffset.x,
      y: def.y + this.combatLogOffset.y,
      w: this.CL_W,
      h: this.CL_H,
    };
  }

  /** Resolve an entity ID (player/NPC session or state ID) to a display name. */
  private getCombatEntityName(id: string): string {
    if (id === this.network.sessionId) return 'You';
    // Check party members
    const partyMember = this.partyMembers.find(m => m.sessionId === id);
    if (partyMember) return partyMember.characterName;
    // Check other players
    const cached = this.remotePlayerCache.get(id);
    if (cached) return cached.characterName || id.slice(0, 6);
    // Check NPCs
    const npc = this.npcCache.get(id);
    if (npc) return npc.name || 'NPC';
    return id.slice(0, 6);
  }

  /** Check if a given entity is the local player or a party member. */
  private isLocalOrParty(id: string): boolean {
    if (id === this.network.sessionId) return true;
    return this.partyMembers.some(m => m.sessionId === id);
  }

  /** Push a combat log entry. Respects max message cap. */
  private pushCombatLog(text: string, color: string, type: string): void {
    this.combatLogMessages.push({ text, color, type });
    if (this.combatLogMessages.length > this.CL_MAX_MESSAGES) {
      this.combatLogMessages.shift();
      // Adjust scroll offset if we pruned from the top
      if (this.combatLogScrollOffset > 0) {
        this.combatLogScrollOffset = Math.max(0, this.combatLogScrollOffset - 1);
      }
    }
    // Auto-scroll to bottom if already near the bottom
    if (this.combatLogScrollOffset <= 2) {
      this.combatLogScrollOffset = 0;
    }
  }

  /** Get filtered messages for display. */
  private getFilteredCombatLogMessages(): { text: string; color: string; type: string }[] {
    return this.combatLogMessages.filter(m => this.combatLogFilters[m.type] !== false);
  }

  private drawCombatLogPanel(): void {
    const cam = this.cameras.main;
    const def = this.getCombatLogDefaultPos();
    const panelX = def.x + this.combatLogOffset.x;
    const panelY = def.y + this.combatLogOffset.y;

    // Draw background
    this.combatLogBgGfx.clear();
    this.combatLogBgGfx.setPosition(panelX, panelY);
    this.combatLogBgGfx.fillStyle(0x0a0a1a, 0.78);
    this.combatLogBgGfx.fillRect(0, 0, this.CL_W, this.CL_H);
    this.combatLogBgGfx.lineStyle(1, 0x553322, 0.9);
    this.combatLogBgGfx.strokeRect(0, 0, this.CL_W, this.CL_H);
    // Title strip
    this.combatLogBgGfx.fillStyle(0x1a1008, 0.6);
    this.combatLogBgGfx.fillRect(0, 0, this.CL_W, this.CL_TITLE_H);

    this.combatLogTitleText.setPosition(panelX + this.CL_PAD, panelY + 3);

    // Get filtered messages
    const filtered = this.getFilteredCombatLogMessages();
    const totalFiltered = filtered.length;
    const maxScroll = Math.max(0, totalFiltered - this.CL_VISIBLE_LINES);
    // Clamp scroll offset
    if (this.combatLogScrollOffset > maxScroll) this.combatLogScrollOffset = maxScroll;

    // Show messages — bottom-up layout so word-wrapped lines don't overlap.
    const msgAreaTopY    = panelY + this.CL_TITLE_H + 2;
    const msgAreaBottomY = panelY + this.CL_H - 4;
    const startIdx = Math.max(0, totalFiltered - this.CL_VISIBLE_LINES - this.combatLogScrollOffset);

    // Pass 1: populate slots with content (slot 0 = oldest visible, slot N-1 = newest).
    for (let i = 0; i < this.CL_VISIBLE_LINES; i++) {
      const t = this.combatLogMessageTexts[i];
      const msgIdx = startIdx + i;
      if (msgIdx >= 0 && msgIdx < totalFiltered) {
        const msg = filtered[msgIdx];
        t.setText(msg.text);
        t.setColor(msg.color);
        t.setVisible(true);
      } else {
        t.setText('');
        t.setVisible(false);
      }
    }

    // Pass 2: read actual rendered heights and stack from bottom up.
    let curY = msgAreaBottomY;
    for (let i = this.CL_VISIBLE_LINES - 1; i >= 0; i--) {
      const t = this.combatLogMessageTexts[i];
      if (!t.text) { t.setVisible(false); continue; }
      const h = Math.max(this.CL_LINE_H, t.height);
      curY -= h;
      if (curY < msgAreaTopY) {
        t.setVisible(false); // would overflow into the title bar — hide it
      } else {
        t.setPosition(panelX + this.CL_PAD, curY);
        t.setVisible(true);
      }
    }

    // Draw scrollbar
    this.combatLogScrollGfx.clear();
    this.combatLogScrollGfx.setPosition(panelX, panelY);
    const sbX = this.CL_W - this.CL_SCROLLBAR_W - 2;
    const sbY = this.CL_TITLE_H + 2;
    const sbH = this.CL_H - this.CL_TITLE_H - 4;

    // Track background
    this.combatLogScrollGfx.fillStyle(0x222233, 0.5);
    this.combatLogScrollGfx.fillRect(sbX, sbY, this.CL_SCROLLBAR_W, sbH);

    if (totalFiltered > this.CL_VISIBLE_LINES) {
      // Thumb
      const thumbRatio = this.CL_VISIBLE_LINES / totalFiltered;
      const thumbH = Math.max(16, sbH * thumbRatio);
      const scrollFraction = maxScroll > 0 ? (maxScroll - this.combatLogScrollOffset) / maxScroll : 1;
      const thumbY = sbY + (sbH - thumbH) * scrollFraction;

      this.combatLogScrollGfx.fillStyle(0x886644, 0.8);
      this.combatLogScrollGfx.fillRoundedRect(sbX, thumbY, this.CL_SCROLLBAR_W, thumbH, 3);
    }

    // Draw context menu if open
    if (this.combatLogContextMenuOpen) {
      this.drawCombatLogContextMenu();
    }
  }

  // ── Combat Log: Context Menu (right-click filter toggles) ──

  private readonly CL_FILTER_LABELS: { key: string; label: string; color: string }[] = [
    { key: 'outDmg', label: 'Your Damage', color: '#ffcc44' },
    { key: 'inDmg',  label: 'Damage Taken', color: '#ff6644' },
    { key: 'misses', label: 'Misses', color: '#999999' },
    { key: 'dodges', label: 'Dodges', color: '#ffffff' },
    { key: 'blocks', label: 'Blocks', color: '#4488ff' },
    { key: 'deaths', label: 'Deaths', color: '#ff4444' },
    { key: 'heals',  label: 'Healing', color: '#44ff44' },
    { key: 'buffs',  label: 'Buffs & Debuffs', color: '#88ccff' },
    { key: 'xp',     label: 'XP Rewards', color: '#ffaa00' },
    { key: 'party',  label: 'Party Combat', color: '#aabb88' },
  ];

  private openCombatLogContextMenu(x: number, y: number): void {
    this.combatLogContextMenuOpen = true;
    this.combatLogContextMenuPos = { x, y };
    this.combatLogContextGfx.setVisible(true);

    // Destroy old text objects
    for (const t of this.combatLogContextTexts) t.destroy();
    this.combatLogContextTexts = [];

    const rowH = 20;
    const menuW = 150;
    const menuH = this.CL_FILTER_LABELS.length * rowH + 8;

    // Clamp menu position to screen
    const cam = this.cameras.main;
    const menuX = Math.min(x, cam.width - menuW - 4);
    const menuY = Math.min(y, cam.height - menuH - 4);

    this.combatLogContextGfx.clear();
    this.combatLogContextGfx.fillStyle(0x1a1a2e, 0.97);
    this.combatLogContextGfx.fillRoundedRect(menuX, menuY, menuW, menuH, 4);
    this.combatLogContextGfx.lineStyle(1, 0x886644, 1);
    this.combatLogContextGfx.strokeRoundedRect(menuX, menuY, menuW, menuH, 4);

    for (let i = 0; i < this.CL_FILTER_LABELS.length; i++) {
      const f = this.CL_FILTER_LABELS[i];
      const ry = menuY + 4 + i * rowH;

      // Checkbox square
      const checked = this.combatLogFilters[f.key] !== false;
      this.combatLogContextGfx.fillStyle(checked ? 0x886644 : 0x333344, 1);
      this.combatLogContextGfx.fillRect(menuX + 8, ry + 4, 12, 12);
      if (checked) {
        this.combatLogContextGfx.lineStyle(2, 0xffffff, 1);
        this.combatLogContextGfx.lineBetween(menuX + 10, ry + 10, menuX + 13, ry + 14);
        this.combatLogContextGfx.lineBetween(menuX + 13, ry + 14, menuX + 18, ry + 6);
        this.combatLogContextGfx.lineStyle(1, 0x886644, 1); // reset
      }

      const label = this.add.text(menuX + 26, ry + 3, f.label, {
        fontSize: '10px',
        color: f.color,
        stroke: '#000000',
        strokeThickness: 1,
      });
      label.setScrollFactor(0);
      label.setDepth(UI_DEPTH_BASE + 311);
      this.combatLogContextTexts.push(label);
    }
  }

  private closeCombatLogContextMenu(): void {
    this.combatLogContextMenuOpen = false;
    this.combatLogContextGfx.setVisible(false);
    for (const t of this.combatLogContextTexts) t.destroy();
    this.combatLogContextTexts = [];
  }

  private drawCombatLogContextMenu(): void {
    // The menu is drawn in openCombatLogContextMenu and stays static until closed
    // This is called from drawCombatLogPanel to keep it visible
  }

  /** Handle a click inside the context menu. Returns true if consumed. */
  private handleCombatLogContextClick(px: number, py: number): boolean {
    if (!this.combatLogContextMenuOpen) return false;

    const rowH = 20;
    const menuW = 150;
    const menuH = this.CL_FILTER_LABELS.length * rowH + 8;
    const cam = this.cameras.main;
    const menuX = Math.min(this.combatLogContextMenuPos.x, cam.width - menuW - 4);
    const menuY = Math.min(this.combatLogContextMenuPos.y, cam.height - menuH - 4);

    // Check if click is inside menu
    if (px >= menuX && px <= menuX + menuW && py >= menuY && py <= menuY + menuH) {
      // Which row?
      const rowIdx = Math.floor((py - menuY - 4) / rowH);
      if (rowIdx >= 0 && rowIdx < this.CL_FILTER_LABELS.length) {
        const key = this.CL_FILTER_LABELS[rowIdx].key;
        this.combatLogFilters[key] = !this.combatLogFilters[key];
        // Redraw menu
        this.closeCombatLogContextMenu();
        this.openCombatLogContextMenu(this.combatLogContextMenuPos.x, this.combatLogContextMenuPos.y);
      }
      return true;
    }
    // Clicked outside menu — close it
    this.closeCombatLogContextMenu();
    return true;
  }

  private drawChatPanel(): void {
    const cam = this.cameras.main;
    const panelX = this.CHAT_LEFT_X + this.chatOffset.x;
    const panelY = cam.height - this.CHAT_H - this.CHAT_BOTTOM_MARGIN + this.chatOffset.y;

    // Compute effective width: fill the gap between the HUD text and the action bar
    const actionBarTotalW = ACTION_BAR_SLOTS * (this.AB_SLOT_SIZE + this.AB_SLOT_GAP)
      - this.AB_SLOT_GAP + this.AB_PADDING * 2;
    const actionBarStartX = Math.floor((cam.width - actionBarTotalW) / 2);
    const newW = Math.min(this.CHAT_MAX_W, Math.max(180, actionBarStartX - panelX - 8));

    // Update word-wrap on text objects only when width actually changes
    if (newW !== this.chatEffectiveW) {
      this.chatEffectiveW = newW;
      const wrapW = newW - this.CHAT_PAD * 2 - this.CHAT_SCROLLBAR_W;
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

    // Messages — bottom-up layout so word-wrapped lines don't overlap.
    const msgAreaTopY    = panelY + 18;
    const msgAreaBottomY = panelY + this.CHAT_H - this.CHAT_INPUT_H - 4;
    const totalMsgs = this.chatMessages.length;
    const maxScroll = Math.max(0, totalMsgs - this.CHAT_VISIBLE_LINES);
    if (this.chatScrollOffset > maxScroll) this.chatScrollOffset = maxScroll;

    // Pass 1: populate slots (slot 0 = oldest visible, slot N-1 = newest visible).
    const start = Math.max(0, totalMsgs - this.CHAT_VISIBLE_LINES - this.chatScrollOffset);
    for (let i = 0; i < this.CHAT_VISIBLE_LINES; i++) {
      const t = this.chatMessageTexts[i];
      const msgIndex = start + i;
      if (msgIndex < totalMsgs) {
        const msg = this.chatMessages[msgIndex];
        t.setText(msg.text);
        t.setColor(msg.color);
        t.setVisible(true);
      } else {
        t.setText('');
        t.setVisible(false);
      }
    }
    // Pass 2: read actual rendered heights and stack from bottom up.
    let curY = msgAreaBottomY;
    for (let i = this.CHAT_VISIBLE_LINES - 1; i >= 0; i--) {
      const t = this.chatMessageTexts[i];
      if (!t.text) { t.setVisible(false); continue; }
      const h = Math.max(this.CHAT_LINE_H, t.height);
      curY -= h;
      if (curY < msgAreaTopY) {
        t.setVisible(false); // would overflow the top — hide it
      } else {
        t.setPosition(panelX + this.CHAT_PAD, curY);
        t.setVisible(true);
      }
    }

    // Scrollbar
    this.chatScrollGfx.clear();
    this.chatScrollGfx.setPosition(panelX, panelY);
    const sbX = this.chatEffectiveW - this.CHAT_SCROLLBAR_W - 2;
    const sbY = 18;
    const sbH = this.CHAT_H - 18 - this.CHAT_INPUT_H - 4;
    this.chatScrollGfx.fillStyle(0x222233, 0.5);
    this.chatScrollGfx.fillRect(sbX, sbY, this.CHAT_SCROLLBAR_W, sbH);
    if (totalMsgs > this.CHAT_VISIBLE_LINES) {
      const thumbRatio = this.CHAT_VISIBLE_LINES / totalMsgs;
      const thumbH = Math.max(16, sbH * thumbRatio);
      const scrollFraction = maxScroll > 0 ? (maxScroll - this.chatScrollOffset) / maxScroll : 1;
      const thumbY = sbY + (sbH - thumbH) * scrollFraction;
      this.chatScrollGfx.fillStyle(0x446688, 0.8);
      this.chatScrollGfx.fillRoundedRect(sbX, thumbY, this.CHAT_SCROLLBAR_W, thumbH, 3);
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

    } else if (lc.startsWith('/invite ')) {
      const name = raw.slice(8).trim();
      if (name.length === 0) {
        this.pushSystemChat('Usage: /invite PlayerName');
        return;
      }
      this.network.sendPartyInvite(name);

    } else if (lc === '/accept') {
      this.network.sendPartyAccept();

    } else if (lc === '/decline') {
      this.network.sendPartyDecline();

    } else if (lc === '/leave') {
      this.network.sendPartyLeave();

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
      // Adjust scroll offset if we pruned from the top
      if (this.chatScrollOffset > 0) {
        this.chatScrollOffset = Math.max(0, this.chatScrollOffset - 1);
      }
    }
    // Auto-scroll to bottom if already near the bottom
    if (this.chatScrollOffset <= 2) {
      this.chatScrollOffset = 0;
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

      // Slot border — glow green when auto-attack is active on this slot
      const isActiveAutoAttack = skill?.isAutoAttack && this.localAutoAttackActive && this.localAutoAttackSkillId === skillId;
      if (isActiveAutoAttack) {
        this.actionBarGfx.lineStyle(2, 0x44ff44, 1);
      } else {
        this.actionBarGfx.lineStyle(1, skill ? 0x888888 : 0x444466, 1);
      }
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

    // Action-bar slot drag ghost — added directly to scene (visible even when skills pane is closed)
    this.abDragGhostBg = this.add.rectangle(0, 0, 52, 22, 0x000000, 0.85);
    this.abDragGhostBg.setStrokeStyle(1, 0xffcc00);
    this.abDragGhostBg.setScrollFactor(0);
    this.abDragGhostBg.setVisible(false);
    this.abDragGhostBg.setDepth(UI_DEPTH_BASE + 899);

    this.abDragGhost = this.add.text(0, 0, '', {
      fontSize: '11px',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.abDragGhost.setOrigin(0.5, 0.5);
    this.abDragGhost.setScrollFactor(0);
    this.abDragGhost.setVisible(false);
    this.abDragGhost.setDepth(UI_DEPTH_BASE + 900);

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

  // ═══════════════════════════════════════════════════════════
  // LOOT PANEL
  // ═══════════════════════════════════════════════════════════

  private get lootPanelW(): number {
    return this.LOOT_COLS * (this.LOOT_SLOT_SIZE + this.LOOT_SLOT_GAP) + this.LOOT_SLOT_GAP + this.LOOT_PANEL_PAD * 2;
  }

  private get lootPanelH(): number {
    return this.LOOT_ROWS * (this.LOOT_SLOT_SIZE + this.LOOT_SLOT_GAP) + this.LOOT_SLOT_GAP + 70; // 34 title + 36 button area
  }

  private createLootPanel(): void {
    const cam = this.cameras.main;
    this.lootPanelContainer = this.add.container(0, 0);
    this.lootPanelContainer.setScrollFactor(0);
    this.lootPanelContainer.setDepth(UI_DEPTH_BASE + 250);
    this.lootPanelContainer.setVisible(false);

    this.lootPanelGfx = this.add.graphics();
    this.lootPanelContainer.add(this.lootPanelGfx);

    this.lootPanelTitleText = this.add.text(0, 0, 'Loot', {
      fontSize: '13px',
      fontStyle: 'bold',
      color: '#ffcc00',
      stroke: '#000000',
      strokeThickness: 2,
    });
    this.lootPanelContainer.add(this.lootPanelTitleText);

    // "Loot All" button text (positioned in renderLootPanel)
    const lootAllText = this.add.text(0, 0, 'Loot All', {
      fontSize: '12px',
      fontStyle: 'bold',
      color: '#ffffff',
      stroke: '#000000',
      strokeThickness: 2,
    });
    lootAllText.setOrigin(0.5, 0.5);
    this.lootPanelContainer.add(lootAllText);
    (this as any)._lootAllBtnText = lootAllText;

    // Create slot texts and qty texts
    const maxSlots = this.LOOT_COLS * this.LOOT_ROWS;
    for (let i = 0; i < maxSlots; i++) {
      const slotText = this.add.text(0, 0, '', {
        fontSize: '9px',
        color: '#ffffff',
        stroke: '#000000',
        strokeThickness: 1,
      });
      slotText.setOrigin(0.5, 0.5);
      this.lootPanelContainer.add(slotText);
      this.lootPanelSlotTexts.push(slotText);
      this.lootPanelSlotIcons.push(null);

      const qtyText = this.add.text(0, 0, '', {
        fontSize: '9px',
        color: '#ffffff',
        stroke: '#000000',
        strokeThickness: 2,
      });
      qtyText.setOrigin(1, 1);
      this.lootPanelContainer.add(qtyText);
      this.lootPanelQtyTexts.push(qtyText);
    }

    // Register click handlers for loot panel
    this.input.on('pointerdown', (pointer: Phaser.Input.Pointer) => {
      if (!this.lootPanelOpen || !this.currentLootBagId) return;

      // Right-click on loot slot = auto-loot
      if (pointer.button === 2) {
        const slot = this.getLootSlotAt(pointer.x, pointer.y);
        if (slot >= 0) {
          const item = this.lootPanelItems[slot];
          if (item) {
            this.network.sendLootItem(this.currentLootBagId, slot, item.quantity);
          }
        }
        return;
      }

      if (pointer.button === 0) {
        // Shift+left-click on loot slot = inspect item
        if ((pointer.event as MouseEvent).shiftKey) {
          const slot = this.getLootSlotAt(pointer.x, pointer.y);
          if (slot >= 0) {
            const item = this.lootPanelItems[slot];
            if (item) {
              this.showItemDetail(item.itemId);
              return;
            }
          }
        }

        // Left-click: close button
        if (this.isLootPanelCloseBtn(pointer.x, pointer.y)) {
          this.closeLootPanel();
          return;
        }

        // Left-click: Loot All button
        if (this.isLootAllBtn(pointer.x, pointer.y)) {
          this.network.sendLootAll(this.currentLootBagId);
          return;
        }
      }
    });
  }

  private openLootPanel(bagId: string): void {
    const cached = this.lootBagCache.get(bagId);
    if (!cached) return;

    this.currentLootBagId = bagId;
    this.lootPanelOpen = true;
    this.lootPanelContainer.setVisible(true);

    // Auto-open inventory
    if (!this.inventoryOpen) {
      this.toggleInventory();
    }

    // Parse items from the bag state
    this.refreshLootPanelItems(cached);
    this.renderLootPanel();
  }

  private closeLootPanel(): void {
    this.lootPanelOpen = false;
    this.currentLootBagId = null;
    this.lootPanelContainer.setVisible(false);
    this.lootPanelItems = [];
  }

  private refreshLootPanelItems(bag: any): void {
    this.lootPanelItems = [];
    if (bag.items) {
      // Colyseus ArraySchema — iterate with forEach or indexed access
      if (typeof bag.items.forEach === 'function') {
        bag.items.forEach((slot: any) => {
          if (slot && slot.itemId) {
            this.lootPanelItems.push({ itemId: slot.itemId, quantity: slot.quantity ?? 1 });
          }
        });
      } else if (bag.items.length !== undefined) {
        for (let i = 0; i < bag.items.length; i++) {
          const slot = bag.items[i];
          if (slot && slot.itemId) {
            this.lootPanelItems.push({ itemId: slot.itemId, quantity: slot.quantity ?? 1 });
          }
        }
      }
    }

    // If bag is empty, auto-close
    if (this.lootPanelItems.length === 0 && this.lootPanelOpen) {
      this.closeLootPanel();
      return;
    }

    if (this.lootPanelOpen) {
      this.renderLootPanel();
    }
  }

  private renderLootPanel(): void {
    const cam = this.cameras.main;
    const panelW = this.lootPanelW;
    const panelH = this.lootPanelH;

    // Position to the left of the inventory panel
    const px = this.panelStartX + this.invOffset.x - panelW - 8;
    const py = this.panelY + this.invOffset.y;
    this.lootPanelContainer.setPosition(px, py);

    // Draw background
    this.lootPanelGfx.clear();
    this.lootPanelGfx.fillStyle(0x1a1a2e, 0.95);
    this.lootPanelGfx.fillRoundedRect(0, 0, panelW, panelH, 6);
    this.lootPanelGfx.lineStyle(2, 0xccaa44, 0.8);
    this.lootPanelGfx.strokeRoundedRect(0, 0, panelW, panelH, 6);

    // Title bar
    this.lootPanelGfx.fillStyle(0x2a2a3e, 0.9);
    this.lootPanelGfx.fillRect(2, 2, panelW - 4, 28);

    this.lootPanelTitleText.setPosition(this.LOOT_PANEL_PAD, 7);

    // Close button (X) in title bar
    this.lootPanelGfx.fillStyle(0xff4444, 0.7);
    this.lootPanelGfx.fillRect(panelW - 24, 5, 18, 18);
    this.lootPanelGfx.lineStyle(2, 0xffffff, 0.9);
    this.lootPanelGfx.lineBetween(panelW - 20, 9, panelW - 10, 19);
    this.lootPanelGfx.lineBetween(panelW - 10, 9, panelW - 20, 19);

    const maxSlots = this.LOOT_COLS * this.LOOT_ROWS;
    const dm = ClientDataManager.instance;

    // Draw slots
    for (let i = 0; i < maxSlots; i++) {
      const col = i % this.LOOT_COLS;
      const row = Math.floor(i / this.LOOT_COLS);
      const sx = this.LOOT_PANEL_PAD + col * (this.LOOT_SLOT_SIZE + this.LOOT_SLOT_GAP);
      const sy = 34 + this.LOOT_SLOT_GAP + row * (this.LOOT_SLOT_SIZE + this.LOOT_SLOT_GAP);

      const item = this.lootPanelItems[i];

      // Slot background
      if (item) {
        const template = dm.getItem(item.itemId);
        const rarityColor = template ? (RARITY_COLORS[template.rarity] ?? '#333344') : '#333344';
        const hexColor = parseInt(rarityColor.replace('#', ''), 16);
        this.lootPanelGfx.fillStyle(hexColor, 0.25);
      } else {
        this.lootPanelGfx.fillStyle(0x333344, 0.6);
      }
      this.lootPanelGfx.fillRect(sx, sy, this.LOOT_SLOT_SIZE, this.LOOT_SLOT_SIZE);
      this.lootPanelGfx.lineStyle(1, 0x555566, 0.6);
      this.lootPanelGfx.strokeRect(sx, sy, this.LOOT_SLOT_SIZE, this.LOOT_SLOT_SIZE);

      // Destroy old icon if it exists
      if (this.lootPanelSlotIcons[i]) {
        this.lootPanelSlotIcons[i]!.destroy();
        this.lootPanelSlotIcons[i] = null;
      }

      if (item) {
        const template = dm.getItem(item.itemId);
        const iconKey = template?.inventoryIcon ? `icon_${template.inventoryIcon}` : null;

        if (iconKey && this.textures.exists(iconKey)) {
          const iconImg = this.add.image(sx + this.LOOT_SLOT_SIZE / 2, sy + this.LOOT_SLOT_SIZE / 2, iconKey);
          const maxDim = this.LOOT_SLOT_SIZE - 4;
          const scale = Math.min(maxDim / iconImg.width, maxDim / iconImg.height);
          iconImg.setScale(scale);
          this.lootPanelContainer.add(iconImg);
          this.lootPanelSlotIcons[i] = iconImg;
          this.lootPanelSlotTexts[i].setText('');
        } else {
          const abbr = template ? template.name.slice(0, 5) : item.itemId.slice(0, 5);
          this.lootPanelSlotTexts[i].setText(abbr);
          const rarityColor = template ? (RARITY_COLORS[template.rarity] ?? '#ffffff') : '#ffffff';
          this.lootPanelSlotTexts[i].setColor(rarityColor);
        }

        this.lootPanelSlotTexts[i].setPosition(sx + this.LOOT_SLOT_SIZE / 2, sy + this.LOOT_SLOT_SIZE / 2);

        // Quantity badge
        if (item.quantity > 1) {
          this.lootPanelQtyTexts[i].setText(`${item.quantity}`);
          this.lootPanelQtyTexts[i].setPosition(sx + this.LOOT_SLOT_SIZE - 2, sy + this.LOOT_SLOT_SIZE - 2);
        } else {
          this.lootPanelQtyTexts[i].setText('');
        }
      } else {
        this.lootPanelSlotTexts[i].setText('');
        this.lootPanelQtyTexts[i].setText('');
      }
    }

    // "Loot All" button at bottom
    const btnW = 90;
    const btnH = 24;
    const btnX = (panelW - btnW) / 2;
    const btnY = panelH - btnH - 8;
    this.lootPanelGfx.fillStyle(0x44aa44, 0.8);
    this.lootPanelGfx.fillRoundedRect(btnX, btnY, btnW, btnH, 4);
    this.lootPanelGfx.lineStyle(1, 0x66cc66, 0.9);
    this.lootPanelGfx.strokeRoundedRect(btnX, btnY, btnW, btnH, 4);

    // Position the "Loot All" text
    const lootAllText = (this as any)._lootAllBtnText as Phaser.GameObjects.Text;
    if (lootAllText) {
      lootAllText.setPosition(btnX + btnW / 2, btnY + btnH / 2);
    }
  }

  /** Get the loot panel slot at a given screen position (absolute, not offset-adjusted). */
  private getLootSlotAt(screenX: number, screenY: number): number {
    if (!this.lootPanelOpen) return -1;

    // Convert from screen to loot panel local coords
    const px = screenX - this.lootPanelContainer.x;
    const py = screenY - this.lootPanelContainer.y;

    const maxSlots = this.LOOT_COLS * this.LOOT_ROWS;
    for (let i = 0; i < maxSlots; i++) {
      const col = i % this.LOOT_COLS;
      const row = Math.floor(i / this.LOOT_COLS);
      const sx = this.LOOT_PANEL_PAD + col * (this.LOOT_SLOT_SIZE + this.LOOT_SLOT_GAP);
      const sy = 34 + this.LOOT_SLOT_GAP + row * (this.LOOT_SLOT_SIZE + this.LOOT_SLOT_GAP);

      if (px >= sx && px <= sx + this.LOOT_SLOT_SIZE && py >= sy && py <= sy + this.LOOT_SLOT_SIZE) {
        return i;
      }
    }
    return -1;
  }

  /** Check if a screen position is inside the loot panel. */
  private isInsideLootPanel(screenX: number, screenY: number): boolean {
    const px = screenX - this.lootPanelContainer.x;
    const py = screenY - this.lootPanelContainer.y;
    return px >= 0 && px <= this.lootPanelW && py >= 0 && py <= this.lootPanelH;
  }

  /** Check if a screen position hits the loot panel close button. */
  private isLootPanelCloseBtn(screenX: number, screenY: number): boolean {
    const px = screenX - this.lootPanelContainer.x;
    const py = screenY - this.lootPanelContainer.y;
    return px >= this.lootPanelW - 24 && px <= this.lootPanelW - 6 && py >= 5 && py <= 23;
  }

  /** Check if a screen position hits the "Loot All" button. */
  private isLootAllBtn(screenX: number, screenY: number): boolean {
    const px = screenX - this.lootPanelContainer.x;
    const py = screenY - this.lootPanelContainer.y;
    const btnW = 90;
    const btnH = 24;
    const btnX = (this.lootPanelW - btnW) / 2;
    const btnY = this.lootPanelH - btnH - 8;
    return px >= btnX && px <= btnX + btnW && py >= btnY && py <= btnY + btnH;
  }

  // ── Item Detail Panel ────────────────────────────────────────

  /** Create the item detail panel container (hidden on startup). */
  private createItemDetailPanel(): void {
    this.itemDetailContainer = this.add.container(0, 0);
    this.itemDetailContainer.setScrollFactor(0);
    this.itemDetailContainer.setDepth(UI_DEPTH_BASE + 300);
    this.itemDetailContainer.setVisible(false);

    this.itemDetailGfx = this.add.graphics();
    this.itemDetailContainer.add(this.itemDetailGfx);
  }

  /**
   * Populate and show the item detail panel for the given item ID.
   * Shift+clicking an inventory or equipment slot calls this.
   */
  private showItemDetail(itemId: string): void {
    const template = ClientDataManager.instance.getItem(itemId);
    if (!template) return;

    // Destroy any previously created dynamic objects
    for (const obj of this._itemDetailDynamic) {
      obj.destroy();
    }
    this._itemDetailDynamic = [];
    this.itemDetailGfx.clear();

    const PANEL_W = 224;
    const PADDING = 12;
    const TITLE_H = 34;
    const LINE_H = 18;
    const rarityColor = RARITY_COLORS[template.rarity as keyof typeof RARITY_COLORS] ?? '#cccccc';

    // Build stat bonus lines
    const statLines: string[] = [];

    // Weapon-specific stats — shown first, before generic stat bonuses
    if (template.equipSlot === 'weapon') {
      if (template.attackDamage) {
        statLines.push(`+${template.attackDamage} Attack Damage`);
      }
      if (template.attackSpeedMs) {
        const speedSec = (template.attackSpeedMs / 1000).toFixed(2);
        statLines.push(`${speedSec}s Attack Speed`);
      }
    }

    if (template.statBonuses) {
      const STAT_LABELS: Record<string, string> = {
        hp: 'HP',
        mana: 'Mana',
        strength: 'Strength',
        stamina: 'Stamina',
        dexterity: 'Dexterity',
        intelligence: 'Intelligence',
        wisdom: 'Wisdom',
        physicalResist: 'Phys Resist',
        spellResist: 'Spell Resist',
        critChance: 'Crit Chance',
        critDamage: 'Crit Damage',
        physicalDefense: 'Phys Defense',
        blockRating: 'Block',
        dodgeRating: 'Dodge',
      };
      const PCT_STATS = new Set(['critChance', 'critDamage', 'blockRating', 'dodgeRating', 'physicalResist', 'spellResist']);
      for (const [key, val] of Object.entries(template.statBonuses)) {
        if (val === undefined || val === 0) continue;
        const label = STAT_LABELS[key] ?? key;
        const numVal = val as number;
        const isPct = PCT_STATS.has(key);
        const display = isPct ? `+${(numVal * 100).toFixed(1)}%` : `+${numVal}`;
        statLines.push(`${display} ${label}`);
      }
    }

    // Calculate panel height dynamically based on content
    const DESC_WRAP_W = PANEL_W - PADDING * 2;
    // Rough estimate: 6.5px average char width at 11px font
    const charsPerLine = Math.floor(DESC_WRAP_W / 6.5);
    const descLineCount = Math.ceil(template.description.length / charsPerLine) + 1;
    const descH = Math.max(descLineCount * 15, 15);
    const hasDivider = statLines.length > 0;

    let PANEL_H =
      TITLE_H +
      PADDING + LINE_H +          // rarity / category row
      PADDING / 2 + descH +       // description
      (hasDivider ? PADDING + 1 : 0) +
      statLines.length * LINE_H +
      PADDING;
    PANEL_H = Math.max(PANEL_H, 100);

    // Draw panel background with gold border
    this.itemDetailGfx.fillStyle(0x1a1a2e, 0.97);
    this.itemDetailGfx.fillRoundedRect(0, 0, PANEL_W, PANEL_H, 6);
    this.itemDetailGfx.lineStyle(2, 0xccaa44, 0.9);
    this.itemDetailGfx.strokeRoundedRect(0, 0, PANEL_W, PANEL_H, 6);

    // Title bar background
    this.itemDetailGfx.fillStyle(0x111122, 0.9);
    this.itemDetailGfx.fillRoundedRect(1, 1, PANEL_W - 2, TITLE_H - 2, { tl: 5, tr: 5, bl: 0, br: 0 });

    // Item name in rarity colour
    const titleText = this.add.text(PADDING, TITLE_H / 2, template.name, {
      fontSize: '13px',
      fontStyle: 'bold',
      color: rarityColor,
      stroke: '#000000',
      strokeThickness: 2,
    });
    titleText.setOrigin(0, 0.5);
    this.itemDetailContainer.add(titleText);
    this._itemDetailDynamic.push(titleText);

    // Close [✕] button — rendered as plain text; click is handled via scene-level
    // pointerdown + isItemDetailCloseBtn() to avoid the Phaser Container input bug.
    const closeBtn = this.add.text(PANEL_W - PADDING, TITLE_H / 2, '✕', {
      fontSize: '13px',
      color: '#aaaaaa',
      stroke: '#000000',
      strokeThickness: 1,
    });
    closeBtn.setOrigin(1, 0.5);
    this.itemDetailContainer.add(closeBtn);
    this._itemDetailDynamic.push(closeBtn);

    // Rarity + category + equip-slot line
    let curY = TITLE_H + PADDING;
    const rarityLabel = template.rarity.charAt(0).toUpperCase() + template.rarity.slice(1);
    const categoryLabel = template.category.charAt(0).toUpperCase() + template.category.slice(1);
    const slotLabel = template.equipSlot
      ? ` — ${template.equipSlot.charAt(0).toUpperCase() + template.equipSlot.slice(1)}`
      : '';
    const badgeText = this.add.text(PADDING, curY, `${rarityLabel} ${categoryLabel}${slotLabel}`, {
      fontSize: '11px',
      fontStyle: 'italic',
      color: rarityColor,
      stroke: '#000000',
      strokeThickness: 1,
    });
    this.itemDetailContainer.add(badgeText);
    this._itemDetailDynamic.push(badgeText);
    curY += LINE_H + PADDING / 2;

    // Description (word-wrapped)
    const descText = this.add.text(PADDING, curY, template.description, {
      fontSize: '11px',
      color: '#cccccc',
      wordWrap: { width: DESC_WRAP_W },
      lineSpacing: 2,
    });
    this.itemDetailContainer.add(descText);
    this._itemDetailDynamic.push(descText);
    curY += descText.height + PADDING / 2;

    // Stat bonuses section
    if (statLines.length > 0) {
      // Divider line
      this.itemDetailGfx.lineStyle(1, 0x444466, 0.9);
      this.itemDetailGfx.lineBetween(PADDING, curY, PANEL_W - PADDING, curY);
      curY += PADDING / 2 + 2;

      for (const line of statLines) {
        const statText = this.add.text(PADDING, curY, line, {
          fontSize: '11px',
          color: '#55ff77',
          stroke: '#000000',
          strokeThickness: 1,
        });
        this.itemDetailContainer.add(statText);
        this._itemDetailDynamic.push(statText);
        curY += LINE_H;
      }
    }

    // Position to the right of the inventory panel, aligned to its top edge
    const rightEdge = this.panelStartX + this.invOffset.x + this.totalPanelW + 8;
    const topY = this.panelY + this.invOffset.y;
    this.itemDetailContainer.setPosition(rightEdge, topY);

    this.itemDetailPanelOpen = true;
    this.itemDetailContainer.setVisible(true);
  }

  /** Returns true if the screen position is over the item detail panel's close button. */
  private isItemDetailCloseBtn(screenX: number, screenY: number): boolean {
    const px = screenX - this.itemDetailContainer.x;
    const py = screenY - this.itemDetailContainer.y;
    // Hit area covers the right ~32px of the 34px-tall title bar
    return px >= 192 && px <= 226 && py >= 2 && py <= 32;
  }

  /** Hide the item detail panel and clean up its dynamic objects. */
  private closeItemDetailPanel(): void {
    this.itemDetailPanelOpen = false;
    this.itemDetailContainer.setVisible(false);
    for (const obj of this._itemDetailDynamic) {
      obj.destroy();
    }
    this._itemDetailDynamic = [];
    this.itemDetailGfx.clear();
  }
}
