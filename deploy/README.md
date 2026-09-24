# Hosting a test session

Everything runs on this PC. Testers get a packaged client that already points at
the public address (`PublicBackendUrl` / `PublicGameServerAddress` in
`Valhalla2/Config/DefaultGame.ini`, currently `184.96.133.165`).

| Process | Script | Listens on | Reached by |
|---|---|---|---|
| Account backend ("auth server") | `start-auth.bat` | 127.0.0.1:2567 (loopback only) | the game server directly; testers through Caddy |
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

- Logins and all backend traffic from testers are HTTPS.
- The game connection itself (UDP 7777) is Unreal's normal unencrypted
  netcode. The login token travels in the join URL and is valid for 24 hours;
  fine for a friends test, worth revisiting (Unreal packet encryption) before a
  public one.
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
