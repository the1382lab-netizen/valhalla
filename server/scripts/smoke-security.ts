/**
 * Security smoke test (B-04): CORS allow-list, rate limits, and the
 * production-mode secret checks.
 *
 * Run from the repo root or server/:
 *   npx tsx server/scripts/smoke-security.ts
 *
 * Boots its own servers on free ports against throwaway SQLite files, with
 * VALHALLA_SECRETS_FILE pointed at a file that does not exist, so neither the
 * real database nor secrets.local.env is involved:
 *
 *   A. development mode, RATE_LIMIT_WINDOW_MS=4000 (so a tripped limit clears
 *      in seconds, not a minute): CORS, rate limits, unlimited routes
 *   B. NODE_ENV=production with a real JWT_SECRET but the public dev server
 *      secret: internal routes must answer 503
 *   C. NODE_ENV=production with no JWT_SECRET: the process must refuse to start
 *
 * Rate-limit buckets are separated with X-Forwarded-For, which the backend
 * believes only from loopback (Caddy): the test runs on loopback, so it can
 * play several "IPs" and also checks that they really are separate buckets.
 */

import { spawn, type ChildProcess } from 'child_process';
import { createServer } from 'net';
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';
import { existsSync, rmSync, mkdirSync } from 'fs';
import { tmpdir } from 'os';
import { randomBytes } from 'crypto';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SERVER_DIR = resolve(__dirname, '..');
const WINDOW_MS = 4000;
const EDITOR_ORIGIN = 'http://localhost:5180';
const FOREIGN_ORIGIN = 'https://evil.example';

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
  ok(label, e);
}
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));

// ── Servers ─────────────────────────────────────────────────

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

interface Spawned { child: ChildProcess; base: string; dir: string; output: string[] }
const spawned: Spawned[] = [];

async function spawnServer(label: string, env: Record<string, string>): Promise<Spawned> {
  const port = await freePort();
  const dir = resolve(tmpdir(), `valhalla-smoke-sec-${label}-${Date.now()}`);
  mkdirSync(dir, { recursive: true });
  const output: string[] = [];
  const cleanEnv: Record<string, string> = {};
  for (const [k, v] of Object.entries(process.env)) {
    // Nothing from the caller's shell or secrets file leaks into the test servers.
    if (v !== undefined && !/^(NODE_ENV|JWT_SECRET|VALHALLA_SERVER_SECRET|CORS_ORIGINS|HOST|PORT|VALHALLA_DB)$/.test(k)) cleanEnv[k] = v;
  }
  const child = spawn(process.execPath, ['--import', 'tsx', 'src/index.ts'], {
    cwd: SERVER_DIR,
    env: {
      ...cleanEnv,
      VALHALLA_SECRETS_FILE: resolve(dir, 'no-such-secrets.env'),
      HOST: '127.0.0.1',
      PORT: String(port),
      VALHALLA_DB: resolve(dir, 'smoke.db'),
      ...env,
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  child.stdout?.on('data', d => output.push(String(d)));
  child.stderr?.on('data', d => output.push(String(d)));
  const s: Spawned = { child, base: `http://127.0.0.1:${port}`, dir, output };
  spawned.push(s);
  return s;
}

async function waitHealthy(s: Spawned): Promise<void> {
  const deadline = Date.now() + 60_000;
  while (Date.now() < deadline) {
    if (s.child.exitCode !== null) throw new Error(`server exited early:\n${s.output.join('')}`);
    try {
      if ((await fetch(`${s.base}/api/health`)).status === 200) return;
    } catch { /* not up yet */ }
    await sleep(300);
  }
  throw new Error('server did not become healthy within 60 s');
}

function stopAll(): void {
  for (const s of spawned) {
    if (s.child.exitCode === null) s.child.kill();
    try { if (existsSync(s.dir)) rmSync(s.dir, { recursive: true, force: true }); } catch { /* still locked */ }
  }
}

// ── HTTP ────────────────────────────────────────────────────

interface Res { status: number; headers: Headers; body: any }

async function http(base: string, method: string, path: string,
  opts: { body?: unknown; origin?: string; ip?: string; token?: string; secret?: string; headers?: Record<string, string> } = {}): Promise<Res> {
  const headers: Record<string, string> = { ...(opts.headers ?? {}) };
  if (opts.body !== undefined) headers['Content-Type'] = 'application/json';
  if (opts.origin) headers.Origin = opts.origin;
  if (opts.ip) headers['X-Forwarded-For'] = opts.ip;
  if (opts.token) headers.Authorization = `Bearer ${opts.token}`;
  if (opts.secret) headers['X-Server-Secret'] = opts.secret;
  const res = await fetch(`${base}${path}`, { method, headers, body: opts.body === undefined ? undefined : JSON.stringify(opts.body) });
  const text = await res.text();
  let body: any = text;
  try { body = text ? JSON.parse(text) : null; } catch { /* not JSON */ }
  return { status: res.status, headers: res.headers, body };
}

// ── The test ────────────────────────────────────────────────

async function run(): Promise<void> {
  console.log('\n[smoke-security] A: development server (rate-limit window 4 s)');
  const dev = await spawnServer('dev', { NODE_ENV: 'development', RATE_LIMIT_WINDOW_MS: String(WINDOW_MS) });
  await waitHealthy(dev);
  const B = dev.base;

  console.log('\n[1] CORS allow-list');
  const pre = await http(B, 'OPTIONS', '/api/auth/login', {
    origin: EDITOR_ORIGIN, headers: { 'Access-Control-Request-Method': 'POST', 'Access-Control-Request-Headers': 'content-type' },
  });
  assertEq(pre.status, 204, 'preflight from the editor origin -> 204');
  assertEq(pre.headers.get('access-control-allow-origin'), EDITOR_ORIGIN, 'preflight echoes the editor origin');
  assertEq(pre.headers.get('access-control-allow-credentials'), 'true', 'preflight allows credentials');
  assert(/POST/.test(pre.headers.get('access-control-allow-methods') ?? ''), 'preflight allows POST');
  assert(!/x-server-secret/i.test(pre.headers.get('access-control-allow-headers') ?? ''), 'browsers are not offered X-Server-Secret');
  const alt = await http(B, 'GET', '/api/health', { origin: 'http://127.0.0.1:5180' });
  assertEq(alt.headers.get('access-control-allow-origin'), 'http://127.0.0.1:5180', 'the 127.0.0.1 editor origin is allowed too');
  const badPre = await http(B, 'OPTIONS', '/api/auth/login', { origin: FOREIGN_ORIGIN, headers: { 'Access-Control-Request-Method': 'POST' } });
  assertEq(badPre.status, 403, 'preflight from a foreign origin -> 403');
  assertEq(badPre.headers.get('access-control-allow-origin'), null, 'no Allow-Origin for a foreign origin');
  const badPost = await http(B, 'POST', '/api/auth/login', { origin: FOREIGN_ORIGIN, body: { username: 'x', password: 'y' } });
  assertEq(badPost.status, 403, 'POST from a foreign origin -> 403');
  const noOrigin = await http(B, 'GET', '/api/health');
  assertEq(noOrigin.status, 200, 'no Origin (game client / server) -> 200');
  assertEq(noOrigin.headers.get('access-control-allow-origin'), null, 'no Origin -> no CORS headers');

  console.log('\n[2] Login rate limit: 10 per window per IP');
  const ipA = '203.0.113.7';
  const wrong = { username: 'nobody_here', password: 'wrong-password' };
  for (let i = 1; i <= 10; i++) {
    const r = await http(B, 'POST', '/api/auth/login', { body: wrong, ip: ipA });
    if (r.status !== 401) throw new Error(`attempt ${i}: expected 401, got ${r.status}`);
  }
  ok('attempts 1-10 -> 401 (wrong password, not limited)');
  const eleventh = await http(B, 'POST', '/api/auth/login', { body: wrong, ip: ipA });
  assertEq(eleventh.status, 429, 'attempt 11 -> 429');
  const retryAfter = Number(eleventh.headers.get('retry-after'));
  assert(Number.isInteger(retryAfter) && retryAfter >= 1 && retryAfter <= WINDOW_MS / 1000, 'Retry-After in seconds', String(retryAfter));
  console.log(`      -> ${eleventh.body?.error}`);
  const ipB = await http(B, 'POST', '/api/auth/login', { body: wrong, ip: '203.0.113.8' });
  assertEq(ipB.status, 401, 'another IP (X-Forwarded-For from loopback) is not limited');
  await sleep(retryAfter * 1000 + 250);
  const cleared = await http(B, 'POST', '/api/auth/login', { body: wrong, ip: ipA });
  assertEq(cleared.status, 401, `after Retry-After (${retryAfter} s) the limit has cleared`);

  console.log('\n[3] Register rate limit: 10 per window per IP');
  const ipR = '198.51.100.20';
  for (let i = 1; i <= 10; i++) {
    const r = await http(B, 'POST', '/api/auth/register', { body: {}, ip: ipR });
    if (r.status !== 400) throw new Error(`register attempt ${i}: expected 400, got ${r.status}`);
  }
  ok('register attempts 1-10 -> 400 (empty body, not limited)');
  assertEq((await http(B, 'POST', '/api/auth/register', { body: {}, ip: ipR })).status, 429, 'register attempt 11 -> 429');

  console.log('\n[4] Character create rate limit: 5 per window per IP');
  const ipC = '198.51.100.30';
  const username = `sec_${String(Date.now()).slice(-10)}`;
  const reg = await http(B, 'POST', '/api/auth/register', { body: { username, password: 'smoke-pass-123' }, ip: ipC });
  assertEq(reg.status, 200, 'register a throwaway account');
  const token: string = reg.body.token;
  const createStatuses: number[] = [];
  for (let i = 1; i <= 5; i++) {
    const r = await http(B, 'POST', '/api/characters', { body: { name: `Sec${i}${String(Date.now()).slice(-6)}`, classId: 'warrior' }, token, ip: ipC });
    createStatuses.push(r.status);
  }
  assert(!createStatuses.includes(429), 'creates 1-5 are not limited', createStatuses.join(','));
  const sixth = await http(B, 'POST', '/api/characters', { body: { name: 'SecSix', classId: 'warrior' }, token, ip: ipC });
  assertEq(sixth.status, 429, 'create 6 -> 429');
  assert(Number(sixth.headers.get('retry-after')) >= 1, 'create 429 carries Retry-After', sixth.headers.get('retry-after') ?? '');

  console.log('\n[5] Health and internal routes are not limited');
  let healthOk = 0;
  for (let i = 0; i < 30; i++) if ((await http(B, 'GET', '/api/health', { ip: ipA })).status === 200) healthOk++;
  assertEq(healthOk, 30, '30 x GET /api/health from one IP -> all 200');
  let verify401 = 0;
  for (let i = 0; i < 20; i++) {
    const r = await http(B, 'POST', '/api/auth/verify', { body: { token: 'not-a-jwt' }, secret: 'dev-server-secret', ip: ipA });
    if (r.status === 401) verify401++;
  }
  assertEq(verify401, 20, '20 x POST /api/auth/verify (dev mode, dev secret, bad token) -> all 401, none 429');

  console.log('\n[smoke-security] B: NODE_ENV=production, real JWT_SECRET, dev server secret');
  const prod = await spawnServer('prod', {
    NODE_ENV: 'production',
    JWT_SECRET: randomBytes(32).toString('hex'),
    VALHALLA_SERVER_SECRET: 'dev-server-secret',
  });
  await waitHealthy(prod);
  assertEq((await http(prod.base, 'GET', '/api/health')).status, 200, 'production health -> 200');
  const refused = await http(prod.base, 'POST', '/api/auth/verify', { body: { token: 'x' }, secret: 'dev-server-secret' });
  assertEq(refused.status, 503, 'internal route with the dev secret in production -> 503');
  console.log(`      -> ${refused.body?.error}`);
  const refusedLoad = await http(prod.base, 'GET', '/api/characters/1/load?userId=1', { secret: 'dev-server-secret' });
  assertEq(refusedLoad.status, 503, 'character load with the dev secret in production -> 503');
  assert(prod.output.join('').includes('DISABLED'), 'startup log says the internal routes are DISABLED');

  console.log('\n[smoke-security] C: NODE_ENV=production without JWT_SECRET');
  const noJwt = await spawnServer('nojwt', { NODE_ENV: 'production', VALHALLA_SERVER_SECRET: randomBytes(32).toString('hex') });
  const exitCode = await new Promise<number | null>(res => {
    const t = setTimeout(() => res(null), 30_000);
    noJwt.child.on('exit', code => { clearTimeout(t); res(code); });
  });
  assert(exitCode !== null && exitCode !== 0, 'production without JWT_SECRET refuses to start', `exit ${exitCode}`);
  assert(noJwt.output.join('').includes('JWT_SECRET is not set'), 'and says why');

  console.log(`\n[smoke-security] ALL CHECKS PASSED (${passed} assertions)\n`);
}

run()
  .then(() => { stopAll(); process.exit(0); })
  .catch(err => {
    console.error(`\n[smoke-security] FAILED: ${err?.message ?? err}`);
    for (const s of spawned) console.error(`--- ${s.base} output ---\n${s.output.join('').slice(-3000)}`);
    stopAll();
    process.exit(1);
  });
