/**
 * Shared utility functions for skill effect handlers.
 */

import { PlayerState } from '../../schema/PlayerState.js';

/**
 * Read a stat value from the player's resolved stat block.
 */
export function getStatValue(player: PlayerState, statName: string): number {
  if (!player.stats) return 0;
  return (player.stats as any)[statName] ?? 0;
}
