/**
 * Per-IP sliding-window rate limit for the player-facing write routes (B-04).
 *
 *   POST /api/auth/login      10 per minute per IP
 *   POST /api/auth/register   10 per minute per IP
 *   POST /api/characters       5 per minute per IP
 *   GET|PUT /api/characters/:id/settings   60 per minute per IP (B-21; the
 *                              client saves at most once per ~2 s of editing)
 *
 * In memory: one list of attempt timestamps per (bucket, IP), pruned on every
 * hit and swept once a window. Every attempt counts, successful or not. Over
 * the limit the answer is 429 with `Retry-After` (seconds until the oldest
 * attempt in the window expires). A restart clears it, which is fine for one
 * backend process.
 *
 * Client IP: the socket address, except that `X-Forwarded-For` is believed
 * when the socket is loopback, because that is Caddy (deploy/Caddyfile) on
 * this machine proxying a tester. Caddy appends the address it saw, so the
 * last entry is the one to trust. From anywhere else the header is ignored:
 * a client could otherwise pick a fresh "IP" per request.
 *
 * RATE_LIMIT_WINDOW_MS changes the window (default 60000); only the smoke
 * test sets it, so it does not have to wait a whole minute for a bucket to
 * clear.
 */

import type { Request, Response, NextFunction, RequestHandler } from 'express';

const WINDOW_MS = Number.parseInt(process.env.RATE_LIMIT_WINDOW_MS ?? '', 10) || 60_000;

const LOOPBACK = new Set(['127.0.0.1', '::1', '::ffff:127.0.0.1']);

/** The address a request is counted against. */
export function clientIp(req: Request): string {
  const socketIp = req.socket.remoteAddress ?? 'unknown';
  if (LOOPBACK.has(socketIp)) {
    const header = req.headers['x-forwarded-for'];
    const value = Array.isArray(header) ? header[header.length - 1] : header;
    const last = value?.split(',').map(s => s.trim()).filter(Boolean).pop();
    if (last) return last;
  }
  return socketIp;
}

export function rateLimit(bucket: string, maxPerWindow: number): RequestHandler {
  const hits = new Map<string, number[]>();

  // Drop IPs whose attempts have all expired, so the map does not grow forever.
  setInterval(() => {
    const cutoff = Date.now() - WINDOW_MS;
    for (const [ip, times] of hits) {
      if (times.length === 0 || times[times.length - 1] <= cutoff) hits.delete(ip);
    }
  }, WINDOW_MS).unref();

  return (req: Request, res: Response, next: NextFunction): void => {
    const ip = clientIp(req);
    const now = Date.now();
    const cutoff = now - WINDOW_MS;
    const times = (hits.get(ip) ?? []).filter(t => t > cutoff);

    if (times.length >= maxPerWindow) {
      hits.set(ip, times);
      const retryAfter = Math.max(1, Math.ceil((times[0] + WINDOW_MS - now) / 1000));
      res.setHeader('Retry-After', String(retryAfter));
      res.status(429).json({ error: `Too many attempts. Try again in ${retryAfter} s.` });
      console.warn(`[rate-limit] ${bucket}: ${ip} over ${maxPerWindow}/${WINDOW_MS / 1000}s`);
      return;
    }

    times.push(now);
    hits.set(ip, times);
    next();
  };
}

export const loginLimiter = rateLimit('login', 10);
export const registerLimiter = rateLimit('register', 10);
export const createCharacterLimiter = rateLimit('create-character', 5);
export const settingsLimiter = rateLimit('character-settings', 60);
