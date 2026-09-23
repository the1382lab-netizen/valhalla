/**
 * Picks the VALHALLA_* settings out of `<repo root>/secrets.local.env` (the
 * gitignored file the backend and the Unreal game server also read; see
 * deploy/README.md), so the admin proxy presents the same secret the game
 * server expects without anyone exporting environment variables by hand.
 * Only VALHALLA_* keys are taken — this process has no use for NODE_ENV or
 * JWT_SECRET — and a variable already in the environment wins.
 */
import { existsSync, readFileSync } from 'fs';
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';

const here = dirname(fileURLToPath(import.meta.url));
export const SECRETS_FILE = process.env.VALHALLA_SECRETS_FILE || resolve(here, '../../secrets.local.env');

function load(): string[] {
  if (!existsSync(SECRETS_FILE)) return [];
  const applied: string[] = [];
  for (const raw of readFileSync(SECRETS_FILE, 'utf8').split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq <= 0) continue;
    const key = line.slice(0, eq).trim();
    if (!key.startsWith('VALHALLA_') || process.env[key] !== undefined) continue;
    let value = line.slice(eq + 1).trim();
    if (value.length >= 2 && ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'")))) {
      value = value.slice(1, -1);
    }
    process.env[key] = value;
    applied.push(key);
  }
  return applied;
}

/** Keys taken from the file (never their values), for the startup log. */
export const loadedFromSecretsFile: string[] = load();
