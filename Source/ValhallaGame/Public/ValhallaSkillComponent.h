// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ValhallaGameTypes.h"
#include "ValhallaTypes.h"
#include "ValhallaSkillComponent.generated.h"

class AValhallaCharacter;
class AValhallaPlayerState;
class UValhallaDataSubsystem;
class UValhallaSkillHandlerRegistry;

/** How many action bar slots the 1.0 UI has. GameRoom's ACTION_BAR_SLOTS. */
inline constexpr int32 ValhallaActionBarSlots = 8;

/**
 * Everything a character can do with a skill. The port of `SkillSystem`.
 *
 * Lives on the character rather than the player state because it needs a world
 * position, a facing and a movement component — casting is range-checked, cast
 * bars are interrupted by walking, and auto-attacks need to know how far away
 * the target is. The *durable* state it works on (cooldowns, buffs, the stat
 * block) stays on AValhallaPlayerState, which survives a pawn respawn; the
 * component only owns what is true of this body right now.
 *
 * Authority model, unchanged from Phase 2a: the client asks, the server decides.
 * Every Server* function below is a validated request, never an instruction, and
 * the replicated cast state exists so a client can *draw* a cast bar, not so it
 * can know whether one will succeed.
 */
UCLASS(ClassGroup = (Valhalla), meta = (BlueprintSpawnableComponent))
class VALHALLAGAME_API UValhallaSkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UValhallaSkillComponent();

	//~ Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent interface

	// ── Replicated cast state (PlayerState.ts:56 `casting*`) ────────────

	/** The skill being cast, or None. Drives the client's cast bar. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Casting")
	FName CastingSkillId;

	/** Server time in seconds the cast began. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Casting")
	double CastStartedAt = 0.0;

	/** The cast's full length in ms, so the bar knows how far along it is. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Casting")
	float CastDurationMs = 0.f;

	// ── Replicated auto-attack state (PlayerState.ts:79) ────────────────

	/** True while the auto-attack loop is running. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|AutoAttack")
	bool bAutoAttacking = false;

	/** What the loop is swinging at. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|AutoAttack")
	TObjectPtr<AActor> AutoAttackTargetActor = nullptr;

	/** Which auto-attack skill is running — melee_attack or ranged_attack. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|AutoAttack")
	FName AutoAttackSkillId;

	// ── Replicated action bar (PlayerState.ts:107 `actionBar`) ──────────

	/**
	 * Eight skill ids, defaulted to the first eight of `classSkills[classId]`.
	 *
	 * Replicated rather than client-side because the server is what runs
	 * OnActionBarPressed's cast: if the two disagreed about what is in slot 3,
	 * the player would press a button and get a different spell.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|ActionBar")
	TArray<FName> ActionBar;

	// ── Server RPCs: what the client may ask for ────────────────────────

	/**
	 * SkillSystem.ts:176 `tryStartCast`. Validates, then either fires instantly
	 * or starts a timed cast. A failed validation sends the caster a skillFailed
	 * with the reason and changes nothing.
	 */
	UFUNCTION(Server, Reliable)
	void ServerCastSkill(FName SkillId, AActor* Target, FVector TargetLocation);

	/** SkillSystem.ts:227 `cancelCast` — drop an in-progress cast, no refund. */
	UFUNCTION(Server, Reliable)
	void ServerCancelCast();

	/**
	 * SkillSystem.ts:352 `startAutoAttack`, with whichever auto-attack the
	 * character's weapon implies (ResolveAutoAttackSkillId). Kept for the debug
	 * console command; the action bar uses ServerStartAutoAttackWith.
	 */
	UFUNCTION(Server, Reliable)
	void ServerStartAutoAttack(AActor* Target);

	/**
	 * Start the auto-attack loop with one specific auto-attack skill —
	 * `melee_attack` or `ranged_attack` — rather than letting the weapon decide.
	 * The skill must be an auto-attack the character's class may use, and
	 * `requiresWeapon` is enforced (a ranged attack needs a ranged weapon).
	 */
	UFUNCTION(Server, Reliable)
	void ServerStartAutoAttackWith(AActor* Target, FName SkillId);

	/** SkillSystem.ts:420 `stopAutoAttack`. */
	UFUNCTION(Server, Reliable)
	void ServerStopAutoAttack();

	/** GameRoom.ts:270 SET_ACTION_BAR, for one slot. Slot is 1-based. */
	UFUNCTION(Server, Reliable)
	void ServerSetActionBar(int32 Slot, FName SkillId);

	// ── Server tick ─────────────────────────────────────────────────────

	/**
	 * One fixed step. Runs GameRoom.update's skill work in its original order:
	 * cast progression and interrupts, then buff/DoT/HoT ticking, then the
	 * auto-attack swing. Called by AValhallaGameState, never by an actor tick.
	 */
	void ServerFixedTick(float FixedDeltaSeconds, double Now);

	// ── Queries, for the HUD ────────────────────────────────────────────

	/** 0..1 through the current cast, or 0 when not casting. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Casting")
	float GetCastProgress() const;

	/** Seconds until the skill in a 1-based slot is ready. 0 means now. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|ActionBar")
	float GetSlotCooldownRemaining(int32 Slot) const;

	/** The skill id in a 1-based slot, or None. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|ActionBar")
	FName GetSlotSkillId(int32 Slot) const;

	/** Fill ActionBar from classSkills. Server only; called once the class is known. */
	void InitializeActionBarFromClass();

	/**
	 * Cast whatever is in a 1-based slot at the given target and aim point.
	 * The body of AValhallaPlayerController::OnActionBarPressed.
	 */
	void CastFromActionBar(int32 Slot, AActor* Target, const FVector& AimPoint);

	/** The auto-attack skill this character would use: ranged_attack or melee_attack. */
	FName ResolveAutoAttackSkillId() const;

	/**
	 * Why this character cannot auto-attack with SkillId, or empty if it can.
	 * Checks: it is an auto-attack skill, the class may use it, and a weapon
	 * the skill requires is equipped (ranged: an `isRangedWeapon` weapon).
	 */
	FString GetAutoAttackBlocker(FName SkillId) const;

	/** The current swing interval in ms, after ComputeAutoAttackSpeed. For the report and the HUD. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|AutoAttack")
	float GetAutoAttackIntervalMs() const;

protected:
	/**
	 * SkillSystem.ts:684 `validateCast`.
	 * @return An empty string when the cast may proceed, otherwise the 1.0 reason text.
	 */
	FString ValidateCast(const FValhallaSkillTemplate& Skill, AActor* Target, const FVector& TargetLocation, double Now) const;

	/** SkillSystem.ts:832 — begin a cast with a cast time. */
	void StartTimedCast(const FValhallaSkillTemplate& Skill, AActor* Target, const FVector& TargetLocation, double Now);

	/** SkillSystem.ts:863 `completeCast` — spend the resource, start the cooldown, run the handler. */
	void CompleteCast(const FValhallaSkillTemplate& Skill, AActor* Target, const FVector& TargetLocation, double Now);

	/** SkillSystem.ts:909 `deductResource`. */
	void DeductResource(const FValhallaSkillTemplate& Skill);

	/** SkillSystem.ts:917 `clearCastingState`. */
	void ClearCastingState();

	/** SkillSystem.ts:923 `tickBuffs` — expire, run cleanups, tick DoTs and HoTs. */
	void TickBuffs(double Now);

	/** SkillSystem.ts:434 `updateAutoAttacks`, for this one character. */
	void TickAutoAttack(double Now);

	/**
	 * The cast-interrupt rule. Accumulates time while the owner is being told to
	 * move and returns true once that passes MovementInterruptSeconds.
	 */
	bool AccumulateMovementInterrupt(float FixedDeltaSeconds);

	/** Tell the owning client why their cast did not happen. */
	void SendSkillFailed(const FString& Reason) const;

	/** Shared body of both start RPCs. SkillId must already be validated. */
	void StartAutoAttackInternal(AActor* Target, FName SkillId);

	/** Build the context a handler is run with. */
	FValhallaSkillContext MakeContext(const FValhallaSkillTemplate& Skill, AActor* Target, const FVector& TargetLocation, double Now) const;

	// ── Owner accessors ─────────────────────────────────────────────────

	AValhallaCharacter* GetValhallaOwner() const;
	AValhallaPlayerState* GetValhallaPlayerState() const;
	UValhallaDataSubsystem* GetData() const;
	UValhallaSkillHandlerRegistry* GetRegistry() const;

	// ── Tuning ──────────────────────────────────────────────────────────

	/**
	 * How much cumulative movement cancels a cast, in seconds.
	 *
	 * 1.0 interrupted on 2 pixels of displacement (SkillSystem.ts:274). That rule
	 * cannot be ported: CharacterMovement resolves penetration, slides along
	 * geometry and settles onto the floor every tick, so a character standing
	 * perfectly still routinely drifts more than 2 cm and every cast in the game
	 * would fizzle. The 2.0 rule reads *intent* instead of position — the server
	 * sees the acceleration the client's move produced — and requires 100 ms of
	 * it, so a single stray input frame does not cost a 2.5 s Fireball while
	 * actually walking still cancels within two frames of setting off.
	 */
	static constexpr float MovementInterruptSeconds = 0.1f;

	/** Below this much acceleration the owner counts as standing still, cm/s². */
	static constexpr float MovementInterruptAccelThreshold = 1.f;

	/** stats.ts:504 — the class fallback when no weapon names an attack speed. */
	static constexpr float FallbackMeleeAttackSpeedMs = 1800.f;

	/** stats.ts:501 — the same for ranged. */
	static constexpr float FallbackRangedAttackSpeedMs = 2000.f;

	/**
	 * Range of the magical auto-attack, cm.
	 *
	 * No 1.0 equivalent: 1.0 had exactly two auto-attacks, melee and the ranger's
	 * bow, and a wizard auto-attacking meant walking into melee. 2.0 gives the
	 * `isRangedMagic` classes (stats.ts:208 — wizard, cleric, shaman) a magical
	 * ranged basic instead, at Magic Missile's range, so a caster is not obliged
	 * to stand in the fire to contribute between cooldowns.
	 */
	static constexpr float MagicalAutoAttackRange = 450.f;

private:
	/** The target a timed cast was started against, re-validated when it completes. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> PendingCastTarget = nullptr;

	/** The aim point a timed cast was started with. Ground skills land here. */
	FVector PendingCastLocation = FVector::ZeroVector;

	/** Accumulated seconds of movement input during the current cast. */
	float MovementDuringCastSeconds = 0.f;

	/** Server time of the next allowed swing. PlayerState.ts:98 `nextAutoAttackAt`. */
	double NextAutoAttackAt = 0.0;
};
