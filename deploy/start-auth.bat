@echo off
rem Account backend (accounts, characters, game data) on 127.0.0.1:2567.
rem Reads NODE_ENV, HOST, JWT_SECRET and VALHALLA_SERVER_SECRET from
rem secrets.local.env at the repo root by itself. Run from the server folder so
rem it uses the same database file (server\valhalla.db) as npm run dev:server.
title Valhalla - auth server
cd /d "%~dp0..\server"
call npx tsx src/index.ts
pause
