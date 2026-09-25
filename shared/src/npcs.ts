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
  /**
   * `enemy`: hostile, can be attacked. `npc`: friendly — never aggroes, cannot
   * be attacked or picked as an attack target.
   * This is the NPC's hostility; there is no separate hostility field.
   */
  type: 'enemy' | 'npc';
  /**
   * Optional job for a friendly NPC, e.g. `vendor`, `guard`, `innkeeper`
   * (B-06). Free text for now; vendor trading will read it later.
   */
  role?: string;
  level: number;
  stats: Partial<StatBlock>;
  hp: number;
  mana?: number;
  lootTableId?: string;
  behaviorType: 'passive' | 'aggressive' | 'patrol' | 'stationary' | 'fleeing';
  /** Whether this NPC can aggro. Defaults to true for enemies, false for friendly NPCs. A friendly NPC never aggroes, whatever this says. */
  canAggro?: boolean;
  /**
   * Social aggro (B-10): when this NPC picks up a target it calls for help, and
   * every NPC of the same social group within socialRange that can see it and
   * isn't already fighting joins in (and calls its own neighbours if it has
   * this on too). Off by default. Only meaningful when the NPC can aggro.
   */
  canSocialAggro?: boolean;
  /** Who answers the call. NPCs with the same group help each other; blank means this template only. */
  socialGroup?: string;
  /** How far the call reaches (same units as aggroRange). Blank or 0 means aggroRange. */
  socialRange?: number;
  aggroRange?: number;
  /** Max distance from spawn before NPC resets (default: aggroRange * 3). */
  leashRange?: number;
  /** Base attack damage (default: 5). Every hit, unless minDamage/maxDamage set a range. */
  damage?: number;
  /** Valhalla 2.0 damage roll: each hit rolls uniformly in [minDamage, maxDamage]. */
  minDamage?: number;
  maxDamage?: number;
  /**
   * Item id (items.json) of the weapon this NPC carries. It shows in the NPC's
   * hand, picks its attack animation, and each hit adds the weapon's own
   * [minDamage, maxDamage] roll on top of the NPC's damage roll.
   */
  weaponId?: string;
  /**
   * How the auto-attack reaches its target (default: melee). A ranged NPC stops
   * chasing once the target is in attackRange and in sight, and shoots from
   * there; walls and sight blockers stop the shot. Give it a ranged weapon so it
   * plays the shot animation.
   */
  attackType?: 'melee' | 'ranged';
  /** Milliseconds between attacks (default: 1500). */
  attackSpeed?: number;
  /** Distance in px the NPC must be within to attack (default: 40 melee, 600 ranged). */
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
