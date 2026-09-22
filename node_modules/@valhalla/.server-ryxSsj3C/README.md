# Valhalla server

Express 4 + Colyseus game server. SQLite via sql.js (`server/valhalla.db`), bcryptjs
password hashing, HS256 JWTs.

```bash
npm run dev --workspace=server     # tsx watch src/index.ts
```

## Environment variables

| Variable | Default | Purpose |
| --- | --- | --- |
| `JWT_SECRET` | `valhalla-dev-secret-change-in-production` | HS256 signing key for player tokens (`services/AuthService.ts`). Must be the same across every process that issues or verifies tokens. |
| `VALHALLA_SERVER_SECRET` | `dev-server-secret` when `NODE_ENV !== 'production'`; **none** in production | Shared secret for the server-to-server routes below. |
| `PORT` | `2567` (`SERVER_PORT` in `@valhalla/shared`) | HTTP/WebSocket listen port. |
| `NODE_ENV` | unset (= development) | `production` disables the dev fallback secret. |
| `VALHALLA_DB` | `valhalla.db` (relative to cwd) | SQLite file path. Used by the smoke test to run against a throwaway DB. |

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
