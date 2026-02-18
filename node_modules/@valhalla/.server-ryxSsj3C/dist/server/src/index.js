import { defineServer, defineRoom } from '@colyseus/core';
import { WebSocketTransport } from '@colyseus/ws-transport';
import { GameRoom } from './rooms/GameRoom.js';
import { SERVER_PORT, ROOM_NAME } from '@valhalla/shared';
import { initDatabase } from './db/index.js';
import { authRouter } from './routes/auth.js';
import { charactersRouter } from './routes/characters.js';
import express from 'express';
import { resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const DATA_DIR = resolve(__dirname, '..', '..', 'shared', 'data');
// Initialize database (async — sql.js loads WASM) then start the server
await initDatabase();
const server = defineServer({
    transport: new WebSocketTransport({}),
    rooms: {
        [ROOM_NAME]: defineRoom(GameRoom),
    },
    express: (app) => {
        // JSON body parsing
        app.use(express.json());
        // CORS for dev — allow any origin on the LAN, with credentials
        app.use((req, res, next) => {
            const origin = req.headers.origin;
            if (origin) {
                res.header('Access-Control-Allow-Origin', origin);
            }
            res.header('Access-Control-Allow-Headers', 'Origin, X-Requested-With, Content-Type, Accept, Authorization');
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
        // Serve shared/data/*.json files so clients can load editor content
        app.use('/api/data', express.static(DATA_DIR));
        // Auth & character API routes
        app.use('/api/auth', authRouter);
        app.use('/api/characters', charactersRouter);
    },
});
server.listen(SERVER_PORT, '0.0.0.0').then(() => {
    console.log(`⚔️  Valhalla server listening on 0.0.0.0:${SERVER_PORT}`);
});
//# sourceMappingURL=index.js.map