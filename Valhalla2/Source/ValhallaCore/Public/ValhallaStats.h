// Copyright Valhalla 2.0. All Rights Reserved.
//
// Deterministic port of Valhalla 1.0's stat and damage math.
//
//   shared/src/stats.ts                    — every exported function
//   server/src/systems/CombatSystem.ts:343 — applyStatDamage
//   server/src/schema/PlayerState.ts:162   — applyShieldAbsorption
//
// Two layers live here:
//
//   Valhalla::Stats::*        Free functions on `double`. These are the port.
//                             JavaScript numbers are IEEE-754 doubles, so doing
//                             the arithmetic in double is what makes the C++
//                             results bit-comparable with the 1.0 server.
//
//   UValhallaStatsLibrary     Thin BlueprintPure wrappers taking/returning
//                             float. Nothing but casts and a call into the
//                             layer above — never put logic here.
//
// Every roll is passed in rather than drawn, so the whole pipeline is a pure
// function: same inputs, same outputs, on client, server and test runner.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ValhallaTypes.h"
#include "ValhallaStats.generated.h"

namespace Valhalla::Stats
{
	// ── Derived stats (stats.ts:25 computeDerivedStats) ──────────────────

	/**
	 * Resolve a character's full stat block: base + (level - 1) * perLevel,
	 * with the level clamped into [1, 20] exactly as the 1.0 server does.
	 * Also fills the energy/mana pools and regen rates.
	 */
	VALHALLACORE_API FValhallaResolvedStats ComputeDerivedStats(const FValhallaClassTemplate& ClassTemplate, int32 Level);

	// ── Damage formulas (stats.ts:78-102) ───────────────────────────────

	/** stats.ts:78 — baseDamage + strength * 0.8. */
	VALHALLACORE_API double ComputePhysicalDamage(double BaseDamage, double Strength);

	/** stats.ts:85 — baseDamage + intelligence * 0.9. */
	VALHALLACORE_API double ComputeSpellDamage(double BaseDamage, double Intelligence);

	/**
	 * stats.ts:98 — diminishing-returns mitigation.
	 * reduction = defense / (defense + 50); at least 15% of raw always lands.
	 */
	VALHALLACORE_API double ApplyDefenseReduction(double RawDamage, double Defense);

	/** stats.ts:112 — the damage multiplier for a hit: 1 + critDamage on a crit. */
	VALHALLACORE_API double ComputeCritMultiplier(bool bIsCrit, double CritDamage);

	// ── Hit / dodge / block (stats.ts:128-154, rolls supplied by caller) ──

	/** stats.ts:128 — 0.65 + dex / (dex + 40), hard-capped at 0.99. */
	VALHALLACORE_API double ComputeHitChance(double Dexterity);

	/** stats.ts:137 — true when the attack connects. Roll is in [0, 1). */
	VALHALLACORE_API bool CheckHit(double Roll, double Dexterity);

	/** stats.ts:144 — true when the target dodges. Roll is in [0, 1). */
	VALHALLACORE_API bool CheckDodge(double Roll, double DodgeRating);

	/** stats.ts:152 — true when the target blocks (halves damage). */
	VALHALLACORE_API bool CheckBlock(double Roll, double BlockRating);

	/** stats.ts:108 — true when the attack crits. Roll is in [0, 1). */
	VALHALLACORE_API bool CheckCrit(double Roll, double CritChance);

	// ── Attack speed (stats.ts:160-187) ─────────────────────────────────

	/** stats.ts:184 — baseMs scaled by dexterity; at most 40% faster. */
	VALHALLACORE_API double ComputeAutoAttackSpeed(double BaseMs, double Dexterity);

	/** stats.ts:160 — same formula, applied to the ranged fire cooldown. */
	VALHALLACORE_API double ComputeFireCooldown(double BaseCooldownMs, double Dexterity);

	/** stats.ts:170 — deprecated in 1.0; kept so the port is complete. */
	VALHALLACORE_API double ComputeMeleeCooldown(double BaseCooldownMs, double Dexterity);

	// ── XP & leveling (stats.ts:199) ────────────────────────────────────

	/**
	 * XP needed to leave CurrentLevel: 100 * 2^(level - 1).
	 * Returns +infinity at or past MAX_LEVEL (25), matching the TS `Infinity`.
	 */
	VALHALLACORE_API double XpRequiredForLevel(int32 CurrentLevel);

	// ── Class predicates (stats.ts:208-218) ─────────────────────────────

	/** stats.ts:208 — wizard / cleric / shaman fire magical projectiles. */
	VALHALLACORE_API bool IsRangedMagic(FName ClassId);

	/** stats.ts:216 — only the Ranger has a ranged basic attack. */
	VALHALLACORE_API bool HasRangedAttack(FName ClassId);

	/**
	 * 2.0: a weapon damage roll — Min + (Max - Min) * Roll01, with Roll01
	 * clamped to [0, 1] and a reversed range treated as fixed at Max. The
	 * caller supplies the roll (FMath::FRand in play, fixed values in tests).
	 */
	VALHALLACORE_API double RollWeaponDamage(double MinDamage, double MaxDamage, double Roll01);

	/**
	 * An NPC's hit before mitigation (2.0): its own roll in [NpcMin, NpcMax],
	 * plus — when it carries a weapon — the weapon's roll in [WeaponMin,
	 * WeaponMax]. Two independent rolls, so a Test Enemy (7–13) with an Iron
	 * Sword (4–9) hits for 11–22. Never below 1.
	 */
	VALHALLACORE_API double RollNPCMeleeDamage(double NpcMin, double NpcMax, double NpcRoll01,
		bool bHasWeapon, double WeaponMin, double WeaponMax, double WeaponRoll01);

	// ── Damage pipeline (CombatSystem.ts:343 applyStatDamage) ───────────

	/**
	 * The full mitigation pipeline, deterministic.
	 *
	 * Order is load-bearing and matches applyStatDamage exactly:
	 *   1. hit roll          -> Miss, nothing else is evaluated
	 *   2. dodge roll        -> Dodge, nothing else is evaluated
	 *   3. crit roll         -> damage *= 1 + critDamage
	 *   4. block roll        -> damage *= 0.5
	 *   5. defense reduction -> physicalDefense or spellResist
	 *   6. floor, minimum 1
	 *   7. shield absorption -> the remainder comes off HP
	 */
	VALHALLACORE_API FValhallaDamageResult ResolveDamage(const FValhallaDamageInput& Input, const FValhallaDamageRolls& Rolls);

	/**
	 * PlayerState.ts:162 — spend an absorb shield against incoming damage.
	 * Returns the damage left over for HP; OutAbsorbed and OutRemainingShield
	 * report what the shield took and what is left of it.
	 */
	VALHALLACORE_API double ApplyShieldAbsorption(double IncomingDamage, double ShieldHp, double& OutAbsorbed, double& OutRemainingShield);
}

/**
 * Blueprint face of the stat library. Every function is a pure cast-and-call
 * into Valhalla::Stats; C++ callers should prefer the namespace directly so
 * they keep double precision.
 */
UCLASS()
class VALHALLACORE_API UValhallaStatsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Resolve a class template's stats at a level. Level is clamped to [1, 20]. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static FValhallaResolvedStats ComputeDerivedStats(const FValhallaClassTemplate& ClassTemplate, int32 Level);

	/** BaseDamage + Strength * 0.8. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static float ComputePhysicalDamage(float BaseDamage, float Strength);

	/** BaseDamage + Intelligence * 0.9. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static float ComputeSpellDamage(float BaseDamage, float Intelligence);

	/** Diminishing-returns mitigation; at least 15% of RawDamage always lands. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static float ApplyDefenseReduction(float RawDamage, float Defense);

	/** 0.65 + Dexterity / (Dexterity + 40), capped at 0.99. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static float ComputeHitChance(float Dexterity);

	/** Effective ms between auto-attacks after dexterity scaling. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static float ComputeAutoAttackSpeed(float BaseMs, float Dexterity);

	/** Ranged fire cooldown after dexterity scaling. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static float ComputeFireCooldown(float BaseCooldownMs, float Dexterity);

	/** XP needed to leave CurrentLevel. Returns -1 at or past max level. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static float XpRequiredForLevel(int32 CurrentLevel);

	/** True for wizard / cleric / shaman — their projectiles are magical. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static bool IsRangedMagic(FName ClassId);

	/** True only for the ranger. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static bool HasRangedAttack(FName ClassId);

	/** Run the deterministic damage pipeline. Rolls are each in [0, 1). */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Combat")
	static FValhallaDamageResult ResolveDamage(const FValhallaDamageInput& Input, const FValhallaDamageRolls& Rolls);

	/** The maximum character level (25). */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Stats")
	static int32 GetMaxLevel();
};
