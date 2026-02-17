/**
 * Auth API routes: register and login.
 */

import { Router } from 'express';
import { register, login } from '../services/AuthService.js';
import { getCharactersByUser } from '../services/CharacterService.js';

export const authRouter = Router();

/**
 * POST /api/auth/register
 * Body: { username: string, password: string }
 * Returns: { token, userId, username }
 */
authRouter.post('/register', async (req, res) => {
  try {
    const { username, password } = req.body;
    if (!username || !password) {
      res.status(400).json({ error: 'Username and password are required.' });
      return;
    }

    const result = await register(username, password);

    // New account — no characters yet
    res.json({
      token: result.token,
      userId: result.userId,
      username: result.username,
      characters: [],
    });
  } catch (err: any) {
    const message = err?.message || 'Registration failed.';
    // Username taken → 409, validation errors → 400
    const status = message.includes('already taken') ? 409 : 400;
    res.status(status).json({ error: message });
  }
});

/**
 * POST /api/auth/login
 * Body: { username: string, password: string }
 * Returns: { token, userId, username, characters: [...] }
 */
authRouter.post('/login', async (req, res) => {
  try {
    const { username, password } = req.body;
    if (!username || !password) {
      res.status(400).json({ error: 'Username and password are required.' });
      return;
    }

    const result = await login(username, password);

    // Fetch user's characters
    const chars = getCharactersByUser(result.userId);

    res.json({
      token: result.token,
      userId: result.userId,
      username: result.username,
      characters: chars,
    });
  } catch (err: any) {
    res.status(401).json({ error: err?.message || 'Login failed.' });
  }
});
