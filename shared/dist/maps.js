/**
 * Map & Zone types for the Tiled map system.
 * Shared between server and client.
 */
// ── Zone identifiers ─────────────────────────────────────────
export var ZoneId;
(function (ZoneId) {
    ZoneId["GRASSLANDS"] = "grasslands";
    ZoneId["DESERT"] = "desert";
    ZoneId["CAVE_DUNGEON"] = "cave_dungeon";
    ZoneId["GRASSLANDS_V2"] = "grasslands_v2";
})(ZoneId || (ZoneId = {}));
/** Registry of all zones. */
export const ZONE_REGISTRY = {
    [ZoneId.GRASSLANDS]: {
        id: ZoneId.GRASSLANDS,
        name: 'Grasslands',
        mapFile: 'grasslands.json',
        defaultSpawn: { x: 10 * 64 + 32, y: 10 * 64 + 32 },
    },
    [ZoneId.DESERT]: {
        id: ZoneId.DESERT,
        name: 'Scorched Desert',
        mapFile: 'desert.json',
        defaultSpawn: { x: 2 * 64 + 32, y: 57 * 64 + 32 },
    },
    [ZoneId.CAVE_DUNGEON]: {
        id: ZoneId.CAVE_DUNGEON,
        name: 'Greyfell Cave',
        mapFile: 'cave_dungeon.json',
        // The entrance hall, at the south end of the carved interior. The old
        // (3,3) spawn predates maps/cave_dungeon.json existing at all — that tile
        // is solid rock now.
        defaultSpawn: { x: 32 * 64 + 32, y: 59 * 64 + 32 },
    },
    [ZoneId.GRASSLANDS_V2]: {
        id: ZoneId.GRASSLANDS_V2,
        name: 'Eldmoor Grasslands',
        mapFile: 'grasslands_v2.json',
        // The main street of Eldmoor, just south of the market square.
        defaultSpawn: { x: 34 * 64 + 32, y: 28 * 64 + 32 },
    },
};
//# sourceMappingURL=maps.js.map