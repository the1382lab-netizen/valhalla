// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaNPCSpawner.h"

#include "Animation/AnimSequence.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaNPC.h"
#include "ValhallaPatrolRouteComponent.h"
#include "ValhallaVisuals.h"

#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#endif

namespace
{
	/** How far above the marker the floor trace starts, cm. Low, so a roof or a stall canopy over the spot is not mistaken for the floor. */
	constexpr double FloorTraceUp = 60.0;

	/** How far below the marker the floor trace looks, cm. */
	constexpr double FloorTraceDown = 1000.0;

	/** Clearance between the capsule's bottom and the floor at spawn, cm. */
	constexpr double SpawnClearance = 2.0;

	/** Five follower slots: chest, helm, legs, boots, gloves. */
	constexpr int32 PreviewPieceCount = 5;

	const UValhallaDataSubsystem* GetData(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	}

	/**
	 * How far a hand-placed patrol point may be from the nav mesh and still
	 * count as on it, cm. Generous vertically: the designer drops points on
	 * the floor and the nav mesh floats a little above it.
	 */
	const FVector RoutePointNavExtent(100.0, 100.0, 150.0);

	/** A follow chain longer than this is a loop (or absurd). */
	constexpr int32 MaxFollowChain = 16;

	/** A template id the running game (or, in the editor, the JSON on disk) knows. */
	bool IsKnownTemplate(const UObject* Context, FName Id)
	{
		if (Id.IsNone())
		{
			return false;
		}
		if (const UValhallaDataSubsystem* Data = GetData(Context))
		{
			return Data->FindNPCTemplate(Id) != nullptr;
		}
		FValhallaNPCTemplate Unused;
		return AValhallaNPC::ReadTemplateFromDisk(Id, Unused);
	}

	/** The world's nav system and the default agent's nav data, or nulls (L_GreyBox, tests). */
	const ANavigationData* FindNavData(const UWorld* World, UNavigationSystemV1*& OutNav)
	{
		OutNav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(const_cast<UWorld*>(World)) : nullptr;
		return OutNav ? OutNav->GetNavDataForProps(FNavAgentProperties::DefaultProperties) : nullptr;
	}
}

AValhallaNPCSpawner::AValhallaNPCSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	// Not replicated: the spawn point is a piece of level authoring, and the NPC
	// it makes replicates on its own.
	bReplicates = false;

	NPCClass = AValhallaNPC::StaticClass();

	Marker = CreateDefaultSubobject<UBillboardComponent>(TEXT("Marker"));
	SetRootComponent(Marker);
	Marker->SetHiddenInGame(true);

#if WITH_EDITORONLY_DATA
	FacingArrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	if (FacingArrow)
	{
		FacingArrow->SetupAttachment(Marker);
		FacingArrow->ArrowColor = FColor(255, 170, 40);
		FacingArrow->ArrowSize = 0.8f;
		FacingArrow->SetRelativeLocation(FVector(0.f, 0.f, 10.f));
	}

	RouteVisual = CreateEditorOnlyDefaultSubobject<UValhallaPatrolRouteComponent>(TEXT("RouteVisual"));
	if (RouteVisual)
	{
		RouteVisual->SetupAttachment(Marker);
	}

	PreviewBody = CreateEditorOnlyDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewBody"));
	if (PreviewBody)
	{
		PreviewBody->SetupAttachment(Marker);
		PreviewBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewBody->SetHiddenInGame(true);
		PreviewBody->bIsEditorOnly = true;
		PreviewBody->SetCastShadow(false);

		PreviewHead = CreateEditorOnlyDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewHead"));
		if (PreviewHead)
		{
			PreviewHead->SetupAttachment(PreviewBody);
			PreviewHead->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PreviewHead->SetHiddenInGame(true);
			PreviewHead->bIsEditorOnly = true;
			PreviewHead->SetCastShadow(false);
			PreviewHead->bUseAttachParentBound = true;
		}

		for (int32 Index = 0; Index < PreviewPieceCount; ++Index)
		{
			USkeletalMeshComponent* Piece = CreateEditorOnlyDefaultSubobject<USkeletalMeshComponent>(*FString::Printf(TEXT("PreviewPiece%d"), Index));
			if (!Piece)
			{
				continue;
			}
			Piece->SetupAttachment(PreviewBody);
			Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Piece->SetHiddenInGame(true);
			Piece->bIsEditorOnly = true;
			Piece->SetCastShadow(false);
			Piece->bUseAttachParentBound = true;
			PreviewPieces.Add(Piece);
		}
	}
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Queries
// ─────────────────────────────────────────────────────────────────────────────

TArray<FString> AValhallaNPCSpawner::GetNPCTemplateOptions() const
{
	TArray<FString> Options = AValhallaNPC::ReadTemplateIdsFromDisk();
	// Empty first: "use the NPC type's own template".
	Options.Insert(FString(), 0);
	return Options;
}

TArray<TWeakObjectPtr<AValhallaNPC>> AValhallaNPCSpawner::GetSpawnedNPCs() const
{
	TArray<TWeakObjectPtr<AValhallaNPC>> Out;
	if (SpawnedNPC.IsValid())
	{
		Out.Add(SpawnedNPC);
	}
	return Out;
}

FName AValhallaNPCSpawner::GetEffectiveTemplateId() const
{
	if (!TemplateId.IsNone())
	{
		return TemplateId;
	}
	const AValhallaNPC* TypeDefaults = NPCClass ? NPCClass->GetDefaultObject<AValhallaNPC>() : nullptr;
	return TypeDefaults ? TypeDefaults->DefaultTemplateId : NAME_None;
}

float AValhallaNPCSpawner::GetEffectiveRespawnSeconds() const
{
	if (RespawnSeconds > 0.f)
	{
		return RespawnSeconds;
	}

	FValhallaNPCTemplate Template;
	const UValhallaDataSubsystem* Data = GetData(this);
	if (const FValhallaNPCTemplate* Found = Data ? Data->FindNPCTemplate(GetEffectiveTemplateId()) : nullptr)
	{
		Template = *Found;
	}
	else if (!AValhallaNPC::ReadTemplateFromDisk(GetEffectiveTemplateId(), Template))
	{
		return 60.f;
	}

	// A template with no respawnMs still has to come back eventually.
	return Template.RespawnMs > 0.f ? Template.RespawnMs / 1000.f : 60.f;
}

float AValhallaNPCSpawner::GetSecondsUntilRespawn() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->GetTimerManager().IsTimerActive(RespawnTimer))
	{
		return 0.f;
	}
	return World->GetTimerManager().GetTimerRemaining(RespawnTimer);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Patrol, pairs, roaming and rare spawns (B-10 part 2)
// ─────────────────────────────────────────────────────────────────────────────

bool AValhallaNPCSpawner::IsFollowing() const
{
	return FollowSpawner != nullptr && FollowSpawner != this;
}

bool AValhallaNPCSpawner::HasPatrolRoute() const
{
	return !IsFollowing() && FValhallaNPCPatrolRules::HasRoute(PatrolMode, PatrolPoints.Num() + 1);
}

float AValhallaNPCSpawner::GetEffectiveWanderRadius() const
{
	return (IsFollowing() || HasPatrolRoute()) ? 0.f : FMath::Max(0.f, WanderRadius);
}

void AValhallaNPCSpawner::GetPatrolStopsWorld(TArray<FVector>& OutStops) const
{
	// MakeEditWidget points are in the actor's space, so the spawn point's
	// yaw turns the whole route with it, as a designer rotating it expects.
	const FTransform& Transform = GetActorTransform();
	OutStops.Reset(PatrolPoints.Num() + 1);
	OutStops.Add(GetActorLocation());
	for (const FVector& Local : PatrolPoints)
	{
		OutStops.Add(Transform.TransformPosition(Local));
	}
}

FValhallaNPCPatrolSetup AValhallaNPCSpawner::BuildPatrolSetup(const FVector& Home) const
{
	FValhallaNPCPatrolSetup Setup;
	Setup.PauseMinSeconds = PatrolPauseMinSeconds;
	Setup.PauseMaxSeconds = PatrolPauseMaxSeconds;
	Setup.SpeedFraction = FMath::Clamp(PatrolSpeedFraction, 0.1f, 1.f);
	Setup.WanderRadius = GetEffectiveWanderRadius();

	if (!HasPatrolRoute())
	{
		return Setup;
	}

	Setup.Mode = PatrolMode;
	GetPatrolStopsWorld(Setup.Stops);
	Setup.Stops[0] = Home;

	// The designer put the points on the floor; the nav mesh floats a little
	// above it and the path query wants a point on it. A point that does not
	// project stays as placed (CheckZones warns about it) and the NPC walks
	// straight at it, skipping it if it gets stuck.
	UNavigationSystemV1* Nav = nullptr;
	if (FindNavData(GetWorld(), Nav) && Nav)
	{
		for (int32 Index = 1; Index < Setup.Stops.Num(); ++Index)
		{
			FNavLocation Projected;
			if (Nav->ProjectPointToNavigation(Setup.Stops[Index], Projected, RoutePointNavExtent))
			{
				Setup.Stops[Index] = Projected.Location;
			}
		}
	}
	return Setup;
}

FName AValhallaNPCSpawner::GetEffectiveRareTemplateId() const
{
	if (!RareTemplateId.IsNone())
	{
		return RareTemplateId;
	}
	const AValhallaNPC* RareDefaults = RareNPCClass ? RareNPCClass->GetDefaultObject<AValhallaNPC>() : nullptr;
	return RareDefaults ? RareDefaults->DefaultTemplateId : NAME_None;
}

void AValhallaNPCSpawner::CollectPatrolProblems(TArray<FString>& OutProblems) const
{
	// ── Pairs ──
	if (FollowSpawner == this)
	{
		OutProblems.Add(TEXT("follows itself; Follow Spawn Point is ignored."));
	}
	else if (FollowSpawner)
	{
		if (!FollowSpawner->NPCClass)
		{
			OutProblems.Add(FString::Printf(TEXT("follows %s, which has no NPC Type, so there is nobody to follow."), *FollowSpawner->GetName()));
		}

		// A follows B follows A: both stand waiting for the other forever.
		const AValhallaNPCSpawner* Walk = FollowSpawner;
		for (int32 Hop = 0; Walk && Hop < MaxFollowChain; ++Hop)
		{
			if (Walk == this)
			{
				OutProblems.Add(TEXT("is part of a follow loop (A follows B follows A); nobody in it leads."));
				break;
			}
			Walk = Walk->IsFollowing() ? Walk->FollowSpawner.Get() : nullptr;
		}

		if (PatrolPoints.Num() > 0 && PatrolMode != EValhallaPatrolMode::None)
		{
			OutProblems.Add(TEXT("has Patrol Points and a Follow Spawn Point; the points are ignored while it follows."));
		}
	}

	// ── Route points on the nav mesh ──
	if (HasPatrolRoute())
	{
		UNavigationSystemV1* Nav = nullptr;
		if (FindNavData(GetWorld(), Nav) && Nav)
		{
			TArray<FVector> Stops;
			GetPatrolStopsWorld(Stops);
			for (int32 Index = 1; Index < Stops.Num(); ++Index)
			{
				FNavLocation Projected;
				if (!Nav->ProjectPointToNavigation(Stops[Index], Projected, RoutePointNavExtent))
				{
					OutProblems.Add(FString::Printf(TEXT("patrol point %d at %s is not on the nav mesh (nothing within 1 m); the NPC will skip it when it gets stuck."),
						Index, *Stops[Index].ToCompactString()));
				}
			}
		}
	}

	// ── Rare spawn ──
	if (RareChance > 0.f)
	{
		const FName Rare = GetEffectiveRareTemplateId();
		if (Rare.IsNone())
		{
			OutProblems.Add(FString::Printf(TEXT("has Rare Chance %.2f but no Rare Template (and no Rare NPC Type with a default template); it never spawns a rare."),
				RareChance));
		}
		else if (!IsKnownTemplate(this, Rare))
		{
			OutProblems.Add(FString::Printf(TEXT("names rare template '%s', which is not in npc-templates.json; rare rolls spawn the normal NPC."),
				*Rare.ToString()));
		}
	}
	else if (!RareTemplateId.IsNone() && !IsKnownTemplate(this, RareTemplateId))
	{
		OutProblems.Add(FString::Printf(TEXT("names rare template '%s', which is not in npc-templates.json."), *RareTemplateId.ToString()));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPCSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		SpawnNPC();
	}
}

void AValhallaNPCSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimer);
	}
	Super::EndPlay(EndPlayReason);
}

bool AValhallaNPCSpawner::TraceFloorZ(double& OutZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Feet = GetActorLocation();
	const FVector Start = Feet + FVector(0.0, 0.0, FloorTraceUp);
	const FVector End = Feet - FVector(0.0, 0.0, FloorTraceDown);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ValhallaSpawnFloor), /*bTraceComplex*/ false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);

	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params))
	{
		OutZ = Hit.ImpactPoint.Z;
		return true;
	}
	return false;
}

FVector AValhallaNPCSpawner::FindGroundedCapsuleCentre(float ScaledHalfHeight) const
{
	FVector Centre = GetActorLocation();
	double FloorZ = Centre.Z;
	if (!TraceFloorZ(FloorZ))
	{
		UE_LOG(LogValhallaGame, Warning, TEXT("%s: no floor found under %s; spawning at the marker's height."),
			*GetName(), *Centre.ToCompactString());
	}
	Centre.Z = FloorZ + ScaledHalfHeight + SpawnClearance;
	return Centre;
}

void AValhallaNPCSpawner::SpawnNPC()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}

	const UValhallaDataSubsystem* Data = GetData(this);
	if (!Data)
	{
		UE_LOG(LogValhallaGame, Error, TEXT("%s: no data subsystem; nothing spawned."), *GetName());
		return;
	}

	UClass* SpawnClass = NPCClass ? NPCClass.Get() : AValhallaNPC::StaticClass();
	FName EffectiveTemplateId = GetEffectiveTemplateId();
	const FValhallaNPCTemplate* Template = Data->FindNPCTemplate(EffectiveTemplateId);

	// ── Rare roll (B-10 part 2) ──────────────────────────────────────────
	// Once per spawn, logged either way so a designer can see the odds work.
	// A rare whose template is missing falls back to the normal spawn: a typo
	// should cost the rare, not the spawn point.
	bRareSpawned = false;
	if (RareChance > 0.f)
	{
		const FName RareId = GetEffectiveRareTemplateId();
		const float Roll = FMath::FRand();
		const bool bRare = FValhallaNPCPatrolRules::RollsRare(RareChance, Roll);
		const FValhallaNPCTemplate* RareTemplate = bRare ? Data->FindNPCTemplate(RareId) : nullptr;
		UE_LOG(LogValhallaGame, Log, TEXT("%s: rare roll %.3f against %.3f -> %s ('%s')."),
			*GetName(), Roll, RareChance, bRare ? (RareTemplate ? TEXT("RARE") : TEXT("rare, but its template is unknown; normal")) : TEXT("normal"),
			*RareId.ToString());
		if (RareTemplate)
		{
			bRareSpawned = true;
			Template = RareTemplate;
			EffectiveTemplateId = RareId;
			if (RareNPCClass)
			{
				SpawnClass = RareNPCClass.Get();
			}
		}
	}

	if (!Template)
	{
		// NPCSystem.ts:80 warned and skipped rather than failing. A designer's
		// typo should cost one empty spawn point, not the level.
		UE_LOG(LogValhallaGame, Warning, TEXT("%s: unknown NPC template '%s' (type %s); nothing spawned."),
			*GetName(), *EffectiveTemplateId.ToString(), *GetNameSafe(SpawnClass));
		return;
	}

	// Deferred, so the template (and with it the capsule's scale) is in place
	// before the NPC is put on the ground and before its BeginPlay runs.
	const FTransform Initial(FRotator(0.f, GetActorRotation().Yaw, 0.f), GetActorLocation());
	AValhallaNPC* Npc = World->SpawnActorDeferred<AValhallaNPC>(SpawnClass, Initial, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Npc)
	{
		UE_LOG(LogValhallaGame, Warning, TEXT("%s: NPC failed to spawn."), *GetName());
		return;
	}

	Npc->InitializeFromTemplate(*Template, GetActorLocation(), this);
	// The name override is this spawn point's usual NPC; a rare keeps its own
	// name ("Castellan Ordric Vane", not the lieutenant's).
	if (!DebugLabel.IsEmpty() && !bRareSpawned)
	{
		Npc->DisplayName = DebugLabel;
	}

	const float HalfHeight = Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Centre = FindGroundedCapsuleCentre(HalfHeight);
	Npc->FinishSpawning(FTransform(Initial.GetRotation(), Centre));

	// The settled location is the leash anchor: if collision handling nudged
	// it, the NPC must not leash the moment it takes a step.
	Npc->SetHomeLocation(Npc->GetActorLocation());

	// B-10 part 2: its idle walk, from the settled spot. A fresh NPC starts at
	// stop 0 (here) with a fresh route state; the rare walks the same route.
	Npc->ConfigureIdleMovement(BuildPatrolSetup(Npc->GetActorLocation()), IsFollowing() ? FollowSpawner.Get() : nullptr);

	SpawnedNPC = Npc;

	FString Idle = TEXT("stands");
	if (IsFollowing())
	{
		Idle = FString::Printf(TEXT("follows %s"), *FollowSpawner->GetName());
	}
	else if (HasPatrolRoute())
	{
		Idle = FString::Printf(TEXT("%s route, %d stops"), PatrolMode == EValhallaPatrolMode::Loop ? TEXT("loop") : TEXT("ping-pong"), PatrolPoints.Num() + 1);
	}
	else if (GetEffectiveWanderRadius() > 0.f)
	{
		Idle = FString::Printf(TEXT("roams %.0f cm"), GetEffectiveWanderRadius());
	}

	UE_LOG(LogValhallaGame, Log, TEXT("%s spawned '%s'%s (%s, template '%s') at %s; respawn %.0f s after death; idle: %s."),
		*GetName(), *Npc->DisplayName, bRareSpawned ? TEXT(" [RARE]") : TEXT(""), *GetNameSafe(SpawnClass), *EffectiveTemplateId.ToString(),
		*Npc->GetActorLocation().ToCompactString(), GetEffectiveRespawnSeconds(), *Idle);
}

void AValhallaNPCSpawner::NotifyNPCDied(AValhallaNPC* Npc)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || Npc != SpawnedNPC.Get())
	{
		return;
	}

	const float Respawn = GetEffectiveRespawnSeconds();

	// The body decays before the next one stands up, never on top of it.
	const float Corpse = bRespawn ? FMath::Min(CorpseSeconds, FMath::Max(0.5f, Respawn - 0.25f)) : CorpseSeconds;
	Npc->SetLifeSpan(Corpse);

	if (bRespawn)
	{
		World->GetTimerManager().SetTimer(RespawnTimer, this, &AValhallaNPCSpawner::HandleRespawnTimer, Respawn, false);
		UE_LOG(LogValhallaGame, Log, TEXT("%s: '%s' died; corpse decays in %.1f s, next spawn in %.1f s."),
			*GetName(), *Npc->DisplayName, Corpse, Respawn);
	}
}

void AValhallaNPCSpawner::HandleRespawnTimer()
{
	if (AValhallaNPC* Old = SpawnedNPC.Get())
	{
		if (Old->IsAlive())
		{
			// Something already stood it up (an admin respawn). Nothing to do.
			return;
		}
		Old->Destroy();
	}
	SpawnedNPC = nullptr;
	SpawnNPC();
}

void AValhallaNPCSpawner::RespawnNow()
{
	if (!HasAuthority())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimer);
	}
	if (AValhallaNPC* Old = SpawnedNPC.Get())
	{
		if (Old->IsAlive())
		{
			return;
		}
		Old->Destroy();
	}
	SpawnedNPC = nullptr;
	SpawnNPC();
}

void AValhallaNPCSpawner::RemoveNPCAndScheduleRespawn()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	if (AValhallaNPC* Old = SpawnedNPC.Get())
	{
		Old->Destroy();
	}
	SpawnedNPC = nullptr;
	if (bRespawn)
	{
		World->GetTimerManager().SetTimer(RespawnTimer, this, &AValhallaNPCSpawner::HandleRespawnTimer, GetEffectiveRespawnSeconds(), false);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Editor
// ─────────────────────────────────────────────────────────────────────────────

#if WITH_EDITOR

void AValhallaNPCSpawner::PostLoad()
{
	Super::PostLoad();

	// A level saved under an older body keeps that body's preview pieces. Drop
	// any that cannot follow the preview body *before* the components register:
	// a follower on another skeleton trips the engine's leader-pose ensure the
	// moment its render state is created (it did, in L_Desert_Gameplay).
	for (USkeletalMeshComponent* Piece : PreviewPieces)
	{
		USkeletalMesh* Mesh = Piece ? Piece->GetSkeletalMeshAsset() : nullptr;
		if (Mesh && !UValhallaVisuals::CanFollowBody(Mesh, PreviewBody))
		{
			Piece->SetLeaderPoseComponent(nullptr);
			Piece->SetSkeletalMeshAsset(nullptr);
		}
	}
}

void AValhallaNPCSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		RefreshPreview();
		RefreshRouteVisual();
	}
}

void AValhallaNPCSpawner::RefreshRouteVisual()
{
	if (!RouteVisual)
	{
		return;
	}

	// Everything in the spawn point's own space: the component sits on the
	// root with no offset, so its space is the actor's.
	TArray<FVector> Local;
	if (HasPatrolRoute())
	{
		Local.Add(FVector::ZeroVector);
		Local.Append(PatrolPoints);
	}

	FVector LeaderLocal = FVector::ZeroVector;
	const bool bFollow = IsFollowing();
	if (bFollow)
	{
		LeaderLocal = GetActorTransform().InverseTransformPosition(FollowSpawner->GetActorLocation());
	}

	RouteVisual->SetRoute(Local, PatrolMode == EValhallaPatrolMode::Loop, PatrolMode == EValhallaPatrolMode::PingPong,
		GetEffectiveWanderRadius(), bFollow, LeaderLocal);
}

void AValhallaNPCSpawner::RefreshPreview()
{
	if (!PreviewBody)
	{
		return;
	}

	const AValhallaNPC* TypeDefaults = NPCClass ? NPCClass->GetDefaultObject<AValhallaNPC>() : nullptr;
	if (!TypeDefaults)
	{
		PreviewBody->SetSkeletalMeshAsset(nullptr);
		if (PreviewHead) { PreviewHead->SetSkeletalMeshAsset(nullptr); }
		for (USkeletalMeshComponent* Piece : PreviewPieces)
		{
			if (Piece) { Piece->SetSkeletalMeshAsset(nullptr); }
		}
		return;
	}

	USkeletalMesh* Body = nullptr;
	TArray<USkeletalMesh*> Pieces;
	TypeDefaults->GetAppearanceMeshes(Body, Pieces);

	FValhallaNPCTemplate Template;
	AValhallaNPC::ReadTemplateFromDisk(GetEffectiveTemplateId(), Template);
	const float Scale = TypeDefaults->ResolveBodyScale(Template);

	// Stand the preview on whatever floor is under the marker, so a spawn
	// point that is floating or buried shows it.
	double FloorZ = GetActorLocation().Z;
	TraceFloorZ(FloorZ);

	PreviewBody->SetSkeletalMeshAsset(Body);
	// Same body scale the live NPC gets (ApplyBodyScale): template scale times
	// the active body profile's normalising scale.
	PreviewBody->SetRelativeScale3D(FVector(Scale * TypeDefaults->GetBaseBodyScale()));
	PreviewBody->SetWorldLocation(FVector(GetActorLocation().X, GetActorLocation().Y, FloorZ));
	PreviewBody->SetRelativeRotation(FRotator(0.f, UValhallaVisuals::MeshYaw, 0.f));

	// The active profile's idle, so the preview plays a clip made for its
	// skeleton — or, for a type with a body of its own (1.9a), its folder's.
	FString IdlePath = UValhallaVisuals::AnimPath(EValhallaAnim::Idle);
	if (TypeDefaults->HasBodyOverride() && !TypeDefaults->AnimFolderOverride.IsEmpty())
	{
		IdlePath = TypeDefaults->AnimFolderOverride / FPaths::GetBaseFilename(IdlePath);
	}
	UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, *IdlePath);
	if (Idle && Body && Idle->GetSkeleton() != Body->GetSkeleton())
	{
		Idle = nullptr;
	}
	if (Idle)
	{
		PreviewBody->SetUpdateAnimationInEditor(true);
		PreviewBody->PlayAnimation(Idle, /*bLooping*/ true);
	}

	if (TypeDefaults->HasBodyOverride())
	{
		// A body of its own has no player head (1.9a).
		if (PreviewHead) { PreviewHead->SetSkeletalMeshAsset(nullptr); }
	}
	else
	{
		UValhallaVisuals::ApplyActiveHead(PreviewHead, PreviewBody);
	}

	for (int32 Index = 0; Index < PreviewPieces.Num(); ++Index)
	{
		USkeletalMeshComponent* Piece = PreviewPieces[Index];
		if (!Piece)
		{
			continue;
		}
		// A piece weighted to another skeleton cannot follow this body (the
		// leader-pose mismatch error); it is left off rather than drawn wrong.
		USkeletalMesh* Wanted = Pieces.IsValidIndex(Index) ? Pieces[Index] : nullptr;
		if (Wanted && Body && Wanted->GetSkeleton() != Body->GetSkeleton())
		{
			Wanted = nullptr;
		}
		Piece->SetSkeletalMeshAsset(Wanted);
		Piece->SetLeaderPoseComponent(Wanted ? PreviewBody.Get() : nullptr);
	}
}

void AValhallaNPCSpawner::CheckForErrors()
{
	Super::CheckForErrors();

	FMessageLog MapCheck("MapCheck");

	if (!NPCClass)
	{
		MapCheck.Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::FromString(TEXT("has no NPC Type; it will spawn a bare AValhallaNPC."))));
	}

	const FName Effective = GetEffectiveTemplateId();
	FValhallaNPCTemplate Template;
	if (Effective.IsNone())
	{
		MapCheck.Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::FromString(TEXT("has no template: set the NPC Type's Default Template Id or a Template Override."))));
	}
	else if (!AValhallaNPC::ReadTemplateFromDisk(Effective, Template))
	{
		MapCheck.Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("names template '%s', which is not in npc-templates.json."), *Effective.ToString()))));
	}

	// Would the NPC's capsule, stood on the floor here, be inside something?
	if (const UWorld* World = GetWorld())
	{
		const AValhallaNPC* TypeDefaults = NPCClass ? NPCClass->GetDefaultObject<AValhallaNPC>() : GetDefault<AValhallaNPC>();
		const float Scale = TypeDefaults->ResolveBodyScale(Template);
		const float Radius = 30.f * Scale;
		const float HalfHeight = 60.f * Scale;
		const FVector Centre = FindGroundedCapsuleCentre(HalfHeight);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(ValhallaSpawnCheck), false, this);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		if (World->OverlapAnyTestByObjectType(Centre, FQuat::Identity, Objects,
				FCollisionShape::MakeCapsule(Radius - 2.f, HalfHeight - 2.f), Params))
		{
			MapCheck.Warning()
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(FText::FromString(TEXT("places its NPC inside level geometry; move it into the open."))));
		}
	}

	// B-10 part 2: patrol, pair and rare settings (the valhalla.CheckZones list).
	TArray<FString> PatrolProblems;
	CollectPatrolProblems(PatrolProblems);
	for (const FString& Problem : PatrolProblems)
	{
		MapCheck.Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::FromString(Problem)));
	}
}

#endif // WITH_EDITOR
