/**
 * Loads `<repo root>/secrets.local.env` into process.env.
 *
 * Imported first by index.ts (and the smoke script) so it runs before any
 * module reads process.env at load time (AuthService's JWT_SECRET,
 * serverSecret.ts). The file is gitignored and is the single place the
 * deployment secrets live: this backend, the web editor's admin proxy and the
 * Unreal game server (UValhallaDataSettings::GetServerSecret) all read it.
 * See deploy/README.md.
 *
 * Variables already set in the real environment win over the file, so a
 * one-off `set PORT=...` or a test harness still controls what it sets.
 * VALHALLA_SECRETS_FILE points at a different file.
 *
 * Format: KEY=VALUE per line, `#` comments, blank lines, optional matching
 * quotes around the value. No interpolation, no multi-line values.
 */
import { existsSync, readFileSync } from 'fs';
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';

const here = dirname(fileURLToPath(import.meta.url));

/** server/src (tsx) and server/dist (node) are both two levels below the repo root. */
export const SECRETS_FILE = process.env.VALHALLA_SECRETS_FILE || resolve(here, '../../secrets.local.env');

export function parseEnvFile(text: string): Record<string, string> {
  const out: Record<string, string> = {};
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq <= 0) continue;
    const key = line.slice(0, eq).trim();
    let value = line.slice(eq + 1).trim();
    if (value.length >= 2 && ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'")))) {
      value = value.slice(1, -1);
    }
    out[key] = value;
  }
  return out;
}

function load(): string[] {
  if (!existsSync(SECRETS_FILE)) return [];
  const applied: string[] = [];
  for (const [key, value] of Object.entries(parseEnvFile(readFileSync(SECRETS_FILE, 'utf8')))) {
    if (process.env[key] === undefined) {
      process.env[key] = value;
      applied.push(key);
    }
  }
  return applied;
}

/** Keys taken from the file (never their values), for the startup log. */
export const loadedFromSecretsFile: string[] = load();
