/**
 * Per-character UI settings smoke test (B-21):
 *   GET/PUT /api/characters/:id/settings under the player JWT.
 *
 * Run from the repo root or server/:
 *   npx tsx server/scripts/smoke-settings.ts
 *
 * Boots its own development server on a free port against a throwaway SQLite
 * file (VALHALLA_SECRETS_FILE points at a file that does not exist, so neither
 * the real database nor secrets.local.env is involved), with
 * RATE_LIMIT_WINDOW_MS=4000 so the rate-limit check clears in seconds.
 *
 * Checks: 401 without a token; GET 404 before anything is saved; PUT then GET
 * returns the same document and an ISO updatedAt; a second PUT replaces it;
 * another user's character is 404 for GET and PUT (and its settings are not
 * touched); a malformed id is 404; a non-object `ui` is 400; a document over
 * 64 KB is 413; the 61st request in a window is 429; deleting the character
 * removes its settings row (deleteCharacter deletes it explicitly: sql.js
 * resets PRAGMA foreign_keys on every export, so ON DELETE CASCADE is not
 * enforced).
 *
 * [7] B-27 account settings, GET/PUT /api/account/settings: 401 without a
 * token, 404 before a save, round trip, upsert, per account, 400 / 413, and
 * deleting the account removes the row.
 */

import { spawn, type ChildProcess } from 'child_process';
import { createServer } from 'net';
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';
import { existsSync, rmSync, mkdirSync, readFileSync } from 'fs';
import { tmpdir } from 'os';
import initSqlJs from 'sql.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SERVER_DIR = resolve(__dirname, '..');
const WINDOW_MS = 4000;

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
  ok(label, e.length > 80 ? `${e.slice(0, 77)}...` : e);
}
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));

// ── Server ──────────────────────────────────────────────────

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
const output: string[] = [];
const dir = resolve(tmpdir(), `valhalla-smoke-settings-${Date.now()}`);
const dbFile = resolve(dir, 'smoke.db');

async function startServer(): Promise<string> {
  const port = await freePort();
  mkdirSync(dir, { recursive: true });
  const cleanEnv: Record<string, string> = {};
  for (const [k, v] of Object.entries(process.env)) {
    if (v !== undefined && !/^(NODE_ENV|JWT_SECRET|VALHALLA_SERVER_SECRET|CORS_ORIGINS|HOST|PORT|VALHALLA_DB|RATE_LIMIT_WINDOW_MS)$/.test(k)) cleanEnv[k] = v;
  }
  child = spawn(process.execPath, ['--import', 'tsx', 'src/index.ts'], {
    cwd: SERVER_DIR,
    env: {
      ...cleanEnv,
      NODE_ENV: 'development',
      VALHALLA_SECRETS_FILE: resolve(dir, 'no-such-secrets.env'),
      HOST: '127.0.0.1',
      PORT: String(port),
      VALHALLA_DB: dbFile,
      RATE_LIMIT_WINDOW_MS: String(WINDOW_MS),
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  child.stdout?.on('data', d => output.push(String(d)));
  child.stderr?.on('data', d => output.push(String(d)));
  const base = `http://127.0.0.1:${port}`;
  const deadline = Date.now() + 60_000;
  while (Date.now() < deadline) {
    if (child.exitCode !== null) throw new Error(`server exited early:\n${output.join('')}`);
    try {
      if ((await fetch(`${base}/api/health`)).status === 200) return base;
    } catch { /* not up yet */ }
    await sleep(300);
  }
  throw new Error('server did not become healthy within 60 s');
}

function stop(): void {
  if (child && child.exitCode === null) child.kill();
  try { if (existsSync(dir)) rmSync(dir, { recursive: true, force: true }); } catch { /* still locked */ }
}

// ── HTTP ────────────────────────────────────────────────────

interface Res { status: number; body: any }

async function http(base: string, method: string, path: string,
  opts: { body?: unknown; rawBody?: string; token?: string; ip?: string } = {}): Promise<Res> {
  const headers: Record<string, string> = {};
  if (opts.body !== undefined || opts.rawBody !== undefined) headers['Content-Type'] = 'application/json';
  if (opts.token) headers.Authorization = `Bearer ${opts.token}`;
  if (opts.ip) headers['X-Forwarded-For'] = opts.ip;
  const body = opts.rawBody ?? (opts.body === undefined ? undefined : JSON.stringify(opts.body));
  const res = await fetch(`${base}${path}`, { method, headers, body });
  const text = await res.text();
  let parsed: any = text;
  try { parsed = text ? JSON.parse(text) : null; } catch { /* not JSON */ }
  return { status: res.status, body: parsed };
}

async function account(base: string, tag: string, ip: string): Promise<{ token: string; characterId: number; username: string }> {
  const username = `set_${tag}_${String(Date.now()).slice(-8)}`;
  const reg = await http(base, 'POST', '/api/auth/register', { body: { username, password: 'smoke-pass-123' }, ip });
  assertEq(reg.status, 200, `register ${tag}`);
  const token: string = reg.body.token;
  const created = await http(base, 'POST', '/api/characters', {
    body: { name: `Set${tag}${String(Date.now()).slice(-6)}`, classId: 'warrior' }, token, ip,
  });
  assertEq(created.status, 201, `create ${tag}'s character`);
  return { token, characterId: created.body.character.id, username };
}

async function accountSettingsRows(userToken: string, base: string): Promise<number> {
  // The row count for the token's user, read straight from the database file.
  const me = await http(base, 'GET', '/api/characters', { token: userToken });
  const SQL = await initSqlJs();
  const db = new SQL.Database(readFileSync(dbFile));
  const result = db.exec('SELECT COUNT(*) FROM account_settings a JOIN users u ON u.id = a.user_id WHERE u.username = ?', [String(me.body?.username ?? '')]);
  db.close();
  return Number(result[0]?.values[0]?.[0] ?? 0);
}

async function settingsRows(characterId: number): Promise<number> {
  const SQL = await initSqlJs();
  const db = new SQL.Database(readFileSync(dbFile));
  const result = db.exec('SELECT COUNT(*) FROM character_settings WHERE character_id = ?', [characterId]);
  db.close();
  return Number(result[0]?.values[0]?.[0] ?? 0);
}

// ── The test ────────────────────────────────────────────────

async function run(): Promise<void> {
  console.log('\n[smoke-settings] development server (rate-limit window 4 s)');
  const B = await startServer();

  const alice = await account(B, 'a', '198.51.100.41');
  const bob = await account(B, 'b', '198.51.100.42');
  const aPath = `/api/characters/${alice.characterId}/settings`;
  const bPath = `/api/characters/${bob.characterId}/settings`;

  console.log('\n[1] auth and first read');
  assertEq((await http(B, 'GET', aPath)).status, 401, 'GET without a token -> 401');
  assertEq((await http(B, 'PUT', aPath, { body: { ui: {} } })).status, 401, 'PUT without a token -> 401');
  const first = await http(B, 'GET', aPath, { token: alice.token });
  assertEq(first.status, 404, 'GET before any save -> 404 (client uses defaults)');
  assertEq(first.body?.noSettings, true, '404 body says noSettings');

  console.log('\n[2] PUT -> GET round trip');
  const ui = {
    Version: 1,
    UpdatedAt: '2026-09-24T12:00:00.000Z',
    bLocked: true,
    UiScale: 1.25,
    Panels: { Chat: { AnchorMin: [0, 1], AnchorMax: [0, 1], Alignment: [0, 1], Position: [300, -8], Size: [0, 0], Scale: 1, bVisible: true, bSet: true } },
    Colours: { HpHighColour: '#44ff44ff' },
    LogFilters: ['misses'],
    FutureField: { anything: 'kept verbatim' },
  };
  const put = await http(B, 'PUT', aPath, { token: alice.token, body: { ui } });
  assertEq(put.status, 200, 'PUT own character -> 200');
  assert(typeof put.body?.updatedAt === 'string' && !Number.isNaN(Date.parse(put.body.updatedAt)), 'PUT returns an ISO updatedAt', put.body?.updatedAt);
  const got = await http(B, 'GET', aPath, { token: alice.token });
  assertEq(got.status, 200, 'GET after PUT -> 200');
  assertEq(got.body.ui, ui, 'GET returns the document exactly as PUT sent it');
  assertEq(got.body.updatedAt, put.body.updatedAt, 'GET updatedAt matches the PUT');

  await sleep(15);
  const ui2 = { ...ui, UiScale: 0.8 };
  const put2 = await http(B, 'PUT', aPath, { token: alice.token, body: { ui: ui2 } });
  assertEq(put2.status, 200, 'second PUT -> 200');
  assert(Date.parse(put2.body.updatedAt) > Date.parse(put.body.updatedAt), 'second PUT moves updatedAt forward');
  assertEq((await http(B, 'GET', aPath, { token: alice.token })).body.ui.UiScale, 0.8, 'second PUT replaced the document');
  assertEq(await settingsRows(alice.characterId), 1, 'one row per character (upsert, not append)');

  console.log('\n[3] ownership');
  assertEq((await http(B, 'GET', aPath, { token: bob.token })).status, 404, "GET another user's character -> 404");
  assertEq((await http(B, 'PUT', aPath, { token: bob.token, body: { ui: { hijack: true } } })).status, 404, "PUT another user's character -> 404");
  assertEq((await http(B, 'GET', aPath, { token: alice.token })).body.ui.UiScale, 0.8, "the owner's settings are untouched");
  assertEq((await http(B, 'GET', '/api/characters/999999/settings', { token: alice.token })).status, 404, 'no such character -> 404');
  assertEq((await http(B, 'GET', '/api/characters/abc/settings', { token: alice.token })).status, 404, 'malformed id -> 404');
  assertEq((await http(B, 'GET', `/api/characters/${alice.characterId}x/settings`, { token: alice.token })).status, 404, 'id with trailing junk -> 404');

  console.log('\n[4] validation');
  assertEq((await http(B, 'PUT', bPath, { token: bob.token, body: { ui: [1, 2] } })).status, 400, 'ui is an array -> 400');
  assertEq((await http(B, 'PUT', bPath, { token: bob.token, body: { ui: 'x' } })).status, 400, 'ui is a string -> 400');
  assertEq((await http(B, 'PUT', bPath, { token: bob.token, body: {} })).status, 400, 'no ui -> 400');
  assertEq((await http(B, 'PUT', bPath, { token: bob.token, body: { ui: null } })).status, 400, 'ui null -> 400');
  const big = { ui: { pad: 'x'.repeat(70 * 1024) } };
  const oversize = await http(B, 'PUT', bPath, { token: bob.token, body: big });
  assertEq(oversize.status, 413, 'a 70 KB document -> 413');
  console.log(`      -> ${oversize.body?.error}`);
  const huge = await http(B, 'PUT', bPath, { token: bob.token, body: { ui: { pad: 'x'.repeat(200 * 1024) } } });
  assertEq(huge.status, 413, 'a 200 KB body (over the JSON parser limit) -> 413');
  assertEq((await http(B, 'GET', bPath, { token: bob.token })).status, 404, 'rejected PUTs stored nothing');
  const fits = await http(B, 'PUT', bPath, { token: bob.token, body: { ui: { pad: 'x'.repeat(60 * 1024) } } });
  assertEq(fits.status, 200, 'a 60 KB document fits');

  console.log('\n[5] rate limit: 60 per window per IP (GET and PUT together)');
  await sleep(WINDOW_MS + 250);
  const ip = '203.0.113.99';
  const statuses: number[] = [];
  for (let i = 0; i < 60; i++) statuses.push((await http(B, 'GET', aPath, { token: alice.token, ip })).status);
  assert(statuses.every(s => s === 200), 'requests 1-60 -> 200', `${statuses.filter(s => s === 200).length} x 200`);
  const limited = await http(B, 'PUT', aPath, { token: alice.token, ip, body: { ui } });
  assertEq(limited.status, 429, 'request 61 -> 429');
  assertEq((await http(B, 'GET', aPath, { token: alice.token, ip: '203.0.113.100' })).status, 200, 'another IP is not limited');

  console.log("\n[6] delete removes the settings");
  assertEq(await settingsRows(bob.characterId), 1, "bob's character has a settings row");
  assertEq((await http(B, 'DELETE', `/api/characters/${bob.characterId}`, { token: bob.token })).status, 200, "delete bob's character");
  assertEq(await settingsRows(bob.characterId), 0, 'its settings row is gone');

  console.log('\n[7] account settings (B-27): GET/PUT /api/account/settings');
  await sleep(WINDOW_MS + 250);
  const acc = '/api/account/settings';
  assertEq((await http(B, 'GET', acc)).status, 401, 'GET without a token -> 401');
  assertEq((await http(B, 'PUT', acc, { body: { graphics: {} } })).status, 401, 'PUT without a token -> 401');
  const accFirst = await http(B, 'GET', acc, { token: alice.token });
  assertEq(accFirst.status, 404, 'GET before any save -> 404');
  assertEq(accFirst.body?.noSettings, true, '404 body says noSettings');
  const graphics = { Version: 1, UpdatedAt: '2026-09-26T12:00:00.000Z', Quality: 'high', GlobalIllumination: true, ResolutionScale: 100, FrameRateCap: 0, VSync: false, MotionBlur: false };
  const accPut = await http(B, 'PUT', acc, { token: alice.token, body: { graphics } });
  assertEq(accPut.status, 200, 'PUT -> 200');
  assert(!Number.isNaN(Date.parse(accPut.body?.updatedAt)), 'PUT returns an ISO updatedAt', accPut.body?.updatedAt);
  const accGot = await http(B, 'GET', acc, { token: alice.token });
  assertEq(accGot.body.graphics, graphics, 'GET returns the document exactly as PUT sent it');
  await sleep(15);
  assertEq((await http(B, 'PUT', acc, { token: alice.token, body: { graphics: { ...graphics, Quality: 'epic' } } })).status, 200, 'second PUT -> 200');
  assertEq((await http(B, 'GET', acc, { token: alice.token })).body.graphics.Quality, 'epic', 'second PUT replaced the document');
  assertEq(await accountSettingsRows(alice.token, B), 1, 'one row per account (upsert)');
  assertEq((await http(B, 'GET', acc, { token: bob.token })).status, 404, "another account does not see alice's");
  assertEq((await http(B, 'PUT', acc, { token: bob.token, body: { graphics: [1] } })).status, 400, 'graphics is an array -> 400');
  assertEq((await http(B, 'PUT', acc, { token: bob.token, body: {} })).status, 400, 'no graphics -> 400');
  assertEq((await http(B, 'PUT', acc, { token: bob.token, body: { graphics: { pad: 'x'.repeat(70 * 1024) } } })).status, 413, 'a 70 KB document -> 413');
  assertEq((await http(B, 'GET', acc, { token: bob.token })).status, 404, 'rejected PUTs stored nothing');
  const gone = await http(B, 'POST', '/api/auth/account/delete', { token: alice.token, body: { password: 'smoke-pass-123', confirm: alice.username } });
  assertEq(gone.status, 200, "delete alice's account");
  const SQL = await initSqlJs();
  const db = new SQL.Database(readFileSync(dbFile));
  const left = db.exec('SELECT COUNT(*) FROM account_settings');
  db.close();
  assertEq(Number(left[0]?.values[0]?.[0] ?? 0), 0, 'its account settings row is gone');

  console.log(`\n[smoke-settings] ALL CHECKS PASSED (${passed} assertions)\n`);
}

run()
  .then(() => { stop(); process.exit(0); })
  .catch(err => {
    console.error(`\n[smoke-settings] FAILED: ${err?.message ?? err}`);
    console.error(`--- server output ---\n${output.join('').slice(-3000)}`);
    stop();
    process.exit(1);
  });
