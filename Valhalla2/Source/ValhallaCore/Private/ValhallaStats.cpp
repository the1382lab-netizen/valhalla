// Copyright Valhalla 2.0. All Rights Reserved.
//
// Line references in the comments point at the 1.0 TypeScript this mirrors.
// If a formula here and the TS disagree, the TS wins — change this file.

#include "ValhallaStats.h"

#include "ValhallaConstants.h"

#include <limits>

namespace Valhalla::Stats
{
	// ─────────────────────────────────────────────────────────────────────
	//  Derived stats
	// ─────────────────────────────────────────────────────────────────────

	FValhallaResolvedStats ComputeDerivedStats(const FValhallaClassTemplate& ClassTemplate, int32 Level)
	{
		const FValhallaStatBlock& Base = ClassTemplate.BaseStats;
		const FValhallaStatBlock& Growth = ClassTemplate.StatsPerLevel;

		// stats.ts:29 — clamp 1-20. Note this is NOT MAX_LEVEL (25); the 1.0
		// server deliberately stops scaling stats at 20.
		const int32 ClampedLevel = FMath::Max(Valhalla::MinStatLevel, FMath::Min(Valhalla::MaxStatLevel, Level));
		const double LevelsGained = static_cast<double>(ClampedLevel - 1);

		// stats.ts:32-47 — base + growth * levelsGained, field by field.
		auto Scale = [LevelsGained](float BaseValue, float GrowthValue) -> double
		{
			return static_cast<double>(BaseValue) + static_cast<double>(GrowthValue) * LevelsGained;
		};

		const double Hp              = Scale(Base.Hp,              Growth.Hp);
		const double Mana            = Scale(Base.Mana,            Growth.Mana);
		const double Strength        = Scale(Base.Strength,        Growth.Strength);
		const double Stamina         = Scale(Base.Stamina,         Growth.Stamina);
		const double Dexterity       = Scale(Base.Dexterity,       Growth.Dexterity);
		const double Intelligence    = Scale(Base.Intelligence,    Growth.Intelligence);
		const double Wisdom          = Scale(Base.Wisdom,          Growth.Wisdom);
		const double PhysicalResist  = Scale(Base.PhysicalResist,  Growth.PhysicalResist);
		const double SpellResist     = Scale(Base.SpellResist,     Growth.SpellResist);
		const double CritChance      = Scale(Base.CritChance,      Growth.CritChance);
		const double CritDamage      = Scale(Base.CritDamage,      Growth.CritDamage);
		const double PhysicalDefense = Scale(Base.PhysicalDefense, Growth.PhysicalDefense);
		const double BlockRating     = Scale(Base.BlockRating,     Growth.BlockRating);
		const double DodgeRating     = Scale(Base.DodgeRating,     Growth.DodgeRating);

		// stats.ts:50-51 — energy is for non-casters only.
		const double MaxEnergy = ClassTemplate.bCanUseMana
			? 0.0
			: Valhalla::BaseEnergy + Stamina * Valhalla::EnergyPerStamina;
		const double EnergyRegenRate = ClassTemplate.bCanUseMana
			? 0.0
			: Stamina * Valhalla::EnergyRegenPerStamina;

		// stats.ts:54 — mana regen is for casters only.
		const double ManaRegenRate = ClassTemplate.bCanUseMana
			? Valhalla::BaseManaRegen + Intelligence * Valhalla::ManaRegenPerIntelligence
			: 0.0;

		FValhallaResolvedStats Out;
		Out.Hp              = static_cast<float>(Hp);
		Out.Mana            = static_cast<float>(Mana);
		Out.Strength        = static_cast<float>(Strength);
		Out.Stamina         = static_cast<float>(Stamina);
		Out.Dexterity       = static_cast<float>(Dexterity);
		Out.Intelligence    = static_cast<float>(Intelligence);
		Out.Wisdom          = static_cast<float>(Wisdom);
		Out.PhysicalResist  = static_cast<float>(PhysicalResist);
		Out.SpellResist     = static_cast<float>(SpellResist);
		Out.CritChance      = static_cast<float>(CritChance);
		Out.CritDamage      = static_cast<float>(CritDamage);
		Out.PhysicalDefense = static_cast<float>(PhysicalDefense);
		Out.BlockRating     = static_cast<float>(BlockRating);
		Out.DodgeRating     = static_cast<float>(DodgeRating);

		// stats.ts:58-63 — maxHp/maxMana mirror the scaled pools exactly.
		Out.MaxHp           = static_cast<float>(Hp);
		Out.MaxMana         = static_cast<float>(Mana);
		Out.MaxEnergy       = static_cast<float>(MaxEnergy);
		Out.EnergyRegenRate = static_cast<float>(EnergyRegenRate);
		Out.ManaRegenRate   = static_cast<float>(ManaRegenRate);
		Out.Speed           = ClassTemplate.BaseSpeed;

		return Out;
	}

	// ─────────────────────────────────────────────────────────────────────
	//  Damage formulas
	// ─────────────────────────────────────────────────────────────────────

	double ComputePhysicalDamage(double BaseDamage, double Strength)
	{
		// stats.ts computePhysicalDamage — `baseDamage + strength * STRENGTH_DAMAGE_SCALING`
		// (0.8 in 1.0, 0.4 since 2026-09-24).
		return BaseDamage + Strength * Valhalla::StrengthDamageScaling;
	}

	double ComputeRangedPhysicalDamage(double BaseDamage, double Dexterity)
	{
		// stats.ts computeRangedPhysicalDamage — a ranged auto-attack scales
		// with Dexterity, not Strength (Kevin, 2026-09-24).
		return BaseDamage + Dexterity * Valhalla::DexterityDamageScaling;
	}

	double ComputeSpellDamage(double BaseDamage, double Intelligence)
	{
		// stats.ts:86 — `return baseDamage + intelligence * 0.9;`
		return BaseDamage + Intelligence * Valhalla::IntelligenceDamageScaling;
	}

	double ApplyDefenseReduction(double RawDamage, double Defense)
	{
		// stats.ts:99-101. Defense of 0 gives a 0 reduction, and the division
		// is safe because DefenseSoftCap (50) keeps the denominator non-zero
		// for every non-negative defense value.
		const double Reduction = Defense / (Defense + Valhalla::DefenseSoftCap);
		const double MinDamage = RawDamage * Valhalla::MinDamageFraction;
		return FMath::Max(MinDamage, RawDamage * (1.0 - Reduction));
	}

	double ComputeCritMultiplier(bool bIsCrit, double CritDamage)
	{
		// stats.ts:112 — `isCrit ? 1 + critDamage : 1`
		return bIsCrit ? (1.0 + CritDamage) : 1.0;
	}

	// ─────────────────────────────────────────────────────────────────────
	//  Hit / dodge / block / crit
	// ─────────────────────────────────────────────────────────────────────

	double ComputeHitChance(double Dexterity)
	{
		// stats.ts:129-130 — 0.65 + dex / (dex + 40), capped at 0.99.
		const double Raw = Valhalla::BaseHitChance + Dexterity / (Dexterity + Valhalla::HitChanceDexSoftCap);
		return FMath::Min(Valhalla::MaxHitChance, Raw);
	}

	bool CheckHit(double Roll, double Dexterity)
	{
		// stats.ts:138 — `Math.random() < computeHitChance(dexterity)`
		return Roll < ComputeHitChance(Dexterity);
	}

	bool CheckDodge(double Roll, double DodgeRating)
	{
		// stats.ts:145 — `Math.random() < dodgeRating`
		return Roll < DodgeRating;
	}

	bool CheckBlock(double Roll, double BlockRating)
	{
		// stats.ts:153 — `Math.random() < blockRating`
		return Roll < BlockRating;
	}

	bool CheckCrit(double Roll, double CritChance)
	{
		// stats.ts:109 — `Math.random() < critChance`
		return Roll < CritChance;
	}

	// ─────────────────────────────────────────────────────────────────────
	//  Attack speed
	// ─────────────────────────────────────────────────────────────────────

	double ComputeAutoAttackSpeed(double BaseMs, double Dexterity)
	{
		// stats.ts:185-186 — reduction = min(0.4, dex * 0.012).
		const double Reduction = FMath::Min(Valhalla::MaxAttackSpeedReduction, Dexterity * Valhalla::AttackSpeedDexScaling);
		return BaseMs * (1.0 - Reduction);
	}

	double ComputeFireCooldown(double BaseCooldownMs, double Dexterity)
	{
		// stats.ts:161-162 — identical formula to ComputeAutoAttackSpeed.
		const double Reduction = FMath::Min(Valhalla::MaxAttackSpeedReduction, Dexterity * Valhalla::AttackSpeedDexScaling);
		return BaseCooldownMs * (1.0 - Reduction);
	}

	double ComputeMeleeCooldown(double BaseCooldownMs, double Dexterity)
	{
		// stats.ts:171-172 — deprecated in 1.0, same formula again.
		const double Reduction = FMath::Min(Valhalla::MaxAttackSpeedReduction, Dexterity * Valhalla::AttackSpeedDexScaling);
		return BaseCooldownMs * (1.0 - Reduction);
	}

	// ─────────────────────────────────────────────────────────────────────
	//  XP & leveling
	// ─────────────────────────────────────────────────────────────────────

	double XpRequiredForLevel(int32 CurrentLevel)
	{
		// stats.ts:200-201. The TS returns `Infinity` at max level; we return a
		// real IEEE infinity so every comparison behaves the same way.
		if (CurrentLevel >= Valhalla::MaxLevel)
		{
			return std::numeric_limits<double>::infinity();
		}
		return Valhalla::BaseXpPerLevel * FMath::Pow(Valhalla::XpScaling, static_cast<double>(CurrentLevel - 1));
	}

	// ─────────────────────────────────────────────────────────────────────
	//  Class predicates
	// ─────────────────────────────────────────────────────────────────────

	bool IsRangedMagic(FName ClassId)
	{
		// stats.ts:209 — wizard, cleric or shaman.
		static const FName NameWizard(TEXT("wizard"));
		static const FName NameCleric(TEXT("cleric"));
		static const FName NameShaman(TEXT("shaman"));
		return ClassId == NameWizard || ClassId == NameCleric || ClassId == NameShaman;
	}

	double RollWeaponDamage(double MinDamage, double MaxDamage, double Roll01)
	{
		if (MaxDamage <= MinDamage)
		{
			return MaxDamage;
		}
		return MinDamage + (MaxDamage - MinDamage) * FMath::Clamp(Roll01, 0.0, 1.0);
	}

	double RollNPCMeleeDamage(double NpcMin, double NpcMax, double NpcRoll01,
		bool bHasWeapon, double WeaponMin, double WeaponMax, double WeaponRoll01)
	{
		double Damage = RollWeaponDamage(NpcMin, NpcMax, NpcRoll01);
		if (bHasWeapon)
		{
			Damage += RollWeaponDamage(WeaponMin, WeaponMax, WeaponRoll01);
		}
		return FMath::Max(1.0, Damage);
	}

	bool HasRangedAttack(FName ClassId)
	{
		// stats.ts:217 — ranger only.
		static const FName NameRanger(TEXT("ranger"));
		return ClassId == NameRanger;
	}

	// ─────────────────────────────────────────────────────────────────────
	//  Shield absorption (PlayerState.ts:162)
	// ─────────────────────────────────────────────────────────────────────

	double ApplyShieldAbsorption(double IncomingDamage, double ShieldHp, double& OutAbsorbed, double& OutRemainingShield)
	{
		OutAbsorbed = 0.0;
		OutRemainingShield = ShieldHp;

		// PlayerState.ts:163 — no shield, nothing to do.
		if (ShieldHp <= 0.0)
		{
			return IncomingDamage;
		}

		// PlayerState.ts:165-167
		OutAbsorbed = FMath::Min(ShieldHp, IncomingDamage);
		OutRemainingShield = ShieldHp - OutAbsorbed;

		// PlayerState.ts:169-171 — a depleted shield clamps to 0 and its buff
		// is dropped. Buff bookkeeping belongs to the caller in 2.0.
		if (OutRemainingShield <= 0.0)
		{
			OutRemainingShield = 0.0;
		}

		// PlayerState.ts:176
		return FMath::Max(0.0, IncomingDamage - OutAbsorbed);
	}

	// ─────────────────────────────────────────────────────────────────────
	//  The damage pipeline (CombatSystem.ts:343 applyStatDamage)
	// ─────────────────────────────────────────────────────────────────────

	FValhallaDamageResult ResolveDamage(const FValhallaDamageInput& Input, const FValhallaDamageRolls& Rolls)
	{
		FValhallaDamageResult Result;
		Result.RemainingShieldHp = Input.DefenderShieldHp;

		// 1. Hit roll — CombatSystem.ts:364. A miss ends the pipeline.
		if (!CheckHit(Rolls.Hit, Input.AttackerDexterity))
		{
			Result.Outcome = EValhallaDamageOutcome::Miss;
			return Result;
		}

		// 2. Dodge roll — CombatSystem.ts:373. Also ends the pipeline.
		if (CheckDodge(Rolls.Dodge, Input.DefenderDodgeRating))
		{
			Result.Outcome = EValhallaDamageOutcome::Dodge;
			return Result;
		}

		// 3. Crit roll — CombatSystem.ts:381-382. Applied before the block.
		const bool bIsCrit = CheckCrit(Rolls.Crit, Input.AttackerCritChance);
		double Damage = Input.RawDamage
			* ComputeCritMultiplier(bIsCrit, Input.AttackerCritDamage);

		// 4. Block roll — CombatSystem.ts:385-389. Reduces, never negates.
		const bool bBlocked = CheckBlock(Rolls.Block, Input.DefenderBlockRating);
		if (bBlocked)
		{
			Damage *= Valhalla::BlockDamageMultiplier;
		}

		// 5. Defense reduction — CombatSystem.ts:391-395. Magical damage is
		//    mitigated by spellResist, physical by physicalDefense.
		const double Defense = Input.bIsMagical
			? Input.DefenderSpellResist
			: Input.DefenderPhysicalDefense;
		Damage = ApplyDefenseReduction(Damage, Defense);

		// 6. Floor, minimum 1 — CombatSystem.ts:392 (`Math.max(1, Math.floor(d))`).
		Damage = FMath::Max(1.0, FMath::FloorToDouble(Damage));

		// 7. Shield absorption — CombatSystem.ts:395. What is left hits HP.
		double Absorbed = 0.0;
		double RemainingShield = 0.0;
		Damage = ApplyShieldAbsorption(Damage, Input.DefenderShieldHp, Absorbed, RemainingShield);

		Result.Outcome = EValhallaDamageOutcome::Hit;
		Result.Damage = static_cast<int32>(Damage);
		Result.bCrit = bIsCrit;
		Result.bBlocked = bBlocked;
		Result.ShieldAbsorbed = static_cast<int32>(Absorbed);
		Result.RemainingShieldHp = RemainingShield;

		// CombatSystem.ts:399 — a landed hit always starts the i-frame window.
		Result.InvulnerabilityMs = Valhalla::InvulnerabilityMs;

		return Result;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Blueprint wrappers — casts only, no logic.
// ─────────────────────────────────────────────────────────────────────────────

FValhallaResolvedStats UValhallaStatsLibrary::ComputeDerivedStats(const FValhallaClassTemplate& ClassTemplate, int32 Level)
{
	return Valhalla::Stats::ComputeDerivedStats(ClassTemplate, Level);
}

float UValhallaStatsLibrary::ComputePhysicalDamage(float BaseDamage, float Strength)
{
	return static_cast<float>(Valhalla::Stats::ComputePhysicalDamage(BaseDamage, Strength));
}

float UValhallaStatsLibrary::ComputeSpellDamage(float BaseDamage, float Intelligence)
{
	return static_cast<float>(Valhalla::Stats::ComputeSpellDamage(BaseDamage, Intelligence));
}

float UValhallaStatsLibrary::ApplyDefenseReduction(float RawDamage, float Defense)
{
	return static_cast<float>(Valhalla::Stats::ApplyDefenseReduction(RawDamage, Defense));
}

float UValhallaStatsLibrary::ComputeHitChance(float Dexterity)
{
	return static_cast<float>(Valhalla::Stats::ComputeHitChance(Dexterity));
}

float UValhallaStatsLibrary::ComputeAutoAttackSpeed(float BaseMs, float Dexterity)
{
	return static_cast<float>(Valhalla::Stats::ComputeAutoAttackSpeed(BaseMs, Dexterity));
}

float UValhallaStatsLibrary::ComputeFireCooldown(float BaseCooldownMs, float Dexterity)
{
	return static_cast<float>(Valhalla::Stats::ComputeFireCooldown(BaseCooldownMs, Dexterity));
}

float UValhallaStatsLibrary::XpRequiredForLevel(int32 CurrentLevel)
{
	// Blueprints have no infinity, so the "you are max level" case reports -1.
	// C++ callers should use Valhalla::Stats::XpRequiredForLevel instead.
	if (CurrentLevel >= Valhalla::MaxLevel)
	{
		return -1.f;
	}
	return static_cast<float>(Valhalla::Stats::XpRequiredForLevel(CurrentLevel));
}

bool UValhallaStatsLibrary::IsRangedMagic(FName ClassId)
{
	return Valhalla::Stats::IsRangedMagic(ClassId);
}

bool UValhallaStatsLibrary::HasRangedAttack(FName ClassId)
{
	return Valhalla::Stats::HasRangedAttack(ClassId);
}

FValhallaDamageResult UValhallaStatsLibrary::ResolveDamage(const FValhallaDamageInput& Input, const FValhallaDamageRolls& Rolls)
{
	return Valhalla::Stats::ResolveDamage(Input, Rolls);
}

int32 UValhallaStatsLibrary::GetMaxLevel()
{
	return Valhalla::MaxLevel;
}
