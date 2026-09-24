/**
 * CORS allow-list (B-04).
 *
 * Browsers are the only thing CORS is about. The game client (UE HTTP) and
 * the game server send no Origin header and pass straight through. A request
 * that does carry an Origin must be on the list, or it gets 403 (preflight or
 * not): the backend has no browser pages of its own, so a foreign origin is
 * never a legitimate caller.
 *
 * CORS_ORIGINS: comma-separated exact origins (scheme://host[:port]).
 * Default: the web editor, http://localhost:5180 and http://127.0.0.1:5180.
 */

import type { Request, Response, NextFunction } from 'express';

const DEFAULT_ORIGINS = 'http://localhost:5180,http://127.0.0.1:5180';

export function parseOrigins(value: string | undefined): Set<string> {
  return new Set(
    (value && value.trim() ? value : DEFAULT_ORIGINS)
      .split(',')
      .map(s => s.trim().replace(/\/+$/, ''))
      .filter(Boolean),
  );
}

export const CORS_ORIGINS: Set<string> = parseOrigins(process.env.CORS_ORIGINS);

export function corsAllowList(req: Request, res: Response, next: NextFunction): void {
  const origin = req.headers.origin;
  if (!origin) {
    next();
    return;
  }

  res.vary('Origin');
  if (!CORS_ORIGINS.has(origin)) {
    res.status(403).json({ error: 'Origin not allowed.' });
    return;
  }

  res.header('Access-Control-Allow-Origin', origin);
  res.header('Access-Control-Allow-Credentials', 'true');
  res.header('Access-Control-Allow-Headers', 'Content-Type, Accept, Authorization, If-None-Match');
  res.header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
  res.header('Access-Control-Max-Age', '600');
  if (req.method === 'OPTIONS') {
    res.sendStatus(204);
    return;
  }
  next();
}
