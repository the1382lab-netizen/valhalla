/**
 * Character API routes: list, create, delete characters.
 */

import { Router } from 'express';
import { authMiddleware } from '../middleware/authMiddleware.js';
import {
  getCharactersByUser,
  createCharacter,
  deleteCharacter,
} from '../services/CharacterService.js';

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
    res.json({ characters: chars });
  } catch (err: any) {
    res.status(500).json({ error: err?.message || 'Failed to fetch characters.' });
  }
});

/**
 * POST /api/characters
 * Body: { name: string, classId: string }
 * Creates a new character with starter equipment.
 */
charactersRouter.post('/', (req, res) => {
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
