// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ValhallaSpellProjectile.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class USphereComponent;

/**
 * One spell projectile in flight. The port of `SpellProjectileState` +
 * `SpellProjectileSystem`.
 *
 * The damage it will do was decided when it was cast and rides along on it —
 * see UValhallaFireballHandler. Everything the client needs to draw it (where
 * it is, where it is going, how fast) replicates; everything that would let a
 * client predict the blast (the damage, the caster's crit stats) does not.
 *
 * Movement is written by the server on the fixed tick rather than by a
 * ProjectileMovementComponent, because the detonation test has to happen
 * between two fixed steps at exactly the rate 1.0 ran it at. A PMC ticking on
 * the render frame would make a 350 cm/s fireball overshoot a 65 cm blast
 * radius on a slow frame and sail straight past its target.
 */
UCLASS()
class VALHALLAGAME_API AValhallaSpellProjectile : public AActor
{
	GENERATED_BODY()

public:
	AValhallaSpellProjectile();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/**
	 * Phase 5's anti-cheat boundary: a client is never sent a projectile it
	 * cannot see. See UValhallaVisibilitySubsystem::IsRelevantForViewer.
	 */
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	//~ End AActor interface

	// ── Replicated: what the client draws ───────────────────────────────

	/** SpellProjectileState.ts:98 `targetX/targetY` — where it is heading. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Projectile")
	FVector_NetQuantize TargetLocation = FVector::ZeroVector;

	/** SpellProjectileState.ts:100 `speed`, cm per second. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Valhalla|Projectile")
	float Speed = 0.f;

	/** SpellProjectileState.ts:93 `skillId` — picks the colour, and Phase 8's VFX. */
	UPROPERTY(ReplicatedUsing = OnRep_SkillId, BlueprintReadOnly, Category = "Valhalla|Projectile")
	FName SkillId;

	// ── Server-only: what it will do ────────────────────────────────────

	/**
	 * Stand a projectile up. Server only.
	 * @param InDamage Already rolled and stat-scaled by the casting handler.
	 */
	void InitializeProjectile(
		AActor* InOwner,
		FName InSkillId,
		const FVector& InTargetLocation,
		double InSpeed,
		double InRadius,
		double InDamage,
		double InAoeRadius);

	/**
	 * SpellProjectileSystem.ts:57 `update`, for one projectile, one fixed step.
	 * Moves, then tests — in that order, because 1.0 tested the position it had
	 * just moved to and a reordering changes which tick a hit lands on.
	 */
	void ServerFixedTick(float FixedDeltaSeconds, double Now);

protected:
	/** Tint the sphere for the skill once the id has replicated. */
	UFUNCTION()
	void OnRep_SkillId();

	/**
	 * SpellProjectileSystem.ts:161 `detonate` — damage everything hostile inside
	 * AoeRadius with distance falloff, emit spellImpact, and destroy.
	 */
	void Detonate(const FVector& DetonationLocation, double Now);

	/** Size and colour the bolt from the skill. */
	void ApplyProjectileAppearance();

	/** Point the Niagara component at `NS_Bolt` and set its user parameters. */
	void ApplyBoltEffect(float Radius);

	/**
	 * The root, and the projectile's extent in the world.
	 *
	 * Phase 8a replaced the *visible* body with `NS_Bolt`, but the sphere
	 * stays and stays visible at a tenth the radius. It is the root that
	 * everything else hangs off — the Niagara component, the replicated
	 * transform, the visibility culling's bounds — and, in a game whose fog of
	 * war can cull a projectile mid-flight, a mesh is also the only part of
	 * this actor that is guaranteed to be somewhere rather than nowhere. The
	 * bolt is what the player sees; the sphere is the core inside it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Projectile")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	/** `NS_Bolt`, tinted by the skill's palette. Client-side only; see BeginPlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Valhalla|Projectile")
	TObjectPtr<UNiagaraComponent> BoltEffect;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ProjectileMaterial;

	/**
	 * SpellProjectileSystem.ts:42 `SPELL_PROJECTILE_MAX_RANGE`.
	 * A projectile that somehow never reaches its target detonates here rather
	 * than flying forever and leaking an actor per cast.
	 */
	static constexpr double MaxRange = 1600.0;

private:
	/** Pre-rolled, stat-scaled damage at the centre of the blast. */
	double Damage = 0.0;

	/** `_aoeRadius` — the blast radius, cm. */
	double AoeRadius = 0.0;

	/** The projectile's own collision radius, cm, for the entity-hit test. */
	double ProjectileRadius = 10.0;

	/** `_distanceTravelled`, for the max-range fallback. */
	double DistanceTravelled = 0.0;

	/** Who cast it. They are immune to their own blast, exactly as in 1.0. */
	TWeakObjectPtr<AActor> CasterActor;

	/** Set the moment Detonate runs, so a double-detonation in one tick cannot happen. */
	bool bDetonated = false;
};
