# Valhalla server

Express 4 + Colyseus game server. SQLite via sql.js (`server/valhalla.db`), bcryptjs
password hashing, HS256 JWTs.

```bash
npm run dev --workspace=server     # tsx watch src/index.ts
```

## Environment variables

All of these can also come from `secrets.local.env` at the repo root
(gitignored), which `src/loadEnv.ts` loads before anything else; a variable
already set in the real environment wins. `VALHALLA_SECRETS_FILE` points at a
different file. See `deploy/README.md` for hosting.

| Variable | Default | Purpose |
| --- | --- | --- |
| `JWT_SECRET` | `valhalla-dev-secret-change-in-production` in development; **required** (32+ characters, not the dev value) in production, or the server refuses to start | HS256 signing key for player tokens (`services/AuthService.ts`). Must be the same across every process that issues or verifies tokens. |
| `VALHALLA_SERVER_SECRET` | `dev-server-secret` when `NODE_ENV !== 'production'`; **none** in production (the dev value is refused there too) | Shared secret for the server-to-server routes below. |
| `PORT` | `2567` (`SERVER_PORT` in `@valhalla/shared`) | HTTP listen port. |
| `HOST` | `0.0.0.0` | Listen address. `127.0.0.1` when hosting, so only Caddy (HTTPS) and the local game server reach it. |
| `NODE_ENV` | unset (= development) | `production` disables both dev fallback secrets. |
| `VALHALLA_DB` | `valhalla.db` (relative to cwd) | SQLite file path. Used by the smoke test to run against a throwaway DB. |
| `CORS_ORIGINS` | `http://localhost:5180,http://127.0.0.1:5180` (the web editor) | Comma-separated browser origins allowed to call the API (`middleware/cors.ts`). A request with any other `Origin` gets 403; requests with no `Origin` (the game client and server) are unaffected. The web editor's API server (`editor/src/server.ts`) reads the same variable. |
| `RATE_LIMIT_WINDOW_MS` | `60000` | Window of the per-IP rate limits (`middleware/rateLimit.ts`): login and register 10 per window, `POST /api/characters` 5, `GET`/`PUT /api/characters/:id/settings` 60 together, then 429 with `Retry-After`. `X-Forwarded-For` is trusted only from loopback (Caddy). Only the smoke test changes the window. |

## Server-to-server API (Valhalla 2.0 / Unreal dedicated server)

Three routes let the Unreal dedicated server authenticate players and load/save
characters without a per-player JWT session. They are **not** for browsers: they
skip `authMiddleware` and are guarded by a shared secret header instead.

    X-Server-Secret: <VALHALLA_SERVER_SECRET>

**The UE dedicated server must be configured with the same secret value.** In
development both sides can use the default `dev-server-secret`; in production set
`VALHALLA_SERVER_SECRET` on the Node server *and* on the UE server. If
`NODE_ENV=production` and no secret is set, the routes are disabled — startup logs
a warning and every request to them returns `503`. A missing or wrong header
returns `401 {"error": "..."}`.

Implementation: `src/routes/internal.ts`, `src/middleware/serverSecret.ts`, mounted
at `/api` *before* the player-facing routers in `src/index.ts`.

### `GET /api/health` — no auth

Startup probe for the UE server.

```bash
curl http://localhost:2567/api/health
# 200
{ "ok": true, "version": "1.0", "serverTime": 1790083483642 }
```

### `POST /api/auth/verify`

Validates a player JWT (the token the 1.0 login flow returns).

```bash
curl -X POST http://localhost:2567/api/auth/verify \
  -H 'Content-Type: application/json' \
  -H 'X-Server-Secret: dev-server-secret' \
  -d '{"token":"eyJhbGciOiJIUzI1NiIs..."}'
```

```jsonc
// 200
{ "userId": 1, "username": "ue_smoke_0083501629", "expiresAt": 1790169901000 }
// 401 — bad/expired token, or bad/missing X-Server-Secret
{ "error": "Invalid or expired token." }
// 400 — body has no "token" string
```

`expiresAt` is the JWT `exp` claim in **unix milliseconds** (tokens live
`JWT_EXPIRY` = 24h). It is `null` only if a token somehow carries no `exp`.

### `GET /api/characters/:id/load?userId=<n>`

Loads one character. `userId` is required and ownership is enforced — a character
that does not exist *or* is not owned by that user returns `404`.

```bash
curl -H 'X-Server-Secret: dev-server-secret' \
  'http://localhost:2567/api/characters/1/load?userId=1'
```

```jsonc
// 200 — the LoadedCharacter exactly as CharacterService returns it
{
  "character": {
    "id": 1,                  // number
    "userId": 1,              // number
    "name": "Ragnar",         // string
    "classId": "warrior",     // string — warrior|cleric|ranger|rogue|shaman|wizard
    "bodyId": "body_tan",     // string — paperdoll base body
    "level": 1,               // number (int)
    "xp": 0,                  // number (int)
    "hp": 150,                // number (int) — current hp, not max
    "mana": 0,                // number (int) — current mana
    "positionX": 672,         // number (float allowed)
    "positionY": 672,         // number (float allowed)
    "zoneId": "grasslands",   // string
    "alive": true,            // boolean
    "inventory": [            // InventorySlotData[]
      { "slotIndex": 0, "itemId": "health_potion", "quantity": 5 }
    ],
    "equipment": [            // EquipmentData[]
      { "slotType": "weapon", "itemId": "iron_sword" }
    ],
    "actionBar": ["", "", "", "", "", "", "", ""]   // string[], always 8 entries
  }
}
// 404
{ "error": "Character not found or access denied." }
```

There is no `energy` column: energy is derived from class + level at join time
(`computeDerivedStats`) and always starts full, so it is neither loaded nor saved.
`maxHp` / `maxMana` / `speed` are likewise derived, not stored.

### `PUT /api/characters/:id/save`

Body is `SaveCharacterData` — the same shape `GameRoom.savePlayer()` builds.

```bash
curl -X PUT http://localhost:2567/api/characters/1/save \
  -H 'Content-Type: application/json' \
  -H 'X-Server-Secret: dev-server-secret' \
  -d '{
        "hp": 77, "mana": 42, "xp": 1234, "level": 5,
        "positionX": 1600.5, "positionY": 900.25,
        "zoneId": "desert", "alive": true,
        "inventory": [{ "slotIndex": 0, "itemId": "health_potion", "quantity": 7 }],
        "equipment": [{ "slotType": "weapon", "itemId": "iron_sword" }],
        "actionBar": ["", "", "", "", "", "", "", ""]
      }'
```

```jsonc
// 200
{ "ok": true, "savedAt": 1790083501885 }
// 400 — validation, e.g.
{ "error": "Unknown item id 'excalibur_9000' in inventory[0]." }
// 404
{ "error": "Character 99999999 not found." }
```

Field contract:

| Field | Type | Rules |
| --- | --- | --- |
| `hp` | int | required, ≥ 0 |
| `mana` | int | required, ≥ 0 |
| `xp` | int | required, ≥ 0 |
| `level` | int | required, ≥ 1 |
| `positionX` | number | required, finite (floats preserved — `REAL` column) |
| `positionY` | number | required, finite |
| `zoneId` | string | required, non-empty (not checked against the zone registry) |
| `alive` | boolean | optional, defaults to `true` |
| `inventory` | array | required, ≤ 32 entries (`INVENTORY_MAX_SLOTS`); each `{ slotIndex: int 0–31 (unique), itemId: known item id, quantity: int ≥ 1 }` |
| `equipment` | array | required; each `{ slotType, itemId }` with `slotType` from `EQUIP_SLOTS` (`weapon, offhand, helm, chest, legs, boots, gloves, back, ring`, each at most once) and a known `itemId` |
| `actionBar` | string[] | optional, ≤ 8 skill ids; empty slots are `""`. Omitted ⇒ 8 empty slots (**which clears any saved action bar**, so send the current one) |

Unknown item ids are rejected against the live item catalog (`DataManager`, which
falls back to the hardcoded `ITEM_CATALOG` when the game room has not loaded the
editor JSON yet).

The save replaces inventory, equipment and action bar wholesale — it is a full
snapshot, not a patch. `name`, `classId`, `bodyId` and `userId` are never modified
by this route.

### Coordinates

`positionX` / `positionY` are stored as opaque `REAL` numbers. In 1.0 they are
Phaser world pixels; the 2.0 UE server writes zone-local centimetres into the same
columns. The backend applies no scaling, clamping or conversion — whatever a client
saves is what it loads back. (Note that a character last saved by the 1.0 client
therefore carries pixel coordinates; the UE side decides how to treat those.)

## Player API: per-character UI settings (B-21)

The game client saves each character's HUD layout, style, chat and nameplate
options here (`routes/characters.ts`, `services/SettingsService.ts`, table
`character_settings`). Player JWT (`Authorization: Bearer`), like the other
`/api/characters` routes; the character must belong to the token's user,
otherwise 404 (so ids cannot be probed). 60 requests per window per IP, GET and
PUT together.

    GET /api/characters/:id/settings
    200 { "ui": { ... }, "updatedAt": "2026-09-24T17:10:17.252Z" }
    404 { "error": "No settings saved for this character.", "noSettings": true }   (client uses defaults)
    404 { "error": "Character not found." }                                         (not yours / no such id)

    PUT /api/characters/:id/settings      { "ui": { ... } }
    200 { "ok": true, "updatedAt": "..." }
    400 ui missing or not a JSON object;  413 ui over 64 KB of JSON (or a body over the 100 KB parser limit)

`ui` is the client's `FValhallaUserUISettings` document
(`Valhalla2/Source/ValhallaCore/Public/ValhallaUserUISettings.h` has the format),
stored verbatim; the backend never reads inside it. `updatedAt` (ISO 8601) is
stamped by the backend on every PUT. Deleting the character deletes the row.

## Account management (B-12)

Player routes (`Authorization: Bearer <token>`, rate limited with the login budget):

| Route | Body | Result |
| --- | --- | --- |
| `POST /api/auth/password` | `{ currentPassword, newPassword }` | `{ ok, token }`: the new token replaces the caller's; every other session on the account stops working |
| `POST /api/auth/account/delete` (or `DELETE /api/auth/account`) | `{ password, confirm }` (confirm = account name) | `{ ok, username, characters }`: account, characters, items and action bars removed |

Admin routes (`X-Server-Secret`; the web editor's Accounts dialog calls them through `editor/src/server.ts`):

| Route | Body / query | Result |
| --- | --- | --- |
| `GET /api/accounts/search` | `?q=<account or character name>&banned=1&limit=` | `{ accounts: [{ userId, username, createdAt, lastLoginAt, characterCount, ban }] }` |
| `GET /api/accounts/detail` | `?userId=` or `?username=` | `{ account: { ...summary, characters: [...] } }` |
| `POST /api/accounts/reset-password` | `{ userId \| username }` | `{ ok, temporaryPassword }` (shown once; sessions end) |
| `POST /api/accounts/delete` | `{ userId \| username, confirm }` | `{ ok, characters }` |
| `POST /api/accounts/rename-character` | `{ characterId, name }` | `{ ok, oldName, name }` (400 invalid, 409 taken) |

Sessions end through `users.token_version`: every login token carries it (`tv`)
and a password change or reset bumps it, so `verifyToken` (players and the game
server's join check alike) refuses anything issued before. No email addresses
are stored; a forgotten password is an admin reset.

## Smoke test

```bash
npx tsx server/scripts/smoke-internal.ts
```

Spawns a server on a free port against a throwaway SQLite file (the real
`valhalla.db` is untouched), registers `ue_smoke_<timestamp>`, creates a warrior,
then exercises health / secret enforcement / verify / load / save / load, the
validation failures, and deletes the character. Set `SMOKE_BASE_URL=http://host:port`
to run it against an already-running server instead — in that mode the throwaway
*user row* stays behind, since there is no API to delete a user.

```bash
npx tsx server/scripts/smoke-security.ts
```

B-04 checks, each against its own throwaway server and database with no
`secrets.local.env`: the CORS allow-list (editor origins allowed with
credentials, a foreign origin 403 on preflight and POST, no Origin untouched),
the login limit tripping at attempt 11 with `Retry-After` and clearing after it,
the register and character-create limits, health and internal routes
unlimited, the dev server secret refused (503) with `NODE_ENV=production`, and
production refusing to start without `JWT_SECRET`.

```bash
npx tsx server/scripts/smoke-settings.ts
```

B-21, against its own throwaway server and database: 401 without a token, GET
404 before a save, PUT then GET returning the same document and an ISO
`updatedAt`, a second PUT replacing it, another user's character 404 for GET and
PUT, malformed ids 404, a non-object `ui` 400, over 64 KB 413, the 61st request
in a window 429, and a deleted character's settings row gone.
