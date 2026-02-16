import { defineServer, defineRoom } from '@colyseus/core';
import { WebSocketTransport } from '@colyseus/ws-transport';
import { GameRoom } from './rooms/GameRoom.js';
import { SERVER_PORT, ROOM_NAME } from '@valhalla/shared';
const server = defineServer({
    transport: new WebSocketTransport({}),
    rooms: {
        [ROOM_NAME]: defineRoom(GameRoom),
    },
    express: (app) => {
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
        app.get('/', (_req, res) => {
            res.json({ status: 'Valhalla server running' });
        });
    },
});
server.listen(SERVER_PORT, '0.0.0.0').then(() => {
    console.log(`⚔️  Valhalla server listening on 0.0.0.0:${SERVER_PORT}`);
});
//# sourceMappingURL=index.js.map