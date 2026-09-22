// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/Object.h"
#include "ValhallaGameTypes.h"
#include "ValhallaSkillHandler.generated.h"

class AActor;
class AValhallaCharacter;

/**
 * What one skill does when it goes off.
 *
 * The port of `EffectHandler` (handlers/registry.ts:78). 1.0 registered a
 * function per skill id in a plain object and fell back to `defaultEffect`; 2.0
 * registers a UObject class per skill id and falls back to
 * UValhallaDefaultSkillHandler. The shape is identical and so is the reason for
 * it: 41 skills is too many to write 41 handlers for, and almost all of them are
 * "roll the damage range, scale it, crit it, maybe leave a DoT" — which is what
 * the default does, straight out of the skill template. A handler exists only
 * for the six skills that genuinely do something the data cannot describe.
 *
 * Handlers are stateless and are instanced once per registry. Everything a cast
 * needs is on the context; a handler that wants to remember something between
 * casts is a handler that has misunderstood where state lives.
 */
UCLASS(Abstract)
class VALHALLAGAME_API UValhallaSkillHandler : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Run the effect. Server only — the registry is not consulted on a client.
	 * Anything that changes HP must go through UValhallaCombatLibrary.
	 */
	virtual void Execute(const FValhallaSkillContext& Context) PURE_VIRTUAL(UValhallaSkillHandler::Execute, );

	/**
	 * Undo whatever the buff this handler applied was doing, when it expires.
	 * The port of `BuffCleanupHandler` (handlers/registry.ts:93) — only Shield of
	 * Faith needs it, to clear the absorb it left behind.
	 */
	virtual void OnBuffExpired(AActor* Target, const FValhallaActiveBuff& Buff) {}

protected:
	// ── Shared helpers, the port of the code every 1.0 handler copied ────

	/** `min + Math.random() * (max - min)` over the template's baseDamage. */
	static double RollBaseDamage(const FValhallaSkillTemplate& Skill);

	/** The same, over baseHealing. */
	static double RollBaseHealing(const FValhallaSkillTemplate& Skill);

	/** The caster's value for the skill's `scalingStat`. 0 when it names nothing. */
	static double GetScalingStat(const AActor* Caster, const FValhallaSkillTemplate& Skill);

	/** The caster's crit chance and crit damage, defaulted like 1.0's `?? 0.05` / `?? 0.5`. */
	static void GetCritStats(const AActor* Caster, double& OutCritChance, double& OutCritDamage);

	/** Push a buff onto whichever kind of thing the target is, honouring its stacking mode. */
	static void ApplyBuffToTarget(AActor* Target, const FValhallaActiveBuff& Buff, const FValhallaSkillTemplate& Skill);

	/** Emit buffApplied so the client's buff bar sees it. */
	static void BroadcastBuffApplied(const FValhallaSkillContext& Context, AActor* Target, float DurationMs);
};

/**
 * SkillEffectHandler.ts:83 `defaultEffect` — what a skill does when nobody
 * wrote a handler for it.
 *
 * Reads baseDamage / baseHealing / dotDamagePerSec / hotHealPerSec /
 * buffDurationMs straight off the template and does the obvious thing with
 * each. Between them these four fields describe 35 of the 41 skills, which is
 * why adding a skill to skills.json usually needs no C++ at all.
 */
UCLASS()
class VALHALLAGAME_API UValhallaDefaultSkillHandler : public UValhallaSkillHandler
{
	GENERATED_BODY()

public:
	virtual void Execute(const FValhallaSkillContext& Context) override;

protected:
	/**
	 * SkillEffectHandler.ts:265 `getAffectedTargets` — who a skill's targetType
	 * says it hits. Single-target returns the target; aoeSelf and cone sweep the
	 * world around the caster; aoeGround returns nothing, because a ground skill
	 * hits things when its projectile detonates, not when it is cast.
	 */
	static void GatherAffectedTargets(const FValhallaSkillContext& Context, TArray<AActor*>& OutTargets);
};

/**
 * fireballHandler.ts — spawn a travelling projectile instead of dealing damage.
 *
 * The damage is rolled and scaled *now*, at cast time, and carried on the
 * projectile. That is 1.0's behaviour and it matters: a wizard who casts and
 * then dies still lands the fireball they paid for, and buffing intelligence
 * mid-flight does not retroactively strengthen a spell already in the air.
 */
UCLASS()
class VALHALLAGAME_API UValhallaFireballHandler : public UValhallaSkillHandler
{
	GENERATED_BODY()

public:
	virtual void Execute(const FValhallaSkillContext& Context) override;
};

/**
 * magicMissileHandler.ts — instant damage that never misses.
 *
 * Deliberately does not go through UValhallaCombatLibrary::ApplyDamage: that
 * function's first act is a hit roll, and the entire identity of this spell is
 * that there is no hit roll. It still crits, and it still respects the target's
 * HP and death handling, so it writes HP through the same NPC / PlayerState
 * paths ApplyDamage would have used.
 */
UCLASS()
class VALHALLAGAME_API UValhallaMagicMissileHandler : public UValhallaSkillHandler
{
	GENERATED_BODY()

public:
	virtual void Execute(const FValhallaSkillContext& Context) override;
};

/** tauntHandler.ts — 50 + 20 × level threat, and an immediate target switch. */
UCLASS()
class VALHALLAGAME_API UValhallaTauntHandler : public UValhallaSkillHandler
{
	GENERATED_BODY()

public:
	virtual void Execute(const FValhallaSkillContext& Context) override;

	/** tauntHandler.ts:19 — flat base threat. */
	static constexpr float BaseThreat = 50.f;

	/** tauntHandler.ts:20 — per caster level. Lv1 = 70, Lv10 = 250, Lv25 = 550. */
	static constexpr float ThreatPerLevel = 20.f;
};

/**
 * shieldOfFaithHandler.ts — an absorb shield of `30 + wisdom * 1.5`.
 *
 * The only handler with a cleanup: the shield lives on PlayerState::ShieldHp,
 * which nothing else would ever clear, so letting the buff expire without
 * OnBuffExpired would leave a permanent absorb behind.
 */
UCLASS()
class VALHALLAGAME_API UValhallaShieldOfFaithHandler : public UValhallaSkillHandler
{
	GENERATED_BODY()

public:
	virtual void Execute(const FValhallaSkillContext& Context) override;
	virtual void OnBuffExpired(AActor* Target, const FValhallaActiveBuff& Buff) override;

	/** shieldOfFaithHandler.ts:35 — flat absorb before wisdom. */
	static constexpr double BaseShield = 30.0;

	/** shieldOfFaithHandler.ts:35 — absorb per point of the *caster's* wisdom. */
	static constexpr double ShieldPerWisdom = 1.5;

	/** The fallback when the template omits buffDurationMs. */
	static constexpr float DefaultDurationMs = 15000.f;
};

/**
 * backstabHandler.ts — +25% crit and 1.5× damage from the target's rear arc.
 *
 * "Behind" is measured against the *target's* facing, not the attacker's: the
 * angle from the target to the attacker has to fall within ±90° of the
 * direction opposite the target's aim. A rogue cannot earn the bonus by turning
 * around; they have to actually get behind the thing.
 */
UCLASS()
class VALHALLAGAME_API UValhallaBackstabHandler : public UValhallaSkillHandler
{
	GENERATED_BODY()

public:
	virtual void Execute(const FValhallaSkillContext& Context) override;

	/** True when Caster stands in Target's rear hemisphere. */
	static bool IsAttackingFromBehind(const AActor* Caster, const AActor* Target);

	/** backstabHandler.ts:26 — ±90°, i.e. the whole back half. */
	static constexpr float BehindHalfArcDegrees = 90.f;

	/** backstabHandler.ts:28 — flat crit chance added when behind. */
	static constexpr double BehindCritBonus = 0.25;

	/** backstabHandler.ts:30 — damage multiplier when behind. */
	static constexpr double BehindDamageMultiplier = 1.5;
};

/** poisonBladeHandler.ts — a dexterity-scaled DoT, on players and NPCs alike. */
UCLASS()
class VALHALLAGAME_API UValhallaPoisonBladeHandler : public UValhallaSkillHandler
{
	GENERATED_BODY()

public:
	virtual void Execute(const FValhallaSkillContext& Context) override;

	/** poisonBladeHandler.ts:27 — the handler's own duration, not the template's. */
	static constexpr float PoisonDurationMs = 6000.f;

	/** poisonBladeHandler.ts:29 — base damage per second before scaling. */
	static constexpr double BaseDotDps = 4.0;

	/** poisonBladeHandler.ts:31 — added DPS per point of dexterity. */
	static constexpr double DexScale = 0.1;
};

/**
 * Skill id -> handler, for the whole game instance.
 *
 * The port of the EFFECT_HANDLERS / BUFF_CLEANUP_HANDLERS objects
 * (handlers/registry.ts:97). A GameInstanceSubsystem rather than a static map
 * because the handlers are UObjects and need an owner the garbage collector
 * knows about, and because a PIE session should not inherit a registry that a
 * previous one mutated.
 */
UCLASS()
class VALHALLAGAME_API UValhallaSkillHandlerRegistry : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/**
	 * Run the skill on the registry's handler for it, or on the default handler.
	 * The port of `executeSkillEffect` (SkillEffectHandler.ts:66).
	 */
	void Execute(const FValhallaSkillContext& Context);

	/** Run the buff cleanup for a skill, if it registered one. */
	void RunBuffCleanup(AActor* Target, const FValhallaActiveBuff& Buff);

	/** The handler for a skill id, or null. Blueprints and tests use this to introspect. */
	UFUNCTION(BlueprintPure, Category = "Valhalla|Skills")
	UValhallaSkillHandler* FindHandler(FName SkillId) const;

	/** Register a handler class for a skill id. Later registrations win. */
	void RegisterHandler(FName SkillId, TSubclassOf<UValhallaSkillHandler> HandlerClass);

private:
	/** Instantiate the six ports. The 2.0 equivalent of handlers/index.ts. */
	void RegisterBuiltInHandlers();

	/** Skill id -> its handler instance. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UValhallaSkillHandler>> Handlers;

	/** Used whenever Handlers has no entry. Never null after Initialize. */
	UPROPERTY(Transient)
	TObjectPtr<UValhallaSkillHandler> DefaultHandler;
};
