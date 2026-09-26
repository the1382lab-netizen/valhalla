/**
 * B-12 account management: change password, delete account, and the admin
 * tools (look up, reset password, delete account, rename character).
 *
 * Every change that should end existing sessions (a new password, a reset)
 * bumps `users.token_version`, which AuthService.verifyToken checks (a deleted
 * account's tokens fail because the account is gone), so a stolen or old login token stops working at once, including for
 * the game server's join check. There are no email addresses: a forgotten
 * password is reset by an admin, who hands the player a temporary password.
 */

import bcrypt from 'bcryptjs';
import { randomInt } from 'crypto';
import { getDb, saveToDisk } from '../db/index.js';
import { MIN_PASSWORD_LENGTH } from '@valhalla/shared';
import { generateToken, getBanStatus, type BanStatus } from './AuthService.js';

const BCRYPT_ROUNDS = 10;

/** Longest password accepted; bcrypt ignores everything past 72 bytes anyway. */
export const MAX_PASSWORD_LENGTH = 72;

export class AccountError extends Error {
  constructor(message: string, public readonly status: number) {
    super(message);
    this.name = 'AccountError';
  }
}

export interface AccountCharacter {
  id: number;
  name: string;
  classId: string;
  level: number;
  zoneId: string;
  updatedAt: number;
}

export interface AccountSummary {
  userId: number;
  username: string;
  createdAt: number;
  lastLoginAt: number | null;
  characterCount: number;
  ban: BanStatus;
}

export interface AccountDetail extends AccountSummary {
  characters: AccountCharacter[];
}

function queryRows(sql: string, params: (string | number | null)[] = []): unknown[][] {
  const result = getDb().exec(sql, params);
  return result.length ? result[0].values : [];
}

function validateNewPassword(password: unknown): string {
  if (typeof password !== 'string' || password.length < MIN_PASSWORD_LENGTH) {
    throw new AccountError(`Password must be at least ${MIN_PASSWORD_LENGTH} characters.`, 400);
  }
  if (password.length > MAX_PASSWORD_LENGTH) {
    throw new AccountError(`Password must be at most ${MAX_PASSWORD_LENGTH} characters.`, 400);
  }
  return password;
}

async function checkPassword(userId: number, password: unknown): Promise<string> {
  const rows = queryRows('SELECT username, password_hash FROM users WHERE id = ?', [userId]);
  if (rows.length === 0) throw new AccountError('Account not found.', 404);
  const [username, hash] = rows[0];
  const ok = typeof password === 'string' && password.length > 0 && (await bcrypt.compare(password, String(hash)));
  if (!ok) throw new AccountError('Current password is incorrect.', 401);
  return String(username);
}

/** Store a new password hash and end every session issued before now. */
async function storePassword(userId: number, password: string): Promise<void> {
  const hash = await bcrypt.hash(password, BCRYPT_ROUNDS);
  getDb().run('UPDATE users SET password_hash = ?, token_version = token_version + 1 WHERE id = ?', [hash, userId]);
  saveToDisk();
}

/**
 * Player: change their own password. Requires the current one. Every other
 * session on the account is logged out; the caller gets a fresh token so the
 * screen they are on keeps working.
 */
export async function changePassword(userId: number, currentPassword: unknown, newPassword: unknown): Promise<{ token: string; username: string }> {
  const username = await checkPassword(userId, currentPassword);
  const next = validateNewPassword(newPassword);
  if (next === currentPassword) {
    throw new AccountError('The new password must be different from the current one.', 400);
  }
  await storePassword(userId, next);
  return { token: generateToken({ userId, username }), username };
}

/** Delete an account's characters (inventory, equipment, action bars and UI settings), its account settings and the account itself. */
function deleteAccountRows(userId: number): { characters: number } {
  const db = getDb();
  const characterIds = queryRows('SELECT id FROM characters WHERE user_id = ?', [userId]).map(r => Number(r[0]));
  db.run('BEGIN TRANSACTION;');
  try {
    for (const id of characterIds) {
      // Explicit as well as ON DELETE CASCADE, like CharacterService.deleteCharacter.
      db.run('DELETE FROM inventory_items WHERE character_id = ?', [id]);
      db.run('DELETE FROM character_equipment WHERE character_id = ?', [id]);
      db.run('DELETE FROM character_action_bar WHERE character_id = ?', [id]);
      db.run('DELETE FROM character_settings WHERE character_id = ?', [id]);
    }
    db.run('DELETE FROM characters WHERE user_id = ?', [userId]);
    db.run('DELETE FROM account_settings WHERE user_id = ?', [userId]);
    db.run('DELETE FROM users WHERE id = ?', [userId]);
    db.run('COMMIT;');
  } catch (err) {
    db.run('ROLLBACK;');
    throw err;
  }
  saveToDisk();
  return { characters: characterIds.length };
}

/**
 * Player: delete their own account. Requires the password and the username
 * typed out as confirmation.
 */
export async function deleteOwnAccount(userId: number, password: unknown, confirm: unknown): Promise<{ username: string; characters: number }> {
  const username = await checkPassword(userId, password);
  if (typeof confirm !== 'string' || confirm.trim().toLowerCase() !== username) {
    throw new AccountError('Type your account name to confirm.', 400);
  }
  const { characters } = deleteAccountRows(userId);
  return { username, characters };
}

// ── Admin ───────────────────────────────────────────────────────────────

function summaryFromRow(row: unknown[]): AccountSummary {
  const [id, username, createdAt, lastLoginAt, characterCount] = row;
  const userId = Number(id);
  return {
    userId,
    username: String(username),
    createdAt: Number(createdAt),
    lastLoginAt: typeof lastLoginAt === 'number' ? lastLoginAt : null,
    characterCount: Number(characterCount ?? 0),
    ban: getBanStatus(userId),
  };
}

const SUMMARY_SELECT = `
  SELECT u.id, u.username, u.created_at, u.last_login_at,
         (SELECT COUNT(*) FROM characters c WHERE c.user_id = u.id) AS character_count
  FROM users u`;

/**
 * Admin: find accounts by part of the account name, or by the name of one of
 * their characters. An empty query lists the most recently created accounts.
 * `bannedOnly` keeps just the accounts whose ban is in force.
 */
export function searchAccounts(query: string, limit = 50, bannedOnly = false): AccountSummary[] {
  const q = query.trim().toLowerCase();
  const cap = Math.max(1, Math.min(200, Math.floor(limit)));
  const where: string[] = [];
  const params: (string | number)[] = [];
  if (q) {
    const like = `%${q.replace(/[\\%_]/g, m => `\\${m}`)}%`;
    where.push(`(u.username LIKE ? ESCAPE '\\' OR u.id IN (SELECT user_id FROM characters WHERE LOWER(name) LIKE ? ESCAPE '\\'))`);
    params.push(like, like);
  }
  if (bannedOnly) {
    where.push('u.banned_until IS NOT NULL AND u.banned_until > ?');
    params.push(Date.now());
  }
  const order = q ? 'u.username ASC' : 'u.created_at DESC';
  return queryRows(
    `${SUMMARY_SELECT} ${where.length ? `WHERE ${where.join(' AND ')}` : ''} ORDER BY ${order} LIMIT ?`,
    [...params, cap],
  ).map(summaryFromRow);
}

/** Admin: one account with its characters, or null. */
export function getAccountDetail(userId: number): AccountDetail | null {
  const rows = queryRows(`${SUMMARY_SELECT} WHERE u.id = ?`, [userId]);
  if (rows.length === 0) return null;
  const characters = queryRows(
    'SELECT id, name, class_id, level, zone_id, updated_at FROM characters WHERE user_id = ? ORDER BY name ASC',
    [userId],
  ).map(([id, name, classId, level, zoneId, updatedAt]) => ({
    id: Number(id),
    name: String(name),
    classId: String(classId),
    level: Number(level),
    zoneId: String(zoneId),
    updatedAt: Number(updatedAt),
  }));
  return { ...summaryFromRow(rows[0]), characters };
}

/** Letters and digits without the look-alikes (0/O, 1/l/I). */
const TEMP_ALPHABET = 'abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789';

export function makeTemporaryPassword(length = 12): string {
  let out = '';
  for (let i = 0; i < length; i++) out += TEMP_ALPHABET[randomInt(TEMP_ALPHABET.length)];
  return out;
}

/**
 * Admin: replace a forgotten password with a temporary one (returned once,
 * never stored in plain text) and log the account out everywhere. The player
 * logs in with it and changes it from the character screen.
 */
export async function adminResetPassword(userId: number): Promise<{ temporaryPassword: string }> {
  if (queryRows('SELECT id FROM users WHERE id = ?', [userId]).length === 0) {
    throw new AccountError('Account not found.', 404);
  }
  const temporaryPassword = makeTemporaryPassword();
  await storePassword(userId, temporaryPassword);
  return { temporaryPassword };
}

/** Admin: delete an account and all its characters. */
export function adminDeleteAccount(userId: number): { characters: number } {
  if (queryRows('SELECT id FROM users WHERE id = ?', [userId]).length === 0) {
    throw new AccountError('Account not found.', 404);
  }
  return deleteAccountRows(userId);
}

/** Admin: rename a character, with the same rules as character creation. */
export function adminRenameCharacter(characterId: number, newName: unknown): { oldName: string; name: string; userId: number } {
  const rows = queryRows('SELECT name, user_id FROM characters WHERE id = ?', [characterId]);
  if (rows.length === 0) throw new AccountError('Character not found.', 404);
  const [oldName, userId] = rows[0];

  const name = typeof newName === 'string' ? newName.trim() : '';
  if (name.length < 2 || name.length > 20) {
    throw new AccountError('Character name must be 2–20 characters.', 400);
  }
  if (!/^[a-zA-Z][a-zA-Z0-9_ ]*$/.test(name)) {
    throw new AccountError('Character name must start with a letter and contain only letters, numbers, spaces, and underscores.', 400);
  }
  const taken = queryRows('SELECT id FROM characters WHERE name = ? AND id != ?', [name, characterId]);
  if (taken.length > 0) throw new AccountError('Character name already taken.', 409);

  getDb().run('UPDATE characters SET name = ?, updated_at = ? WHERE id = ?', [name, Date.now(), characterId]);
  saveToDisk();
  return { oldName: String(oldName), name, userId: Number(userId) };
}
