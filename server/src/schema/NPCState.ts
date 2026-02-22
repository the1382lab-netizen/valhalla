import { Schema, ArraySchema, defineTypes } from '@colyseus/schema';
import type { ActiveBuff } from './PlayerState.js';

// ── NPC Buff Info (synced to clients for target pane display) ─────────────

/**
 * Lightweight buff descriptor synced to clients so they can display active
 * debuffs on the enemy target pane (e.g. Poison Blade DoT pill).
 */
export class NpcBuffInfo extends Schema {
  skillId: string = '';
  /** Server timestamp (ms) when this buff expires */
  expiresAt: number = 0;
  /** DoT damage per second (0 = not a DoT) */
  dotDamagePerSec: number = 0;
}

defineTypes(NpcBuffInfo, {
  skillId: 'string',
  expiresAt: 'float64',
  dotDamagePerSec: 'float32',
});

/**
 * Synced NPC/Enemy state. Sent to all clients for rendering.
 */
export class NPCState extends Schema {
  /** Unique instance ID (e.g., "npc_grasslands_0") */
  id: string = '';
  /** Template ID (references NPCTemplate from DataManager) */
  templateId: string = '';
  /** Display name */
  name: string = '';
  /** "enemy" or "npc" */
  npcType: string = 'enemy';
  /** Current zone */
  zoneId: string = '';
  /** Position */
  x: number = 0;
  y: number = 0;
  /** Current HP */
  hp: number = 0;
  /** Max HP */
  maxHp: number = 0;
  /** Level */
  level: number = 1;
  /** Is alive */
  alive: boolean = true;
  /** Sprite rendering */
  spriteColor: number = 0xff0000;
  spriteSize: number = 24;
  /** Aim angle (for facing direction) */
  aimAngle: number = 0;

  // ── Synced buff display (for client target pane) ─────────────────────────
  /** Active debuffs/buffs visible to clients — kept in sync by applyNpcBuff / tickNpcBuffs */
  syncedBuffs: ArraySchema<NpcBuffInfo> = new ArraySchema<NpcBuffInfo>();

  // ── Server-only buff tracking (NOT synced) ───────────────────────────────
  /** Full buff data for server-side ticking (mirrors PlayerState.activeBuffs pattern) */
  activeBuffs: ActiveBuff[] = [];
}

defineTypes(NPCState, {
  id: 'string',
  templateId: 'string',
  name: 'string',
  npcType: 'string',
  zoneId: 'string',
  x: 'float32',
  y: 'float32',
  hp: 'float32',
  maxHp: 'float32',
  level: 'uint8',
  alive: 'boolean',
  spriteColor: 'uint32',
  spriteSize: 'uint8',
  aimAngle: 'float32',
  syncedBuffs: [NpcBuffInfo],
});
