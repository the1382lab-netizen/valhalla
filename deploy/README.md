# Hosting a test session

Before a session with outside players, go through
[GOING_LIVE.md](GOING_LIVE.md) (secrets, ports, CORS, rate limits, client
audit, backups).

Everything runs on this PC. Testers get a packaged client that already points at
the public address (`PublicBackendUrl` / `PublicGameServerAddress` in
`Valhalla2/Config/DefaultGame.ini`, currently `184.96.133.165`).

| Process | Script | Listens on | Reached by |
|---|---|---|---|
| Account backend ("auth server") | `start-auth.bat` | 127.0.0.1:2567 (loopback only), or 0.0.0.0:2567 with [home-network play](#letting-another-pc-in-the-house-connect) | the game server directly; testers through Caddy; PCs in the house directly (home-network play only) |
| HTTPS in front of it (Caddy) | `start-https.bat` | TCP 443 (and 80 for certificate renewal) | testers' clients |
| Game server (Unreal, dedicated) | `start-gameserver.bat` | UDP 7777 | testers' clients |
| Web editor + admin API | `npm run dev:editor`, 2568 | loopback only | you |

## One-time setup

1. **Secrets.** `secrets.local.env` at the repo root (gitignored) holds
   `NODE_ENV=production`, `HOST=127.0.0.1`, `JWT_SECRET` and
   `VALHALLA_SERVER_SECRET`. The backend, the editor's admin proxy and the game
   server all read it, so nothing needs exporting by hand. Never commit it and
   never put the server secret in an ini under `Config/`: those ship with the
   client. To make a new one, copy the layout and generate values with
   `node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"`.
2. **Caddy 2.11 or newer.** Download the Windows amd64 zip from
   https://github.com/caddyserver/caddy/releases and put `caddy.exe` in this
   folder (gitignored) or on PATH.
3. **Router: forward to this PC**
   - UDP 7777 (game server)
   - TCP 443 (HTTPS to the backend)
   - TCP 80 (Let's Encrypt certificate checks; Caddy only answers the challenge and redirects)

   Do **not** forward 2567, 2568, 5180 or 5181.
4. **Windows firewall.** Allow `UnrealEditor.exe` and `caddy.exe` when Windows
   asks the first time each one listens (private and public networks).

## Each session

1. `start-auth.bat`: its log should show `NODE_ENV=production` and
   `Server-to-server routes enabled`.
2. `start-https.bat`: the first run fetches the certificate. Check it by
   opening https://184.96.133.165/api/health in a browser (no certificate warning).
3. `start-gameserver.bat`: the log should show `Server secret: from ...secrets.local.env`.
   Don't press Play in the editor while it runs (both want admin port 2568).
4. The web editor works as usual; its **Reload data** and auto-reload reach
   this server.

## Letting another PC in the house connect

B-17 decision (2026-09-25): a PC on your home network connects straight to this
PC's local IP, with the launcher's **Settings → Advanced → Server → Custom**
(or the launch options below until the launcher exists). Testers outside the
house keep using **Online** (the public IP through Caddy), and a packaged build
on this PC uses **This PC** (`127.0.0.1`). Home-network play needs this
one-time setup; without it the backend only answers this PC.

1. **Backend on every interface.** In `secrets.local.env`, change
   `HOST=127.0.0.1` to `HOST=0.0.0.0`, then restart `start-auth.bat`. Its log
   should say `listening on 0.0.0.0:2567`. Caddy still reaches it on
   `127.0.0.1:2567`, so testers are unaffected.
2. **Your home network is Private in Windows.** Settings → Network & internet →
   your connection → Network profile type: **Private**. The rules below only
   apply on Private networks, so they close again on public Wi-Fi.
3. **Firewall rules** (PowerShell as administrator):

   ```powershell
   New-NetFirewallRule -DisplayName "Valhalla backend (home network)" -Direction Inbound -Protocol TCP -LocalPort 2567 -Profile Private -RemoteAddress LocalSubnet -Action Allow
   New-NetFirewallRule -DisplayName "Valhalla game server (home network)" -Direction Inbound -Protocol UDP -LocalPort 7777 -Profile Private -RemoteAddress LocalSubnet -Action Allow
   ```

   The second rule is only needed if `UnrealEditor.exe` isn't already allowed
   on private networks; it does no harm either way.
4. **This PC's local IP.** `ipconfig` → *IPv4 Address* (e.g. `192.168.1.50`).
   Reserve it for this PC in the router's DHCP settings so it doesn't change.
5. **Check from the other PC:** open `http://<local IP>:2567/api/health` in a
   browser. Then play with Server set to Custom and that IP, or launch a
   packaged build with
   `-ValhallaBackendUrl=http://<local IP>:2567 -ValhallaGameServer=<local IP>:7777`.

Still true after this: the router does **not** forward 2567, so the backend is
not reachable from the internet, and the admin API (2568) stays loopback only.
To turn home-network play off again, set `HOST=127.0.0.1` and restart
`start-auth.bat`.

## Building the client for testers

1. In the Unreal editor's Python console:
   `py "<project>/Plugins/ValhallaTools/Content/Python/valhalla_tools/stage_game_data.py"`
   (copies `shared/data` into the build).
2. **Platforms → Windows → Package Project** (Development). Zip the output
   folder and send it.
3. The client and the game server must come from the same commit.

Launch options, for when the defaults don't fit:

- `-ValhallaBackendUrl=<url>` / `-ValhallaGameServer=<host:port>`: point a
  build somewhere else without rebuilding (a changed public IP, or a packaged
  client on this PC if the router has no NAT loopback:
  `-ValhallaBackendUrl=http://127.0.0.1:2567 -ValhallaGameServer=127.0.0.1:7777`).
- `-ValhallaServerSecret=<secret>` (servers only): overrides the file, e.g. for a
  server machine without the repo. The environment variable
  `VALHALLA_SERVER_SECRET` does the same.

## What is and isn't protected

- Logins and all backend traffic from testers are HTTPS. With home-network
  play on, PCs in the house talk to the backend over plain HTTP on 2567: anyone
  on your home Wi-Fi can reach the login endpoint, and their passwords cross
  the local network unencrypted. The firewall rule limits this to Private
  networks and the local subnet.
- The game connection itself (UDP 7777) is Unreal's normal unencrypted
  netcode. The login token travels in the join URL and is valid for 24 hours;
  fine for a friends test, worth revisiting (Unreal packet encryption) before a
  public one. Valhalla's own log lines cut the token to 8 characters, but the
  engine's `LogNet: Browse` / `Login request` lines print the whole URL, so
  don't share raw logs.
- Browser origins: the backend, the web editor's API server and the admin API
  only answer the web editor's origins (`CORS_ORIGINS`,
  `AdminApiAllowedOrigins`). Login and register are limited to 10 per minute per
  IP, character creation to 5.
- A game server started outside the editor (`start-gameserver.bat`) exits at
  startup if its secret is missing or is `dev-server-secret`.
- A residential IP can change. If it does, edit the `Caddyfile` and the two
  `Public*` lines in `DefaultGame.ini`, and send testers the new launch option
  or a new build.

## Testing multiplayer in PIE

- **Start Play from `L_FrontEnd`**, not `L_World`. Play settings: Net Mode
  *Play Standalone*, **Number of Players 2** (or more), *Launch Separate Server*
  on, *Run Under One Process* on. The dedicated server sees it has loaded the
  front-end map and travels itself to `L_World`; each client gets the login
  screen, logs in through the backend (`npm run dev:server` must be running)
  and joins with its token, so accounts, saves, bans and the admin `state`
  behave as they will for testers.
- To skip typing, set `valhalla.AutoLogin "user1:pass:Char1:warrior,user2:pass:Char2:wizard"`
  in the editor console before Play: entries are dealt to the clients in start
  order, and missing accounts and characters are created.
- Starting Play from `L_World` with *Launch Separate Server* gives an
  **offline copy**: the clients join without a token (dev sessions), so there is
  no account, nothing is saved, and account actions (ban) refuse them.
- Don't run `start-gameserver.bat` at the same time: PIE's server wants admin
  port 2568 too.
