/**
 * NPC & Enemy Template definitions.
 * Used by the game editor to create NPC/enemy content
 * and by the server to spawn and manage NPCs.
 */

import type { StatBlock } from './classes.js';

export interface NPCTemplate {
  id: string;
  name: string;
  description: string;
  type: 'enemy' | 'npc';
  level: number;
  stats: Partial<StatBlock>;
  hp: number;
  mana?: number;
  lootTableId?: string;
  behaviorType: 'passive' | 'aggressive' | 'patrol' | 'stationary' | 'fleeing';
  /** Whether this NPC can aggro. Defaults to true for enemies, false for friendly NPCs. */
  canAggro?: boolean;
  aggroRange?: number;
  /** Max distance from spawn before NPC resets (default: aggroRange * 3). */
  leashRange?: number;
  /** Base attack damage (default: 5). Every hit, unless minDamage/maxDamage set a range. */
  damage?: number;
  /** Valhalla 2.0 damage roll: each hit rolls uniformly in [minDamage, maxDamage]. */
  minDamage?: number;
  maxDamage?: number;
  /** Milliseconds between attacks (default: 1500). */
  attackSpeed?: number;
  /** Distance in px the NPC must be within to attack (default: 40). */
  attackRange?: number;
  /** Movement speed in px/sec when chasing (default: 60). */
  moveSpeed?: number;
  respawnMs: number;
  skills: string[];
  spriteColor: number;
  spriteSize: number;
  xpReward: number;
  dialogue?: string[];
  vendorInventory?: string[];
}
