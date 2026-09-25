// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaSpellProjectile.h"

#include "ValhallaAssetPreload.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CoreMisc.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaVfxLibrary.h"
#include "ValhallaVisibilitySubsystem.h"

namespace
{
	/** The engine's unit sphere. Scaled to the projectile's radius at spawn. */
	const TCHAR* SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	/** The Phase 2a class-proxy tint material, reused for the projectile's colour. */
	const TCHAR* ProxyMaterialPath = TEXT("/Game/Valhalla/Materials/M_ClassProxy");

	/** The vector parameter on M_ClassProxy. */
	const FName ClassColorParameter(TEXT("ClassColor"));

	/** The engine sphere is 100 cm across, so its radius is 50. */
	constexpr float SphereSourceRadius = 50.f;

	/**
	 * How much of the projectile's radius the collision sphere still draws.
	 *
	 * Phase 8a keeps the sphere but shrinks it to a bright core inside the
	 * bolt, so the actor is never invisible if the Niagara system fails to
	 * load and the bolt has something to be wrapped around.
	 */
	constexpr float CoreSphereFraction = 0.28f;
}

AValhallaSpellProjectile::AValhallaSpellProjectile()
{
	// Moved by AValhallaGameState's fixed step, not by an actor tick — see the
	// class comment. The actor tick stays off entirely.
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	// Phase 2b made a fireball always relevant, on the grounds that it crosses
	// the zone in under two seconds and is worth seeing. Phase 5 takes that
	// back: a projectile is a *position*, and a projectile streaking out of a
	// building the player cannot see into tells them exactly where somebody is
	// standing. It is now culled like everything else, which has one visible
	// consequence — see IsNetRelevantFor.
	bAlwaysRelevant = false;
	SetNetCullDistanceSquared(1.0e12f);
	SetNetUpdateFrequency(30.f);

	// The projectile never lives long enough to be worth cleaning up by hand if
	// something goes wrong with its detonation tests.
	InitialLifeSpan = 10.f;

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	SetRootComponent(ProjectileMesh);
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMesh->SetCollisionProfileName(TEXT("NoCollision"));
	ProjectileMesh->SetGenerateOverlapEvents(false);
	ProjectileMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(SphereMeshPath);
	if (SphereAsset.Succeeded())
	{
		ProjectileMesh->SetStaticMesh(SphereAsset.Object);
	}

	// The bolt. Created in the constructor rather than spawned at BeginPlay so
	// it is a real subobject of the actor and follows the replicated transform
	// without anything having to keep the two in step; it is simply never
	// activated on a server (see BeginPlay).
	BoltEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("BoltEffect"));
	BoltEffect->SetupAttachment(ProjectileMesh);
	BoltEffect->SetAutoActivate(false);
	BoltEffect->SetAutoDestroy(false);
	BoltEffect->SetCastShadow(false);
}

bool AValhallaSpellProjectile::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& /*SrcLocation*/) const
{
	// A projectile that flies out of line of sight mid-flight stops being
	// replicated where it is, and the client keeps drawing it at its last known
	// position until RelevantTimeout closes the channel half a second later.
	// That is the honest trade and it is the right way round: the alternative
	// is telling a client where a fireball is while it is behind a wall, which
	// is the information Phase 5 exists to withhold. The detonation is a
	// multicast combat event, so the *effect* is never lost — only the last few
	// centimetres of travel, out of sight, are.
	return UValhallaVisibilitySubsystem::IsRelevantForViewer(this, RealViewer, ViewTarget);
}

void AValhallaSpellProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValhallaSpellProjectile, TargetLocation);
	DOREPLIFETIME(AValhallaSpellProjectile, Speed);
	DOREPLIFETIME(AValhallaSpellProjectile, SkillId);

	// Damage, AoeRadius and the caster's crit stats are deliberately absent. A
	// client that knew them could show a damage number before the server had
	// decided one, which is the beginning of not needing the server at all.
}

void AValhallaSpellProjectile::BeginPlay()
{
	Super::BeginPlay();
	ApplyProjectileAppearance();
}

void AValhallaSpellProjectile::OnRep_SkillId()
{
	ApplyProjectileAppearance();
}

void AValhallaSpellProjectile::ApplyProjectileAppearance()
{
	if (!ProjectileMesh || !ProjectileMesh->GetStaticMesh())
	{
		return;
	}

	const float Radius = ProjectileRadius > 0.0 ? static_cast<float>(ProjectileRadius) : static_cast<float>(Valhalla::FireballProjectileRadius);
	ProjectileMesh->SetRelativeScale3D(FVector(Radius * CoreSphereFraction / SphereSourceRadius));

	ApplyBoltEffect(Radius);

	if (!ProjectileMaterial)
	{
		UMaterialInterface* Source = LoadObject<UMaterialInterface>(nullptr, ProxyMaterialPath);
		if (!Source)
		{
			Source = ProjectileMesh->GetMaterial(0);
		}
		if (Source)
		{
			ProjectileMaterial = UMaterialInstanceDynamic::Create(Source, this);
			ProjectileMesh->SetMaterial(0, ProjectileMaterial);
		}
	}

	if (!ProjectileMaterial)
	{
		return;
	}

	// skills.json `iconColor` is the colour the 1.0 UI drew the skill's icon in,
	// so a fireball's projectile is the same orange as its button. Phase 8's
	// Niagara systems replace this sphere and can keep reading the same field.
	FLinearColor Colour(1.f, 0.45f, 0.1f);
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UValhallaDataSubsystem* Data = GameInstance->GetSubsystem<UValhallaDataSubsystem>())
		{
			if (const FValhallaSkillTemplate* Skill = Data->FindSkill(SkillId))
			{
				const FColor Srgb(
					static_cast<uint8>((Skill->IconColor >> 16) & 0xFF),
					static_cast<uint8>((Skill->IconColor >> 8) & 0xFF),
					static_cast<uint8>(Skill->IconColor & 0xFF));
				Colour = FLinearColor::FromSRGBColor(Srgb);
			}
		}
	}

	// Over-bright so it reads as emissive without a bespoke material.
	ProjectileMaterial->SetVectorParameterValue(ClassColorParameter, Colour * 4.f);
}

void AValhallaSpellProjectile::ApplyBoltEffect(float Radius)
{
	if (!BoltEffect)
	{
		return;
	}

	// A dedicated server has nobody to draw for, and the components exist there
	// anyway because they are constructor subobjects. Never activating is the
	// whole of the guard: an inactive UNiagaraComponent allocates no emitter
	// instances and costs nothing per tick.
	const UWorld* World = GetWorld();
	if (IsRunningDedicatedServer() || (World && World->GetNetMode() == NM_DedicatedServer))
	{
		return;
	}

	if (!BoltEffect->GetAsset())
	{
		const FString Path = UValhallaVfxLibrary::SystemPath(EValhallaVfx::Bolt);
		UNiagaraSystem* System = ValhallaAssets::Load<UNiagaraSystem>(Path, TEXT("projectile effect"));
		if (!System)
		{
			// The sphere is still there, so a missing asset costs the trail and
			// not the projectile.
			UE_LOG(LogValhallaVfx, Warning, TEXT("missing Niagara system %s"), *Path);
			return;
		}
		BoltEffect->SetAsset(System);
	}

	// The skill's palette, by the same rule every other effect uses, so a
	// fireball in flight is the colour its impact will be.
	FValhallaVfxPlan Plan = UValhallaVfxLibrary::ResolveForSkillId(this, SkillId, GetOwner());
	Plan.Radius = Radius;

	BoltEffect->SetVariableLinearColor(TEXT("Color"), Plan.Color);
	BoltEffect->SetVariableFloat(TEXT("Radius"), Radius);

	if (!BoltEffect->IsActive())
	{
		BoltEffect->Activate(/*bReset=*/true);
		UE_LOG(LogValhallaVfx, Log, TEXT("%s -> %s color=(%.2f,%.2f,%.2f) radius=%.0f projectile"),
			*SkillId.ToString(), UValhallaVfxLibrary::SystemName(EValhallaVfx::Bolt),
			Plan.Color.R, Plan.Color.G, Plan.Color.B, Radius);
	}
}

void AValhallaSpellProjectile::InitializeProjectile(
	AActor* InOwner,
	FName InSkillId,
	const FVector& InTargetLocation,
	double InSpeed,
	double InRadius,
	double InDamage,
	double InAoeRadius)
{
	if (!HasAuthority())
	{
		return;
	}

	CasterActor = InOwner;
	SetOwner(InOwner);

	SkillId = InSkillId;
	TargetLocation = InTargetLocation;
	Speed = static_cast<float>(InSpeed);
	ProjectileRadius = InRadius;
	Damage = InDamage;
	AoeRadius = InAoeRadius;
	DistanceTravelled = 0.0;

	ApplyProjectileAppearance();
}

void AValhallaSpellProjectile::SetHomingTarget(AActor* InTarget)
{
	if (HasAuthority())
	{
		HomingTarget = InTarget;
	}
}

void AValhallaSpellProjectile::ServerFixedTick(float FixedDeltaSeconds, double Now)
{
	if (!HasAuthority() || bDetonated || Speed <= 0.f)
	{
		return;
	}

	const FVector Current = GetActorLocation();

	if (HomingTarget.IsValid())
	{
		AActor* Homing = HomingTarget.Get();
		if (UValhallaCombatLibrary::IsAliveTarget(Homing))
		{
			FVector Followed = Homing->GetActorLocation();
			Followed.Z = Current.Z;
			TargetLocation = Followed;
		}
		else
		{
			// Died in flight: land where they fell.
			HomingTarget.Reset();
		}
	}
	FVector ToTarget = TargetLocation - Current;
	ToTarget.Z = 0.0;

	const double DistanceToTarget = ToTarget.Size2D();
	const double StepDistance = static_cast<double>(Speed) * FixedDeltaSeconds;

	// SpellProjectileSystem.ts:77 — reaching the target is the *first* test, so a
	// projectile that would overshoot lands where it was aimed rather than
	// sailing past it. This is what makes a fireball aimed at a point land at
	// that point rather than a step beyond it.
	if (DistanceToTarget <= StepDistance)
	{
		SetActorLocation(TargetLocation);
		Detonate(TargetLocation, Now);
		return;
	}

	const FVector NextLocation = Current + ToTarget.GetSafeNormal() * StepDistance;
	SetActorLocation(NextLocation);
	DistanceTravelled += StepDistance;

	// SpellProjectileSystem.ts:95 — the max-range safety net.
	if (DistanceTravelled >= MaxRange)
	{
		Detonate(NextLocation, Now);
		return;
	}

	// SpellProjectileSystem.ts:110 — first hostile entity contact detonates it.
	// The caster is skipped, so a wizard casting at their own feet is not
	// stopped by their own capsule on the very first step.
	const double HitDistance = Valhalla::PlayerCollisionRadius + ProjectileRadius;
	const double HitDistanceSq = HitDistance * HitDistance;

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			if (Candidate == CasterActor.Get() || !UValhallaCombatLibrary::IsAliveTarget(Candidate))
			{
				continue;
			}
			if (!UValhallaCombatLibrary::AreHostile(CasterActor.Get(), Candidate))
			{
				continue;
			}

			FVector ToEntity = Candidate->GetActorLocation() - NextLocation;
			ToEntity.Z = 0.0;
			if (ToEntity.SizeSquared2D() < HitDistanceSq)
			{
				// A direct hit on the enemy it was aimed at bursts on them, not a
				// capsule-width short, so the one it was cast at takes the full blow.
				FVector At = NextLocation;
				if (Candidate == HomingTarget.Get())
				{
					At = Candidate->GetActorLocation();
					At.Z = NextLocation.Z;
					SetActorLocation(At);
				}
				Detonate(At, Now);
				return;
			}
		}
	}
}

void AValhallaSpellProjectile::Detonate(const FVector& DetonationLocation, double Now)
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;

	UWorld* World = GetWorld();
	AActor* Caster = CasterActor.Get();

	UE_LOG(LogValhallaCombat, Log, TEXT("spellImpact skill=%s at %s radius=%.0f baseDamage=%.1f"),
		*SkillId.ToString(), *DetonationLocation.ToCompactString(), AoeRadius, Damage);

	if (World)
	{
		// SpellProjectileSystem.ts:174 — everything hostile inside the radius,
		// each scaled by its own distance from the centre.
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			if (Candidate == Caster || !UValhallaCombatLibrary::IsAliveTarget(Candidate))
			{
				continue;
			}
			if (!UValhallaCombatLibrary::AreHostile(Caster, Candidate))
			{
				continue;
			}

			FVector ToTarget = Candidate->GetActorLocation() - DetonationLocation;
			ToTarget.Z = 0.0;
			const double Distance = ToTarget.Size2D();

			if (Distance > AoeRadius)
			{
				continue;
			}

			const float Falloff = UValhallaCombatLibrary::ComputeFalloff(static_cast<float>(Distance), static_cast<float>(AoeRadius));
			const double ScaledDamage = Damage * Falloff;

			UE_LOG(LogValhallaCombat, Log, TEXT("aoe %s dist=%.0f/%.0f falloff=%.3f damage=%.1f"),
				*UValhallaCombatLibrary::GetDisplayName(Candidate), Distance, AoeRadius, Falloff, ScaledDamage);

			// A fireball is magical, so mitigation goes through spell resist.
			UValhallaCombatLibrary::ApplyDamage(Caster, Candidate, ScaledDamage, /*bMagical=*/true, SkillId);
		}
	}

	// SpellProjectileSystem.ts:240 — the VFX event goes out whether or not the
	// blast found anything, because the explosion happened either way.
	FValhallaCombatEvent Impact;
	Impact.Kind = EValhallaCombatEventKind::SpellImpact;
	Impact.Instigator = Caster;
	Impact.SkillId = SkillId;
	Impact.Amount = static_cast<float>(AoeRadius);
	Impact.Location = DetonationLocation;
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Impact);

	Destroy();
}
