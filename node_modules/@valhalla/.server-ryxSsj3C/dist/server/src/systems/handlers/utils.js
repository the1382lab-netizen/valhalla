/**
 * Shared utility functions for skill effect handlers.
 */
/**
 * Read a stat value from the player's resolved stat block.
 */
export function getStatValue(player, statName) {
    if (!player.stats)
        return 0;
    return player.stats[statName] ?? 0;
}
//# sourceMappingURL=utils.js.map