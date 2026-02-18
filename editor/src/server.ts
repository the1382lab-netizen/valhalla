/**
 * Tiny Express file server for the game editor.
 * Serves JSON data files and map overlays, accepts writes.
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
const OVERLAYS_DIR = path.join(MAPS_DIR, 'overlays');

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

// GET /api/maps — list available map files
app.get('/api/maps', (_req, res) => {
  try {
    const files = fs.readdirSync(MAPS_DIR).filter(f => f.endsWith('.json'));
    res.json({ maps: files });
  } catch (e) {
    res.status(500).json({ error: 'Failed to list maps' });
  }
});

// GET /api/maps/:filename — read a Tiled map JSON
app.get('/api/maps/:filename', (req, res) => {
  const filePath = path.join(MAPS_DIR, req.params.filename);
  if (!filePath.startsWith(MAPS_DIR)) return res.status(403).json({ error: 'Forbidden' });
  if (!fs.existsSync(filePath)) return res.status(404).json({ error: 'Not found' });
  try {
    const content = fs.readFileSync(filePath, 'utf-8');
    res.json(JSON.parse(content));
  } catch (e) {
    res.status(500).json({ error: 'Failed to read map' });
  }
});

// GET /api/overlays/:filename — read a map overlay
app.get('/api/overlays/:filename', (req, res) => {
  const filePath = path.join(OVERLAYS_DIR, req.params.filename);
  if (!filePath.startsWith(OVERLAYS_DIR)) return res.status(403).json({ error: 'Forbidden' });
  if (!fs.existsSync(filePath)) {
    // Return empty overlay if file doesn't exist yet
    return res.json({ version: '1.0.0', zoneId: '', spawnPoints: [], zoneConnections: [] });
  }
  try {
    const content = fs.readFileSync(filePath, 'utf-8');
    res.json(JSON.parse(content));
  } catch (e) {
    res.status(500).json({ error: 'Failed to read overlay' });
  }
});

// PUT /api/overlays/:filename — write a map overlay
app.put('/api/overlays/:filename', (req, res) => {
  const filePath = path.join(OVERLAYS_DIR, req.params.filename);
  if (!filePath.startsWith(OVERLAYS_DIR)) return res.status(403).json({ error: 'Forbidden' });
  try {
    if (!fs.existsSync(OVERLAYS_DIR)) fs.mkdirSync(OVERLAYS_DIR, { recursive: true });
    fs.writeFileSync(filePath, JSON.stringify(req.body, null, 2) + '\n');
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ error: 'Failed to write overlay' });
  }
});

const PORT = 5181;
app.listen(PORT, () => {
  console.log(`\n  Valhalla Editor API server running on http://localhost:${PORT}`);
  console.log(`  Data dir: ${DATA_DIR}`);
  console.log(`  Maps dir: ${MAPS_DIR}\n`);
});
