/**
 * B-13: check every cross-reference in the game data.
 *
 *   npm run validate              errors fail (exit 1), warnings are listed
 *   npm run validate -- --strict  warnings fail too
 *   npm run validate -- --quiet   only print problems and the summary
 *   npm run validate -- --errors-only   only print errors and the summary (the pre-commit hook)
 *   npm run validate -- --json    machine-readable output
 *
 * Also run by the git pre-commit hook (npm run hooks:install) and, through
 * the same validateGameData, by the web editor after every Save.
 */
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';
import { summarizeIssues, validateGameData, type ValidationIssue } from '../src/validation.js';
import { loadValidationInput } from '../src/validation-load.js';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const args = new Set(process.argv.slice(2));
const strict = args.has('--strict');
const errorsOnly = args.has('--errors-only');
const quiet = args.has('--quiet') || errorsOnly;
const asJson = args.has('--json');

const { input, problems } = loadValidationInput(repoRoot);
const issues: ValidationIssue[] = [
  ...problems.map(p => ({ severity: 'error' as const, category: 'items' as const, id: p.file, message: p.message })),
  ...validateGameData(input),
];
const summary = summarizeIssues(issues);

if (asJson) {
  console.log(JSON.stringify({ summary, issues }, null, 2));
} else {
  const sections = [
    { severity: 'error', title: 'Errors (the game will misbehave)' },
    { severity: 'warning', title: 'Warnings (works, probably not as intended)' },
    { severity: 'info', title: 'Notes' },
  ] as const;
  for (const section of sections) {
    if (quiet && section.severity === 'info') continue;
    if (errorsOnly && section.severity !== 'error') continue;
    const list = issues.filter(i => i.severity === section.severity);
    if (list.length === 0) continue;
    console.log(`\n${section.title}`);
    for (const category of [...new Set(list.map(i => i.category))]) {
      console.log(`  [${category}]`);
      for (const issue of list.filter(i => i.category === category)) console.log(`    ${issue.id}: ${issue.message}`);
    }
  }
  console.log(`\n[validate] ${summary.errors} error(s), ${summary.warnings} warning(s)${strict ? ' (strict: warnings fail)' : ''}`);
}

const failed = summary.errors > 0 || (strict && summary.warnings > 0);
process.exit(failed ? 1 : 0);
