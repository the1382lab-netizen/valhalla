/**
 * Admin REST API for the Game Editor's live server dashboard.
 *
 * GET  /api/admin/state          — snapshot of all zones, players, NPCs, loot bags
 * POST /api/admin/spawn-npc      — spawn an NPC at a position
 * POST /api/admin/drop-item      — drop an item as a loot bag
 * POST /api/admin/kick-player    — disconnect a player
 * POST /api/admin/teleport-player — move a player to a zone/position
 * POST /api/admin/kill-npc       — kill an NPC (will respawn normally)
 * POST /api/admin/respawn-npc    — force-respawn a dead NPC
 * POST /api/admin/delete-npc     — permanently remove an NPC (no respawn)
 */
export declare const adminRouter: import("express-serve-static-core").Router;
//# sourceMappingURL=admin.d.ts.map