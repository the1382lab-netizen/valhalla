# Going live: checklist

Before any session that people outside this PC join. Tick each item. How to set
things up is in [README.md](README.md). This page only lists what to check.

## Secrets

- [ ] `secrets.local.env` exists at the repo root and is **not** tracked
      (`git check-ignore -v secrets.local.env` names `.gitignore`).
- [ ] `JWT_SECRET` and `VALHALLA_SERVER_SECRET` are freshly generated
      (`node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"`),
      at least 32 characters each, and different from each other. Neither is
      `valhalla-dev-secret-change-in-production` or `dev-server-secret`.
- [ ] `NODE_ENV=production` and `HOST=127.0.0.1` are in the same file.
- [ ] `start-auth.bat` log shows `NODE_ENV=production`,
      `Server-to-server routes enabled (VALHALLA_SERVER_SECRET set)` and
      `[cors] allowed origins: ...` with only the origins you expect.
      (With the dev JWT secret, production refuses to start. With the dev
      server secret, the internal routes answer 503.)
- [ ] `start-gameserver.bat` log shows `Server secret: from ...secrets.local.env`.
      If it prints `FATAL: this server resolved ...` and exits, the secret is
      missing or is the dev default. That refusal is on purpose.

## Network

- [ ] Caddy is up (`start-https.bat`), and https://&lt;public IP&gt;/api/health
      opens in a browser **without a certificate warning** (the Let's Encrypt
      IP certificate lasts 6 days; Caddy renews it while it runs).
- [ ] Router forwards **only** UDP 7777, TCP 443 and TCP 80 to this PC.
      Never 2567 (backend), 2568 (admin API), 5180/5181 (web editor).
- [ ] Windows firewall: `UnrealEditor.exe` and `caddy.exe` allowed. Nothing
      else listening on a public interface: `netstat -ano | findstr LISTENING`
      shows 2567, 2568 and 5181 on `127.0.0.1` only.
- [ ] Admin API: loopback only, `bAdminApiRequireSecret=True` in
      `DefaultGame.ini` (a request without `Authorization: Bearer <secret>`
      gets 401). Its CORS list `AdminApiAllowedOrigins` is the web editor only.
- [ ] Backend CORS: `CORS_ORIGINS` unset (the default is the web editor,
      `http://localhost:5180` and `http://127.0.0.1:5180`) or set to exactly the
      origins you want. Game clients send no Origin and do not need it.
- [ ] Rate limits are on (built in, nothing to set): login and register
      10 per minute per IP, character create 5 per minute, 429 with
      `Retry-After`. `npx tsx server/scripts/smoke-security.ts` passes.

## Client build

- [ ] Client and game server are built from the same commit.
- [ ] `python Tools/audit_client_secrets.py <packaged folder>` (and before
      paking, on `Valhalla2/Saved/StagedBuilds/Windows`) reports
      `0 finding(s)`. INFO lines for code literals in the `.exe` are expected.

## Data

- [ ] Back up `server/valhalla.db` before the session and after it
      (copy it with the backend stopped, or right after a save; keep a few
      dated copies off this PC). It holds every account and character.
- [ ] Dev and test accounts: the accounts in the 1.0 git history
      (`alpha`, `beta`, `insidious*`, `salvo`, `probe_tmp`, `pt_*`,
      `ue_bantest`) are still in the live database **with the same bcrypt
      hashes that are in the public GitHub repo**. Delete them or change their
      passwords before going live, and never reuse a real password on a test
      account. See PLAN.md, B-04.

## If the public IP changes

1. Edit both addresses in `deploy/Caddyfile` and restart `start-https.bat`
   (it fetches a new IP certificate).
2. Edit `PublicBackendUrl` and `PublicGameServerAddress` in
   `Valhalla2/Config/DefaultGame.ini`.
3. Testers either get a new build or launch the old one with
   `-ValhallaBackendUrl=https://<new IP> -ValhallaGameServer=<new IP>:7777`.

## Accepted for now

- Login tokens are JWTs valid for **24 hours**, with no refresh and no
  revocation apart from a ban (checked on every join and API call). Kept as is
  for friends tests. Short-lived access tokens plus refresh tokens are a later
  backlog item.
- The game connection (UDP 7777) is not encrypted and carries the token in the
  join URL. Valhalla's own logs cut it to 8 characters. The engine's
  `LogNet: Browse:` / `LogNet: Login request:` lines on the client and server
  print the whole URL, and that can't be changed from game code. Treat
  server and client logs as containing live tokens for 24 hours, and don't
  post them publicly.
