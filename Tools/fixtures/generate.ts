/**
 * Valhalla 2.0 — Phase 1b stats fixture generator.
 *
 * Produces tools/fixtures/stats_fixtures.json: a golden-value file the C++ port
 * is tested against. Every `expected` value in the output is produced by calling
 * the REAL 1.0 TypeScript functions — nothing in this file re-implements a formula.
 *
 * Run from the repo root:
 *     npx tsx tools/fixtures/generate.ts
 *
 * See tools/fixtures/README.md for the schema and the 1.0 quirks the C++ side
 * has to reproduce.
 */

import { writeFileSync, readFileSync, existsSync } from 'fs';
import { resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import { execFileSync } from 'child_process';

// ── Real 1.0 formulas — every exported function from shared/src/stats.ts ──
import {
  // stat resolution
  computeDerivedStats,
  // damage formulas
  computePhysicalDamage,
  computeSpellDamage,
  applyDefenseReduction,
  rollCrit,
  computeHitChance,
  rollHit,
  rollDodge,
  rollBlock,
  // attack speed
  computeFireCooldown,
  computeMeleeCooldown,
  computeAutoAttackSpeed,
  // xp
  xpRequiredForLevel,
  MAX_LEVEL,
  BASE_XP_PER_LEVEL,
  XP_SCALING,
  // class predicates
  isRangedMagic,
  hasRangedAttack,
  // damage bases
  BASE_MELEE_DAMAGE,
  BASE_RANGED_DAMAGE,
  BASE_SPELL_DAMAGE,
  // class data
  ClassId,
  ALL_CLASS_IDS,
  CLASS_TEMPLATES,
  loadClassesFromJson,
} from '@valhalla/shared';
import type { ClassTemplate, ResolvedStats } from '@valhalla/shared';

// ── Real server-side pipeline, for the Math.random self-check ──
import { CombatSystem } from '../../server/src/systems/CombatSystem.js';
import { PlayerState } from '../../server/src/schema/PlayerState.js';

import { resolveDamage, type DamageInput, type DamageRolls } from './resolveDamage.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const REPO_ROOT = resolve(__dirname, '..', '..');
const OUT_PATH = resolve(__dirname, 'stats_fixtures.json');

// ── Class templates, loaded the way the server's DataManager does ──────────
// (server/src/systems/DataManager.ts:178-190 — classes.json wins, CLASS_TEMPLATES
// is the fallback.) NOTE: computeDerivedStats() does NOT consult this; it reads
// CLASS_TEMPLATES directly (shared/src/stats.ts:26). See README.
function loadClassTemplates(): { classes: Record<string, ClassTemplate>; fromJson: boolean } {
  const jsonPath = resolve(REPO_ROOT, 'shared', 'data', 'classes.json');
  if (existsSync(jsonPath)) {
    try {
      const loaded = loadClassesFromJson(JSON.parse(readFileSync(jsonPath, 'utf-8')));
      if (loaded && Object.keys(loaded.classes).length > 0) {
        return { classes: loaded.classes, fromJson: true };
      }
    } catch (e) {
      console.warn('[fixtures] classes.json unreadable, falling back:', e);
    }
  }
  return { classes: { ...CLASS_TEMPLATES } as Record<string, ClassTemplate>, fromJson: false };
}

// ── Seeded PRNG (mulberry32) — deterministic regeneration ──────────────────
function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return function () {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** JSON has no Infinity. xpRequiredForLevel returns Infinity at MAX_LEVEL. */
function jsonNumber(n: number): number | null {
  return Number.isFinite(n) ? n : null;
}

function gitShortSha(): string {
  try {
    return execFileSync('git', ['rev-parse', '--short', 'HEAD'], {
      cwd: REPO_ROOT,
      stdio: ['ignore', 'pipe', 'ignore'],
    })
      .toString()
      .trim();
  } catch {
    return 'unknown';
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Section 1 — derivedStats: 6 classes x levels 1..25
// ═══════════════════════════════════════════════════════════════════════════
interface DerivedStatsCase {
  classId: string;
  level: number;
  expected: ResolvedStats;
}

function buildDerivedStats(): DerivedStatsCase[] {
  const cases: DerivedStatsCase[] = [];
  for (const classId of ALL_CLASS_IDS) {
    for (let level = 1; level <= 25; level++) {
      cases.push({
        classId,
        level,
        expected: computeDerivedStats(classId as ClassId, level),
      });
    }
  }
  return cases;
}

// ═══════════════════════════════════════════════════════════════════════════
// Section 2 — physicalDamage: base + strength * STRENGTH_DAMAGE_SCALING
// (0.8 in 1.0; 0.4 since 2026-09-24, Kevin — the committed fixture's
// `physicalDamage` expectations were rewritten to match)
// ═══════════════════════════════════════════════════════════════════════════
function buildPhysicalDamage(): Array<{ base: number; strength: number; expected: number }> {
  // Bases: unarmed melee/ranged constants plus realistic weapon attackDamage bonuses.
  const bases = [
    BASE_RANGED_DAMAGE,                 // 8
    BASE_MELEE_DAMAGE,                  // 10
    BASE_MELEE_DAMAGE + 3,
    BASE_MELEE_DAMAGE + 7,
    BASE_MELEE_DAMAGE + 12,
    BASE_MELEE_DAMAGE + 20,
    BASE_RANGED_DAMAGE + 5,
    BASE_RANGED_DAMAGE + 14,
    0,                                  // degenerate: no weapon, no base
    45,                                 // high-end endgame weapon
  ];
  // Strengths spanning wizard L1 (4) through warrior L20 (77) and beyond.
  const strengths = [0, 4, 10, 20, 25, 38, 55, 77, 90, 120];
  const cases: Array<{ base: number; strength: number; expected: number }> = [];
  for (const base of bases) {
    for (const strength of strengths) {
      cases.push({ base, strength, expected: computePhysicalDamage(strength, base) });
    }
  }
  return cases; // 10 x 10 = 100 -> trimmed below
}

// ═══════════════════════════════════════════════════════════════════════════
// Section 3 — spellDamage: base + intelligence * 0.9
// ═══════════════════════════════════════════════════════════════════════════
function buildSpellDamage(): Array<{ base: number; intelligence: number; expected: number }> {
  const bases = [
    BASE_SPELL_DAMAGE,                  // 12
    BASE_SPELL_DAMAGE + 4,
    BASE_SPELL_DAMAGE + 9,
    BASE_SPELL_DAMAGE + 18,
    BASE_SPELL_DAMAGE + 30,
    0,
    25,
    40,
    60,
    8,
  ];
  // Wizard L1 = 22, wizard L20 = 98; cleric/shaman sit lower.
  const intelligences = [0, 4, 6, 10, 14, 22, 40, 60, 98, 130];
  const cases: Array<{ base: number; intelligence: number; expected: number }> = [];
  for (const base of bases) {
    for (const intelligence of intelligences) {
      cases.push({ base, intelligence, expected: computeSpellDamage(intelligence, base) });
    }
  }
  return cases;
}

// ═══════════════════════════════════════════════════════════════════════════
// Section 4 — xpRequired: 100 * 2^(level-1), Infinity at MAX_LEVEL (25)
// ═══════════════════════════════════════════════════════════════════════════
function buildXpRequired(): Array<{ level: number; expected: number | null }> {
  const cases: Array<{ level: number; expected: number | null }> = [];
  for (let level = 1; level <= 25; level++) {
    cases.push({ level, expected: jsonNumber(xpRequiredForLevel(level)) });
  }
  return cases;
}

// ═══════════════════════════════════════════════════════════════════════════
// Section 5 — autoAttackSpeed: baseMs * (1 - min(0.4, dex * 0.012))
// ═══════════════════════════════════════════════════════════════════════════
function buildAutoAttackSpeed(
  templates: Record<string, ClassTemplate>,
): Array<{ baseMs: number; dexterity: number; expected: number }> {
  // Class unarmed swing speeds, plus typical weapon attackSpeedMs values.
  const classBases = new Set<number>();
  for (const t of Object.values(templates)) {
    if (t.baseMeleeAttackSpeedMs > 0) classBases.add(t.baseMeleeAttackSpeedMs);
    if (t.baseRangedAttackSpeedMs > 0) classBases.add(t.baseRangedAttackSpeedMs);
  }
  const bases = [...classBases, 1500, 2000].sort((a, b) => a - b);
  // dex 33.334 is where the 40% reduction cap bites (0.4 / 0.012 = 33.33...)
  const dexterities = [0, 6, 10, 20, 33, 34, 50, 100];
  const cases: Array<{ baseMs: number; dexterity: number; expected: number }> = [];
  for (const baseMs of bases) {
    for (const dexterity of dexterities) {
      cases.push({ baseMs, dexterity, expected: computeAutoAttackSpeed(baseMs, dexterity) });
    }
  }
  return cases;
}

// ═══════════════════════════════════════════════════════════════════════════
// Section 6 — damagePipeline: 200 seeded cases through resolveDamage()
// ═══════════════════════════════════════════════════════════════════════════
interface PipelineCase {
  input: DamageInput;
  rolls: DamageRolls;
  expected: ReturnType<typeof resolveDamage>;
}

function buildPipelineCases(count: number): PipelineCase[] {
  const rnd = mulberry32(0x5a1f0dd1);
  const pick = <T,>(arr: T[]): T => arr[Math.floor(rnd() * arr.length)]!;
  const cases: PipelineCase[] = [];

  // Deterministic corner cases first, so each branch is guaranteed covered.
  const forced: Array<Partial<DamageInput> & { rolls: DamageRolls }> = [
    // guaranteed miss: hit roll above the hit chance ceiling (0.99 cap)
    { attackerDexterity: 10, rolls: { hit: 0.999, dodge: 0.0, crit: 0.0, block: 0.0 } },
    // 0-dex attacker, hit chance = 0.65 exactly
    { attackerDexterity: 0, rolls: { hit: 0.6499, dodge: 0.99, crit: 0.99, block: 0.99 } },
    { attackerDexterity: 0, rolls: { hit: 0.65, dodge: 0.99, crit: 0.99, block: 0.99 } },
    // guaranteed dodge
    { defenderDodgeRating: 0.5, rolls: { hit: 0.0, dodge: 0.0, crit: 0.0, block: 0.0 } },
    // zero dodge rating: roll 0 must still NOT dodge (0 < 0 is false)
    { defenderDodgeRating: 0, rolls: { hit: 0.0, dodge: 0.0, crit: 0.99, block: 0.99 } },
    // guaranteed crit, no block
    { attackerCritChance: 0.5, attackerCritDamage: 1.5, rolls: { hit: 0.0, dodge: 0.9, crit: 0.0, block: 0.99 } },
    // guaranteed crit AND block
    { attackerCritChance: 0.5, defenderBlockRating: 0.5, rolls: { hit: 0.0, dodge: 0.9, crit: 0.0, block: 0.0 } },
    // zero defense (no reduction at all)
    { defenderPhysicalDefense: 0, defenderSpellResist: 0, isMagical: false, rolls: { hit: 0.0, dodge: 0.9, crit: 0.9, block: 0.9 } },
    // huge defense: the 15% floor of applyDefenseReduction must bind
    { defenderPhysicalDefense: 5000, rawDamage: 100, isMagical: false, rolls: { hit: 0.0, dodge: 0.9, crit: 0.9, block: 0.9 } },
    { defenderSpellResist: 5000, rawDamage: 100, isMagical: true, rolls: { hit: 0.0, dodge: 0.9, crit: 0.9, block: 0.9 } },
    // tiny raw damage: the max(1, floor(..)) minimum must bind
    { rawDamage: 1, defenderPhysicalDefense: 200, rolls: { hit: 0.0, dodge: 0.9, crit: 0.9, block: 0.9 } },
    { rawDamage: 0, defenderPhysicalDefense: 0, rolls: { hit: 0.0, dodge: 0.9, crit: 0.9, block: 0.9 } },
    // shield fully absorbs
    { rawDamage: 20, defenderShieldHp: 500, rolls: { hit: 0.0, dodge: 0.9, crit: 0.9, block: 0.9 } },
    // shield partially absorbs
    { rawDamage: 200, defenderPhysicalDefense: 0, defenderShieldHp: 25, rolls: { hit: 0.0, dodge: 0.9, crit: 0.9, block: 0.9 } },
    // magical damage ignores physicalDefense entirely
    { isMagical: true, defenderPhysicalDefense: 999, defenderSpellResist: 10, rawDamage: 80, rolls: { hit: 0.0, dodge: 0.9, crit: 0.9, block: 0.9 } },
    // the fixture example from the Phase 1b spec
    {
      rawDamage: 50, isMagical: false, attackerDexterity: 20, attackerCritChance: 0.1,
      attackerCritDamage: 0.5, defenderDodgeRating: 0.05, defenderBlockRating: 0.1,
      defenderPhysicalDefense: 30, defenderSpellResist: 10, defenderShieldHp: 0,
      rolls: { hit: 0.3, dodge: 0.9, crit: 0.95, block: 0.5 },
    },
  ];

  const DEFAULTS: DamageInput = {
    rawDamage: 50,
    isMagical: false,
    attackerDexterity: 20,
    attackerCritChance: 0.1,
    attackerCritDamage: 0.5,
    defenderDodgeRating: 0.05,
    defenderBlockRating: 0.1,
    defenderPhysicalDefense: 30,
    defenderSpellResist: 10,
    defenderShieldHp: 0,
  };

  for (const f of forced) {
    const { rolls, ...overrides } = f;
    const input: DamageInput = { ...DEFAULTS, ...overrides };
    cases.push({ input, rolls, expected: resolveDamage(input, rolls) });
  }

  // Randomised body, drawn from realistic 1.0 stat ranges.
  const rawDamages = [1, 6, 12, 18, 26, 34, 50, 68, 88, 120, 175];
  const dexes = [0, 4, 6, 8, 10, 14, 18, 20, 28, 40, 67];
  const critChances = [0, 0.03, 0.05, 0.08, 0.12, 0.2, 0.3];
  const critDamages = [0.4, 0.5, 0.6, 0.7, 0.8, 1.2];
  const dodges = [0, 0.03, 0.04, 0.1, 0.12, 0.25, 0.4];
  const blocks = [0, 0.02, 0.08, 0.1, 0.15, 0.3, 0.45];
  const physDefs = [0, 2, 4, 6, 12, 25, 50, 100, 260];
  const resists = [0, 3, 4, 6, 8, 15, 30, 70];
  const shields = [0, 0, 0, 5, 20, 45, 120, 400];

  while (cases.length < count) {
    const input: DamageInput = {
      rawDamage: pick(rawDamages),
      isMagical: rnd() < 0.4,
      attackerDexterity: pick(dexes),
      attackerCritChance: pick(critChances),
      attackerCritDamage: pick(critDamages),
      defenderDodgeRating: pick(dodges),
      defenderBlockRating: pick(blocks),
      defenderPhysicalDefense: pick(physDefs),
      defenderSpellResist: pick(resists),
      defenderShieldHp: pick(shields),
    };
    // Bias the roll distribution so misses/dodges/crits/blocks all appear often.
    const rolls: DamageRolls = {
      hit: rnd() < 0.15 ? 0.9 + rnd() * 0.1 : rnd() * 0.9,
      dodge: rnd() < 0.3 ? rnd() * 0.12 : rnd(),
      crit: rnd() < 0.35 ? rnd() * 0.12 : rnd(),
      block: rnd() < 0.35 ? rnd() * 0.15 : rnd(),
    };
    cases.push({ input, rolls, expected: resolveDamage(input, rolls) });
  }
  return cases;
}

// ═══════════════════════════════════════════════════════════════════════════
// Self-check: replay resolveDamage() against the REAL applyStatDamage()
// with Math.random monkey-patched to a fixed sequence.
// ═══════════════════════════════════════════════════════════════════════════
interface SelfCheckResult { total: number; passed: number; failures: string[] }

function makeTarget(input: DamageInput): PlayerState {
  const p = new PlayerState();
  p.id = 'target';
  p.alive = true;
  p.hp = 100000;          // huge, so the death branch never fires and never clamps
  p.maxHp = 100000;
  p.shieldHp = input.defenderShieldHp;
  p.activeBuffs = [];
  // CombatSystem only reads these four fields off target.stats (lines 366/380/387/388).
  p.stats = {
    dodgeRating: input.defenderDodgeRating,
    blockRating: input.defenderBlockRating,
    physicalDefense: input.defenderPhysicalDefense,
    spellResist: input.defenderSpellResist,
  } as any;
  return p;
}

function runSelfCheck(cases: PipelineCase[]): SelfCheckResult {
  const combat = new CombatSystem(null as any); // applyStatDamage never touches collision
  const applyStatDamage = (combat as any).applyStatDamage.bind(combat) as (
    target: PlayerState, rawDamage: number, damageType: 'physical' | 'magical',
    attackerDex: number, critChance: number, critDamage: number,
    attackerId: string, now: number,
  ) => Array<{ type: string; data: any }>;

  const realRandom = Math.random;
  const failures: string[] = [];
  let passed = 0;

  try {
    for (let i = 0; i < cases.length; i++) {
      const { input, rolls, expected } = cases[i]!;
      const target = makeTarget(input);
      const hpBefore = target.hp;
      const shieldBefore = target.shieldHp;

      // Feed the four rolls in applyStatDamage's exact consumption order.
      const queue = [rolls.hit, rolls.dodge, rolls.crit, rolls.block];
      let idx = 0;
      Math.random = () => {
        if (idx >= queue.length) throw new Error(`case ${i}: applyStatDamage drew more than 4 randoms`);
        return queue[idx++]!;
      };

      let events: Array<{ type: string; data: any }>;
      try {
        events = applyStatDamage(
          target, input.rawDamage, input.isMagical ? 'magical' : 'physical',
          input.attackerDexterity, input.attackerCritChance, input.attackerCritDamage,
          'attacker', 0,
        );
      } finally {
        Math.random = realRandom;
      }

      // Read the real outcome back off the events + mutated state.
      const missed = events.some(e => e.type === 'missed');
      const dodged = events.some(e => e.type === 'dodged');
      const hitEvt = events.find(e => e.type === 'playerHit');
      const actual = {
        outcome: missed ? 'miss' : dodged ? 'dodge' : 'hit',
        damage: hitEvt ? hitEvt.data.damage : 0,
        crit: hitEvt ? !!hitEvt.data.isCrit : false,
        blocked: hitEvt ? !!hitEvt.data.blocked : false,
        shieldAbsorbed: shieldBefore - target.shieldHp,
      };

      const mismatches: string[] = [];
      for (const k of ['outcome', 'damage', 'crit', 'blocked', 'shieldAbsorbed'] as const) {
        if ((actual as any)[k] !== (expected as any)[k]) {
          mismatches.push(`${k}: real=${(actual as any)[k]} pure=${(expected as any)[k]}`);
        }
      }
      // Side-effect cross-check: hp actually dropped by the reported damage.
      if (hpBefore - target.hp !== actual.damage) {
        mismatches.push(`hp delta ${hpBefore - target.hp} != damage ${actual.damage}`);
      }

      if (mismatches.length) {
        failures.push(`case ${i} ${JSON.stringify({ input, rolls })}: ${mismatches.join('; ')}`);
      } else {
        passed++;
      }
    }
  } finally {
    Math.random = realRandom;
  }

  return { total: cases.length, passed, failures };
}

/**
 * Extra coverage for exported formulas that have no dedicated fixture section
 * because they are bit-identical to, or trivially derived from, one that does.
 */
function runFormulaCoverageChecks(): string[] {
  const notes: string[] = [];
  let cooldownMatches = true;
  for (const baseMs of [1500, 2400, 3600, 4800]) {
    for (const dex of [0, 10, 33, 34, 100]) {
      const a = computeAutoAttackSpeed(baseMs, dex);
      if (computeFireCooldown(baseMs, dex) !== a || computeMeleeCooldown(baseMs, dex) !== a) {
        cooldownMatches = false;
      }
    }
  }
  notes.push(
    `computeFireCooldown / computeMeleeCooldown identical to computeAutoAttackSpeed: ${cooldownMatches}`,
  );
  notes.push(
    `isRangedMagic: ${ALL_CLASS_IDS.map(c => `${c}=${isRangedMagic(c as ClassId)}`).join(' ')}`,
  );
  notes.push(
    `hasRangedAttack: ${ALL_CLASS_IDS.map(c => `${c}=${hasRangedAttack(c as ClassId)}`).join(' ')}`,
  );
  notes.push(
    `xp constants: BASE_XP_PER_LEVEL=${BASE_XP_PER_LEVEL} XP_SCALING=${XP_SCALING} MAX_LEVEL=${MAX_LEVEL}`,
  );
  // rollHit/rollDodge/rollBlock/rollCrit are the Math.random wrappers exercised by
  // the pipeline self-check; assert their threshold semantics once here.
  const real = Math.random;
  try {
    Math.random = () => 0;
    const ok =
      rollHit(10) === true &&
      rollDodge(0) === false &&
      rollDodge(0.01) === true &&
      rollBlock(0) === false &&
      rollCrit(0, 0.5).isCrit === false &&
      rollCrit(1, 0.5).multiplier === 1.5 &&
      applyDefenseReduction(100, 50) === 50 &&
      computeHitChance(1000) === 0.99;
    notes.push(`roll wrapper / threshold semantics at Math.random()=0: ${ok}`);
  } finally {
    Math.random = real;
  }
  return notes;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════
function main(): void {
  const { classes, fromJson } = loadClassTemplates();
  console.log(`[fixtures] class templates: ${fromJson ? 'shared/data/classes.json' : 'CLASS_TEMPLATES fallback'} (${Object.keys(classes).length} classes)`);

  const derivedStats = buildDerivedStats();
  const physicalDamage = buildPhysicalDamage().slice(0, 50);
  const spellDamage = buildSpellDamage().slice(0, 50);
  const xpRequired = buildXpRequired();
  const autoAttackSpeed = buildAutoAttackSpeed(classes).slice(0, 40);
  const damagePipeline = buildPipelineCases(200);

  // ── Self-check against the real applyStatDamage ──
  const selfCheck = runSelfCheck(damagePipeline);
  console.log(`\n[self-check] applyStatDamage vs resolveDamage: ${selfCheck.passed}/${selfCheck.total} matched`);
  for (const f of selfCheck.failures.slice(0, 10)) console.error(`  FAIL ${f}`);
  if (selfCheck.failures.length > 10) console.error(`  ... and ${selfCheck.failures.length - 10} more`);

  console.log('\n[coverage]');
  for (const n of runFormulaCoverageChecks()) console.log(`  ${n}`);

  if (selfCheck.passed !== selfCheck.total) {
    console.error('\n[fixtures] ABORTING: pure resolveDamage() diverges from applyStatDamage().');
    process.exit(1);
  }

  const fixtures = {
    version: 1,
    generatedAt: new Date().toISOString(),
    source: `valhalla 1.0 @ ${gitShortSha()}`,
    derivedStats,
    physicalDamage,
    spellDamage,
    xpRequired,
    autoAttackSpeed,
    damagePipeline,
  };

  const text = JSON.stringify(fixtures, null, 2);
  JSON.parse(text); // validity guard
  writeFileSync(OUT_PATH, text + '\n', 'utf-8');

  const counts = {
    derivedStats: derivedStats.length,
    physicalDamage: physicalDamage.length,
    spellDamage: spellDamage.length,
    xpRequired: xpRequired.length,
    autoAttackSpeed: autoAttackSpeed.length,
    damagePipeline: damagePipeline.length,
  };
  const total = Object.values(counts).reduce((a, b) => a + b, 0);

  console.log(`\n[fixtures] wrote ${OUT_PATH}`);
  for (const [k, v] of Object.entries(counts)) console.log(`  ${k.padEnd(16)} ${v}`);
  console.log(`  ${'TOTAL'.padEnd(16)} ${total}`);

  const outcomes = damagePipeline.reduce<Record<string, number>>((acc, c) => {
    acc[c.expected.outcome] = (acc[c.expected.outcome] ?? 0) + 1;
    return acc;
  }, {});
  const crits = damagePipeline.filter(c => c.expected.crit).length;
  const blocks = damagePipeline.filter(c => c.expected.blocked).length;
  const shielded = damagePipeline.filter(c => c.expected.shieldAbsorbed > 0).length;
  const magical = damagePipeline.filter(c => c.input.isMagical).length;
  console.log(
    `\n[pipeline coverage] hit=${outcomes.hit ?? 0} miss=${outcomes.miss ?? 0} dodge=${outcomes.dodge ?? 0} ` +
    `crit=${crits} blocked=${blocks} shieldAbsorbed>0=${shielded} magical=${magical} physical=${damagePipeline.length - magical}`,
  );
}

main();
