@echo off
rem Stop everything start-all.bat started (game server, Caddy, auth server),
rem then back up the database. Asks first. Details: stop-all.ps1 and README.md.
title Valhalla - stop all
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0stop-all.ps1"
echo.
pause
