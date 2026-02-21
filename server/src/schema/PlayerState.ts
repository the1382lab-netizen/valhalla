import { Schema, ArraySchema, defineTypes } from '@colyseus/schema';
import { PLAYER_MAX_HP, SkillId } from '@valhalla/shared';
import type { ResolvedStats } from '@valhalla/shared';

// ── Active Buff (server-only tracking) ─────────────────────
export interface ActiveBuff {
  skillId: string;
  casterId: string;
  appliedAt: number;     // timestamp
  expiresAt: number;     // timestamp
  dotDamagePerSec?: number;
  hotHealPerSec?: number;
  /** Generic effect data — specific handlers interpret this */
  effectData?: Record<string, any>;
}

// ── Inventory Slot Schema (synced to client) ─────────────────
export class InventorySlotState extends Schema {
  itemId: string = '';
  quantity: number = 1;
}

defineTypes(InventorySlotState, {
  itemId: 'string',
  quantity: 'uint8',
});

export class PlayerState extends Schema {
  id: string = '';
  x: number = 0;
  y: number = 0;
  aimAngle: number = 0;
  speed: number = 0;

  // ── Identity & Progression (synced) ─────────────────────
  characterName: string = '';
  classId: string = 'warrior';
  level: number = 1;
  xp: number = 0;
  zoneId: string = 'grasslands';

  // ── Vitals (synced) ──────────────────────────────────
  hp: number = PLAYER_MAX_HP;
  maxHp: number = PLAYER_MAX_HP;
  mana: number = 0;
  maxMana: number = 0;
  energy: number = 0;
  maxEnergy: number = 0;
  alive: boolean = true;

  // ── Shield (synced) ── remaining absorption from Shield of Faith etc.
  shieldHp: number = 0;

  // ── Casting State (synced for cast bar) ────────────
  castingSkillId: string = '';
  castingStartedAt: number = 0;
  castingDurationMs: number = 0;

  // ── Equipment (synced) — empty string = nothing equipped ──
  equipWeapon: string = '';
  equipHelm: string = '';
  equipChest: string = '';
  equipLegs: string = '';
  equipBoots: string = '';
  equipRing: string = '';

  // ── Inventory (synced) ───────────────────────────────
  inventory: ArraySchema<InventorySlotState> = new ArraySchema<InventorySlotState>();

  // ── Server-only (not synced) ──────────────────────────
  inputSeq: number = 0;
  fireCooldown: number = 0;
  meleeCooldown: number = 0;
  invulnerableUntil: number = 0;
  respawnAt: number = 0;

  // ── Skill Server-only State ───────────────────────────
  /** Skill cooldowns: skillId → timestamp when cooldown expires */
  skillCooldowns: Map<string, number> = new Map();
  /** Active buffs on this player */
  activeBuffs: ActiveBuff[] = [];
  /** Action bar skill assignments (8 slots) */
  actionBar: string[] = ['', '', '', '', '', '', '', ''];

  /**
   * Full resolved stat block — server-only, used for combat math.
   * Recomputed on join, level-up, and (later) gear changes.
   * NOT a Colyseus schema property — just a plain object.
   */
  stats: ResolvedStats | null = null;
}

defineTypes(PlayerState, {
  id: 'string',
  x: 'float32',
  y: 'float32',
  aimAngle: 'float32',
  speed: 'float32',
  characterName: 'string',
  classId: 'string',
  level: 'uint8',
  xp: 'uint16',
  zoneId: 'string',
  hp: 'int16',
  maxHp: 'int16',
  mana: 'int16',
  maxMana: 'int16',
  energy: 'float32',
  maxEnergy: 'int16',
  alive: 'boolean',
  shieldHp: 'int16',
  castingSkillId: 'string',
  castingStartedAt: 'float64',
  castingDurationMs: 'uint16',
  inputSeq: 'uint32',
  equipWeapon: 'string',
  equipHelm: 'string',
  equipChest: 'string',
  equipLegs: 'string',
  equipBoots: 'string',
  equipRing: 'string',
  inventory: [InventorySlotState],
});

// ── Shield Absorption Helper ─────────────────────────────────

/**
 * Apply incoming damage against the player's shield (shieldHp) first.
 * Returns the remaining damage that should be applied to the player's HP.
 * When the shield is fully depleted, removes the Shield of Faith buff and
 * resets shieldHp to 0.
 */
export function applyShieldAbsorption(player: PlayerState, incomingDamage: number): number {
  if (player.shieldHp <= 0) return incomingDamage;

  const absorbed = Math.min(player.shieldHp, incomingDamage);
  player.shieldHp -= absorbed;

  if (player.shieldHp <= 0) {
    player.shieldHp = 0;
    // Remove the shield buff so SkillSystem doesn't double-expire it
    player.activeBuffs = player.activeBuffs.filter(
      b => b.skillId !== SkillId.CLERIC_SHIELD_OF_FAITH,
    );
  }

  return Math.max(0, incomingDamage - absorbed);
}
