/**
 * Auth API routes: register and login.
 */

import { Router } from 'express';
import { register, login, AccountBannedError } from '../services/AuthService.js';
import { getCharactersByUser } from '../services/CharacterService.js';
import { loginLimiter, registerLimiter, rateLimit } from '../middleware/rateLimit.js';
import { authMiddleware } from '../middleware/authMiddleware.js';
import { changePassword, deleteOwnAccount, AccountError } from '../services/AccountService.js';

/** B-12: both routes check a password, so they share the login limiter's budget per IP. */
const accountLimiter = rateLimit('account', 10);

function sendAccountError(res: import('express').Response, err: unknown, fallback: string): void {
  if (err instanceof AccountError) {
    res.status(err.status).json({ error: err.message });
    return;
  }
  console.error(`[auth] ${fallback}:`, err);
  res.status(500).json({ error: fallback });
}

export const authRouter = Router();

/**
 * POST /api/auth/register
 * Body: { username: string, password: string }
 * Returns: { token, userId, username }
 * Rate limited: 10 per minute per IP (429 + Retry-After), see middleware/rateLimit.ts.
 */
authRouter.post('/register', registerLimiter, async (req, res) => {
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
 * Rate limited: 10 per minute per IP (429 + Retry-After).
 */
authRouter.post('/login', loginLimiter, async (req, res) => {
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
    if (err instanceof AccountBannedError) {
      res.status(403).json({
        error: err.message,
        banned: true,
        bannedUntil: err.status.until,
        permanent: err.status.permanent,
        reason: err.status.reason,
      });
      return;
    }
    res.status(401).json({ error: err?.message || 'Login failed.' });
  }
});

/**
 * POST /api/auth/password   (Authorization: Bearer <token>)
 * Body: { currentPassword: string, newPassword: string }
 * -> 200 { ok, token }   the new token replaces the caller's; every other
 *    session on the account is logged out (AuthService.verifyToken).
 * -> 400 bad new password, 401 wrong current password, 429 rate limited.
 */
authRouter.post('/password', accountLimiter, authMiddleware, async (req, res) => {
  try {
    const { currentPassword, newPassword } = req.body ?? {};
    const result = await changePassword(req.userId!, currentPassword, newPassword);
    console.log(`[auth] password changed: ${result.username} (#${req.userId})`);
    res.json({ ok: true, token: result.token });
  } catch (err) {
    sendAccountError(res, err, 'Password change failed.');
  }
});

/**
 * DELETE /api/auth/account   (Authorization: Bearer <token>)
 * POST   /api/auth/account/delete   (same thing, for HTTP clients that won't send a DELETE body)
 * Body: { password: string, confirm: string (the account name) }
 * -> 200 { ok, username, characters }   the account, its characters, their
 *    inventories, equipment and action bars are gone; its tokens stop working.
 * -> 400 confirmation mismatch, 401 wrong password, 429 rate limited.
 */
const deleteAccountHandler: import('express').RequestHandler = async (req, res) => {
  try {
    const { password, confirm } = req.body ?? {};
    const result = await deleteOwnAccount(req.userId!, password, confirm);
    console.log(`[auth] account deleted by its owner: ${result.username} (#${req.userId}), ${result.characters} character(s)`);
    res.json({ ok: true, username: result.username, characters: result.characters });
  } catch (err) {
    sendAccountError(res, err, 'Account deletion failed.');
  }
};
authRouter.delete('/account', accountLimiter, authMiddleware, deleteAccountHandler);
authRouter.post('/account/delete', accountLimiter, authMiddleware, deleteAccountHandler);
