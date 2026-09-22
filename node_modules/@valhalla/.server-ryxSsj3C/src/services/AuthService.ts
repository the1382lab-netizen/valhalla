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

const JWT_SECRET = process.env.JWT_SECRET || 'valhalla-dev-secret-change-in-production';
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
