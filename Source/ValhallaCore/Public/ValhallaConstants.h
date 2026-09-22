// Copyright Valhalla 2.0. All Rights Reserved.
//
// Ported verbatim from the Valhalla 1.0 TypeScript sources:
//   shared/src/constants.ts
//   shared/src/stats.ts  (the damage / XP constants declared alongside the formulas)
//
// Every value is `double` so the arithmetic in ValhallaStats matches JavaScript
// number semantics bit-for-bit. Nothing here is a UPROPERTY: these are compile
// time constants, not reflected data.

#pragma once

#include "CoreMinimal.h"

namespace Valhalla
{
	// ── World & Tiles (constants.ts:2-10) ───────────────────────────────
	/** Default Tiled tile size in 1.0 pixels. Maps may override it. */
	inline constexpr int32 DefaultTileSize = 64;

	// ── Server (constants.ts:13-15) ─────────────────────────────────────
	inline constexpr int32 ServerTickRate = 60;
	inline constexpr double ServerTickMs = 1000.0 / 60.0;
	inline constexpr int32 ServerPort = 2567;

	// ── Player (constants.ts:19-21) ─────────────────────────────────────
	/** Legacy fallback speed; prefer FValhallaClassTemplate::BaseSpeed. */
	inline constexpr double LegacyPlayerSpeed = 160.0;
	inline constexpr double PlayerSize = 48.0;
	inline constexpr double PlayerCollisionRadius = 20.0;

	// ── Combat (constants.ts:25-38) ─────────────────────────────────────
	/** Legacy fallback max HP; prefer ComputeDerivedStats(). */
	inline constexpr double LegacyPlayerMaxHp = 100.0;
	inline constexpr double ProjectileSpeed = 400.0;
	inline constexpr double ProjectileRadius = 6.0;
	inline constexpr double ProjectileMaxRange = 600.0;
	inline constexpr double ProjectileDamage = 15.0;
	/** Invulnerability window (i-frames) applied after a landed hit, in ms. */
	inline constexpr double InvulnerabilityMs = 500.0;
	inline constexpr double RespawnTimeMs = 3000.0;
	inline constexpr double MeleeDamage = 25.0;
	inline constexpr double MeleeRange = 60.0;
	/** 90 degrees. Spelled out rather than using a PI macro so this header
	 *  depends on nothing but CoreMinimal. */
	inline constexpr double MeleeArc = 3.141592653589793238462643383279502884 / 2.0;

	// ── Spell projectiles (constants.ts:42-48) ──────────────────────────
	inline constexpr double FireballProjectileSpeed = 350.0;
	inline constexpr double FireballAoeRadius = 96.0;
	inline constexpr double FireballProjectileRadius = 10.0;
	inline constexpr double FireballDamageFalloffMin = 0.35;

	// ── Network (constants.ts:51) ───────────────────────────────────────
	inline constexpr double InterpolationBufferMs = 100.0;

	// ── Base weapon damage (stats.ts:70-72) ─────────────────────────────
	inline constexpr double BaseMeleeDamage = 10.0;
	inline constexpr double BaseRangedDamage = 8.0;
	inline constexpr double BaseSpellDamage = 12.0;

	// ── Formula tuning constants (stats.ts) ─────────────────────────────
	/** computePhysicalDamage: base + strength * this. (stats.ts:79) */
	inline constexpr double StrengthDamageScaling = 0.8;
	/** computeSpellDamage: base + intelligence * this. (stats.ts:86) */
	inline constexpr double IntelligenceDamageScaling = 0.9;
	/** applyDefenseReduction: reduction = defense / (defense + this). (stats.ts:99) */
	inline constexpr double DefenseSoftCap = 50.0;
	/** applyDefenseReduction: this fraction of raw damage always gets through. (stats.ts:100) */
	inline constexpr double MinDamageFraction = 0.15;
	/** computeHitChance: 0.65 + dex / (dex + 40). (stats.ts:129) */
	inline constexpr double BaseHitChance = 0.65;
	inline constexpr double HitChanceDexSoftCap = 40.0;
	/** computeHitChance caps here — attacks can always miss. (stats.ts:130) */
	inline constexpr double MaxHitChance = 0.99;
	/** computeAutoAttackSpeed / computeFireCooldown: reduction = dex * this. (stats.ts:185) */
	inline constexpr double AttackSpeedDexScaling = 0.012;
	/** …capped here (40% faster at most). (stats.ts:185) */
	inline constexpr double MaxAttackSpeedReduction = 0.4;
	/** A blocked hit deals this fraction of its damage. (CombatSystem.ts:383) */
	inline constexpr double BlockDamageMultiplier = 0.5;

	// ── XP & leveling (stats.ts:191-193) ────────────────────────────────
	inline constexpr double BaseXpPerLevel = 100.0;
	inline constexpr double XpScaling = 2.0;
	inline constexpr int32 MaxLevel = 25;
	/** computeDerivedStats clamps the level into [1, 20]. (stats.ts:29) */
	inline constexpr int32 MinStatLevel = 1;
	inline constexpr int32 MaxStatLevel = 20;

	// ── Derived resource pools (stats.ts:50-54) ─────────────────────────
	/** Non-casters: maxEnergy = this + stamina * EnergyPerStamina. */
	inline constexpr double BaseEnergy = 100.0;
	inline constexpr double EnergyPerStamina = 2.0;
	/** Non-casters: energyRegenRate = stamina * this. */
	inline constexpr double EnergyRegenPerStamina = 0.25;
	/** Casters: manaRegenRate = this + intelligence * ManaRegenPerIntelligence. */
	inline constexpr double BaseManaRegen = 1.25;
	inline constexpr double ManaRegenPerIntelligence = 0.1;

	// ── Inventory / loot (items.ts:14-20) ───────────────────────────────
	inline constexpr int32 InventoryMaxSlots = 32;
	inline constexpr double LootBagMergeRange = 80.0;
	inline constexpr double LootBagPickupRange = 200.0;
	inline constexpr double LootBagDespawnMs = 300000.0;
	inline constexpr int32 LootBagMaxSlots = 18;

	// ── Persistence & accounts (constants.ts:62-67) ─────────────────────
	/** GameRoom's auto-save cadence, ms. 2.0 spells it as a per-player timer. */
	inline constexpr double SaveIntervalMs = 30000.0;
	/** AuthService.ts validation, mirrored client-side so the UI can say why. */
	inline constexpr int32 MinUsernameLength = 3;
	inline constexpr int32 MaxUsernameLength = 20;
	inline constexpr int32 MinPasswordLength = 6;
	/** CharacterService.ts:138 — the backend rejects the fifth. */
	inline constexpr int32 MaxCharactersPerUser = 4;
	/** CharacterService.ts:129 — character name bounds. */
	inline constexpr int32 MinCharacterNameLength = 2;
	inline constexpr int32 MaxCharacterNameLength = 20;

	// ── Phase 1a defaults (no 1.0 equivalent) ───────────────────────────
	/** Default FValhallaClassTemplate::VisionRange when classes.json omits it. */
	inline constexpr double DefaultVisionRange = 1200.0;

	// ── Controls rework: facing (2026-09-22, no 1.0 equivalent) ─────────
	/**
	 * Half-angle of the "facing your target" cone, degrees either side of the
	 * actor's forward, 2D. A player's auto-attack swing and targeted casts need
	 * the target inside it (UValhallaCombatLibrary::IsFacing). 60 is a 120
	 * degree front arc: generous enough that nobody has to aim with the mouse,
	 * tight enough that a target at your side or behind you is not "in front".
	 */
	inline constexpr double FacingHalfAngleDegrees = 60.0;

	/** Seconds between repeats of the auto-attack "not facing" message, per attacker. */
	inline constexpr double NotFacingMessageIntervalSeconds = 2.0;
}
