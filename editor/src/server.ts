/**
 * Tiny Express file server for the game editor.
 * Serves the shared/data JSON files and accepts writes, plus:
 *   - /api/thumbs/:file      top-down zone captures + their metadata in maps/thumbs/
 *   - /api/assets/mesh-ids   art ids from the UE import directory
 *   - /api/assets/icons      item icon PNGs from Import/UI/Icons (served at /assets/icons/)
 *   - /api/assets/class-icons class icon PNGs from Import/UI/ClassIcons (served at /assets/class-icons/)
 *   - /api/admin/*           proxied to the UE admin HTTP API (VALHALLA_ADMIN_URL)
 *   - /api/accounts/*        account management (B-12), straight to the account backend
 *   - /api/validate          B-13 cross-reference check of the saved files (shared validator)
 *   - /api/validate/context  the on-disk context (meshes, icons, Unreal refs) the
 *                            Validation page needs to check unsaved edits in the browser
 *   - /api/data-sync         B-13 live-vs-disk: the game server's loaded data hashes (from the
 *                            admin API's /state) vs the files on disk
 *
 * The 1.0 Tiled map, 1.0 overlay and sprite routes were retired with the 1.0
 * client (git tag archive/1.0-final). The 2.0 zone overlays (maps/overlays-2.0)
 * were retired too: portals, zone entries, player starts and NPC Spawn Points
 * are placed in Unreal, and the Live Dashboard reads them from /api/admin/state.
 */
// First: VALHALLA_* from secrets.local.env must be in process.env before ADMIN_SECRET is read.
import { SECRETS_FILE, loadedFromSecretsFile } from './loadEnv.js';
import express from 'express';
import cors from 'cors';
import { summarizeIssues, validateGameData } from '@valhalla/shared';
import { loadValidationInput } from '../../shared/src/validation-load.js';
import fs from 'fs';
import path from 'path';
import { createHash } from 'crypto';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.resolve(__dirname, '../../');
const DATA_DIR = path.join(PROJECT_ROOT, 'shared/data');
const MAPS_DIR = path.join(PROJECT_ROOT, 'maps');
const THUMBS_DIR = path.join(MAPS_DIR, 'thumbs');

// ── Valhalla 2.0 (Unreal) configuration ──────────────────────────────────
/** Admin HTTP API of the UE dedicated server. Same contract as server/src/routes/admin.ts. */
const ADMIN_URL = (process.env.VALHALLA_ADMIN_URL || 'http://localhost:2568').replace(/\/+$/, '');

/** The account backend (server/), for the Accounts dialog. Loopback, like the game server's own BackendUrl. */
const BACKEND_URL = (process.env.VALHALLA_BACKEND_URL || 'http://127.0.0.1:2567').replace(/\/+$/, '');

/**
 * Shared secret the UE admin API checks (`Authorization: Bearer <secret>`,
 * UValhallaDataSettings::ServerSecret / bAdminApiRequireSecret). The default
 * is the development value the game server and the backend fall back to;
 * secrets.local.env (loadEnv.ts) or the environment set the real one.
 */
const ADMIN_SECRET = process.env.VALHALLA_SERVER_SECRET || 'dev-server-secret';

/**
 * Resolve the Valhalla 2.0 Import directory (holds the equipment meshes).
 * Env `VALHALLA2_IMPORT_DIR` wins; it may be absolute or relative to the repo root.
 * Otherwise a few sensible neighbours of the repo are probed.
 */
function resolveImportDir(): { dir: string; found: boolean } {
  const candidates: string[] = [];
  const envDir = process.env.VALHALLA2_IMPORT_DIR;
  if (envDir) {
    candidates.push(path.isAbsolute(envDir) ? envDir : path.resolve(PROJECT_ROOT, envDir));
  }
  // The repo root holds Import/ beside Valhalla2/ (the Unreal project).
  candidates.push(path.resolve(PROJECT_ROOT, 'Import'));
  for (const c of candidates) {
    if (fs.existsSync(path.join(c, 'Characters', 'Equipment'))) return { dir: c, found: true };
  }
  for (const c of candidates) {
    if (fs.existsSync(c)) return { dir: c, found: true };
  }
  return { dir: candidates[0], found: false };
}

const IMPORT = resolveImportDir();
const IMPORT_DIR = IMPORT.dir;
const EQUIPMENT_MESH_DIR = path.join(IMPORT_DIR, 'Characters', 'Equipment');

const app = express();

// CORS: the editor's own front end only (B-04). This server writes the data
// files and forwards admin actions to the game server *with the server secret
// attached*, so a page on any other origin must not be able to call it from
// the browser. Same CORS_ORIGINS list (and default) as the backend. A request
// with a foreign Origin is refused outright; none (curl, scripts) passes.
const CORS_ORIGINS = new Set(
  (process.env.CORS_ORIGINS?.trim() || 'http://localhost:5180,http://127.0.0.1:5180')
    .split(',').map(s => s.trim().replace(/\/+$/, '')).filter(Boolean),
);
app.use((req, res, next) => {
  const origin = req.headers.origin;
  if (origin && !CORS_ORIGINS.has(origin)) {
    res.status(403).json({ error: 'Origin not allowed.' });
    return;
  }
  next();
});
app.use(cors({ origin: [...CORS_ORIGINS] }));
app.use(express.json({ limit: '10mb' }));

// GET /api/data/:filename — read a JSON data file
app.get('/api/data/:filename', (req, res) => {
  const filePath = path.join(DATA_DIR, req.params.filename);
  if (!filePath.startsWith(DATA_DIR)) return res.status(403).json({ error: 'Forbidden' });
  if (!fs.existsSync(filePath)) return res.status(404).json({ error: 'Not found' });
  try {
    const content = fs.readFileSync(filePath, 'utf-8');
    res.json(JSON.parse(content));
  } catch (e) {
    res.status(500).json({ error: 'Failed to read file' });
  }
});

// PUT /api/data/:filename — write a JSON data file
app.put('/api/data/:filename', (req, res) => {
  const filePath = path.join(DATA_DIR, req.params.filename);
  if (!filePath.startsWith(DATA_DIR)) return res.status(403).json({ error: 'Forbidden' });
  try {
    fs.writeFileSync(filePath, JSON.stringify(req.body, null, 2) + '\n');
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ error: 'Failed to write file' });
  }
});

// ═══ Valhalla 2.0 routes ═══════════════════════════════════════════════

// GET /api/config — what this editor is pointed at (used by the UI status lines)
app.get('/api/config', (_req, res) => {
  res.json({
    adminUrl: ADMIN_URL,
    importDir: IMPORT_DIR,
    importDirFound: IMPORT.found,
    thumbsDir: THUMBS_DIR,
  });
});

// GET /api/thumbs — list zones that have a top-down capture
app.get('/api/thumbs', (_req, res) => {
  if (!fs.existsSync(THUMBS_DIR)) return res.json({ zones: [] });
  try {
    const zones = fs.readdirSync(THUMBS_DIR)
      .filter(f => f.endsWith('.png'))
      .map(f => f.slice(0, -4))
      .filter(z => fs.existsSync(path.join(THUMBS_DIR, `${z}.json`)))
      .sort();
    res.json({ zones });
  } catch {
    res.status(500).json({ error: 'Failed to list thumbs' });
  }
});

// GET /api/thumbs/:zone.png | :zone.json — the capture and its metadata (static)
app.get('/api/thumbs/:file', (req, res) => {
  const file = req.params.file;
  if (!/^[A-Za-z0-9_-]+\.(png|json)$/.test(file)) {
    return res.status(400).json({ error: 'Expected <zone>.png or <zone>.json' });
  }
  const filePath = path.join(THUMBS_DIR, file);
  if (!filePath.startsWith(THUMBS_DIR)) return res.status(403).json({ error: 'Forbidden' });
  if (!fs.existsSync(filePath)) return res.status(404).json({ error: `No thumb: ${file}` });
  res.sendFile(filePath);
});

// Item icons: the source PNGs in Import/UI/Icons (Unreal imports the same files)
const ICONS_DIR = path.join(IMPORT_DIR, 'UI', 'Icons');
app.use('/assets/icons', express.static(ICONS_DIR));
app.get('/api/assets/icons', (_req, res) => {
  if (!fs.existsSync(ICONS_DIR)) return res.json({ files: [] });
  try {
    const files = fs.readdirSync(ICONS_DIR).filter(f => /\.png$/i.test(f)).sort();
    res.json({ files });
  } catch {
    res.status(500).json({ error: 'Failed to read the icons directory' });
  }
});

// Class icons (B-08a): the kit's PNGs in Import/UI/ClassIcons (Tools/ui/make_class_icons.py)
const CLASS_ICONS_DIR = path.join(IMPORT_DIR, 'UI', 'ClassIcons');
app.use('/assets/class-icons', express.static(CLASS_ICONS_DIR));
app.get('/api/assets/class-icons', (_req, res) => {
  if (!fs.existsSync(CLASS_ICONS_DIR)) return res.json({ files: [] });
  try {
    const files = fs.readdirSync(CLASS_ICONS_DIR).filter(f => /\.png$/i.test(f)).sort();
    res.json({ files });
  } catch {
    res.status(500).json({ error: 'Failed to read the class icons directory' });
  }
});

// GET /api/assets/mesh-ids — valid art ids for FValhallaItemTemplate (meshId ?? spriteId)
app.get('/api/assets/mesh-ids', (_req, res) => {
  if (!fs.existsSync(EQUIPMENT_MESH_DIR)) {
    const warning = `Valhalla 2.0 import directory not found: ${EQUIPMENT_MESH_DIR} — set VALHALLA2_IMPORT_DIR`;
    console.warn(`  [mesh-ids] ${warning}`);
    return res.json({ ids: [], dir: EQUIPMENT_MESH_DIR, warning });
  }
  try {
    const ids = Array.from(new Set(
      fs.readdirSync(EQUIPMENT_MESH_DIR)
        .filter(f => /\.(glb|gltf)$/i.test(f))
        .map(f => f.replace(/\.(glb|gltf)$/i, '').replace(/^(SK|SM)_/, ''))
    )).sort();
    res.json({ ids, dir: EQUIPMENT_MESH_DIR });
  } catch (e: any) {
    res.status(500).json({ error: `Failed to read mesh dir: ${e.message}` });
  }
});

// ── Admin proxy — forward /api/admin/* to the Valhalla 2.0 (UE) admin API ───
/** Only these actions are forwarded; anything else is a 404 from the editor. */
const ADMIN_ACTIONS = new Set([
  'state',
  'spawn-npc', 'drop-item', 'kick-player', 'teleport-player',
  'kill-npc', 'respawn-npc', 'delete-npc',
  'reload-data',
  // 2.0 MMO admin actions
  'player-action', 'player-inspect', 'broadcast', 'spawn-point-action', 'account-action',
]);

app.all('/api/admin/:action', async (req, res) => {
  const action = req.params.action;
  if (!ADMIN_ACTIONS.has(action)) {
    return res.status(404).json({ error: `Unknown admin action: ${action}`, allowed: [...ADMIN_ACTIONS] });
  }
  const url = `${ADMIN_URL}/api/admin/${action}`;
  try {
    const fetchOpts: RequestInit = {
      method: req.method,
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${ADMIN_SECRET}`,
      },
    };
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      fetchOpts.body = JSON.stringify(req.body ?? {});
    }
    const upstream = await fetch(url, fetchOpts);
    const text = await upstream.text();
    let data: any;
    try { data = text ? JSON.parse(text) : {}; }
    catch { data = { error: 'Upstream returned non-JSON', body: text.slice(0, 500) }; }
    res.status(upstream.status).json(data);
  } catch (err: any) {
    res.status(502).json({
      error: `Valhalla 2.0 admin API unreachable: ${err.message}`,
      adminUrl: ADMIN_URL,
      action,
      online: false,
    });
  }
});

// ── Validation and live-vs-disk (B-13) ───────────────────────────────────

/** GET /api/validate — the saved files, checked by the same rules as `npm run validate`. */
app.get('/api/validate', (_req, res) => {
  try {
    const { input, problems } = loadValidationInput(PROJECT_ROOT);
    const issues = [
      ...problems.map(p => ({ severity: 'error' as const, category: 'items' as const, id: p.file, message: p.message })),
      ...validateGameData(input),
    ];
    res.json({ summary: summarizeIssues(issues), issues, checkedAt: Date.now() });
  } catch (e: any) {
    res.status(500).json({ error: `Validation failed to run: ${e.message}` });
  }
});

/** GET /api/validate/context — everything but the data itself, for checking unsaved edits in the browser. */
app.get('/api/validate/context', (_req, res) => {
  try {
    const { input, problems } = loadValidationInput(PROJECT_ROOT);
    res.json({
      meshFiles: input.meshFiles ?? null,
      iconFiles: input.iconFiles ?? null,
      classIconFiles: input.classIconFiles ?? null,
      unrealRefs: input.unrealRefs ?? null,
      problems,
    });
  } catch (e: any) {
    res.status(500).json({ error: `Could not read the validation context: ${e.message}` });
  }
});

/** The data files the game server loads (UValhallaDataSubsystem::GetDataFilenames). */
const DATA_FILES = ['classes.json', 'items.json', 'skills.json', 'npc-templates.json', 'loot-tables.json', 'zones.json', 'ui-config.json'];

/**
 * GET /api/data-sync — compare what the running game server loaded (SHA-1 per
 * file, from its admin state) with the files on disk now.
 * -> { online, supported, loadedAt, allMatch, files: [{ name, disk, live, match }] }
 */
app.get('/api/data-sync', async (_req, res) => {
  const disk: Record<string, string | null> = {};
  for (const name of DATA_FILES) {
    try { disk[name] = createHash('sha1').update(fs.readFileSync(path.join(DATA_DIR, name))).digest('hex'); }
    catch { disk[name] = null; }
  }
  let state: any;
  try {
    const upstream = await fetch(`${ADMIN_URL}/api/admin/state`, { headers: { Authorization: `Bearer ${ADMIN_SECRET}` } });
    if (!upstream.ok) return res.json({ online: false, supported: false, error: `admin API answered HTTP ${upstream.status}` });
    state = await upstream.json();
  } catch {
    return res.json({ online: false, supported: false });
  }
  const live: Record<string, string> | undefined = state?.data?.files;
  if (!live) return res.json({ online: true, supported: false });
  const files = DATA_FILES.map(name => ({
    name,
    disk: disk[name],
    live: live[name] ?? null,
    match: !!disk[name] && disk[name] === live[name],
  }));
  res.json({
    online: true,
    supported: true,
    loadedAt: state.data.loadedAt ?? null,
    downloadedCopy: !!state.data.downloadedCopy,
    allMatch: files.every(f => f.match),
    files,
  });
});

// ── Account management (B-12) ─────────────────────────────────────────────
// The Live Dashboard's Accounts dialog. These go straight to the account
// backend's server-to-server routes with the server secret, so accounts can be
// looked up, reset, banned or deleted whether or not the game server runs.
// After a ban, password reset or deletion the game server (when it is up) is
// asked to kick that account's live sessions; it's best-effort and reported.

async function backendCall(method: 'GET' | 'POST', pathAndQuery: string, body?: unknown): Promise<{ status: number; data: any }> {
  try {
    const upstream = await fetch(`${BACKEND_URL}${pathAndQuery}`, {
      method,
      headers: { 'Content-Type': 'application/json', 'X-Server-Secret': ADMIN_SECRET },
      body: method === 'POST' ? JSON.stringify(body ?? {}) : undefined,
    });
    const text = await upstream.text();
    let data: any;
    try { data = text ? JSON.parse(text) : {}; } catch { data = { error: 'Backend returned non-JSON', body: text.slice(0, 500) }; }
    return { status: upstream.status, data };
  } catch (err: any) {
    return { status: 502, data: { error: `Account backend unreachable at ${BACKEND_URL}: ${err.message}`, backendUrl: BACKEND_URL } };
  }
}

/** Ask the game server to disconnect every session of an account. Never throws. */
async function kickAccount(userId: number, message: string): Promise<string> {
  try {
    const upstream = await fetch(`${ADMIN_URL}/api/admin/account-action`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${ADMIN_SECRET}` },
      body: JSON.stringify({ action: 'kick', userId, message }),
    });
    const data: any = await upstream.json().catch(() => ({}));
    if (upstream.ok) return data?.message || 'kicked';
    return `game server did not kick: ${data?.error || `HTTP ${upstream.status}`}`;
  } catch {
    return 'game server offline, nobody to kick';
  }
}

app.get('/api/accounts/search', async (req, res) => {
  const q = typeof req.query.q === 'string' ? req.query.q : '';
  const banned = req.query.banned === '1' ? '&banned=1' : '';
  const r = await backendCall('GET', `/api/accounts/search?q=${encodeURIComponent(q)}&limit=100${banned}`);
  res.status(r.status).json(r.data);
});

app.get('/api/accounts/detail', async (req, res) => {
  const userId = Number(req.query.userId);
  if (!Number.isInteger(userId) || userId <= 0) return res.status(400).json({ error: 'userId required' });
  const r = await backendCall('GET', `/api/accounts/detail?userId=${userId}`);
  res.status(r.status).json(r.data);
});

app.get('/api/accounts/bans', async (_req, res) => {
  const r = await backendCall('GET', '/api/accounts/bans');
  res.status(r.status).json(r.data);
});

const ACCOUNT_POSTS: Record<string, { kick?: (data: any, body: any) => string | null }> = {
  ban: { kick: data => data?.ban?.banned ? (data.message || 'This account has been banned.') : null },
  unban: {},
  'reset-password': { kick: () => 'Your password was reset by an administrator. Log in with the temporary password you were given.' },
  delete: { kick: () => 'This account has been deleted.' },
  'rename-character': { kick: () => 'One of your characters was renamed by an administrator. Please log in again.' },
};

app.post('/api/accounts/:action', async (req, res) => {
  const action = req.params.action;
  const spec = ACCOUNT_POSTS[action];
  if (!spec) return res.status(404).json({ error: `Unknown account action: ${action}`, allowed: Object.keys(ACCOUNT_POSTS) });
  const body = { ...(req.body ?? {}), by: 'web editor' };
  const r = await backendCall('POST', `/api/accounts/${action}`, body);
  if (r.status === 200 && spec.kick) {
    const message = spec.kick(r.data, body);
    const userId = Number(r.data?.userId);
    if (message && Number.isInteger(userId) && userId > 0) {
      r.data.kick = await kickAccount(userId, message);
    }
  }
  res.status(r.status).json(r.data);
});

const PORT = Number(process.env.EDITOR_PORT || 5181);
// Loopback only: this server writes game data with no login and attaches the
// admin secret to whatever it proxies, so nothing off this machine may reach it.
// EDITOR_HOST=0.0.0.0 opens it to the LAN deliberately.
const HOST = process.env.EDITOR_HOST || '127.0.0.1';
app.listen(PORT, HOST, () => {
  console.log(`\n  Valhalla Editor API server running on http://${HOST}:${PORT}`);
  console.log(`  Data dir:        ${DATA_DIR}`);
  console.log(`  Maps dir:        ${MAPS_DIR}`);
  console.log(`  Zone thumbs:     ${THUMBS_DIR}`);
  console.log(`  UE import dir:   ${IMPORT_DIR}${IMPORT.found ? '' : '  (NOT FOUND — set VALHALLA2_IMPORT_DIR)'}`);
  console.log(`  UE admin API:    ${ADMIN_URL}  (VALHALLA_ADMIN_URL)`);
  console.log(`  Account backend: ${BACKEND_URL}  (VALHALLA_BACKEND_URL)`);
  console.log(`  Admin secret:    ${loadedFromSecretsFile.includes('VALHALLA_SERVER_SECRET') ? `from ${SECRETS_FILE}` : process.env.VALHALLA_SERVER_SECRET ? 'from VALHALLA_SERVER_SECRET' : 'dev default (no secrets.local.env)'}\n`);
});
