/**
 * Game data for clients (backlog B-03).
 *
 * A packaged or remote game client has no copy of shared/data, so it downloads
 * the files from here after login and whenever the game server reloads its
 * data. The manifest lists each file with a SHA-1 hash (change detection, not security), so a client only
 * downloads what changed.
 *
 *   GET /api/data/manifest   -> { dataVersion, files: [{ name, sha1, size }] }
 *   GET /api/data/<name>     -> the file (only the names in DATA_FILES)
 *
 * No auth: the files hold nothing secret (the web editor edits them and every
 * client displays them), and the login screen needs them before a session.
 */
import { Router } from 'express';
import { createHash } from 'crypto';
import { readFileSync, statSync, existsSync } from 'fs';
import { resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import { DATA_VERSION } from '@valhalla/shared';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DATA_DIR = resolve(__dirname, '..', '..', '..', 'shared', 'data');

/** The files a client needs — the same seven UValhallaDataSubsystem reads. */
export const DATA_FILES = [
  'classes.json',
  'items.json',
  'skills.json',
  'npc-templates.json',
  'loot-tables.json',
  'zones.json',
  'ui-config.json',
] as const;

interface CachedFile { mtimeMs: number; size: number; sha1: string }
const cache = new Map<string, CachedFile>();

/** Hash a data file, re-reading it only when its timestamp or size changed. */
function describe(name: string): CachedFile | null {
  const path = resolve(DATA_DIR, name);
  if (!existsSync(path)) return null;
  const st = statSync(path);
  const hit = cache.get(name);
  if (hit && hit.mtimeMs === st.mtimeMs && hit.size === st.size) return hit;
  const sha1 = createHash('sha1').update(readFileSync(path)).digest('hex');
  const entry = { mtimeMs: st.mtimeMs, size: st.size, sha1 };
  cache.set(name, entry);
  return entry;
}

export const dataRouter = Router();

dataRouter.get('/manifest', (_req, res) => {
  const files = DATA_FILES.map(name => {
    const d = describe(name);
    return d ? { name, sha1: d.sha1, size: d.size } : { name, sha1: '', size: 0, missing: true };
  });
  res.set('Cache-Control', 'no-store');
  res.json({ dataVersion: DATA_VERSION, files });
});

dataRouter.get('/:name', (req, res) => {
  const name = req.params.name;
  if (!(DATA_FILES as readonly string[]).includes(name)) {
    res.status(404).json({ error: `Unknown data file '${name}'.` });
    return;
  }
  const d = describe(name);
  if (!d) {
    res.status(404).json({ error: `${name} is missing on the server.` });
    return;
  }
  res.set('ETag', `"${d.sha1}"`);
  res.set('Cache-Control', 'no-cache');
  if (req.headers['if-none-match'] === `"${d.sha1}"`) {
    res.status(304).end();
    return;
  }
  res.type('application/json').send(readFileSync(resolve(DATA_DIR, name)));
});
