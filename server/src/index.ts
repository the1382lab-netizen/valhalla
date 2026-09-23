/**
 * Valhalla account backend.
 *
 * Plain Express over sql.js: accounts, characters, the game data files, and
 * the server-to-server routes the Unreal dedicated server calls with
 * X-Server-Secret. (The 1.0 Colyseus game room that used to share this process
 * was retired with the 1.0 client; its last state is the git tag
 * archive/1.0-final.)
 */
import { createServer } from 'http';
import express from 'express';
import { SERVER_PORT } from '@valhalla/shared';
import { initDatabase } from './db/index.js';
import { authRouter } from './routes/auth.js';
import { charactersRouter } from './routes/characters.js';
import { internalRouter } from './routes/internal.js';
import { dataRouter } from './routes/data.js';
import { logServerSecretStatus } from './middleware/serverSecret.js';
import { DataManager } from './systems/DataManager.js';

// Initialize database (async — sql.js loads WASM) then start the server.
// VALHALLA_DB overrides the database file (used by scripts/smoke-internal.ts).
await initDatabase(process.env.VALHALLA_DB || 'valhalla.db');

// Load shared/data now and keep it current. Character creation (the class
// kit) and the Unreal server's character save validation read it.
DataManager.initializeAndWatch();

// Report whether the server-to-server (Unreal) routes are enabled
logServerSecretStatus();

// Listen port: PORT env wins, otherwise the shared default (2567)
const PORT = Number.parseInt(process.env.PORT ?? '', 10) || SERVER_PORT;

const app = express();

// JSON body parsing
app.use(express.json());

// CORS for dev — allow any origin on the LAN, with credentials
app.use((req, res, next) => {
  const origin = req.headers.origin;
  if (origin) {
    res.header('Access-Control-Allow-Origin', origin);
  }
  res.header('Access-Control-Allow-Headers', 'Origin, X-Requested-With, Content-Type, Accept, Authorization, X-Server-Secret');
  res.header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
  res.header('Access-Control-Allow-Credentials', 'true');
  if (req.method === 'OPTIONS') {
    res.sendStatus(200);
    return;
  }
  next();
});

// Health check
app.get('/', (_req, res) => {
  res.json({ status: 'Valhalla server running' });
});

// Game data for clients: GET /api/data/manifest and /api/data/<file>.json
app.use('/api/data', dataRouter);

// Server-to-server routes for the Unreal dedicated server (X-Server-Secret),
// plus the unauthenticated /api/health probe. Mounted BEFORE the player
// routers so /api/characters/:id/load|save is not intercepted by the
// JWT-protected charactersRouter; unmatched paths fall through untouched.
app.use('/api', internalRouter);

// Auth & character API routes
app.use('/api/auth', authRouter);
app.use('/api/characters', charactersRouter);

const server = createServer(app);
server.listen(PORT, '0.0.0.0', () => {
  console.log(`⚔️  Valhalla server listening on 0.0.0.0:${PORT}`);
});
