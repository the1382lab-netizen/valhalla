/**
 * Account routes (B-27): settings that follow the player's login rather than
 * a character. Today that is the game client's graphics options, which are
 * applied at login, before a character enters the world.
 */

import { Router } from 'express';
import { authMiddleware } from '../middleware/authMiddleware.js';
import { settingsLimiter } from '../middleware/rateLimit.js';
import {
  getAccountSettings,
  saveAccountSettings,
  validateAccountDocument,
  SettingsValidationError,
} from '../services/SettingsService.js';

export const accountRouter = Router();

accountRouter.use(authMiddleware);

/**
 * GET /api/account/settings
 * -> 200 { graphics, updatedAt }   404 { noSettings: true } when nothing has
 * been saved yet (the client then keeps its own copy or its defaults).
 * Rate limited: 60 per minute per IP (shared with the character settings).
 */
accountRouter.get('/settings', settingsLimiter, (req, res) => {
  try {
    const settings = getAccountSettings(req.userId!);
    if (!settings) {
      res.status(404).json({ error: 'No settings saved for this account.', noSettings: true });
      return;
    }
    res.json(settings);
  } catch (err: any) {
    res.status(500).json({ error: err?.message || 'Failed to read settings.' });
  }
});

/**
 * PUT /api/account/settings
 * Body: { graphics: { ... } }  (a JSON object, <= 64 KB)
 * -> 200 { ok: true, updatedAt }   400 bad body, 413 too big.
 */
accountRouter.put('/settings', settingsLimiter, (req, res) => {
  try {
    const body = req.body;
    if (!body || typeof body !== 'object' || Array.isArray(body)) {
      res.status(400).json({ error: 'Body must be { "graphics": { ... } }.' });
      return;
    }
    const graphicsJson = validateAccountDocument((body as Record<string, unknown>).graphics);
    const updatedAt = saveAccountSettings(req.userId!, graphicsJson);
    res.json({ ok: true, updatedAt });
  } catch (err: any) {
    if (err instanceof SettingsValidationError) {
      res.status(err.status).json({ error: err.message });
      return;
    }
    res.status(500).json({ error: err?.message || 'Failed to save settings.' });
  }
});
