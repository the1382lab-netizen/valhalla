# Stats fixtures — Valhalla 2.0 Phase 1b

`stats_fixtures.json` is the golden-value file the C++ port is tested against.
Every `expected` value in it was produced by calling the **real 1.0 TypeScript
functions**; nothing in this directory re-implements a formula.

## Regenerating

```sh
# from the repo root
npx tsx tools/fixtures/generate.ts
# or, if tsx is not on PATH:
./node_modules/.bin/tsx tools/fixtures/generate.ts
```

The generator is deterministic apart from `generatedAt` and the git SHA: the
damage pipeline uses a seeded mulberry32 PRNG (seed `0x5a1f0dd1`). It refuses to
write the file if the pure `resolveDamage()` ever diverges from the real
`CombatSystem.applyStatDamage()`.

## Files

| File | Purpose |
| --- | --- |
| `generate.ts` | Builds every section, runs the self-check, writes the JSON. |
| `resolveDamage.ts` | Pure, roll-explicit re-expression of `applyStatDamage()`. |
| `stats_fixtures.json` | The generated golden values. Do not hand-edit. |

## Top-level schema

```jsonc
{
  "version": 1,
  "generatedAt": "<iso8601>",
  "source": "valhalla 1.0 @ <git short sha>",
  "derivedStats":   [ { "classId", "level", "expected": ResolvedStats } ],
  "physicalDamage": [ { "base", "strength", "expected" } ],
  "spellDamage":    [ { "base", "intelligence", "expected" } ],
  "xpRequired":     [ { "level", "expected": number | null } ],
  "autoAttackSpeed":[ { "baseMs", "dexterity", "expected" } ],
  "damagePipeline": [ { "input": DamageInput, "rolls": DamageRolls, "expected": DamageOutcome } ]
}
```

## `damagePipeline` — the exact inputs the pipeline needs

`applyStatDamage()` takes six arguments plus four fields off the target's
`PlayerState`. Flattened into `input` (all camelCase, all numbers/booleans):

| Field | Source | Meaning |
| --- | --- | --- |
| `rawDamage` | `CombatSystem.ts:344` | Pre-crit, pre-block weapon/spell damage. |
| `isMagical` | `CombatSystem.ts:345` | `false` → `'physical'`, `true` → `'magical'`. |
| `attackerDexterity` | `CombatSystem.ts:346` | Feeds `computeHitChance()`. |
| `attackerCritChance` | `CombatSystem.ts:347` | Decimal (`0.1` = 10%). |
| `attackerCritDamage` | `CombatSystem.ts:348` | Decimal bonus (`0.5` → x1.5 on crit). |
| `defenderDodgeRating` | `target.stats.dodgeRating`, `CombatSystem.ts:366` | Decimal. |
| `defenderBlockRating` | `target.stats.blockRating`, `CombatSystem.ts:380` | Decimal. |
| `defenderPhysicalDefense` | `target.stats.physicalDefense`, `CombatSystem.ts:387` | Used only when `isMagical === false`. |
| `defenderSpellResist` | `target.stats.spellResist`, `CombatSystem.ts:388` | Used only when `isMagical === true`. |
| `defenderShieldHp` | `target.shieldHp`, `PlayerState.ts:162` | Absorbed before HP damage. |

`rolls` supplies the four `Math.random()` draws in **consumption order**:
`hit`, `dodge`, `crit`, `block` — each in `[0, 1)`.

`expected` is `{ outcome: "hit" | "miss" | "dodge", damage, crit, blocked, shieldAbsorbed }`.
`damage` is what is actually subtracted from HP, i.e. post-shield.

### Algorithm (`CombatSystem.ts:343–435`)

1. **Hit** — miss unless `rolls.hit < computeHitChance(attackerDexterity)`, where
   `computeHitChance(dex) = min(0.99, 0.65 + dex / (dex + 40))`. A miss returns immediately.
2. **Dodge** — dodged if `rolls.dodge < defenderDodgeRating`. Returns immediately.
3. **Crit** — `isCrit = rolls.crit < attackerCritChance`;
   `damage = rawDamage * (isCrit ? 1 + attackerCritDamage : 1)`.
4. **Block** — if `rolls.block < defenderBlockRating`, `damage *= 0.5`.
5. **Defense** — `damage = max(damage * 0.15, damage * (1 - defense / (defense + 50)))`
   with `defense = isMagical ? defenderSpellResist : defenderPhysicalDefense`.
6. **Floor** — `damage = max(1, floor(damage))`.
7. **Shield** — `absorbed = min(shieldHp, damage)`; `damage = max(0, damage - absorbed)`.

## Quirks the C++ port must reproduce exactly

1. **Roll order is crit-before-block, not block-before-crit.** The doc comment at
   `CombatSystem.ts:336` says "hit → dodge → block → crit"; the code at lines
   357–383 does hit → dodge → **crit** → **block**. The code is authoritative.
   This matters both for the arithmetic (crit multiplies the raw damage, block
   then halves the already-critted value — the result happens to be commutative,
   but the *RNG consumption order* is not) and for any shared RNG stream.
2. **Rolls short-circuit.** A miss consumes 1 random, a dodge consumes 2, a
   landed hit consumes 4. A port that always draws 4 will desync a shared stream.
3. **Strict `<` comparisons.** `rollDodge(0)` with a roll of `0.0` does **not**
   dodge (`0 < 0` is false). Same for block and crit at rating 0.
4. **`computeDerivedStats` clamps level to 1–20, but `MAX_LEVEL` is 25.**
   `shared/src/stats.ts:29` does `Math.max(1, Math.min(20, level))`, while
   `MAX_LEVEL = 25` (`stats.ts:193`). Levels 21–25 therefore return *identical*
   stats to level 20. The fixtures include levels 1–25 for all six classes so the
   port reproduces this plateau rather than "fixing" it.
5. **`computeDerivedStats` ignores `shared/data/classes.json`.** It reads the
   hardcoded `CLASS_TEMPLATES` from `shared/src/classes.ts:26` directly, and
   never consults `DataManager`. The two sources have **diverged**: e.g. warrior
   `baseSpeed` is `144` in `CLASS_TEMPLATES` but `110` in `classes.json`, so
   `expected.speed` in `derivedStats` is `144`. The C++ port must match
   `CLASS_TEMPLATES` for `derivedStats`, not the JSON.
6. **`xpRequiredForLevel(25)` returns `Infinity`.** JSON cannot encode that, so
   `expected` is `null` for level 25 (and would be for anything above). Levels
   1–24 are `100 * 2^(level-1)`.
7. **Magical damage uses `spellResist`, not `physicalResist`.** `physicalResist`
   is never read by the damage pipeline at all in 1.0.
8. **Floating point.** Values such as `dodgeRating: 0.041999999999999996` are
   genuine IEEE-754 double results of repeated addition in
   `base + growth * levelsGained`. The port should use `double` and compare with
   a tight epsilon (or reproduce the same accumulation order) rather than
   rounding.
9. **Energy / mana regen depend on `canUseMana`.** `maxEnergy` and
   `energyRegenRate` are `0` for casters; `manaRegenRate` is `0` for non-casters
   (`stats.ts:50–54`).
10. **`applyDefenseReduction` has a 15% floor**, so defense can never reduce
    damage below `rawDamage * 0.15` — and the subsequent `max(1, floor(...))`
    means a landed hit always deals at least 1 before shields.

## Coverage of `shared/src/stats.ts` exports

| Export | Covered by |
| --- | --- |
| `computeDerivedStats` | `derivedStats` |
| `computePhysicalDamage` | `physicalDamage` |
| `computeSpellDamage` | `spellDamage` |
| `xpRequiredForLevel` | `xpRequired` |
| `computeAutoAttackSpeed` | `autoAttackSpeed` |
| `applyDefenseReduction`, `computeHitChance` | `damagePipeline` (steps 1 and 5) |
| `rollHit`, `rollDodge`, `rollCrit`, `rollBlock` | `damagePipeline` — the `rolls` are exactly these functions' `Math.random()` draws |
| `computeFireCooldown`, `computeMeleeCooldown` | Bit-identical to `computeAutoAttackSpeed`; asserted by the generator's coverage check, so `autoAttackSpeed` serves all three. |
| `isRangedMagic` | Constant predicate: `true` for cleric, shaman, wizard; `false` for warrior, ranger, rogue. |
| `hasRangedAttack` | Constant predicate: `true` for ranger only. |
| `BASE_MELEE_DAMAGE` 10, `BASE_RANGED_DAMAGE` 8, `BASE_SPELL_DAMAGE` 12, `BASE_XP_PER_LEVEL` 100, `XP_SCALING` 2, `MAX_LEVEL` 25 | Constants; used as fixture bases. |

`isRangedMagic` / `hasRangedAttack` and the cooldown aliases have no dedicated
JSON section because the top-level schema is fixed; the generator asserts them on
every run and prints the results.
