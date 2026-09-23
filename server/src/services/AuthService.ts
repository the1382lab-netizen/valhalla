/**
 * Authentication service: registration, login, and JWT verification.
 * Uses raw sql.js queries (no ORM).
 */

import bcrypt from 'bcryptjs';
import jwt from 'jsonwebtoken';
import { getDb, saveToDisk } from '../db/index.js';
import {
  MIN_USERNAME_LENGTH,
  MAX_USERNAME_LENGTH,
  MIN_PASSWORD_LENGTH,
  JWT_EXPIRY,
} from '@valhalla/shared';

const DEV_JWT_SECRET = 'valhalla-dev-secret-change-in-production';

/**
 * The key player tokens are signed with. In production it must be set (in
 * secrets.local.env or the environment): the dev default is public, and anyone
 * who knows it can mint a token for any account.
 */
function resolveJwtSecret(): string {
  const fromEnv = process.env.JWT_SECRET;
  if (fromEnv && fromEnv.length > 0) {
    if (process.env.NODE_ENV === 'production' && (fromEnv === DEV_JWT_SECRET || fromEnv.length < 32)) {
      throw new Error('JWT_SECRET is the dev default or shorter than 32 characters; refusing to start with NODE_ENV=production.');
    }
    return fromEnv;
  }
  if (process.env.NODE_ENV === 'production') {
    throw new Error('JWT_SECRET is not set; refusing to start with NODE_ENV=production (put it in secrets.local.env, see deploy/README.md).');
  }
  return DEV_JWT_SECRET;
}

const JWT_SECRET = resolveJwtSecret();
const BCRYPT_ROUNDS = 10;

export interface JwtPayload {
  userId: number;
  username: string;
  /**
   * Token expiry as a unix-ms timestamp. Present on payloads returned by
   * verifyToken(); never passed in when signing (the `exp` claim is set from
   * JWT_EXPIRY instead).
   */
  expiresAt?: number;
}

export interface AuthResult {
  token: string;
  userId: number;
  username: string;
}

/**
 * Register a new user account.
 * @throws Error if username is taken or input is invalid.
 */
export async function register(username: string, password: string): Promise<AuthResult> {
  // Validate input
  const trimmed = username.trim().toLowerCase();
  if (trimmed.length < MIN_USERNAME_LENGTH || trimmed.length > MAX_USERNAME_LENGTH) {
    throw new Error(`Username must be ${MIN_USERNAME_LENGTH}–${MAX_USERNAME_LENGTH} characters.`);
  }
  if (!/^[a-z0-9_]+$/.test(trimmed)) {
    throw new Error('Username can only contain letters, numbers, and underscores.');
  }
  if (password.length < MIN_PASSWORD_LENGTH) {
    throw new Error(`Password must be at least ${MIN_PASSWORD_LENGTH} characters.`);
  }

  const db = getDb();

  // Check if username is taken
  const existing = db.exec('SELECT id FROM users WHERE username = ?', [trimmed]);
  if (existing.length > 0 && existing[0].values.length > 0) {
    throw new Error('Username already taken.');
  }

  // Hash password and insert
  const passwordHash = await bcrypt.hash(password, BCRYPT_ROUNDS);
  db.run(
    'INSERT INTO users (username, password_hash, created_at) VALUES (?, ?, ?)',
    [trimmed, passwordHash, Date.now()],
  );

  // Get the inserted user ID
  const result = db.exec('SELECT last_insert_rowid() as id');
  const userId = result[0].values[0][0] as number;

  saveToDisk();

  // Generate JWT
  const token = generateToken({ userId, username: trimmed });

  return { token, userId, username: trimmed };
}

// ── Account bans ────────────────────────────────────────────

/** `users.banned_until` value meaning "no end date" (max JS Date, year 275760). */
export const BAN_PERMANENT = 8.64e15;

export interface BanStatus {
  banned: boolean;
  /** Unix ms the ban ends, or null when permanent (or not banned). */
  until: number | null;
  permanent: boolean;
  reason: string;
  bannedBy: string;
}

export interface BannedAccount extends BanStatus {
  userId: number;
  username: string;
}

/** Thrown by login() for a banned account; the auth route turns it into a 403. */
export class AccountBannedError extends Error {
  constructor(public readonly status: BanStatus) {
    super(describeBan(status));
    this.name = 'AccountBannedError';
  }
}

/** The sentence a banned player sees on the login screen or join rejection. */
export function describeBan(status: BanStatus): string {
  const reason = status.reason ? ` Reason: ${status.reason}` : '';
  if (status.permanent) return `This account has been banned.${reason}`;
  const until = status.until ? new Date(status.until).toISOString().replace('T', ' ').slice(0, 16) + ' UTC' : 'later';
  return `This account is suspended until ${until}.${reason}`;
}

function rowToBan(bannedUntil: unknown, reason: unknown, bannedBy: unknown): BanStatus {
  const until = typeof bannedUntil === 'number' ? bannedUntil : null;
  const active = until !== null && until > Date.now();
  if (!active) return { banned: false, until: null, permanent: false, reason: '', bannedBy: '' };
  const permanent = until >= BAN_PERMANENT;
  return {
    banned: true,
    until: permanent ? null : until,
    permanent,
    reason: String(reason ?? ''),
    bannedBy: String(bannedBy ?? ''),
  };
}

/** Current ban for a user id. An expired ban reads as not banned. */
export function getBanStatus(userId: number): BanStatus {
  const result = getDb().exec(
    'SELECT banned_until, ban_reason, banned_by FROM users WHERE id = ?',
    [userId],
  );
  if (result.length === 0 || result[0].values.length === 0) {
    return { banned: false, until: null, permanent: false, reason: '', bannedBy: '' };
  }
  const [until, reason, by] = result[0].values[0];
  return rowToBan(until, reason, by);
}

/** `users.id` for a username (case-insensitive, as stored), or null. */
export function findUserId(username: string): number | null {
  const result = getDb().exec('SELECT id FROM users WHERE username = ?', [username.trim().toLowerCase()]);
  if (result.length === 0 || result[0].values.length === 0) return null;
  return result[0].values[0][0] as number;
}

/** `users.username` for an id, or null. */
export function findUsername(userId: number): string | null {
  const result = getDb().exec('SELECT username FROM users WHERE id = ?', [userId]);
  if (result.length === 0 || result[0].values.length === 0) return null;
  return String(result[0].values[0][0]);
}

/**
 * Ban or suspend an account. `minutes` <= 0 (or omitted) means permanent.
 * @returns the resulting status.
 */
export function setBan(userId: number, minutes: number | undefined, reason: string, bannedBy: string): BanStatus {
  const until = minutes && minutes > 0 ? Date.now() + Math.round(minutes * 60_000) : BAN_PERMANENT;
  getDb().run(
    'UPDATE users SET banned_until = ?, ban_reason = ?, banned_by = ? WHERE id = ?',
    [until, reason.slice(0, 200), bannedBy.slice(0, 64), userId],
  );
  saveToDisk();
  return getBanStatus(userId);
}

/** Lift a ban. */
export function clearBan(userId: number): void {
  getDb().run("UPDATE users SET banned_until = NULL, ban_reason = '', banned_by = '' WHERE id = ?", [userId]);
  saveToDisk();
}

/** Every account whose ban is still in force, soonest-ending first, permanent last. */
export function listBans(): BannedAccount[] {
  const result = getDb().exec(
    'SELECT id, username, banned_until, ban_reason, banned_by FROM users WHERE banned_until IS NOT NULL AND banned_until > ? ORDER BY banned_until ASC',
    [Date.now()],
  );
  if (result.length === 0) return [];
  return result[0].values.map(([id, username, until, reason, by]) => ({
    userId: id as number,
    username: String(username),
    ...rowToBan(until, reason, by),
  }));
}

/**
 * Log in an existing user.
 * @throws Error if credentials are invalid.
 */
export async function login(username: string, password: string): Promise<AuthResult> {
  const trimmed = username.trim().toLowerCase();
  const db = getDb();

  const result = db.exec(
    'SELECT id, username, password_hash FROM users WHERE username = ?',
    [trimmed],
  );

  if (result.length === 0 || result[0].values.length === 0) {
    throw new Error('Invalid username or password.');
  }

  const row = result[0].values[0];
  const userId = row[0] as number;
  const storedUsername = row[1] as string;
  const storedHash = row[2] as string;

  const valid = await bcrypt.compare(password, storedHash);
  if (!valid) {
    throw new Error('Invalid username or password.');
  }

  // Checked after the password, so a ban never confirms an account exists to
  // someone who does not know its password.
  const ban = getBanStatus(userId);
  if (ban.banned) {
    throw new AccountBannedError(ban);
  }

  const token = generateToken({ userId, username: storedUsername });

  return { token, userId, username: storedUsername };
}

/**
 * Verify and decode a JWT token.
 * `expiresAt` is the `exp` claim converted to unix ms (undefined if absent).
 * @throws Error if token is invalid or expired.
 */
export function verifyToken(token: string): JwtPayload {
  try {
    const decoded = jwt.verify(token, JWT_SECRET) as JwtPayload & { exp?: number };
    return {
      userId: decoded.userId,
      username: decoded.username,
      expiresAt: typeof decoded.exp === 'number' ? decoded.exp * 1000 : undefined,
    };
  } catch {
    throw new Error('Invalid or expired token.');
  }
}

/**
 * Generate a JWT token for a user.
 */
function generateToken(payload: JwtPayload): string {
  return jwt.sign(payload, JWT_SECRET, { expiresIn: JWT_EXPIRY });
}
