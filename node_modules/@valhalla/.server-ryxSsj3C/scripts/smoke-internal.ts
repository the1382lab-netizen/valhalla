/**
 * Smoke test for the Valhalla 2.0 server-to-server API (Phase 7a).
 *
 * Run from the repo root or server/:
 *   npx tsx server/scripts/smoke-internal.ts
 *
 * By default it boots its own copy of the server on a free port against a
 * throwaway SQLite file, so the real server/valhalla.db is never touched.
 *
 * Env:
 *   SMOKE_BASE_URL   run against an already-running server instead of spawning
 *                    one (e.g. http://localhost:2567). The throwaway user then
 *                    lands in that server's database — the character is deleted
 *                    at the end, but there is no API to delete the user row.
 *   VALHALLA_SERVER_SECRET  secret to send (default: dev-server-secret)
 */

import { spawn, type ChildProcess } from 'child_process';
import { createServer } from 'net';
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';
import { existsSync, rmSync, mkdirSync } from 'fs';
import { tmpdir } from 'os';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SERVER_DIR = resolve(__dirname, '..');

const SECRET = process.env.VALHALLA_SERVER_SECRET || 'dev-server-secret';
const EXTERNAL_BASE_URL = process.env.SMOKE_BASE_URL;

let passed = 0;
function ok(label: string, detail = ''): void {
  passed++;
  console.log(`  ✓ ${label}${detail ? ` — ${detail}` : ''}`);
}
function assert(cond: unknown, label: string, detail = ''): asserts cond {
  if (!cond) throw new Error(`ASSERTION FAILED: ${label}${detail ? ` (${detail})` : ''}`);
  ok(label, detail);
}
function assertEq(actual: unknown, expected: unknown, label: string): void {
  const a = JSON.stringify(actual);
  const e = JSON.stringify(expected);
  if (a !== e) throw new Error(`ASSERTION FAILED: ${label} — expected ${e}, got ${a}`);
  ok(label, `${e}`);
}

// ── HTTP helpers ────────────────────────────────────────────

let BASE = '';

interface Res<T = any> { status: number; body: T }

async function http<T = any>(
  method: string,
  path: string,
  opts: { body?: unknown; token?: string; secret?: string | null } = {},
): Promise<Res<T>> {
  const headers: Record<string, string> = { 'Content-Type': 'application/json' };
  if (opts.token) headers.Authorization = `Bearer ${opts.token}`;
  if (opts.secret) headers['X-Server-Secret'] = opts.secret;
  const res = await fetch(`${BASE}${path}`, {
    method,
    headers,
    body: opts.body === undefined ? undefined : JSON.stringify(opts.body),
  });
  let body: any = null;
  const text = await res.text();
  try { body = text ? JSON.parse(text) : null; } catch { body = text; }
  return { status: res.status, body };
}

// ── Server lifecycle ────────────────────────────────────────

function freePort(): Promise<number> {
  return new Promise((res, rej) => {
    const srv = createServer();
    srv.on('error', rej);
    srv.listen(0, '127.0.0.1', () => {
      const port = (srv.address() as { port: number }).port;
      srv.close(() => res(port));
    });
  });
}

let child: ChildProcess | null = null;
let tmpDbDir: string | null = null;

async function startServer(): Promise<void> {
  const port = await freePort();
  tmpDbDir = resolve(tmpdir(), `valhalla-smoke-${Date.now()}`);
  mkdirSync(tmpDbDir, { recursive: true });
  const dbPath = resolve(tmpDbDir, 'smoke.db');

  console.log(`\n[smoke] Starting server on port ${port} (db: ${dbPath})`);
  child = spawn('npx', ['tsx', 'src/index.ts'], {
    cwd: SERVER_DIR,
    env: { ...process.env, PORT: String(port), VALHALLA_DB: dbPath, NODE_ENV: 'development' },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  child.stdout?.on('data', d => process.stdout.write(`    [server] ${d}`));
  child.stderr?.on('data', d => process.stderr.write(`    [server:err] ${d}`));
  child.on('exit', code => { if (code !== 0 && code !== null) console.error(`[smoke] server exited: ${code}`); });

  BASE = `http://127.0.0.1:${port}`;

  // Wait for /api/health
  const deadline = Date.now() + 60_000;
  while (Date.now() < deadline) {
    try {
      const res = await http('GET', '/api/health');
      if (res.status === 200) return;
    } catch { /* not up yet */ }
    await new Promise(r => setTimeout(r, 400));
  }
  throw new Error('Server did not become healthy within 60s.');
}

function stopServer(): void {
  if (child && !child.killed) {
    child.kill('SIGTERM');
    setTimeout(() => { if (child && !child.killed) child.kill('SIGKILL'); }, 2000).unref();
  }
  if (tmpDbDir && existsSync(tmpDbDir)) {
    rmSync(tmpDbDir, { recursive: true, force: true });
    console.log(`[smoke] Removed throwaway database dir ${tmpDbDir} (test user row gone with it).`);
  }
}

// ── The test ────────────────────────────────────────────────

async function run(): Promise<void> {
  if (EXTERNAL_BASE_URL) {
    BASE = EXTERNAL_BASE_URL.replace(/\/$/, '');
    console.log(`\n[smoke] Using running server at ${BASE}`);
  } else {
    await startServer();
  }

  // MAX_USERNAME_LENGTH is 20, so use the last 10 digits of the ms timestamp.
  const username = `ue_smoke_${String(Date.now()).slice(-10)}`;
  const password = 'smoke-pass-123';
  let characterId = 0;
  let userId = 0;

  try {
    // 1. health
    console.log('\n[1] GET /api/health');
    const health = await http('GET', '/api/health');
    assertEq(health.status, 200, 'health status');
    assertEq(health.body.ok, true, 'health.ok');
    assertEq(health.body.version, '1.0', 'health.version');
    assert(typeof health.body.serverTime === 'number' && health.body.serverTime > 0,
      'health.serverTime is unix ms', String(health.body.serverTime));

    // 2. secret enforcement
    console.log('\n[2] X-Server-Secret enforcement');
    const noSecret = await http('POST', '/api/auth/verify', { body: { token: 'x' } });
    assertEq(noSecret.status, 401, 'verify without secret -> 401');
    assert(typeof noSecret.body?.error === 'string', 'error body on missing secret', noSecret.body?.error);
    const badSecret = await http('POST', '/api/auth/verify', { body: { token: 'x' }, secret: 'wrong-secret' });
    assertEq(badSecret.status, 401, 'verify with wrong secret -> 401');

    // 3. register + login (1.0 player routes still work)
    console.log(`\n[3] Register + login player '${username}'`);
    const reg = await http('POST', '/api/auth/register', { body: { username, password } });
    assertEq(reg.status, 200, 'register status');
    const loginRes = await http('POST', '/api/auth/login', { body: { username, password } });
    assertEq(loginRes.status, 200, 'login status');
    const token: string = loginRes.body.token;
    userId = loginRes.body.userId;
    assert(typeof token === 'string' && token.length > 20, 'login returned a JWT');
    assert(Number.isInteger(userId) && userId > 0, 'login returned userId', String(userId));

    // 4. create a warrior
    console.log('\n[4] Create warrior character');
    const charName = `Smoke${String(Date.now()).slice(-8)}`;
    const created = await http('POST', '/api/characters', {
      body: { name: charName, classId: 'warrior' }, token,
    });
    assertEq(created.status, 201, 'create character status');
    characterId = created.body.character.id;
    assert(Number.isInteger(characterId) && characterId > 0, 'characterId', String(characterId));

    // 4b. the 1.0 player route must still work behind the new /api mount
    const list = await http('GET', '/api/characters', { token });
    assertEq(list.status, 200, '1.0 GET /api/characters still works');
    assert(list.body.characters.some((c: any) => c.id === characterId),
      'listed character matches the created one');

    // 5. POST /api/auth/verify
    console.log('\n[5] POST /api/auth/verify');
    const verify = await http('POST', '/api/auth/verify', { body: { token }, secret: SECRET });
    assertEq(verify.status, 200, 'verify status');
    assertEq(verify.body.userId, userId, 'verify.userId');
    assertEq(verify.body.username, username, 'verify.username');
    assert(typeof verify.body.expiresAt === 'number' && verify.body.expiresAt > Date.now(),
      'verify.expiresAt is a future unix ms', new Date(verify.body.expiresAt).toISOString());
    const badToken = await http('POST', '/api/auth/verify', { body: { token: `${token}tampered` }, secret: SECRET });
    assertEq(badToken.status, 401, 'tampered token -> 401');

    // 6. initial load
    console.log('\n[6] GET /api/characters/:id/load');
    const load1 = await http('GET', `/api/characters/${characterId}/load?userId=${userId}`, { secret: SECRET });
    assertEq(load1.status, 200, 'load status');
    const c1 = load1.body.character;
    assertEq(c1.id, characterId, 'character.id');
    assertEq(c1.name, charName, 'character.name');
    assertEq(c1.classId, 'warrior', 'character.classId');
    assertEq(c1.level, 1, 'character.level');
    for (const field of ['userId', 'bodyId', 'xp', 'hp', 'mana', 'positionX', 'positionY', 'zoneId', 'alive', 'inventory', 'equipment', 'actionBar']) {
      assert(field in c1, `character has field '${field}'`, JSON.stringify(c1[field]));
    }
    console.log(`    starter equipment: ${JSON.stringify(c1.equipment)}`);
    console.log(`    starter inventory: ${JSON.stringify(c1.inventory)}`);

    // wrong owner -> 404
    const wrongOwner = await http('GET', `/api/characters/${characterId}/load?userId=${userId + 9999}`, { secret: SECRET });
    assertEq(wrongOwner.status, 404, 'load with wrong userId -> 404');

    // 7. save a mutated state
    console.log('\n[7] PUT /api/characters/:id/save (round trip)');
    const saveBody = {
      hp: 77,
      mana: 42,
      xp: 1234,
      level: 5,
      positionX: 1600.5,
      positionY: 900.25,
      zoneId: 'desert',
      alive: true,
      inventory: [
        { slotIndex: 0, itemId: 'health_potion', quantity: 7 },
        { slotIndex: 1, itemId: 'rat_tail', quantity: 3 },
      ],
      equipment: [
        { slotType: 'weapon', itemId: 'iron_sword' },
        { slotType: 'chest', itemId: 'chainmail' },
      ],
      actionBar: ['', '', '', '', '', '', '', ''],
    };
    const save = await http('PUT', `/api/characters/${characterId}/save`, { body: saveBody, secret: SECRET });
    assertEq(save.status, 200, 'save status');
    assertEq(save.body.ok, true, 'save.ok');
    assert(typeof save.body.savedAt === 'number' && save.body.savedAt > 0, 'save.savedAt is unix ms', String(save.body.savedAt));

    // 8. reload and compare
    console.log('\n[8] GET load again — verify round trip');
    const load2 = await http('GET', `/api/characters/${characterId}/load?userId=${userId}`, { secret: SECRET });
    assertEq(load2.status, 200, 'reload status');
    const c2 = load2.body.character;
    assertEq(c2.level, 5, 'level round trip');
    assertEq(c2.xp, 1234, 'xp round trip');
    assertEq(c2.hp, 77, 'hp round trip');
    assertEq(c2.mana, 42, 'mana round trip');
    assertEq(c2.zoneId, 'desert', 'zoneId round trip');
    assertEq(c2.positionX, 1600.5, 'positionX round trip (float preserved)');
    assertEq(c2.positionY, 900.25, 'positionY round trip (float preserved)');
    assertEq(c2.alive, true, 'alive round trip');
    assertEq([...c2.inventory].sort((a: any, b: any) => a.slotIndex - b.slotIndex), saveBody.inventory, 'inventory round trip');
    assertEq([...c2.equipment].sort((a: any, b: any) => a.slotType.localeCompare(b.slotType)),
      [...saveBody.equipment].sort((a, b) => a.slotType.localeCompare(b.slotType)), 'equipment round trip');
    assertEq(c2.name, charName, 'name unchanged by save');
    assertEq(c2.classId, 'warrior', 'classId unchanged by save');

    // 9. validation
    console.log('\n[9] Save validation (expect 400 / 404)');
    const bad = async (label: string, patch: Record<string, unknown>, expect = 400) => {
      const res = await http('PUT', `/api/characters/${characterId}/save`, { body: { ...saveBody, ...patch }, secret: SECRET });
      assertEq(res.status, expect, label);
      console.log(`      -> ${res.body?.error}`);
    };
    // Regression: items that exist only in the editor's items.json (not in the
    // hardcoded ITEM_CATALOG) must save. Before DataManager was initialized at
    // startup, these were rejected with 400 and the whole save was lost.
    {
      const editorOnly = await http('PUT', `/api/characters/${characterId}/save`, {
        body: { ...saveBody, inventory: [{ slotIndex: 0, itemId: 'iron_buckler', quantity: 1 }] },
        secret: SECRET,
      });
      assertEq(editorOnly.status, 200, 'save with editor-only item (iron_buckler)');
      // Put the round-trip state back; the checks below compare against it.
      const restore = await http('PUT', `/api/characters/${characterId}/save`, { body: saveBody, secret: SECRET });
      assertEq(restore.status, 200, 'restore round-trip state');
    }
    await bad('unknown inventory item -> 400', { inventory: [{ slotIndex: 0, itemId: 'excalibur_9000', quantity: 1 }] });
    await bad('quantity 0 -> 400', { inventory: [{ slotIndex: 0, itemId: 'health_potion', quantity: 0 }] });
    await bad('33 inventory slots -> 400', {
      inventory: Array.from({ length: 33 }, (_, i) => ({ slotIndex: i, itemId: 'health_potion', quantity: 1 })),
    });
    await bad('bad equip slot -> 400', { equipment: [{ slotType: 'tiara', itemId: 'iron_sword' }] });
    await bad('unknown equipment item -> 400', { equipment: [{ slotType: 'weapon', itemId: 'nope' }] });
    await bad('missing level -> 400', { level: undefined });
    const missingChar = await http('PUT', '/api/characters/99999999/save', { body: saveBody, secret: SECRET });
    assertEq(missingChar.status, 404, 'save to missing character -> 404');

    // 10. reload once more — a rejected save must not have mutated anything
    const load3 = await http('GET', `/api/characters/${characterId}/load?userId=${userId}`, { secret: SECRET });
    assertEq(load3.body.character.level, 5, 'state intact after rejected saves');
    assertEq(load3.body.character.inventory.length, 2, 'inventory intact after rejected saves');

    // 11. account bans
    console.log('\n[9b] Account bans');
    const banNoSecret = await http('POST', '/api/accounts/ban', { body: { userId } });
    assertEq(banNoSecret.status, 401, 'ban without secret -> 401');
    const banMissing = await http('POST', '/api/accounts/ban', { body: { username: 'no_such_user_xyz' }, secret: SECRET });
    assertEq(banMissing.status, 404, 'ban unknown account -> 404');
    const ban = await http('POST', '/api/accounts/ban', {
      body: { username, minutes: 30, reason: 'smoke test', by: 'smoke' }, secret: SECRET,
    });
    assertEq(ban.status, 200, 'suspend 30 min -> 200');
    assertEq(ban.body.userId, userId, 'ban resolved username to userId');
    assert(ban.body.ban.banned === true && ban.body.ban.permanent === false, 'ban is temporary');
    const bans = await http('GET', '/api/accounts/bans', { secret: SECRET });
    assert(bans.body.bans.some((b: any) => b.userId === userId && b.reason === 'smoke test'), 'ban listed');
    const bannedVerify = await http('POST', '/api/auth/verify', { body: { token }, secret: SECRET });
    assertEq(bannedVerify.status, 403, 'verify existing token of banned account -> 403');
    assert(String(bannedVerify.body.error).includes('suspended'), 'verify says suspended', bannedVerify.body.error);
    const bannedLogin = await http('POST', '/api/auth/login', { body: { username, password } });
    assertEq(bannedLogin.status, 403, 'login while banned -> 403');
    const bannedList = await http('GET', '/api/characters', { token });
    assertEq(bannedList.status, 403, 'player API while banned -> 403');
    const permaBan = await http('POST', '/api/accounts/ban', { body: { userId, reason: 'perma' }, secret: SECRET });
    assert(permaBan.body.ban.permanent === true, 'ban with no minutes is permanent');
    const unban = await http('POST', '/api/accounts/unban', { body: { userId }, secret: SECRET });
    assertEq(unban.status, 200, 'unban -> 200');
    const afterUnban = await http('POST', '/api/auth/verify', { body: { token }, secret: SECRET });
    assertEq(afterUnban.status, 200, 'verify after unban -> 200');
    const bansAfter = await http('GET', '/api/accounts/bans', { secret: SECRET });
    assert(!bansAfter.body.bans.some((b: any) => b.userId === userId), 'unbanned account not listed');
  } finally {
    // 11. cleanup
    if (characterId) {
      console.log('\n[10] Cleanup');
      const loginAgain = await http('POST', '/api/auth/login', { body: { username, password } });
      if (loginAgain.status === 200) {
        const del = await http('DELETE', `/api/characters/${characterId}`, { token: loginAgain.body.token });
        assertEq(del.status, 200, 'delete character');
        const gone = await http('GET', `/api/characters/${characterId}/load?userId=${userId}`, { secret: SECRET });
        assertEq(gone.status, 404, 'deleted character -> 404 on load');
      }
      if (EXTERNAL_BASE_URL) {
        console.log(`    NOTE: user '${username}' remains in that server's database — there is no API to delete a user row.`);
      }
    }
  }

  console.log(`\n[smoke] ALL CHECKS PASSED (${passed} assertions)\n`);
}

run()
  .then(() => { stopServer(); process.exit(0); })
  .catch(err => {
    console.error(`\n[smoke] FAILED: ${err?.message ?? err}`);
    stopServer();
    process.exit(1);
  });
