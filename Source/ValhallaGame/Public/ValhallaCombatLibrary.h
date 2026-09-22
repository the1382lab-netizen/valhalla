// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaGameTypes.h"
#include "ValhallaTypes.h"
#include "ValhallaCombatLibrary.generated.h"

class AActor;
class AValhallaGameState;

/** Log for everything that takes HP off something. Its roll lines are the audit trail. */
VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaCombat, Log, All);

/**
 * One combatant's stats, whichever kind of thing it is.
 *
 * Players carry a full FValhallaResolvedStats on their PlayerState; NPCs carry a
 * (usually mostly empty) FValhallaStatBlock on their template. Both have to feed
 * the same damage pipeline, so both are flattened into this on the way in.
 */
struct VALHALLAGAME_API FValhallaCombatant
{
	/** True when the actor was something this library knows how to read. */
	bool bValid = false;

	/** True for an AValhallaCharacter, false for an AValhallaNPC. */
	bool bIsPlayer = false;

	/** True while it is up. Dead things neither deal nor take damage. */
	bool bAlive = false;

	double Hp = 0.0;
	double MaxHp = 0.0;

	double Strength = 0.0;
	double Dexterity = 0.0;
	double Intelligence = 0.0;
	double Wisdom = 0.0;

	double CritChance = 0.0;
	double CritDamage = 0.0;
	double DodgeRating = 0.0;
	double BlockRating = 0.0;
	double PhysicalDefense = 0.0;
	double SpellResist = 0.0;

	/** Remaining absorb shield. Only players ever have one. */
	double ShieldHp = 0.0;

	int32 Level = 1;

	/** For the log line and for the HUD. */
	FString DisplayName;
};

/**
 * Every server-side write to someone's HP goes through here.
 *
 * There is exactly one damage function in Phase 2b, on purpose. 1.0 had four
 * near-copies of the same pipeline — CombatSystem.applyStatDamage,
 * SkillSystem.executeAutoAttackOnTarget, SpellProjectileSystem.applyAoeDamage
 * and the ad-hoc subtraction inside every effect handler — and they had already
 * drifted: the auto-attack path skipped the `blocked` event, the AoE path
 * swallowed misses silently, and the handlers ignored defence entirely. Funnel
 * everything through ApplyDamage and a rule can only be got wrong once.
 *
 * The pure helpers below take no world and no actors. That is what makes the
 * automation tests possible: cooldown groups, buff stacking and AoE falloff are
 * the three rules most likely to be broken by a later edit, and all three can be
 * checked without standing a map up.
 */
UCLASS()
class VALHALLAGAME_API UValhallaCombatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ── The damage path ─────────────────────────────────────────────────

	/**
	 * Resolve and apply one hit. Server only; a no-op anywhere else.
	 *
	 * Draws the four rolls with FMath::FRand(), logs them, runs
	 * Valhalla::Stats::ResolveDamage, spends the target's absorb shield, applies
	 * the invulnerability window, writes the HP, and broadcasts the resulting
	 * 1.0 event (playerHit / npcHit / missed / dodged / blocked, then
	 * playerDied / npcDied). Death and the respawn timer are handled here too,
	 * because a caller that could forget to check is a caller that will.
	 *
	 * @param Attacker      AValhallaCharacter or AValhallaNPC. May be null for
	 *                      environmental damage; the rolls then use zeroed stats.
	 * @param Target        AValhallaCharacter or AValhallaNPC.
	 * @param RawDamage     Pre-mitigation damage. Already stat-scaled by the caller.
	 * @param bMagical      Routes mitigation through SpellResist instead of PhysicalDefense.
	 * @param SourceSkillId Named in the log line and carried on the event.
	 * @return The damage actually taken off HP. 0 on a miss, a dodge, or a no-op.
	 */
	static int32 ApplyDamage(AActor* Attacker, AActor* Target, double RawDamage, bool bMagical, FName SourceSkillId);

	/**
	 * Heal a target, clamped to its max HP. Server only.
	 * Returns the HP actually restored, which is 0 on a dead or full target.
	 */
	static int32 ApplyHealing(AActor* Healer, AActor* Target, double Amount, FName SourceSkillId);

	// ── Reading a combatant ─────────────────────────────────────────────

	/** Flatten whatever this actor is into the common stat view. */
	static FValhallaCombatant DescribeCombatant(const AActor* Actor);

	/** True when the two actors are on opposing sides. Players and NPCs are enemies; two players are not (Phase 2c adds PvP). */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Combat")
	static bool AreHostile(const AActor* A, const AActor* B);

	/** True for an AValhallaNPC. The port of `isNpcTarget` (handlers/registry.ts:22). */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Combat")
	static bool IsNpcTarget(const AActor* Actor);

	/** True when the actor exists and its bAlive is set. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Combat")
	static bool IsAliveTarget(const AActor* Actor);

	/** The display name for a log line or a nameplate. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Combat")
	static FString GetDisplayName(const AActor* Actor);

	// ── Facing (controls rework) ────────────────────────────────────────

	/**
	 * True when Target is inside Actor's front arc: the 2D angle between the
	 * actor's yaw and the direction to the target is at most HalfAngleDeg
	 * (inclusive). Server rule for a player's swings and targeted casts; the
	 * server never turns a player to make it true.
	 *
	 * A target on top of the actor (under 1 cm apart in 2D) counts as faced —
	 * there is no direction to be wrong about. Null actors are not facing.
	 */
	static bool IsFacing(const AActor* Actor, const AActor* Target,
		float HalfAngleDeg = static_cast<float>(Valhalla::FacingHalfAngleDegrees));

	/** The pure half of IsFacing: no actors, tested directly. */
	static bool IsFacingPoint(const FVector& ActorLocation, float ActorYawDegrees, const FVector& TargetLocation,
		float HalfAngleDeg = static_cast<float>(Valhalla::FacingHalfAngleDegrees));

	/** The skillFailed text for a swing or cast at a target the player is not facing. */
	static const TCHAR* NotFacingText() { return TEXT("You must be facing your target"); }

	/** The skillFailed reason code carried on the event (FValhallaCombatEvent::Reason). */
	static FName NotFacingReason() { return FName(TEXT("notFacing")); }

	/** utils.ts:10 `getStatValue` — read a named StatBlock field off a resolved block. */
	static double GetStatValue(const FValhallaResolvedStats& Stats, FName StatName);

	/** Send one event to every client, and to this server's own log. */
	static void BroadcastCombatEvent(const UObject* WorldContext, const FValhallaCombatEvent& Event);

	/** Build the event a hit produced, so callers do not each invent their own. */
	static FValhallaCombatEvent MakeHitEvent(AActor* Attacker, AActor* Target, const FValhallaDamageResult& Result, FName SkillId);

	// ── Pure rules: no world, no actors, tested directly ────────────────

	/**
	 * SpellProjectileSystem.ts:340 `computeFalloff`.
	 *
	 * Linear from 1.0 at the centre of the blast to
	 * Valhalla::FireballDamageFalloffMin (0.35) at its edge, and flat at the
	 * minimum beyond it. A radius of 0 or less means no falloff at all.
	 */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Combat")
	static float ComputeFalloff(float Distance, float Radius);

	/**
	 * SkillSystem.ts:889 `applyCooldown`.
	 *
	 * Writes the skill's own expiry, then — if it names a `cooldownGroup` — the
	 * same expiry for every other skill in ClassSkills sharing that group. This
	 * is why a warrior's Shield Bash and Cleave can be made to lock each other
	 * out purely from data, with no code knowing either skill exists.
	 *
	 * @param Cooldowns   Skill id -> server time in seconds when it comes off cooldown.
	 * @param Skill       The skill that was just cast.
	 * @param ClassSkills The caster's class skill list — the only ids a group can reach.
	 * @param FindSkill   Lookup for the other members of the group.
	 * @param Now         Server time in seconds.
	 */
	static void ApplyCooldown(
		TMap<FName, double>& Cooldowns,
		const FValhallaSkillTemplate& Skill,
		const TArray<FName>& ClassSkills,
		TFunctionRef<const FValhallaSkillTemplate*(FName)> FindSkill,
		double Now);

	/**
	 * SkillSystem.ts:716-735 — the cooldown half of `validateCast`.
	 *
	 * @return The seconds remaining, or 0 when the skill is ready. OutBlockingSkill
	 *         names which skill in the group is holding it, for the failure text.
	 */
	static double GetCooldownRemaining(
		const TMap<FName, double>& Cooldowns,
		const FValhallaSkillTemplate& Skill,
		const TArray<FName>& ClassSkills,
		TFunctionRef<const FValhallaSkillTemplate*(FName)> FindSkill,
		double Now,
		FName& OutBlockingSkill);

	/**
	 * SkillEffectHandler.ts:226 `applyBuff`.
	 *
	 * Buffs are identified by (skill, caster), so two rogues' poisons are two
	 * buffs. When one is re-applied by the same caster, StackingMode decides:
	 *
	 *   Replace  drop the old one and push the new one — the default
	 *   Stack    keep the old one, +1 stack up to MaxStacks, refresh the timings
	 *   Extend   add the new duration to whatever is left of the old one
	 *
	 * Extend is the one worth reading twice: it is remaining + new, not
	 * max(remaining, new), so spamming an extend buff really does bank duration.
	 */
	static void ApplyBuff(
		TArray<FValhallaActiveBuff>& Buffs,
		const FValhallaActiveBuff& NewBuff,
		EValhallaStackingMode StackingMode,
		int32 MaxStacks);

	/** Find a buff by (skill, caster). Null when it is not there. */
	static FValhallaActiveBuff* FindBuff(TArray<FValhallaActiveBuff>& Buffs, FName SkillId, const AActor* Caster);

	/** The replicated world clock, in seconds. 0 when there is no game state yet. */
	static double GetServerTime(const UObject* WorldContext);
};
