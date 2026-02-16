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
    // CORS for dev — must allow credentials for Colyseus 0.17 SDK matchmaking
    app.use((_req, res, next) => {
      res.header('Access-Control-Allow-Origin', 'http://localhost:3000');
      res.header('Access-Control-Allow-Headers', 'Origin, X-Requested-With, Content-Type, Accept, Authorization');
      res.header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
      res.header('Access-Control-Allow-Credentials', 'true');
      if (_req.method === 'OPTIONS') {
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

server.listen(SERVER_PORT).then(() => {
  console.log(`⚔️  Valhalla server listening on ws://localhost:${SERVER_PORT}`);
});
