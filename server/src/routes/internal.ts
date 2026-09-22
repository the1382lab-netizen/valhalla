/**
 * Server-to-server ("internal") API routes — Valhalla 2.0, Phase 7a.
 *
 * These endpoints let the Unreal dedicated server authenticate players and
 * load/save characters without going through a per-player JWT session. They are
 * guarded by the shared secret header `X-Server-Secret` (see
 * middleware/serverSecret.ts), NOT by authMiddleware.
 *
 * Mounted at /api (BEFORE the player-facing routers in index.ts, so that
 * /api/characters/:id/load is not swallowed by the JWT-protected
 * charactersRouter).
 *
 *   GET  /api/health                     (no auth — startup probe)
 *   POST /api/auth/verify                { token } -> { userId, username, expiresAt }
 *   GET  /api/characters/:id/load?userId= -> { character: LoadedCharacter }
 *   PUT  /api/characters/:id/save        SaveCharacterData -> { ok, savedAt }
 */

import { Router } from 'express';
import { requireServerSecret } from '../middleware/serverSecret.js';
import { verifyToken } from '../services/AuthService.js';
import {
  loadCharacter,
  saveCharacter,
  getCharacterOwner,
  type SaveCharacterData,
  type InventorySlotData,
  type EquipmentData,
} from '../services/CharacterService.js';
import {
  EQUIP_SLOTS,
  EquipSlotType,
  INVENTORY_MAX_SLOTS,
  ITEM_CATALOG,
  type ItemTemplate,
} from '@valhalla/shared';
import { DataManager } from '../systems/DataManager.js';

export const internalRouter = Router();

/** Server API version reported by /api/health. */
const API_VERSION = '1.0';

/** Action bar size (mirrors loadCharacter, which always returns 8 slots). */
const ACTION_BAR_SLOTS = 8;

const VALID_EQUIP_SLOTS = new Set<string>(EQUIP_SLOTS as string[]);

/**
 * Item catalog lookup. Prefers the DataManager (editor JSON) when the game room
 * has initialized it, otherwise falls back to the hardcoded catalog — the same
 * pattern CharacterService uses.
 */
function itemExists(itemId: string): boolean {
  const dm = (() => { try { return DataManager.instance; } catch { return null; } })();
  if (dm) return dm.getItem(itemId) !== undefined;
  return (ITEM_CATALOG as Record<string, ItemTemplate>)[itemId] !== undefined;
}

class ValidationError extends Error {}

function fail(message: string): never {
  throw new ValidationError(message);
}

function requireFiniteNumber(value: unknown, field: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    fail(`${field} must be a finite number.`);
  }
  return value;
}

function requireInt(value: unknown, field: string, min: number): number {
  const n = requireFiniteNumber(value, field);
  if (!Number.isInteger(n)) fail(`${field} must be an integer.`);
  if (n < min) fail(`${field} must be >= ${min}.`);
  return n;
}

/**
 * Validate and normalize a SaveCharacterData payload coming off the wire.
 * @throws ValidationError with a human-readable message (-> HTTP 400).
 */
function parseSavePayload(body: unknown): SaveCharacterData {
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    fail('Request body must be a JSON object.');
  }
  const b = body as Record<string, unknown>;

  const hp = requireInt(b.hp, 'hp', 0);
  const mana = requireInt(b.mana, 'mana', 0);
  const xp = requireInt(b.xp, 'xp', 0);
  const level = requireInt(b.level, 'level', 1);
  const positionX = requireFiniteNumber(b.positionX, 'positionX');
  const positionY = requireFiniteNumber(b.positionY, 'positionY');

  if (typeof b.zoneId !== 'string' || b.zoneId.trim().length === 0) {
    fail('zoneId must be a non-empty string.');
  }
  const zoneId = b.zoneId.trim();

  if (b.alive !== undefined && typeof b.alive !== 'boolean') {
    fail('alive must be a boolean.');
  }
  const alive = b.alive === undefined ? true : (b.alive as boolean);

  // ── inventory ──
  if (!Array.isArray(b.inventory)) fail('inventory must be an array.');
  if (b.inventory.length > INVENTORY_MAX_SLOTS) {
    fail(`inventory may contain at most ${INVENTORY_MAX_SLOTS} slots (got ${b.inventory.length}).`);
  }
  const seenSlots = new Set<number>();
  const inventory: InventorySlotData[] = b.inventory.map((entry, i) => {
    if (!entry || typeof entry !== 'object' || Array.isArray(entry)) {
      fail(`inventory[${i}] must be an object.`);
    }
    const e = entry as Record<string, unknown>;
    const slotIndex = requireInt(e.slotIndex, `inventory[${i}].slotIndex`, 0);
    if (slotIndex >= INVENTORY_MAX_SLOTS) {
      fail(`inventory[${i}].slotIndex must be < ${INVENTORY_MAX_SLOTS}.`);
    }
    if (seenSlots.has(slotIndex)) fail(`inventory slotIndex ${slotIndex} appears more than once.`);
    seenSlots.add(slotIndex);

    if (typeof e.itemId !== 'string' || e.itemId.length === 0) {
      fail(`inventory[${i}].itemId must be a non-empty string.`);
    }
    if (!itemExists(e.itemId)) fail(`Unknown item id '${e.itemId}' in inventory[${i}].`);

    const quantity = requireInt(e.quantity, `inventory[${i}].quantity`, 1);
    return { slotIndex, itemId: e.itemId, quantity };
  });

  // ── equipment ──
  if (!Array.isArray(b.equipment)) fail('equipment must be an array.');
  const seenEquip = new Set<string>();
  const equipment: EquipmentData[] = b.equipment.map((entry, i) => {
    if (!entry || typeof entry !== 'object' || Array.isArray(entry)) {
      fail(`equipment[${i}] must be an object.`);
    }
    const e = entry as Record<string, unknown>;
    if (typeof e.slotType !== 'string' || !VALID_EQUIP_SLOTS.has(e.slotType)) {
      fail(`equipment[${i}].slotType must be one of: ${EQUIP_SLOTS.join(', ')}.`);
    }
    if (seenEquip.has(e.slotType)) fail(`equipment slotType '${e.slotType}' appears more than once.`);
    seenEquip.add(e.slotType);

    if (typeof e.itemId !== 'string' || e.itemId.length === 0) {
      fail(`equipment[${i}].itemId must be a non-empty string.`);
    }
    if (!itemExists(e.itemId)) fail(`Unknown item id '${e.itemId}' in equipment[${i}].`);
    return { slotType: e.slotType as EquipSlotType, itemId: e.itemId };
  });

  // ── action bar (optional; defaults to 8 empty slots) ──
  let actionBar: string[] = Array(ACTION_BAR_SLOTS).fill('');
  if (b.actionBar !== undefined) {
    if (!Array.isArray(b.actionBar)) fail('actionBar must be an array of strings.');
    if (b.actionBar.length > ACTION_BAR_SLOTS) {
      fail(`actionBar may contain at most ${ACTION_BAR_SLOTS} entries.`);
    }
    actionBar = b.actionBar.map((skillId, i) => {
      if (skillId === null || skillId === undefined) return '';
      if (typeof skillId !== 'string') fail(`actionBar[${i}] must be a string.`);
      return skillId;
    });
    while (actionBar.length < ACTION_BAR_SLOTS) actionBar.push('');
  }

  return { hp, mana, xp, level, positionX, positionY, zoneId, alive, inventory, equipment, actionBar };
}

// ── Routes ──────────────────────────────────────────────────

/**
 * GET /api/health  (no auth)
 * Startup probe for the Unreal dedicated server.
 * -> 200 { ok: true, version: '1.0', serverTime: <unix ms> }
 */
internalRouter.get('/health', (_req, res) => {
  res.json({ ok: true, version: API_VERSION, serverTime: Date.now() });
});

/**
 * POST /api/auth/verify
 * Body: { token: string }
 * -> 200 { userId, username, expiresAt }  (expiresAt = unix ms)
 * -> 400 { error } on a malformed body, 401 { error } on a bad/expired token.
 */
internalRouter.post('/auth/verify', requireServerSecret, (req, res) => {
  const token = (req.body ?? {})?.token;
  if (typeof token !== 'string' || token.length === 0) {
    res.status(400).json({ error: 'Body must contain a non-empty "token" string.' });
    return;
  }

  try {
    const payload = verifyToken(token);
    res.json({
      userId: payload.userId,
      username: payload.username,
      expiresAt: payload.expiresAt ?? null,
    });
  } catch (err: any) {
    res.status(401).json({ error: err?.message || 'Invalid or expired token.' });
  }
});

/**
 * GET /api/characters/:id/load?userId=<n>
 * -> 200 { character: LoadedCharacter }
 * -> 400 { error } on a bad id/userId, 404 { error } when missing or not owned.
 */
internalRouter.get('/characters/:id/load', requireServerSecret, (req, res) => {
  const characterId = Number.parseInt(req.params.id, 10);
  if (!Number.isInteger(characterId) || characterId <= 0) {
    res.status(400).json({ error: 'Invalid character id.' });
    return;
  }

  const rawUserId = req.query.userId;
  const userId = Number.parseInt(typeof rawUserId === 'string' ? rawUserId : '', 10);
  if (!Number.isInteger(userId) || userId <= 0) {
    res.status(400).json({ error: 'Query parameter "userId" is required and must be a positive integer.' });
    return;
  }

  try {
    const character = loadCharacter(characterId, userId);
    res.json({ character });
  } catch (err: any) {
    res.status(404).json({ error: err?.message || 'Character not found or access denied.' });
  }
});

/**
 * PUT /api/characters/:id/save
 * Body: SaveCharacterData
 * -> 200 { ok: true, savedAt }  (savedAt = unix ms)
 * -> 400 { error } on validation failure, 404 { error } if the character is gone.
 */
internalRouter.put('/characters/:id/save', requireServerSecret, (req, res) => {
  const characterId = Number.parseInt(req.params.id, 10);
  if (!Number.isInteger(characterId) || characterId <= 0) {
    res.status(400).json({ error: 'Invalid character id.' });
    return;
  }

  if (getCharacterOwner(characterId) === null) {
    res.status(404).json({ error: `Character ${characterId} not found.` });
    return;
  }

  let data: SaveCharacterData;
  try {
    data = parseSavePayload(req.body);
  } catch (err: any) {
    if (err instanceof ValidationError) {
      res.status(400).json({ error: err.message });
      return;
    }
    res.status(400).json({ error: err?.message || 'Invalid save payload.' });
    return;
  }

  try {
    saveCharacter(characterId, data);
    res.json({ ok: true, savedAt: Date.now() });
  } catch (err: any) {
    console.error(`[internal] Save failed for character ${characterId}:`, err?.message);
    res.status(500).json({ error: err?.message || 'Failed to save character.' });
  }
});
