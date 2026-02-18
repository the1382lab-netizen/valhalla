// ── Input ───────────────────────────────────────────────────
export interface InputPayload {
  /** Movement direction flags */
  up: boolean;
  down: boolean;
  left: boolean;
  right: boolean;
  /** Aim angle in radians (0 = right, PI/2 = down) */
  aimAngle: number;
  /** Monotonically increasing input sequence number */
  seq: number;
  /** Fire a ranged projectile */
  fire: boolean;
  /** Melee attack */
  melee: boolean;
}

// ── Player State (mirrored in Colyseus schema) ─────────────
export interface IPlayerState {
  id: string;
  x: number;
  y: number;
  aimAngle: number;
  speed: number;
  hp: number;
  maxHp: number;
  mana: number;
  maxMana: number;
  alive: boolean;
  inputSeq: number;
  classId: string;
  level: number;
  xp: number;
}

// ── Tile Map ────────────────────────────────────────────────
export interface TileMapData {
  width: number;
  height: number;
  tileSize: number;
  /** 1D collision grid: 0 = passable, 1 = blocked */
  collisionGrid: number[];
}

// ── Projectile State ────────────────────────────────────────
export interface IProjectileState {
  id: string;
  ownerId: string;
  x: number;
  y: number;
  angle: number;
  speed: number;
  damage: number;
}

// ── Messages ────────────────────────────────────────────────
export enum MessageType {
  INPUT = 'input',
  PLAYER_HIT = 'playerHit',
  PLAYER_DIED = 'playerDied',
  PLAYER_RESPAWNED = 'playerRespawned',
  MELEE_ATTACK = 'meleeAttack',
  MISSED = 'missed',
  DODGED = 'dodged',
  BLOCKED = 'blocked',
  INVENTORY_ACTION = 'inventoryAction',
  EQUIP_ITEM = 'equipItem',
  UNEQUIP_ITEM = 'unequipItem',
  DROP_ITEM = 'dropItem',
  SWAP_INVENTORY = 'swapInventory',
  MAP_DATA = 'mapData',
  ZONE_CHANGE = 'zoneChange',

  // ── Skills & Spells ──
  CAST_SKILL = 'castSkill',
  CANCEL_CAST = 'cancelCast',
  SKILL_STARTED = 'skillStarted',
  SKILL_EFFECT = 'skillEffect',
  SKILL_FAILED = 'skillFailed',
  SKILL_INTERRUPTED = 'skillInterrupted',
  BUFF_APPLIED = 'buffApplied',
  BUFF_REMOVED = 'buffRemoved',
  SET_ACTION_BAR = 'setActionBar',
  ACTION_BAR_DATA = 'actionBarData',

  // ── NPC Combat ──
  NPC_HIT = 'npcHit',
  NPC_DIED = 'npcDied',

  // ── Chat ──
  CHAT_MESSAGE = 'chatMessage',
}

// ── Chat ─────────────────────────────────────────────────────
export interface ChatMessagePayload {
  /** Channel the message was sent on. 'system' is server-generated. */
  channel: 'general' | 'world' | 'whisper' | 'system';
  /** Display name of the sender (characterName). Empty for system messages. */
  senderName: string;
  /** The message text (already validated & trimmed by the server). */
  message: string;
  /** Whisper only: recipient's character name. */
  targetName?: string;
  /** Unix ms timestamp set by server. */
  timestamp: number;
}
