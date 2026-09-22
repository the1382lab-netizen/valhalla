// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8a: which Niagara system a skill produces, what colour it is, and where
// it is attached. The same shape as ValhallaVisuals — everything that turns a
// *data* id into *content* lives in one place — and for the same reason: three
// callers (the combat-event hook, the projectile, and the test) would otherwise
// each spell out their own version of the mapping and two of them would rot.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"
#include "ValhallaGameTypes.h"
#include "ValhallaVisuals.h"
#include "ValhallaVfxLibrary.generated.h"

class AActor;
class UNiagaraComponent;
class UNiagaraSystem;
struct FValhallaSkillTemplate;

/**
 * One line per spawned effect: `<skill> -> <system> color=(...)`. This is what
 * the Phase 8a gate is read off, so it is a category of its own rather than a
 * verbosity level on LogValhallaGame.
 */
VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaVfx, Log, All);

/** The eight systems under `/Game/Valhalla/VFX`, plus "nothing". */
UENUM()
enum class EValhallaVfx : uint8
{
	/** No effect. Only ever the answer for an event that is not a skill. */
	None,
	/** `NS_Bolt` — a travelling core sprite with a ribbon trail. */
	Bolt,
	/** `NS_Impact` — a radial burst and a flat shockwave ring. */
	Impact,
	/** `NS_Slash` — a 120 degree arc lying on the ground, 0.25 s. */
	Slash,
	/** `NS_Heal` — sparkles rising out of a disc at the feet. */
	Heal,
	/** `NS_BuffAura` — a looping ground ring and motes. Lives as long as the buff. */
	BuffAura,
	/** `NS_Debuff` — motes dripping out of a cloud above the target. */
	Debuff,
	/** `NS_AoERing` — a ground ring expanding to `Radius`. */
	AoERing,
	/** `NS_Cone` — a fan thrown along the component's +X. */
	Cone,
};

/** Where a system is put when it is spawned. */
UENUM()
enum class EValhallaVfxAttach : uint8
{
	/** `socket_weapon_r` on the caster's body — a cast leaving the hand. */
	WeaponHand,
	/** The actor's root, i.e. its feet. Auras and ground rings. */
	ActorFeet,
	/** A world location with no parent. Impacts and ground-targeted rings. */
	WorldLocation,
};

/**
 * Everything the runtime needs to put one effect on screen.
 *
 * A plain value, resolved by a pure function, so the whole mapping is testable
 * without a world, a pawn, or a loaded Niagara asset — which is what
 * `Valhalla.Game.Vfx.Mapping` does for all 41 skills.
 */
USTRUCT()
struct VALHALLAGAME_API FValhallaVfxPlan
{
	GENERATED_BODY()

	/** Which system. Never `None` for a real skill. */
	UPROPERTY()
	EValhallaVfx System = EValhallaVfx::None;

	/** The `Color` user parameter. */
	UPROPERTY()
	FLinearColor Color = FLinearColor::White;

	/** The `Radius` user parameter, centimetres. */
	UPROPERTY()
	float Radius = 100.f;

	/** Where it goes. */
	UPROPERTY()
	EValhallaVfxAttach Attach = EValhallaVfxAttach::WorldLocation;

	/**
	 * True only for `BuffAura`. A looping system is *held* — it is stopped by
	 * the matching buffRemoved rather than by running out — and everything
	 * else is spawned with bAutoDestroy and forgotten.
	 */
	UPROPERTY()
	bool bLooping = false;

	bool IsValid() const { return System != EValhallaVfx::None; }
};

/**
 * Pure look-ups from skill data to VFX. No state, no world, no side effects.
 */
UCLASS()
class VALHALLAGAME_API UValhallaVfxLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Where the Phase 8a systems live. */
	static const TCHAR* VfxRoot() { return TEXT("/Game/Valhalla/VFX"); }

	/** The asset path of one system, or an empty string for `None`. */
	static FString SystemPath(EValhallaVfx System);

	/** `NS_Bolt` … `NS_Cone`, for the log line. */
	static const TCHAR* SystemName(EValhallaVfx System);

	// ── The mapping ─────────────────────────────────────────────────────

	/**
	 * The whole rule set, in one function.
	 *
	 * Order matters and is the rule, not an implementation detail:
	 *
	 *   1. an auto-attack is the *weapon*, so the weapon's cycle decides — a
	 *      swing is a Slash and a bow or a staff is a Bolt;
	 *   2. a skill that flies (fireball, magic missile) is a Bolt, whatever
	 *      else it is, because the projectile is the thing you look at;
	 *   3. healing, buff and defensive are read off the *category*, because a
	 *      heal that happens to be an aoeSelf is still a heal;
	 *   4. cone and the two aoe target types are read off the *shape*, because
	 *      an area effect's job on screen is to show its area — which is why
	 *      `wizard_frost_nova` (a debuff, aoeSelf) is a ring and not a cloud;
	 *   5. what is left is a debuff on a single target, a utility cast on a
	 *      friendly, and otherwise a hit.
	 *
	 * @param ClassColorPacked  `classes.json` `classColors[classId]`, 0xRRGGBB.
	 *                          Used when the skill's scalingStat names no
	 *                          palette entry — stamina, in practice.
	 */
	static FValhallaVfxPlan ResolveForSkill(
		const FValhallaSkillTemplate& Skill,
		int32 ClassColorPacked,
		EValhallaAttackCycle WeaponCycle);

	/** The same, resolving the skill id and the caster's class through the world. */
	static FValhallaVfxPlan ResolveForSkillId(
		const UObject* WorldContext,
		FName SkillId,
		AActor* Caster);

	/**
	 * `scalingStat` -> palette, with the class colour as the fallback.
	 *
	 * strength is red-orange, dexterity green, intelligence blue-violet and
	 * wisdom gold; stamina names no palette entry, so a stamina skill is drawn
	 * in the colour of whoever cast it.
	 */
	static FLinearColor ColorForSkill(const FValhallaSkillTemplate& Skill, int32 ClassColorPacked);

	/** 0xRRGGBB as authored for sRGB, as a linear colour. */
	static FLinearColor FromPackedColor(int32 Packed);

	/** True for the two skills that are drawn as a bolt (fireball, magic missile, or a data `projectile`). */
	static bool IsProjectileSkill(const FValhallaSkillTemplate& Skill);

	/**
	 * Phase 8b: the subset of IsProjectileSkill whose hit is drawn by a real
	 * `AValhallaSpellProjectile` detonating (its spellImpact event), so the cast
	 * event must not draw a second impact on the target. Magic Missile is the
	 * one bolt that never becomes an actor, so its cast event keeps the impact.
	 */
	static bool SpawnsProjectileActor(const FValhallaSkillTemplate& Skill);

	/**
	 * The attack cycle whoever is holding the weapon would swing.
	 *
	 * Read off the actor's UValhallaAnimComponent, which has already resolved
	 * it from the equipped weapon's `weaponStyle` on every end for exactly the
	 * same question. Asking it again rather than re-deriving it is what keeps
	 * the swing animation and the swing effect from disagreeing about which
	 * one is playing.
	 */
	static EValhallaAttackCycle CycleFor(AActor* Actor);

	// ── Tuning ──────────────────────────────────────────────────────────

	/** The bolt radius, cm, for a spell with no projectile of its own. */
	static constexpr float DefaultBoltRadius = 15.f;

	/** A bow's arrow is thinner than a fireball. */
	static constexpr float ArrowBoltRadius = 7.f;

	/** What an AoE ring falls back to when the skill names no `aoeRadius`. */
	static constexpr float DefaultAoeRadius = 110.f;

	/** An aura's ground ring, cm. */
	static constexpr float AuraRadius = 85.f;

	/**
	 * Yaw applied to `NS_Slash` so its arc opens along the attacker's facing.
	 *
	 * The arc is a third of a circle starting at the shape's local +X, so
	 * rotating it back by half of that (0.34 turn / 2 = 61 degrees) centres it.
	 * Kept here rather than baked into the asset so it can be retuned without
	 * rebuilding a Niagara system.
	 */
	static constexpr float SlashArcYaw = -61.f;

	/** How long the muzzle-flash bolt of a non-travelling spell is held, seconds. */
	static constexpr float InstantBoltSeconds = 0.35f;

	/**
	 * How far above the feet a ground-attached effect sits, cm.
	 *
	 * The capsule's origin is its middle, so the feet are MeshZOffset below it
	 * — and that is *exactly* the floor plane. A flat ring drawn there z-fights
	 * with the tile it is standing on and disappears; a few centimetres of lift
	 * is the whole fix. Small enough that the ring still reads as lying on the
	 * ground rather than hovering over it.
	 */
	static constexpr float GroundLift = 6.f;
};

/**
 * The client-side half: spawns the components and owns the ones that have to be
 * stopped by hand.
 *
 * WHY A WORLD SUBSYSTEM
 *
 * Two of the three jobs here need somewhere to keep a pointer. A buff aura is
 * started by a `buffApplied` and stopped by the matching `buffRemoved`, which
 * are separate events arriving seconds apart, so something has to remember
 * which component belongs to which (actor, skill) pair. And a bolt fired by an
 * *instant* spell has no projectile actor to die with, so something has to stop
 * it after a third of a second. A world subsystem is the smallest thing that
 * has a lifetime matching the effects' — it dies with the world, and PIE gives
 * each client world its own.
 *
 * It refuses to exist on a dedicated server at all (see ShouldCreateSubsystem),
 * which is stronger than a guard at each call site: there is no code path that
 * can reach a Niagara spawn on the authority-only build, because there is no
 * object to reach it through.
 */
UCLASS()
class VALHALLAGAME_API UValhallaVfxSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	//~ Begin FTickableGameObject interface
	/**
	 * Retire the effects that have to be stopped by hand.
	 *
	 * A world timer was the obvious way to do this and it is the wrong one:
	 * timers are scaled by global time dilation, so a `slomo 0.2` used to look
	 * at an effect stretches its cleanup by the same factor, and a timer that
	 * outlives its component is a silent leak with nothing to grep for. A
	 * swept list on the subsystem's own tick is a handful of pointers and is
	 * exact.
	 */
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

	/**
	 * Turn one combat event into VFX, on the clients and the listen host.
	 *
	 * Called from AValhallaGameState::RecordCombatEvent, which is the one
	 * funnel every combat event passes through on every end — the same hook
	 * UValhallaAnimComponent::DispatchCombatEvent uses, for the same reason.
	 */
	static void DispatchCombatEvent(const UObject* WorldContext, const FValhallaCombatEvent& Event);

	/** The subsystem for a world, or null on a dedicated server. */
	static UValhallaVfxSubsystem* Find(const UObject* WorldContext);

	/**
	 * Spawn one plan. Returns the component, which the caller owns only if the
	 * plan was looping — everything else auto-destroys.
	 *
	 * @param Anchor   The actor a WeaponHand / ActorFeet plan attaches to.
	 * @param Location Where a WorldLocation plan goes.
	 */
	UNiagaraComponent* Spawn(const FValhallaVfxPlan& Plan, FName SkillId, AActor* Anchor, const FVector& Location);

	/** Start, or restart, the aura a buff put on an actor. */
	void StartAura(const FValhallaVfxPlan& Plan, FName SkillId, AActor* Target);

	/** Stop the aura a buff put on an actor. Silent when there is none. */
	void StopAura(FName SkillId, AActor* Target);

	/** The system asset for an enum value, loaded and cached. */
	UNiagaraSystem* GetSystem(EValhallaVfx System);

protected:
	/** The per-event routing. See the .cpp for the table. */
	void HandleCombatEvent(const FValhallaCombatEvent& Event);

	/** The swing an auto-attack outcome draws on the attacker and its target. */
	void HandleSwingOutcome(const FValhallaCombatEvent& Event);

	/** Spawn a system that loops forever, and stop it again after Seconds. */
	void SpawnTimed(const FValhallaVfxPlan& Plan, FName SkillId, AActor* Anchor, const FVector& Location, float Seconds);

private:
	/** One live looping effect, keyed by what started it. */
	struct FActiveAura
	{
		TWeakObjectPtr<AActor> Target;
		FName SkillId;
		TWeakObjectPtr<UNiagaraComponent> Component;
	};

	/** Live auras. Never more than a handful; a linear scan is the right shape. */
	TArray<FActiveAura> Auras;

	/** One looping effect that is being held open for a fixed wall-clock time. */
	struct FTimedEffect
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		float RemainingSeconds = 0.f;
	};

	/** Muzzle flashes and anything else a looping system is borrowed for. */
	TArray<FTimedEffect> Timed;

	/** Index is (uint8)EValhallaVfx. Null until first use, then cached. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraSystem>> Systems;

	/** So a missing asset is complained about once rather than once per cast. */
	TSet<uint8> ReportedMissing;
};
