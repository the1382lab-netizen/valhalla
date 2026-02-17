import { Schema, ArraySchema, defineTypes } from '@colyseus/schema';
import { PLAYER_MAX_HP } from '@valhalla/shared';
import type { ResolvedStats } from '@valhalla/shared';

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

  // ── Vitals (synced) ──────────────────────────────────
  hp: number = PLAYER_MAX_HP;
  maxHp: number = PLAYER_MAX_HP;
  mana: number = 0;
  maxMana: number = 0;
  alive: boolean = true;

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
  hp: 'int16',
  maxHp: 'int16',
  mana: 'int16',
  maxMana: 'int16',
  alive: 'boolean',
  inputSeq: 'uint32',
  equipWeapon: 'string',
  equipHelm: 'string',
  equipChest: 'string',
  equipLegs: 'string',
  equipBoots: 'string',
  equipRing: 'string',
  inventory: [InventorySlotState],
});
