import { Schema, defineTypes } from '@colyseus/schema';

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
});
