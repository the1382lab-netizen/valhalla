/**
 * Shared-secret middleware for server-to-server routes.
 *
 * The Unreal dedicated server (Valhalla 2.0) calls a small set of internal
 * endpoints with the header `X-Server-Secret: <secret>`. These routes are NOT
 * for players/browsers — they bypass the per-user JWT middleware, so the secret
 * is the only thing guarding them.
 *
 * Secret resolution:
 *   - env VALHALLA_SERVER_SECRET (or secrets.local.env, via loadEnv.ts), when
 *     set and non-empty — except the dev value in production, which disables
 *     the routes like no secret at all
 *   - otherwise 'dev-server-secret' when NODE_ENV !== 'production'
 *   - otherwise (production, no secret) the routes are disabled: every request
 *     gets 503 and a warning is printed at startup.
 */

import type { Request, Response, NextFunction } from 'express';
import { timingSafeEqual } from 'crypto';

export const SERVER_SECRET_HEADER = 'x-server-secret';

const DEV_FALLBACK_SECRET = 'dev-server-secret';

function resolveSecret(): string | null {
  const fromEnv = process.env.VALHALLA_SERVER_SECRET;
  if (fromEnv && fromEnv.length > 0) {
    // The dev value is public; in production it would be no protection at all.
    if (process.env.NODE_ENV === 'production' && fromEnv === DEV_FALLBACK_SECRET) return null;
    return fromEnv;
  }
  if (process.env.NODE_ENV === 'production') return null;
  return DEV_FALLBACK_SECRET;
}

/** The active shared secret, or null when the internal routes are disabled. */
export const SERVER_SECRET: string | null = resolveSecret();

/** True when the internal (server-to-server) routes will accept requests. */
export const internalRoutesEnabled: boolean = SERVER_SECRET !== null;

/** True when the dev fallback secret is in use (never the case in production). */
export const usingDevSecret: boolean =
  SERVER_SECRET === DEV_FALLBACK_SECRET && !process.env.VALHALLA_SERVER_SECRET;

/**
 * Print a one-line startup notice about the internal-route secret.
 * Called once from index.ts.
 */
export function logServerSecretStatus(): void {
  if (!internalRoutesEnabled) {
    console.warn(
      '[internal] ⚠️  VALHALLA_SERVER_SECRET is not set (or is the public dev value) and NODE_ENV=production — ' +
      'server-to-server routes (/api/auth/verify, /api/characters/:id/load, ' +
      '/api/characters/:id/save) are DISABLED and will return 503. ' +
      'Set VALHALLA_SERVER_SECRET to enable them.',
    );
    return;
  }
  if (usingDevSecret) {
    console.warn(
      `[internal] Server-to-server routes enabled with the DEV fallback secret ` +
      `('${DEV_FALLBACK_SECRET}'). Set VALHALLA_SERVER_SECRET before deploying.`,
    );
  } else {
    console.log('[internal] Server-to-server routes enabled (VALHALLA_SERVER_SECRET set).');
  }
}

/** Constant-time string comparison that tolerates length mismatches. */
function secretsMatch(provided: string, expected: string): boolean {
  const a = Buffer.from(provided, 'utf8');
  const b = Buffer.from(expected, 'utf8');
  if (a.length !== b.length) return false;
  return timingSafeEqual(a, b);
}

/**
 * Reject any request that does not carry the correct X-Server-Secret header.
 * 503 when the routes are disabled, 401 for a missing/wrong secret.
 */
export function requireServerSecret(req: Request, res: Response, next: NextFunction): void {
  if (!SERVER_SECRET) {
    res.status(503).json({ error: 'Server-to-server routes are disabled (no VALHALLA_SERVER_SECRET configured).' });
    return;
  }

  const header = req.headers[SERVER_SECRET_HEADER];
  const provided = Array.isArray(header) ? header[0] : header;

  if (!provided || !secretsMatch(provided, SERVER_SECRET)) {
    res.status(401).json({ error: 'Invalid or missing X-Server-Secret header.' });
    return;
  }

  next();
}
