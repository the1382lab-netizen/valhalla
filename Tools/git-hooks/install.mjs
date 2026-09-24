/**
 * Installs Tools/git-hooks/pre-commit as this checkout's git pre-commit hook.
 * Run once per clone: `npm run hooks:install`.
 *
 * Writes into the hooks directory git actually uses (core.hooksPath if set,
 * otherwise .git/hooks), next to Git LFS's own hooks, and refuses to replace a
 * pre-commit hook it didn't write.
 */
import { execSync } from 'child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, readFileSync } from 'fs';
import { dirname, isAbsolute, join, resolve } from 'path';
import { fileURLToPath } from 'url';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..', '..');
const source = join(here, 'pre-commit');
const MARKER = 'Valhalla pre-commit hook (B-13)';

let hooksDir;
try {
  const rel = execSync('git rev-parse --git-path hooks', { cwd: repoRoot, encoding: 'utf8' }).trim();
  hooksDir = isAbsolute(rel) ? rel : resolve(repoRoot, rel);
} catch {
  console.error('[hooks] not a git checkout (git rev-parse failed).');
  process.exit(1);
}

mkdirSync(hooksDir, { recursive: true });
const target = join(hooksDir, 'pre-commit');
if (existsSync(target) && !readFileSync(target, 'utf8').includes(MARKER)) {
  console.error(`[hooks] ${target} already exists and is not ours; not replacing it.`);
  console.error('[hooks] Add this line to it instead:  npx --no-install tsx shared/scripts/validate-data.ts --errors-only || exit 1');
  process.exit(1);
}
copyFileSync(source, target);
try { chmodSync(target, 0o755); } catch { /* Windows: git runs it through sh anyway */ }
console.log(`[hooks] installed ${target}`);
