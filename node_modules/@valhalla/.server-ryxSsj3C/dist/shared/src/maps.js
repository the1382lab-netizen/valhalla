/**
 * Map & Zone types for the Tiled map system.
 * Shared between server and client.
 */
// ── Zone identifiers ─────────────────────────────────────────
export var ZoneId;
(function (ZoneId) {
    ZoneId["GRASSLANDS"] = "grasslands";
    ZoneId["CAVE_DUNGEON"] = "cave_dungeon";
})(ZoneId || (ZoneId = {}));
/** Registry of all zones. */
export const ZONE_REGISTRY = {
    [ZoneId.GRASSLANDS]: {
        id: ZoneId.GRASSLANDS,
        name: 'Grasslands',
        mapFile: 'grasslands.json',
        defaultSpawn: { x: 10 * 64 + 32, y: 10 * 64 + 32 },
    },
    [ZoneId.CAVE_DUNGEON]: {
        id: ZoneId.CAVE_DUNGEON,
        name: 'Cave Dungeon',
        mapFile: 'cave_dungeon.json',
        defaultSpawn: { x: 3 * 64 + 32, y: 3 * 64 + 32 },
    },
};
//# sourceMappingURL=maps.js.map