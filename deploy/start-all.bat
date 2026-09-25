@echo off
rem Start a hosting session in one click: back up the database, start the auth
rem server, Caddy (HTTPS) and the game server (restarts by itself if it stops).
rem Each opens in its own window. Details: start-all.ps1 and README.md.
title Valhalla - start all
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-all.ps1"
echo.
pause
