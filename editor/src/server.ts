/**
 * Tiny Express file server for the game editor.
 * Serves the shared/data JSON files and accepts writes, plus:
 *   - /api/overlays2/:zone   overlay 2.0 files (zone-local cm) in maps/overlays-2.0/
 *   - /api/thumbs/:file      top-down zone captures + their metadata in maps/thumbs/
 *   - /api/assets/mesh-ids   art ids from the UE import directory
 *   - /api/assets/icons      item icon PNGs from Import/UI/Icons (served at /assets/icons/)
 *   - /api/admin/*           proxied to the UE admin HTTP API (VALHALLA_ADMIN_URL)
 *
 * The 1.0 Tiled map, 1.0 overlay and sprite routes were retired with the 1.0
 * client (git tag archive/1.0-final).
 */
import express from 'express';
import cors from 'cors';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.resolve(__dirname, '../../');
const DATA_DIR = path.join(PROJECT_ROOT, 'shared/data');
const MAPS_DIR = path.join(PROJECT_ROOT, 'maps');
const OVERLAYS2_DIR = path.join(MAPS_DIR, 'overlays-2.0');
const THUMBS_DIR = path.join(MAPS_DIR, 'thumbs');

// ── Valhalla 2.0 (Unreal) configuration ──────────────────────────────────
/** Admin HTTP API of the UE dedicated server. Same contract as server/src/routes/admin.ts. */
const ADMIN_URL = (process.env.VALHALLA_ADMIN_URL || 'http://localhost:2568').replace(/\/+$/, '');

/**
 * Shared secret the UE admin API checks (`Authorization: Bearer <secret>`,
 * UValhallaDataSettings::ServerSecret / bAdminApiRequireSecret). The default
 * matches the development value in Valhalla2's Config/DefaultGame.ini and the
 * 1.0 backend's; set VALHALLA_SERVER_SECRET for anything that is not a dev box.
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

const SPAWN_TYPES = ['player_spawn', 'enemy_spawn', 'npc_spawn', 'portal', 'zone_entry'] as const;

/** Reject anything that isn't a bare zone id (no slashes, no dots, no traversal). */
function safeZoneId(raw: string): string | null {
  return /^[A-Za-z0-9_-]+$/.test(raw) ? raw : null;
}

/**
 * Validate an overlay 2.0 document. Returns the list of problems (empty = valid).
 * Coordinates are zone-local centimetres; the UE server reads this file verbatim.
 */
function validateOverlay2(body: any, zoneId: string): string[] {
  const errors: string[] = [];
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    return ['body must be a JSON object'];
  }
  if (body.version !== '2.0') {
    errors.push(`version must be "2.0" (got ${JSON.stringify(body.version)})`);
  }
  if (body.units !== undefined && body.units !== 'cm') {
    errors.push(`units must be "cm" (got ${JSON.stringify(body.units)})`);
  }
  if (body.zoneId !== undefined && body.zoneId !== zoneId) {
    errors.push(`zoneId "${body.zoneId}" does not match the URL zone "${zoneId}"`);
  }
  if (!Array.isArray(body.spawnPoints)) {
    errors.push('spawnPoints must be an array');
    return errors;
  }
  const seen = new Set<string>();
  body.spawnPoints.forEach((sp: any, i: number) => {
    const where = `spawnPoints[${i}]`;
    if (!sp || typeof sp !== 'object') { errors.push(`${where} must be an object`); return; }
    if (typeof sp.id !== 'string' || !sp.id.trim()) errors.push(`${where}.id must be a non-empty string`);
    else if (seen.has(sp.id)) errors.push(`${where}.id "${sp.id}" is duplicated`);
    else seen.add(sp.id);

    if (!SPAWN_TYPES.includes(sp.type)) {
      errors.push(`${where}.type must be one of ${SPAWN_TYPES.join(', ')} (got ${JSON.stringify(sp.type)})`);
    }
    for (const axis of ['x', 'y'] as const) {
      if (typeof sp[axis] !== 'number' || !Number.isFinite(sp[axis])) {
        errors.push(`${where}.${axis} must be a finite number (cm)`);
      }
    }
    if (sp.type === 'enemy_spawn' || sp.type === 'npc_spawn') {
      if (typeof sp.templateId !== 'string' || !sp.templateId.trim()) {
        errors.push(`${where}.templateId is required for ${sp.type}`);
      }
    }
    if (sp.type === 'portal' && (typeof sp.targetZone !== 'string' || !sp.targetZone.trim())) {
      errors.push(`${where}.targetZone is required for portal`);
    }
    if (sp.type === 'zone_entry' && (typeof sp.fromZone !== 'string' || !sp.fromZone.trim())) {
      errors.push(`${where}.fromZone is required for zone_entry`);
    }
    if (sp.count !== undefined && (!Number.isInteger(sp.count) || sp.count < 1)) {
      errors.push(`${where}.count must be a positive integer`);
    }
    if (sp.radius !== undefined && (typeof sp.radius !== 'number' || !Number.isFinite(sp.radius) || sp.radius < 0)) {
      errors.push(`${where}.radius must be a non-negative number (cm)`);
    }
    for (const f of ['templateId', 'targetZone', 'targetEntry', 'fromZone', 'label'] as const) {
      if (sp[f] !== undefined && typeof sp[f] !== 'string') errors.push(`${where}.${f} must be a string`);
    }
  });
  return errors;
}

const app = express();
app.use(cors());
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
    overlays2Dir: OVERLAYS2_DIR,
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

// GET /api/overlays2/:zone — overlay 2.0 (zone-local cm); skeleton when absent
app.get('/api/overlays2/:zone', (req, res) => {
  const zone = safeZoneId(req.params.zone.replace(/\.json$/, ''));
  if (!zone) return res.status(400).json({ error: 'Invalid zone id' });
  const filePath = path.join(OVERLAYS2_DIR, `${zone}.json`);
  if (!fs.existsSync(filePath)) {
    return res.json({ version: '2.0', units: 'cm', zoneId: zone, spawnPoints: [] });
  }
  try {
    res.json(JSON.parse(fs.readFileSync(filePath, 'utf-8')));
  } catch (e: any) {
    res.status(500).json({ error: `Failed to read overlay 2.0: ${e.message}` });
  }
});

// PUT /api/overlays2/:zone — validate + write an overlay 2.0 file
app.put('/api/overlays2/:zone', (req, res) => {
  const zone = safeZoneId(req.params.zone.replace(/\.json$/, ''));
  if (!zone) return res.status(400).json({ error: 'Invalid zone id' });

  const errors = validateOverlay2(req.body, zone);
  if (errors.length > 0) {
    return res.status(400).json({ error: 'Overlay 2.0 validation failed', errors });
  }

  const doc = {
    version: '2.0',
    units: 'cm',
    zoneId: zone,
    spawnPoints: req.body.spawnPoints,
  };
  try {
    if (!fs.existsSync(OVERLAYS2_DIR)) fs.mkdirSync(OVERLAYS2_DIR, { recursive: true });
    fs.writeFileSync(path.join(OVERLAYS2_DIR, `${zone}.json`), JSON.stringify(doc, null, 2) + '\n');
    res.json({ ok: true, zoneId: zone, spawnPoints: doc.spawnPoints.length });
  } catch (e: any) {
    res.status(500).json({ error: `Failed to write overlay 2.0: ${e.message}` });
  }
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
  'reload-overlays', 'reload-data',
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

const PORT = Number(process.env.EDITOR_PORT || 5181);
app.listen(PORT, () => {
  console.log(`\n  Valhalla Editor API server running on http://localhost:${PORT}`);
  console.log(`  Data dir:        ${DATA_DIR}`);
  console.log(`  Maps dir:        ${MAPS_DIR}`);
  console.log(`  Overlays 2.0:    ${OVERLAYS2_DIR}`);
  console.log(`  Zone thumbs:     ${THUMBS_DIR}`);
  console.log(`  UE import dir:   ${IMPORT_DIR}${IMPORT.found ? '' : '  (NOT FOUND — set VALHALLA2_IMPORT_DIR)'}`);
  console.log(`  UE admin API:    ${ADMIN_URL}  (VALHALLA_ADMIN_URL)`);
  console.log(`  Admin secret:    ${process.env.VALHALLA_SERVER_SECRET ? 'from VALHALLA_SERVER_SECRET' : 'dev default (set VALHALLA_SERVER_SECRET outside development)'}\n`);
});
