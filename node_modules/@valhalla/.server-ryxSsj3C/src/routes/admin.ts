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

import { Router } from 'express';
import { getActiveGameRoom } from '../rooms/GameRoom.js';
import type { GameRoom } from '../rooms/GameRoom.js';

export const adminRouter = Router();

// ── Helpers ─────────────────────────────────────────────────

/**
 * Retrieve the single active GameRoom instance.
 * Uses a direct module-level reference set in GameRoom.onCreate().
 */
function getGameRoom(): GameRoom | null {
  return getActiveGameRoom();
}

// ── GET /state — full snapshot ──────────────────────────────

adminRouter.get('/state', async (_req, res) => {
  const room = await getGameRoom();
  if (!room) {
    return res.json({
      online: false,
      zones: {},
      totalPlayers: 0,
      totalNpcs: 0,
      totalLootBags: 0,
    });
  }

  const state = (room as any).state;

  // Group entities by zone
  const zones: Record<string, {
    players: any[];
    npcs: any[];
    lootBags: any[];
  }> = {};

  const ensureZone = (zoneId: string) => {
    if (!zones[zoneId]) zones[zoneId] = { players: [], npcs: [], lootBags: [] };
  };

  // Players
  state.players.forEach((p: any, sessionId: string) => {
    const zoneId = p.zoneId || 'unknown';
    ensureZone(zoneId);
    zones[zoneId].players.push({
      sessionId,
      name: p.characterName,
      classId: p.classId,
      level: p.level,
      x: Math.round(p.x),
      y: Math.round(p.y),
      hp: Math.round(p.hp),
      maxHp: Math.round(p.maxHp),
      mana: Math.round(p.mana),
      maxMana: Math.round(p.maxMana),
      alive: p.alive,
    });
  });

  // NPCs
  state.npcs.forEach((n: any, id: string) => {
    const zoneId = n.zoneId || 'unknown';
    ensureZone(zoneId);
    zones[zoneId].npcs.push({
      id,
      templateId: n.templateId,
      name: n.name,
      npcType: n.npcType,
      level: n.level,
      x: Math.round(n.x),
      y: Math.round(n.y),
      hp: Math.round(n.hp),
      maxHp: Math.round(n.maxHp),
      alive: n.alive,
    });
  });

  // Loot Bags
  state.lootBags.forEach((b: any, id: string) => {
    const zoneId = b.zoneId || 'unknown';
    ensureZone(zoneId);
    const items: any[] = [];
    for (let i = 0; i < b.items.length; i++) {
      const slot = b.items[i];
      if (slot.itemId) items.push({ itemId: slot.itemId, quantity: slot.quantity });
    }
    zones[zoneId].lootBags.push({
      id,
      x: Math.round(b.x),
      y: Math.round(b.y),
      itemCount: items.length,
      items,
    });
  });

  let totalPlayers = 0;
  let totalNpcs = 0;
  let totalLootBags = 0;
  for (const z of Object.values(zones)) {
    totalPlayers += z.players.length;
    totalNpcs += z.npcs.length;
    totalLootBags += z.lootBags.length;
  }

  res.json({
    online: true,
    zones,
    totalPlayers,
    totalNpcs,
    totalLootBags,
  });
});

// ── POST /spawn-npc ─────────────────────────────────────────

adminRouter.post('/spawn-npc', async (req, res) => {
  const { templateId, zoneId, x, y } = req.body;
  if (!templateId || !zoneId || x == null || y == null) {
    return res.status(400).json({ error: 'templateId, zoneId, x, y required' });
  }

  const room = await getGameRoom();
  if (!room) return res.status(503).json({ error: 'Game server not running' });

  try {
    // Access NPCSystem and DataManager through the room
    const npcSystem = (room as any).npcSystem;
    const dm = (await import('../systems/DataManager.js')).DataManager.instance;
    const template = dm.npcTemplates[templateId];
    if (!template) return res.status(404).json({ error: `Unknown template: ${templateId}` });

    const { NPCState } = await import('../schema/NPCState.js');
    const npc = new NPCState();
    const npcId = `npc_admin_${Date.now()}_${Math.floor(Math.random() * 1000)}`;
    npc.id = npcId;
    npc.templateId = templateId;
    npc.name = template.name;
    npc.npcType = template.type || 'enemy';
    npc.zoneId = zoneId;
    npc.x = x;
    npc.y = y;
    npc.hp = template.hp;
    npc.maxHp = template.hp;
    npc.level = template.level ?? 1;
    npc.alive = true;
    npc.spriteColor = template.spriteColor ?? 0xff0000;
    npc.spriteSize = template.spriteSize ?? 24;

    // Add to game state so clients see it
    const state = (room as any).state;
    state.npcs.set(npcId, npc);

    // Register with NPCSystem for AI behavior
    npcSystem.registerAdminNPC(npcId, npc, x, y, template);

    res.json({ ok: true, npcId });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /drop-item ─────────────────────────────────────────

adminRouter.post('/drop-item', async (req, res) => {
  const { itemId, zoneId, x, y, quantity } = req.body;
  if (!itemId || !zoneId || x == null || y == null) {
    return res.status(400).json({ error: 'itemId, zoneId, x, y required' });
  }

  const room = await getGameRoom();
  if (!room) return res.status(503).json({ error: 'Game server not running' });

  try {
    const lootBagSystem = (room as any).lootBagSystem;
    const state = (room as any).state;
    const bagId = lootBagSystem.spawnBag(
      zoneId, x, y,
      [{ itemId, quantity: quantity ?? 1 }],
      state.lootBags,
    );
    res.json({ ok: true, bagId });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /kick-player ───────────────────────────────────────

adminRouter.post('/kick-player', async (req, res) => {
  const { sessionId } = req.body;
  if (!sessionId) return res.status(400).json({ error: 'sessionId required' });

  const room = await getGameRoom();
  if (!room) return res.status(503).json({ error: 'Game server not running' });

  try {
    const client = room.clients.find((c: any) => c.sessionId === sessionId);
    if (!client) return res.status(404).json({ error: 'Player not found' });

    client.send('kicked', { reason: 'Kicked by admin.' });
    client.leave(4002);
    res.json({ ok: true });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /teleport-player ───────────────────────────────────

adminRouter.post('/teleport-player', async (req, res) => {
  const { sessionId, zoneId, x, y } = req.body;
  if (!sessionId || !zoneId || x == null || y == null) {
    return res.status(400).json({ error: 'sessionId, zoneId, x, y required' });
  }

  const room = await getGameRoom();
  if (!room) return res.status(503).json({ error: 'Game server not running' });

  try {
    const state = (room as any).state;
    const player = state.players.get(sessionId);
    if (!player) return res.status(404).json({ error: 'Player not found' });

    const currentZone = player.zoneId;
    player.x = x;
    player.y = y;

    // If changing zone, update zoneId and send new map data
    if (zoneId !== currentZone) {
      player.zoneId = zoneId;
      const mapManager = (room as any).mapManager;
      // Ensure zone is cached
      (room as any).loadZoneCache(zoneId);
      const mapPayload = mapManager.getMapDataForClient(zoneId);
      const client = room.clients.find((c: any) => c.sessionId === sessionId);
      if (client) {
        const { MessageType } = await import('@valhalla/shared');
        client.send(MessageType.MAP_DATA, mapPayload);
        client.send(MessageType.ZONE_CHANGE, { zoneId, spawnX: x, spawnY: y });
      }
    }

    res.json({ ok: true });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /kill-npc ──────────────────────────────────────────

adminRouter.post('/kill-npc', async (req, res) => {
  const { npcId } = req.body;
  if (!npcId) return res.status(400).json({ error: 'npcId required' });

  const room = await getGameRoom();
  if (!room) return res.status(503).json({ error: 'Game server not running' });

  try {
    const state = (room as any).state;
    const npc = state.npcs.get(npcId);
    if (!npc) return res.status(404).json({ error: 'NPC not found' });

    npc.alive = false;
    npc.hp = 0;

    // Let NPCSystem handle respawn timer
    const npcSystem = (room as any).npcSystem;
    npcSystem.adminKill(npcId);

    res.json({ ok: true });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /respawn-npc ───────────────────────────────────────

adminRouter.post('/respawn-npc', async (req, res) => {
  const { npcId } = req.body;
  if (!npcId) return res.status(400).json({ error: 'npcId required' });

  const room = await getGameRoom();
  if (!room) return res.status(503).json({ error: 'Game server not running' });

  try {
    const state = (room as any).state;
    const npc = state.npcs.get(npcId);
    if (!npc) return res.status(404).json({ error: 'NPC not found' });

    const npcSystem = (room as any).npcSystem;
    npcSystem.adminRespawn(npcId, state.npcs);

    res.json({ ok: true });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});

// ── POST /delete-npc ────────────────────────────────────────

adminRouter.post('/delete-npc', async (req, res) => {
  const { npcId } = req.body;
  if (!npcId) return res.status(400).json({ error: 'npcId required' });

  const room = await getGameRoom();
  if (!room) return res.status(503).json({ error: 'Game server not running' });

  try {
    const state = (room as any).state;
    // NPC may already be dead (awaiting respawn) — check AI tracker, not just live state
    const npcSystem = (room as any).npcSystem;
    npcSystem.adminDelete(npcId, state.npcs);

    res.json({ ok: true });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});
