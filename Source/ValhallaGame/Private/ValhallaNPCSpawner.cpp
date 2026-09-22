// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaNPCSpawner.h"

#include "Animation/AnimSequence.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaNPC.h"
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

	const TCHAR* IdleAnimPath = TEXT("/Game/Valhalla/Characters/Animations/A_Idle");

	const UValhallaDataSubsystem* GetData(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
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

	PreviewBody = CreateEditorOnlyDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewBody"));
	if (PreviewBody)
	{
		PreviewBody->SetupAttachment(Marker);
		PreviewBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewBody->SetHiddenInGame(true);
		PreviewBody->bIsEditorOnly = true;
		PreviewBody->SetCastShadow(false);

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
	const FName EffectiveTemplateId = GetEffectiveTemplateId();
	const FValhallaNPCTemplate* Template = Data->FindNPCTemplate(EffectiveTemplateId);
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
	if (!DebugLabel.IsEmpty())
	{
		Npc->DisplayName = DebugLabel;
	}

	const float HalfHeight = Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Centre = FindGroundedCapsuleCentre(HalfHeight);
	Npc->FinishSpawning(FTransform(Initial.GetRotation(), Centre));

	// The settled location is the leash anchor: if collision handling nudged
	// it, the NPC must not leash the moment it takes a step.
	Npc->SetHomeLocation(Npc->GetActorLocation());

	SpawnedNPC = Npc;

	UE_LOG(LogValhallaGame, Log, TEXT("%s spawned '%s' (%s, template '%s') at %s; respawn %.0f s after death."),
		*GetName(), *Npc->DisplayName, *GetNameSafe(SpawnClass), *EffectiveTemplateId.ToString(),
		*Npc->GetActorLocation().ToCompactString(), GetEffectiveRespawnSeconds());
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

void AValhallaNPCSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		RefreshPreview();
	}
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
	PreviewBody->SetRelativeScale3D(FVector(Scale));
	PreviewBody->SetWorldLocation(FVector(GetActorLocation().X, GetActorLocation().Y, FloorZ));
	PreviewBody->SetRelativeRotation(FRotator(0.f, UValhallaVisuals::MeshYaw, 0.f));

	if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, IdleAnimPath))
	{
		PreviewBody->SetUpdateAnimationInEditor(true);
		PreviewBody->PlayAnimation(Idle, /*bLooping*/ true);
	}

	for (int32 Index = 0; Index < PreviewPieces.Num(); ++Index)
	{
		USkeletalMeshComponent* Piece = PreviewPieces[Index];
		if (!Piece)
		{
			continue;
		}
		Piece->SetSkeletalMeshAsset(Pieces.IsValidIndex(Index) ? Pieces[Index] : nullptr);
		Piece->SetLeaderPoseComponent(PreviewBody);
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
}

#endif // WITH_EDITOR
