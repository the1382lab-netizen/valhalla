/**
 * Express middleware that validates JWT tokens on protected routes.
 * Attaches userId and username to the request object.
 */

import type { Request, Response, NextFunction } from 'express';
import { verifyToken, getBanStatus, describeBan } from '../services/AuthService.js';

// Extend Express Request with auth fields
declare global {
  namespace Express {
    interface Request {
      userId?: number;
      username?: string;
    }
  }
}

export function authMiddleware(req: Request, res: Response, next: NextFunction): void {
  const authHeader = req.headers.authorization;

  if (!authHeader || !authHeader.startsWith('Bearer ')) {
    res.status(401).json({ error: 'Missing or invalid authorization header.' });
    return;
  }

  const token = authHeader.slice(7); // Remove 'Bearer '

  try {
    const payload = verifyToken(token);
    const ban = getBanStatus(payload.userId);
    if (ban.banned) {
      res.status(403).json({ error: describeBan(ban), banned: true });
      return;
    }
    req.userId = payload.userId;
    req.username = payload.username;
    next();
  } catch {
    res.status(401).json({ error: 'Invalid or expired token.' });
  }
}
