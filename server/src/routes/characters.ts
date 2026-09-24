/**
 * Character API routes: list, create, delete characters, and (B-21) each
 * character's UI settings.
 */

import { Router, type Request } from 'express';
import { authMiddleware } from '../middleware/authMiddleware.js';
import { createCharacterLimiter, settingsLimiter } from '../middleware/rateLimit.js';
import {
  getCharactersByUser,
  createCharacter,
  deleteCharacter,
} from '../services/CharacterService.js';
import {
  characterBelongsTo,
  getCharacterSettings,
  saveCharacterSettings,
  validateSettingsDocument,
  SettingsValidationError,
} from '../services/SettingsService.js';

export const charactersRouter = Router();

// All character routes require authentication
charactersRouter.use(authMiddleware);

/**
 * GET /api/characters
 * Returns all characters for the authenticated user.
 */
charactersRouter.get('/', (req, res) => {
  try {
    const chars = getCharactersByUser(req.userId!);
    res.json({ characters: chars, username: req.username ?? '' });
  } catch (err: any) {
    res.status(500).json({ error: err?.message || 'Failed to fetch characters.' });
  }
});

/**
 * POST /api/characters
 * Body: { name: string, classId: string }
 * Creates a new character with starter equipment.
 * Rate limited: 5 per minute per IP (429 + Retry-After).
 */
charactersRouter.post('/', createCharacterLimiter, (req, res) => {
  try {
    const { name, classId } = req.body;
    if (!name || !classId) {
      res.status(400).json({ error: 'Name and classId are required.' });
      return;
    }

    const char = createCharacter(req.userId!, name, classId);
    res.status(201).json({ character: char });
  } catch (err: any) {
    const message = err?.message || 'Failed to create character.';
    const status = message.includes('already taken') || message.includes('Maximum')
      ? 409
      : 400;
    res.status(status).json({ error: message });
  }
});

/**
 * DELETE /api/characters/:id
 * Deletes a character owned by the authenticated user.
 */
charactersRouter.delete('/:id', (req, res) => {
  try {
    const characterId = parseInt(req.params.id, 10);
    if (isNaN(characterId)) {
      res.status(400).json({ error: 'Invalid character ID.' });
      return;
    }

    deleteCharacter(characterId, req.userId!);
    res.json({ success: true });
  } catch (err: any) {
    res.status(404).json({ error: err?.message || 'Failed to delete character.' });
  }
});

// ── UI settings (B-21) ──────────────────────────────────────
//
// The game client's per-character HUD layout / style / chat / nameplate
// options. Ownership: the character must belong to the token's user; another
// user's character (or none) is 404, so ids cannot be probed. The document is
// opaque here: a JSON object of at most 64 KB (SettingsService).

function ownedCharacterId(req: Request): number | null {
  const raw = String(req.params.id ?? '');
  const characterId = Number.parseInt(raw, 10);
  if (!Number.isInteger(characterId) || characterId <= 0 || String(characterId) !== raw) return null;
  return characterBelongsTo(characterId, req.userId!) ? characterId : null;
}

/**
 * GET /api/characters/:id/settings
 * -> 200 { ui, updatedAt }   404 when the character is not the caller's, or
 * nothing has been saved yet (the client then uses its defaults).
 * Rate limited: 60 per minute per IP (shared with PUT).
 */
charactersRouter.get('/:id/settings', settingsLimiter, (req, res) => {
  try {
    const characterId = ownedCharacterId(req);
    if (characterId === null) {
      res.status(404).json({ error: 'Character not found.' });
      return;
    }
    const settings = getCharacterSettings(characterId);
    if (!settings) {
      res.status(404).json({ error: 'No settings saved for this character.', noSettings: true });
      return;
    }
    res.json(settings);
  } catch (err: any) {
    res.status(500).json({ error: err?.message || 'Failed to read settings.' });
  }
});

/**
 * PUT /api/characters/:id/settings
 * Body: { ui: { ... } }  (a JSON object, <= 64 KB)
 * -> 200 { ok: true, updatedAt }   400 bad body, 404 not the caller's
 * character, 413 too big. Rate limited with GET.
 */
charactersRouter.put('/:id/settings', settingsLimiter, (req, res) => {
  try {
    const characterId = ownedCharacterId(req);
    if (characterId === null) {
      res.status(404).json({ error: 'Character not found.' });
      return;
    }
    const body = req.body;
    if (!body || typeof body !== 'object' || Array.isArray(body)) {
      res.status(400).json({ error: 'Body must be { "ui": { ... } }.' });
      return;
    }
    const uiJson = validateSettingsDocument((body as Record<string, unknown>).ui);
    const updatedAt = saveCharacterSettings(characterId, uiJson);
    res.json({ ok: true, updatedAt });
  } catch (err: any) {
    if (err instanceof SettingsValidationError) {
      res.status(err.status).json({ error: err.message });
      return;
    }
    res.status(500).json({ error: err?.message || 'Failed to save settings.' });
  }
});
