@echo off
rem Caddy: HTTPS on 443 for the auth server (see Caddyfile). Uses caddy.exe from
rem this folder if present, otherwise the one on PATH.
title Valhalla - https (Caddy)
cd /d "%~dp0"
set "CADDY=caddy"
if exist "%~dp0caddy.exe" set "CADDY=%~dp0caddy.exe"
"%CADDY%" run --config Caddyfile --adapter caddyfile
pause
