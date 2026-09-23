/**
 * Pure, deterministic re-expression of CombatSystem.applyStatDamage().
 *
 * Source: server/src/systems/CombatSystem.ts, applyStatDamage() — lines 343–435.
 * Shield helper: server/src/schema/PlayerState.ts, applyShieldAbsorption() — lines 162–177.
 *
 * The real implementation draws four Math.random() values internally (via
 * rollHit / rollDodge / rollCrit / rollBlock in shared/src/stats.ts). This
 * function takes them explicitly so the exact same math can be replayed
 * deterministically — in TypeScript here, and in the C++ port later.
 *
 * IMPORTANT ORDERING NOTE: the doc comment above applyStatDamage (CombatSystem.ts
 * line 336) claims "hit -> dodge -> block -> crit", but the CODE order is
 * hit -> dodge -> CRIT -> BLOCK -> defense. The code is authoritative; this
 * function follows the code. The Math.random() consumption order is therefore
 * hit, dodge, crit, block — and short-circuits: a miss consumes 1 roll, a dodge
 * consumes 2, a landed hit consumes all 4.
 *
 * All formula helpers are imported from the real shared module — nothing here
 * is re-implemented.
 */

import {
  computeHitChance,
  applyDefenseReduction,
} from '@valhalla/shared';

/** Every input applyStatDamage() reads, flattened out of PlayerState / the call args. */
export interface DamageInput {
  /** CombatSystem.ts:344 `rawDamage` — pre-crit, pre-block weapon/spell damage. */
  rawDamage: number;
  /** CombatSystem.ts:345 `damageType`: false => 'physical', true => 'magical'. */
  isMagical: boolean;
  /** CombatSystem.ts:346 `attackerDex` — feeds computeHitChance(). */
  attackerDexterity: number;
  /** CombatSystem.ts:347 `attackerCritChance` — decimal, 0.1 = 10%. */
  attackerCritChance: number;
  /** CombatSystem.ts:348 `attackerCritDamage` — decimal bonus, 0.5 => x1.5 on crit. */
  attackerCritDamage: number;
  /** CombatSystem.ts:366 `target.stats.dodgeRating` — decimal. */
  defenderDodgeRating: number;
  /** CombatSystem.ts:380 `target.stats.blockRating` — decimal. */
  defenderBlockRating: number;
  /** CombatSystem.ts:387 `target.stats.physicalDefense` — used when isMagical === false. */
  defenderPhysicalDefense: number;
  /** CombatSystem.ts:388 `target.stats.spellResist` — used when isMagical === true. */
  defenderSpellResist: number;
  /** PlayerState.ts:162 `player.shieldHp` — absorbed before HP damage. */
  defenderShieldHp: number;
}

/** The four Math.random() draws, in consumption order. Each in [0, 1). */
export interface DamageRolls {
  hit: number;
  dodge: number;
  crit: number;
  block: number;
}

export interface DamageOutcome {
  outcome: 'hit' | 'miss' | 'dodge';
  /** Damage actually subtracted from target.hp (post-shield). 0 on miss/dodge. */
  damage: number;
  crit: boolean;
  blocked: boolean;
  /** How much of the post-floor damage the shield ate. */
  shieldAbsorbed: number;
}

export function resolveDamage(input: DamageInput, rolls: DamageRolls): DamageOutcome {
  // ── 1. Hit roll ── CombatSystem.ts:357 `if (!rollHit(attackerDex))`
  //    rollHit (stats.ts:137) === Math.random() < computeHitChance(dexterity)
  if (!(rolls.hit < computeHitChance(input.attackerDexterity))) {
    return { outcome: 'miss', damage: 0, crit: false, blocked: false, shieldAbsorbed: 0 };
  }

  // ── 2. Dodge roll ── CombatSystem.ts:366 `if (rollDodge(tStats?.dodgeRating ?? 0))`
  //    rollDodge (stats.ts:144) === Math.random() < dodgeRating
  if (rolls.dodge < input.defenderDodgeRating) {
    return { outcome: 'dodge', damage: 0, crit: false, blocked: false, shieldAbsorbed: 0 };
  }

  // ── 3. Crit roll ── CombatSystem.ts:375-376
  //    rollCrit (stats.ts:108) === { isCrit: Math.random() < critChance,
  //                                  multiplier: isCrit ? 1 + critDamage : 1 }
  const isCrit = rolls.crit < input.attackerCritChance;
  const critMultiplier = isCrit ? 1 + input.attackerCritDamage : 1;
  let damage = input.rawDamage * critMultiplier;

  // ── 4. Block roll ── CombatSystem.ts:379-383 (halves, never negates)
  //    rollBlock (stats.ts:152) === Math.random() < blockRating
  let blocked = false;
  if (rolls.block < input.defenderBlockRating) {
    damage *= 0.5;
    blocked = true;
  }

  // ── 5. Defense reduction ── CombatSystem.ts:386-389
  //    physical => physicalDefense, magical => spellResist (NOT physicalResist).
  //    applyDefenseReduction (stats.ts:98):
  //      reduction = defense / (defense + 50)
  //      return max(rawDamage * 0.15, rawDamage * (1 - reduction))
  const defense = input.isMagical ? input.defenderSpellResist : input.defenderPhysicalDefense;
  damage = applyDefenseReduction(damage, defense);

  // ── 6. Floor, minimum 1 ── CombatSystem.ts:392
  damage = Math.max(1, Math.floor(damage));

  // ── 7. Shield absorption ── CombatSystem.ts:395 -> PlayerState.ts:162-177
  //    absorbed = min(shieldHp, incoming); remaining = max(0, incoming - absorbed)
  let shieldAbsorbed = 0;
  if (input.defenderShieldHp > 0) {
    shieldAbsorbed = Math.min(input.defenderShieldHp, damage);
    damage = Math.max(0, damage - shieldAbsorbed);
  }

  return { outcome: 'hit', damage, crit: isCrit, blocked, shieldAbsorbed };
}
