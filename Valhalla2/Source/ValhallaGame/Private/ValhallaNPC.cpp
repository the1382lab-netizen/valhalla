// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaNPC.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaStats.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "ValhallaGame.h"
#include "ValhallaGameMode.h"
#include "ValhallaNPCSpawner.h"
#include "ValhallaSocialAggro.h"
#include "ValhallaNPCCombatRules.h"
#include "ValhallaPlayerState.h"
#include "ValhallaVisibilitySubsystem.h"
#include "ValhallaVisuals.h"

namespace
{
	/** The `BaseColor` parameter on M_ValhallaToon. */
	const FName BaseColorParameter(TEXT("BaseColor"));

	/** Section 0 of the body mesh is `M_Skin`, which is what the tint replaces. */
	constexpr int32 SkinMaterialIndex = 0;

	/**
	 * Enemies are a green-grey whatever their template's spriteColor says.
	 *
	 * All three placeholder templates ship spriteColor 0xFFFFFF, which would make
	 * every enemy an identical white figure indistinguishable from a player. The
	 * "Test Enemy" green-grey is the one thing that makes the grey-box readable
	 * at a glance; a template that authors a real colour gets it instead.
	 */
	const FColor DefaultEnemySrgb(0x6F, 0x8A, 0x63);

	/** The fixed kit a placeholder enemy wears (resolved for the active body). */
	const TCHAR* EnemyChestAsset = TEXT("SK_chest_priests_chain");
	const TCHAR* EnemyHelmAsset = TEXT("SK_helm_iron_full");

	/** NPCSystem.ts:941 — DoTs tick once a second. */
	constexpr double BuffTickIntervalSeconds = 1.0;

	/** NPCSystem.ts:500 — the fallback when a template omits aggroRange. */
	constexpr float DefaultAggroRange = 200.f;

	/**
	 * npc-templates.json read straight off disk, for editor dropdowns and
	 * previews where no game instance (and so no data subsystem) exists.
	 * Re-read when the file's timestamp moves, so the web editor's saves show
	 * up without restarting Unreal.
	 */
	struct FDiskTemplateCache
	{
		FDateTime Stamp;
		FString Path;
		TMap<FName, FValhallaNPCTemplate> Templates;

		const TMap<FName, FValhallaNPCTemplate>& Get()
		{
			const UValhallaDataSettings* Settings = UValhallaDataSettings::Get();
			if (!Settings)
			{
				return Templates;
			}

			const FString Root = Settings->GetResolvedDataRoot();
			const FString File = FPaths::Combine(Root, TEXT("npc-templates.json"));
			const FDateTime Now = IFileManager::Get().GetTimeStamp(*File);
			if (File == Path && Now == Stamp)
			{
				return Templates;
			}

			FValhallaDataTables Tables;
			UValhallaDataSubsystem::LoadTablesFromRoot(Root, Tables);
			Templates = MoveTemp(Tables.NPCTemplates);
			Path = File;
			Stamp = Now;
			return Templates;
		}
	};

	FDiskTemplateCache& DiskTemplates()
	{
		static FDiskTemplateCache Cache;
		return Cache;
	}
}

TArray<FString> AValhallaNPC::ReadTemplateIdsFromDisk()
{
	TArray<FString> Ids;
	for (const TPair<FName, FValhallaNPCTemplate>& Pair : DiskTemplates().Get())
	{
		Ids.Add(Pair.Key.ToString());
	}
	Ids.Sort();
	return Ids;
}

bool AValhallaNPC::ReadTemplateFromDisk(FName InTemplateId, FValhallaNPCTemplate& OutTemplate)
{
	if (const FValhallaNPCTemplate* Found = DiskTemplates().Get().Find(InTemplateId))
	{
		OutTemplate = *Found;
		return true;
	}
	return false;
}

TArray<FString> AValhallaNPC::GetNPCTemplateOptions() const
{
	return ReadTemplateIdsFromDisk();
}

float AValhallaNPC::ResolveBodyScale(const FValhallaNPCTemplate& ForTemplate) const
{
	if (ScaleOverride > 0.f)
	{
		return ScaleOverride;
	}
	return ForTemplate.SpriteSize > 0.f ? ForTemplate.SpriteSize : 1.f;
}

void AValhallaNPC::GetAppearanceMeshes(USkeletalMesh*& OutBody, TArray<USkeletalMesh*>& OutPieces) const
{
	OutBody = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	OutPieces.Reset();

	// B-06 1.9a: a body of its own wears nothing; the mesh is its outfit.
	if (HasBodyOverride())
	{
		OutBody = BodyMeshOverride.LoadSynchronous();
		OutPieces.SetNumZeroed(5);
		return;
	}

	// A mesh can be chosen two ways on a BP_NPC_* type, and both count: the
	// Look properties in Class Defaults, or the Skeletal Mesh Asset on the
	// ChestMesh/HelmMesh/... component in the Components panel. The component
	// is read off the class default object, so what the Blueprint set is never
	// confused with a placeholder this function put on a live NPC earlier.
	const AValhallaNPC* Defaults = GetClass()->GetDefaultObject<AValhallaNPC>();
	auto AuthoredOn = [](const USkeletalMeshComponent* Component) -> USkeletalMesh*
	{
		return Component ? Component->GetSkeletalMeshAsset() : nullptr;
	};

	struct FSlot
	{
		const TSoftObjectPtr<USkeletalMesh>* Asset;
		USkeletalMesh* ComponentMesh;
		const TCHAR* Placeholder;
	};
	const FSlot Slots[] = {
		{ &ChestMeshAsset,  AuthoredOn(Defaults ? Defaults->ChestMesh.Get()  : nullptr), EnemyChestAsset },
		{ &HelmMeshAsset,   AuthoredOn(Defaults ? Defaults->HelmMesh.Get()   : nullptr), EnemyHelmAsset },
		{ &LegsMeshAsset,   AuthoredOn(Defaults ? Defaults->LegsMesh.Get()   : nullptr), nullptr },
		{ &BootsMeshAsset,  AuthoredOn(Defaults ? Defaults->BootsMesh.Get()  : nullptr), nullptr },
		{ &GlovesMeshAsset, AuthoredOn(Defaults ? Defaults->GlovesMesh.Get() : nullptr), nullptr },
	};

	for (const FSlot& Slot : Slots)
	{
		USkeletalMesh* SlotMesh = Slot.Asset->IsNull() ? nullptr : Slot.Asset->LoadSynchronous();
		if (!SlotMesh)
		{
			SlotMesh = Slot.ComponentMesh;
		}
		if (!SlotMesh && bWearPlaceholderKit && Slot.Placeholder)
		{
			SlotMesh = LoadObject<USkeletalMesh>(nullptr, *UValhallaVisuals::EquipmentMeshPath(Slot.Placeholder));
		}
		OutPieces.Add(UValhallaVisuals::PieceForActiveBody(SlotMesh));
	}
}

AValhallaNPC::AValhallaNPC()
{
	// Ticked by AValhallaGameState, in GameRoom.update's order. See the header.
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// An NPC faces where it is going, so unlike the player it *does* orient to
	// movement — 1.0 set `npc.aimAngle = atan2(dy, dx)` every chase tick, which
	// is the same thing said differently.
	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	// An NPC has no controller (see the header: no AIController, by design),
	// and CharacterMovement only consumes AddMovementInput for a pawn that is
	// locally controlled — or, with this flag, for one that has no controller
	// at all. Without it every chase and walk-home input was silently dropped:
	// an aggroed NPC stood still and only ever hit a player who was already in
	// reach.
	Movement->bRunPhysicsWithNoController = true;
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 540.f, 0.f);
	Movement->MaxAcceleration = 4096.f;
	Movement->BrakingDecelerationWalking = 4096.f;
	Movement->MaxWalkSpeed = 60.f;
	Movement->GetNavAgentPropertiesRef().bCanJump = false;
	Movement->GetNavAgentPropertiesRef().bCanCrouch = false;

	JumpMaxCount = 0;

	// ── Body ────────────────────────────────────────────────────────────
	// The same rig, the same skeleton and the same animations as a player, with
	// a fixed set of worn pieces instead of a replicated paperdoll. An enemy is
	// a person, and Phase 4c's whole point is that it should look like one.
	BodyMesh = GetMesh();
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, UValhallaVisuals::MeshZOffset));
	BodyMesh->SetRelativeRotation(FRotator(0.f, UValhallaVisuals::MeshYaw, 0.f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCollisionProfileName(TEXT("NoCollision"));
	BodyMesh->SetGenerateOverlapEvents(false);
	BodyMesh->SetVisibility(true);

	// The active body profile's mesh, so the editor (CDO, placed and preview
	// actors) shows the body PIE will use. PostInitializeComponents re-applies
	// it at runtime in case the profile cvar changed after load.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyAsset(*UValhallaVisuals::ActiveBodyMeshPath());
	if (BodyAsset.Succeeded())
	{
		BodyMesh->SetSkeletalMeshAsset(BodyAsset.Object);
		BodyMesh->SetRelativeScale3D(FVector(UValhallaVisuals::ActiveBodyScale()));
	}

	HeadMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(BodyMesh);
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadMesh->SetCollisionProfileName(TEXT("NoCollision"));
	HeadMesh->SetGenerateOverlapEvents(false);
	HeadMesh->bUseAttachParentBound = true;
	HeadMesh->SetLeaderPoseComponent(BodyMesh);
	if (!UValhallaVisuals::ActiveHeadMeshPath().IsEmpty())
	{
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> HeadAsset(*UValhallaVisuals::ActiveHeadMeshPath());
		if (HeadAsset.Succeeded())
		{
			HeadMesh->SetSkeletalMeshAsset(HeadAsset.Object);
		}
	}

	ChestMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ChestMesh"));
	ChestMesh->SetupAttachment(BodyMesh);
	HelmMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HelmMesh"));
	HelmMesh->SetupAttachment(BodyMesh);
	LegsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("LegsMesh"));
	LegsMesh->SetupAttachment(BodyMesh);
	BootsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BootsMesh"));
	BootsMesh->SetupAttachment(BodyMesh);
	GlovesMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("GlovesMesh"));
	GlovesMesh->SetupAttachment(BodyMesh);

	for (USkeletalMeshComponent* Follower : { ChestMesh.Get(), HelmMesh.Get(), LegsMesh.Get(), BootsMesh.Get(), GlovesMesh.Get() })
	{
		Follower->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Follower->SetCollisionProfileName(TEXT("NoCollision"));
		Follower->SetGenerateOverlapEvents(false);
		Follower->SetCastShadow(false);
		Follower->bUseAttachParentBound = true;
		Follower->SetLeaderPoseComponent(BodyMesh);
	}

	// The template's weapon, in the same hand socket a player's goes in.
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(BodyMesh, UValhallaVisuals::WeaponSocket());
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetCollisionProfileName(TEXT("NoCollision"));
	WeaponMesh->SetGenerateOverlapEvents(false);
	WeaponMesh->SetCastShadow(false);
	WeaponMesh->bAffectDistanceFieldLighting = false;   // see AValhallaCharacter's SetUpProp

	AnimComponent = CreateDefaultSubobject<UValhallaAnimComponent>(TEXT("AnimComponent"));

	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(20.f);

	// An NPC is a click target, so it must block the cursor trace even though it
	// never blocks a pawn's movement channel in any interesting way.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// ── Phase 5 relevancy ───────────────────────────────────────────────
	//
	// An NPC behind a wall is the case the whole phase exists for: until now a
	// client was sent every NPC in the level and could read their positions and
	// health straight out of memory. The cull distance is left enormous so that
	// the line-of-sight override below is the only thing that decides.
	bAlwaysRelevant = false;
	SetNetCullDistanceSquared(1.0e12f);
}

bool AValhallaNPC::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& /*SrcLocation*/) const
{
	return UValhallaVisibilitySubsystem::IsRelevantForViewer(this, RealViewer, ViewTarget);
}

void AValhallaNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValhallaNPC, Hp);
	DOREPLIFETIME(AValhallaNPC, MaxHp);
	DOREPLIFETIME(AValhallaNPC, Level);
	DOREPLIFETIME(AValhallaNPC, DisplayName);
	DOREPLIFETIME(AValhallaNPC, bAlive);
	DOREPLIFETIME(AValhallaNPC, TemplateId);
	DOREPLIFETIME(AValhallaNPC, SyncedBuffs);
	DOREPLIFETIME(AValhallaNPC, bFriendly);
	DOREPLIFETIME(AValhallaNPC, BodyScale);
	DOREPLIFETIME(AValhallaNPC, BodyColor);
	DOREPLIFETIME(AValhallaNPC, WeaponId);

	// The threat table is not here and never will be. Knowing who an NPC is
	// about to switch to is a tactical advantage the server does not give away.
}

void AValhallaNPC::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// B-06 1.9a: a type with a body of its own (the goblin) never wears the
	// player body. Its clips come from its own folder, set here because the
	// anim component loads them in its BeginPlay, before this actor's.
	if (ApplyBodyOverride())
	{
		if (AnimComponent)
		{
			AnimComponent->SetAnimFolderOverride(AnimFolderOverride);
		}
		AttachWeaponToOverrideBody(static_cast<int32>(EValhallaGrip::OneHand));
		return;
	}

	if (UValhallaVisuals::ApplyActiveBody(BodyMesh))
	{
		UValhallaVisuals::ApplyActiveHead(HeadMesh, BodyMesh);
		UValhallaVisuals::AttachHeldProp(WeaponMesh, BodyMesh, EValhallaGrip::OneHand);
	}
}

bool AValhallaNPC::ApplyBodyOverride()
{
	if (!HasBodyOverride() || !BodyMesh)
	{
		return false;
	}

	USkeletalMesh* OverrideMesh = BodyMeshOverride.LoadSynchronous();
	if (!OverrideMesh)
	{
		UE_LOG(LogValhallaVisual, Warning, TEXT("%s: body override %s is missing; keeping the player body."),
			*GetName(), *BodyMeshOverride.ToString());
		return false;
	}

	if (BodyMesh->GetSkeletalMeshAsset() != OverrideMesh)
	{
		BodyMesh->SetSkeletalMeshAsset(OverrideMesh);
	}
	BodyMesh->SetRelativeScale3D(FVector(GetBaseBodyScale() * (BodyScale > 0.f ? BodyScale : 1.f)));

	// No head, hair or armour: they are built for the player skeleton and
	// could not follow this one anyway.
	if (HeadMesh)
	{
		HeadMesh->SetSkeletalMeshAsset(nullptr);
		HeadMesh->SetLeaderPoseComponent(nullptr);
	}
	for (USkeletalMeshComponent* Follower : { ChestMesh.Get(), HelmMesh.Get(), LegsMesh.Get(), BootsMesh.Get(), GlovesMesh.Get() })
	{
		if (Follower && Follower->GetSkeletalMeshAsset())
		{
			Follower->SetSkeletalMeshAsset(nullptr);
		}
	}
	return true;
}

float AValhallaNPC::GetBaseBodyScale() const
{
	if (HasBodyOverride())
	{
		return BodyMeshScale > 0.f ? BodyMeshScale : 1.f;
	}
	return UValhallaVisuals::ActiveBodyScale();
}

void AValhallaNPC::AttachWeaponToOverrideBody(int32 GripIndex)
{
	if (!WeaponMesh || !BodyMesh)
	{
		return;
	}

	const EValhallaGrip Grip = static_cast<EValhallaGrip>(GripIndex);
	const FValhallaNPCGripFrame* Frame = &OneHandGrip;
	if (Grip == EValhallaGrip::Staff)
	{
		Frame = &StaffGrip;
	}
	else if (Grip == EValhallaGrip::Bow)
	{
		Frame = &BowGrip;
	}

	if (Frame->Bone.IsNone())
	{
		// Not calibrated for this body: the player body's frame is the best guess.
		UValhallaVisuals::AttachHeldProp(WeaponMesh, BodyMesh, Grip);
		return;
	}

	// Frames are measured on the unscaled mesh, like the player body's; the
	// prop is drawn at PropScale of its modelled size, whatever the body's scale.
	WeaponMesh->AttachToComponent(BodyMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Frame->Bone);
	WeaponMesh->SetRelativeTransform(FTransform(Frame->Rotation, Frame->Location,
		FVector(Frame->PropScale / FMath::Max(GetBaseBodyScale(), KINDA_SMALL_NUMBER))));
}

void AValhallaNPC::BeginPlay()
{
	Super::BeginPlay();

	if (AnimComponent)
	{
		AnimComponent->SetBodyMesh(BodyMesh);
	}

	ApplyAppearance();
	OnRep_Alive();

	// An NPC type dragged straight into a level, rather than spawned by an
	// AValhallaNPCSpawner, plays its own template from where it was placed.
	if (HasAuthority() && !bInitializedFromTemplate)
	{
		if (DefaultTemplateId.IsNone())
		{
			UE_LOG(LogValhallaGame, Warning, TEXT("%s: placed with no Default Template Id; it has no stats. Set one on the Blueprint."), *GetName());
			return;
		}

		const UGameInstance* GameInstance = GetGameInstance();
		const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
		if (const FValhallaNPCTemplate* Found = Data ? Data->FindNPCTemplate(DefaultTemplateId) : nullptr)
		{
			InitializeFromTemplate(*Found, GetActorLocation(), nullptr);
		}
		else
		{
			UE_LOG(LogValhallaGame, Warning, TEXT("%s: unknown template '%s'."), *GetName(), *DefaultTemplateId.ToString());
		}
	}
}

void AValhallaNPC::ApplyBodyScale()
{
	const float Scale = BodyScale > 0.f ? BodyScale : 1.f;
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float OldHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	Capsule->SetCapsuleSize(CapsuleRadius * Scale, CapsuleHalfHeight * Scale);

	// Keep the feet where they were when the capsule changes height on an NPC
	// that is already standing (a hot reload of spriteSize).
	const float Delta = CapsuleHalfHeight * Scale - OldHalfHeight;
	if (HasActorBegunPlay() && !FMath::IsNearlyZero(Delta))
	{
		AddActorWorldOffset(FVector(0.f, 0.f, Delta), false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (BodyMesh)
	{
		// ActiveBodyScale normalises the active body to 122 cm (1 for the Valhalla
		// body); a type with a body of its own brings its own factor (1.9a).
		BodyMesh->SetRelativeScale3D(FVector(Scale * GetBaseBodyScale()));
		// The body's pivot is at its feet, so the offset that stands it on the
		// capsule's bottom is the (scaled) capsule half-height, no more.
		BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, UValhallaVisuals::MeshZOffset * Scale));
	}
}

void AValhallaNPC::OnRep_BodyScale()
{
	ApplyAppearance();
}

void AValhallaNPC::ApplyAppearance()
{
	if (!BodyMesh || !BodyMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	// `spriteSize` (or the type's ScaleOverride) scales the whole body *and* its
	// capsule. BodyScale is replicated; the template is not.
	ApplyBodyScale();

	// Tint precedence: the NPC type's TintOverride, then the template's
	// spriteColor (replicated as BodyColor), then — for enemies only — the
	// grey-box green. A friendly NPC with no colour keeps its natural skin.
	FLinearColor Tint = FLinearColor::FromSRGBColor(DefaultEnemySrgb);
	bool bApplyTint = true;
	if (TintOverride.A > 0.f)
	{
		Tint = FLinearColor(TintOverride.R, TintOverride.G, TintOverride.B, 1.f);
	}
	// 0xFFFFFF is the editor's "no colour chosen" default, so treat it as
	// unauthored rather than as a deliberate white.
	else if (BodyColor != 0 && BodyColor != 0xFFFFFF)
	{
		Tint = FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>((BodyColor >> 16) & 0xFF),
			static_cast<uint8>((BodyColor >> 8) & 0xFF),
			static_cast<uint8>(BodyColor & 0xFF)));
	}
	else if (bFriendly)
	{
		bApplyTint = false;
	}
	// External bodies (the MetaHuman) carry their own baked skin: the
	// grey-box tints were for the untextured legacy body only.
	if (UValhallaVisuals::UseExternalBody())
	{
		bApplyTint = false;
	}

	if (bApplyTint && !TintMaterial)
	{
		if (UMaterialInterface* Source = BodyMesh->GetMaterial(SkinMaterialIndex))
		{
			TintMaterial = UMaterialInstanceDynamic::Create(Source, this);
			BodyMesh->SetMaterial(SkinMaterialIndex, TintMaterial);
		}
	}

	if (bApplyTint && TintMaterial)
	{
		TintMaterial->SetVectorParameterValue(BaseColorParameter, Tint);
	}

	// The NPC type's look: its own armour meshes, with the placeholder chain
	// shirt and full helm filling chest and helm when it names none (and wants
	// the placeholder). Resolved on every machine from the class defaults, so
	// nothing about it has to replicate.
	USkeletalMesh* Body = nullptr;
	TArray<USkeletalMesh*> Pieces;
	GetAppearanceMeshes(Body, Pieces);

	USkeletalMeshComponent* Components[] = { ChestMesh, HelmMesh, LegsMesh, BootsMesh, GlovesMesh };
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Components)); ++Index)
	{
		USkeletalMeshComponent* Component = Components[Index];
		// A piece weighted to another rig cannot follow this body; it stays off.
		USkeletalMesh* Wanted = Pieces.IsValidIndex(Index) ? Pieces[Index] : nullptr;
		if (Wanted && !UValhallaVisuals::CanFollowBody(Wanted, BodyMesh))
		{
			Wanted = nullptr;
		}
		if (!Component || Component->GetSkeletalMeshAsset() == Wanted)
		{
			continue;
		}

		Component->SetSkeletalMeshAsset(Wanted);
		// Re-linked after the mesh swap: SetSkeletalMeshAsset drops the link.
		Component->SetLeaderPoseComponent(BodyMesh);
		UE_LOG(LogValhallaVisual, Log, TEXT("npc=%s asset=%s"), *DisplayName, *GetPathNameSafe(Wanted));
	}

	ApplyWeaponVisual();
}

void AValhallaNPC::ApplyWeaponVisual()
{
	// The weapon decides the attack animation, exactly as it does for a player
	// (AValhallaCharacter::RefreshEquipmentVisuals): sword/mace swing, bow
	// shoots, staff swings. Unarmed is the melee cycle.
	if (AnimComponent)
	{
		AnimComponent->SetAttackCycle(UValhallaVisuals::AttackCycleForEquippedWeapon(this, WeaponId));
		const bool bHasWeapon = !WeaponId.IsNone();
		const bool bBow = bHasWeapon && UValhallaVisuals::GripForWeapon(this, WeaponId) == EValhallaGrip::Bow;
		AnimComponent->SetWeaponLoadout(UValhallaVisuals::AttackAnimForWeapon(this, WeaponId), bHasWeapon && !bBow, bBow, false);
	}

	if (!WeaponMesh || AppliedWeaponId == WeaponId)
	{
		return;
	}
	AppliedWeaponId = WeaponId;

	if (WeaponId.IsNone())
	{
		WeaponMesh->SetStaticMesh(nullptr);
		return;
	}

	bool bIsSkeletal = true;
	const FString AssetPath = UValhallaVisuals::EquipmentAssetPathForItem(this, WeaponId, bIsSkeletal);
	UStaticMesh* Loaded = (!AssetPath.IsEmpty() && !bIsSkeletal) ? LoadObject<UStaticMesh>(nullptr, *AssetPath) : nullptr;
	WeaponMesh->SetStaticMesh(Loaded);
	if (!Loaded)
	{
		UE_LOG(LogValhallaVisual, Warning, TEXT("npc=%s weapon=%s asset=%s MISSING"),
			*DisplayName, *WeaponId.ToString(), AssetPath.IsEmpty() ? TEXT("<no art>") : *AssetPath);
		return;
	}

	// A bow is held pitched up in the right hand, the same correction a player gets.
	FRotator PropRotation = FRotator::ZeroRotator;
	if (HasBodyOverride())
	{
		// B-06 1.9a: this type's own grip frames (the goblin's hands).
		AttachWeaponToOverrideBody(static_cast<int32>(UValhallaVisuals::GripForWeapon(this, WeaponId)));
		PropRotation = WeaponMesh->GetRelativeRotation();
	}
	else if (UValhallaVisuals::UseExternalBody())
	{
		UValhallaVisuals::AttachHeldProp(WeaponMesh, BodyMesh, UValhallaVisuals::GripForWeapon(this, WeaponId));
		PropRotation = WeaponMesh->GetRelativeRotation();
	}
	else if (UValhallaVisuals::AttackCycleForEquippedWeapon(this, WeaponId) == EValhallaAttackCycle::Shoot)
	{
		PropRotation.Pitch = UValhallaVisuals::BowWeaponSocketPitch;
	}
	WeaponMesh->SetRelativeRotation(PropRotation);
	UE_LOG(LogValhallaVisual, Log, TEXT("npc=%s weapon=%s asset=%s"), *DisplayName, *WeaponId.ToString(), *AssetPath);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Spawning
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::InitializeFromTemplate(const FValhallaNPCTemplate& InTemplate, const FVector& InHomeLocation, AValhallaNPCSpawner* InSpawner)
{
	if (!HasAuthority())
	{
		return;
	}

	Template = InTemplate;
	HomeLocation = InHomeLocation;
	Spawner = InSpawner;
	bInitializedFromTemplate = true;

	TemplateId = InTemplate.Id;
	DisplayName = NameOverride.IsEmpty() ? InTemplate.Name : NameOverride;
	bFriendly = InTemplate.Type == EValhallaNPCType::Npc;
	BodyScale = ResolveBodyScale(InTemplate);
	BodyColor = InTemplate.SpriteColor;
	WeaponId = InTemplate.WeaponId;
	ApplyAppearance();
	Level = FMath::Max(1, InTemplate.Level);
	MaxHp = InTemplate.Hp;
	Hp = InTemplate.Hp;
	bAlive = true;

	// NPCSystem.ts:94 — leashRange defaults to three times the aggro range, so a
	// template that only tunes aggro still gets a sensible chase limit.
	LeashRange = InTemplate.LeashRange > 0.f
		? InTemplate.LeashRange
		: (InTemplate.AggroRange > 0.f ? InTemplate.AggroRange : DefaultAggroRange) * 3.f;

	// 1.0 pixels per second become cm per second unchanged.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = InTemplate.MoveSpeed > 0.f ? InTemplate.MoveSpeed : 60.f;
	}

	ThreatTable.Reset();
	ActiveBuffs.Reset();
	SyncedBuffs.Reset();
	AggroTarget = nullptr;
	RespawnAt = 0.0;
	LastAttackTime = 0.0;

	UE_LOG(LogValhallaGame, Log, TEXT("NPC '%s' (%s) spawned at %s: hp=%.0f lv%d aggro=%.0f leash=%.0f speed=%.0f dmg=%.0f every %.0f ms, xp=%d"),
		*DisplayName, *TemplateId.ToString(), *InHomeLocation.ToCompactString(),
		MaxHp, Level, InTemplate.AggroRange, LeashRange, InTemplate.MoveSpeed,
		InTemplate.Damage, InTemplate.AttackSpeedMs, InTemplate.XpReward);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Visibility
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::OnRep_Alive()
{
	// `A_Death` plays once and holds its last frame, so a corpse lies where it
	// fell until the respawn timer stands it back up — which is also what makes
	// a loot bag land next to a body rather than next to nothing. Still no
	// ragdoll: the import pipeline generates no physics asset, and an authored
	// death is the only one that looks identical on every client.
	if (AnimComponent)
	{
		AnimComponent->SetDead(!bAlive);
	}
	SetActorEnableCollision(bAlive);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (!bAlive)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		else
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
}

void AValhallaNPC::OnRep_Hp()
{
	// Nothing to do yet: AValhallaHUD reads Hp directly when it draws a
	// nameplate. The hook exists so Phase 8's damage flash has somewhere to go.
}

// ─────────────────────────────────────────────────────────────────────────────
//  Damage, threat and death — NPCSystem.ts:279 / 306
// ─────────────────────────────────────────────────────────────────────────────

bool AValhallaNPC::CanEverAggro() const
{
	// NPCSystem.ts:497 — canAggro defaults to true for enemies, false for
	// friendly NPCs. B-06: a friendly NPC never aggroes, whatever its template's
	// canAggro says — an outpost guard with the box ticked must not start a
	// fight with the players it is there to keep safe.
	return !bFriendly && (Template.bCanAggro || Template.Type == EValhallaNPCType::Enemy);
}

int32 AValhallaNPC::ApplyDamageFromAttacker(AActor* Attacker, int32 Damage, double Now, bool& bOutDied)
{
	bOutDied = false;

	// B-06: a friendly NPC cannot be hurt. Every attack path already refuses
	// it (UValhallaCombatLibrary::AreHostile); this is the backstop for any
	// path that does not ask, so a vendor can never be killed by a stray AoE.
	if (!HasAuthority() || !bAlive || Damage <= 0 || bFriendly)
	{
		return 0;
	}

	Hp = FMath::Max(0.f, Hp - Damage);
	LastAttacker = Attacker;

	// NPCSystem.ts:286 — damage dealt is threat generated, and this is the only
	// place threat is created by combat. A `canAggro: false` NPC takes the damage
	// but never builds a table, so it never fights back.
	const bool bCanAggro = CanEverAggro();
	if (bCanAggro && Attacker)
	{
		float& Threat = ThreatTable.FindOrAdd(Attacker);
		Threat += Damage;
	}

	if (Hp <= 0.f)
	{
		bOutDied = true;
		Die(Attacker, Now);
		return Template.XpReward;
	}

	return 0;
}

void AValhallaNPC::Die(AActor* Killer, double Now)
{
	if (!HasAuthority() || !bAlive)
	{
		return;
	}

	bAlive = false;
	Hp = 0.f;
	// A spawn point's NPC is replaced by a fresh instance on the spawn point's
	// timer; only a hand-placed NPC keeps 1.0's stand-back-up timer.
	RespawnAt = Spawner.IsValid() ? 0.0 : Now + Template.RespawnMs / 1000.0;
	AggroTarget = nullptr;
	ThreatTable.Reset();
	bReturning = false;
	bHasFightStart = false;
	bHoldingPosition = false;
	GaveUpOn = nullptr;
	ActiveBuffs.Reset();
	SyncedBuffs.Reset();

	OnRep_Alive();

	FValhallaCombatEvent Event;
	Event.Kind = EValhallaCombatEventKind::NpcDied;
	Event.Target = this;
	Event.Instigator = Killer;
	Event.Amount = static_cast<float>(Template.XpReward);
	Event.RemainingHp = 0.f;
	Event.Location = GetActorLocation();
	UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

	UE_LOG(LogValhallaCombat, Log, TEXT("npcDied %s killed by %s, xpReward=%d%s"),
		*DisplayName, *UValhallaCombatLibrary::GetDisplayName(Killer), Template.XpReward,
		Spawner.IsValid() ? TEXT(", respawn on its spawn point's timer") : *FString::Printf(TEXT(", respawn in %.0f ms"), Template.RespawnMs));

	// GameRoom.broadcastCombatEvents:942 — loot and XP are the game mode's job,
	// not the NPC's. Phase 2c's party split and loot tables hang off this hook.
	if (AValhallaGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AValhallaGameMode>() : nullptr)
	{
		GameMode->OnNPCKilled(this, Killer);
	}

	// Last, after the loot has been dropped where the body lies: the spawn
	// point starts the corpse's decay and the respawn countdown.
	if (AValhallaNPCSpawner* OwningSpawner = Spawner.Get())
	{
		OwningSpawner->NotifyNPCDied(this);
	}
}

void AValhallaNPC::Respawn()
{
	if (!HasAuthority() || bAlive)
	{
		return;
	}

	bAlive = true;
	Hp = MaxHp;
	RespawnAt = 0.0;
	AggroTarget = nullptr;
	ThreatTable.Reset();
	ActiveBuffs.Reset();
	SyncedBuffs.Reset();

	SetActorLocation(HomeLocation, false, nullptr, ETeleportType::TeleportPhysics);
	MovePath.Reset();
	bCalledForHelp = false;
	bReturning = false;
	bHasFightStart = false;
	bHoldingPosition = false;
	GaveUpOn = nullptr;
	// B-10 part 2: back at stop 0 (home), so the route starts over from there.
	ResetIdleState();
	OnRep_Alive();

	UE_LOG(LogValhallaCombat, Log, TEXT("%s respawned at %s with %.0f hp."),
		*DisplayName, *HomeLocation.ToCompactString(), MaxHp);
}

void AValhallaNPC::ReapplyTemplate(const FValhallaNPCTemplate& NewTemplate)
{
	if (!HasAuthority())
	{
		return;
	}

	// Taken before MaxHp moves. A dead NPC has Hp 0 and stays on 0.
	const float HpFraction = MaxHp > 0.f ? FMath::Clamp(Hp / MaxHp, 0.f, 1.f) : 1.f;

	// Only a name that came from the template follows the template; a type's
	// NameOverride or a spawn point's label stays put.
	const bool bNameFromTemplate = NameOverride.IsEmpty() && DisplayName == Template.Name;

	Template = NewTemplate;

	TemplateId = NewTemplate.Id;
	if (bNameFromTemplate)
	{
		DisplayName = NewTemplate.Name;
	}
	bFriendly = NewTemplate.Type == EValhallaNPCType::Npc;
	BodyScale = ResolveBodyScale(NewTemplate);
	BodyColor = NewTemplate.SpriteColor;
	WeaponId = NewTemplate.WeaponId;
	Level = FMath::Max(1, NewTemplate.Level);
	MaxHp = NewTemplate.Hp;
	Hp = bAlive ? FMath::Max(1.f, MaxHp * HpFraction) : 0.f;

	// Same defaulting as InitializeFromTemplate — NPCSystem.ts:94.
	LeashRange = NewTemplate.LeashRange > 0.f
		? NewTemplate.LeashRange
		: (NewTemplate.AggroRange > 0.f ? NewTemplate.AggroRange : DefaultAggroRange) * 3.f;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = NewTemplate.MoveSpeed > 0.f ? NewTemplate.MoveSpeed : 60.f;
	}

	ApplyAppearance();
}

void AValhallaNPC::NotifyAttackAvoided(AActor* Attacker)
{
	if (!HasAuthority() || !bAlive || !Attacker)
	{
		return;
	}

	// A swing that missed or was dodged is still an attack. Without this an NPC
	// whose attacker missed first never noticed, and one aggroed only by misses
	// never fought back. One point puts the attacker in the table (so a
	// canAggro NPC turns on them) without outranking anyone who has hit it.
	const bool bCanAggro = CanEverAggro();
	if (bCanAggro)
	{
		ThreatTable.FindOrAdd(Attacker) += 1.f;
	}
}

void AValhallaNPC::AddThreat(AActor* Player, float BonusThreat, bool bForceTarget)
{
	if (!HasAuthority() || !bAlive || !Player)
	{
		return;
	}

	float& Threat = ThreatTable.FindOrAdd(Player);
	Threat += BonusThreat;

	if (bForceTarget)
	{
		AggroTarget = Player;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Buffs — NPCSystem.ts:406
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::ApplyNPCBuff(const FValhallaActiveBuff& Buff)
{
	if (!HasAuthority())
	{
		return;
	}

	// SkillEffectHandler.ts:213 — NPCs use replace semantics unconditionally. A
	// stacking DoT on an NPC would need the same stackingMode plumbing players
	// have, and no 1.0 skill needs it, so the simple rule is the ported one.
	UValhallaCombatLibrary::ApplyBuff(ActiveBuffs, Buff, EValhallaStackingMode::Replace, 1);
	SyncBuffsToReplicatedView();
}

void AValhallaNPC::SyncBuffsToReplicatedView()
{
	SyncedBuffs.Reset(ActiveBuffs.Num());
	for (const FValhallaActiveBuff& Buff : ActiveBuffs)
	{
		FValhallaNPCBuffInfo Info;
		Info.SkillId = Buff.SkillId;
		Info.ExpiresAt = Buff.ExpiresAt;
		Info.DotDamagePerSec = Buff.DotDamagePerSec;
		SyncedBuffs.Add(Info);
	}
}

void AValhallaNPC::TickBuffs(double Now)
{
	if (ActiveBuffs.Num() == 0)
	{
		return;
	}

	bool bChanged = false;

	for (int32 Index = ActiveBuffs.Num() - 1; Index >= 0; --Index)
	{
		FValhallaActiveBuff& Buff = ActiveBuffs[Index];

		if (Now >= Buff.ExpiresAt)
		{
			ActiveBuffs.RemoveAt(Index);
			bChanged = true;
			continue;
		}

		if (Buff.DotDamagePerSec <= 0.f || (Now - Buff.LastTickAt) < BuffTickIntervalSeconds)
		{
			continue;
		}

		// NPCSystem.ts:428 — advance by exactly one interval, not to Now.
		Buff.LastTickAt += BuffTickIntervalSeconds;

		const int32 DotDamage = FMath::RoundToInt32(static_cast<double>(Buff.DotDamagePerSec) * FMath::Max(1, Buff.Stacks));
		AActor* DotCaster = Buff.Caster.Get();
		const FName DotSkill = Buff.SkillId;

		Hp = FMath::Max(0.f, Hp - DotDamage);

		FValhallaCombatEvent Event;
		Event.Kind = EValhallaCombatEventKind::NpcHit;
		Event.Target = this;
		Event.Instigator = DotCaster;
		Event.SkillId = DotSkill;
		Event.Amount = static_cast<float>(DotDamage);
		Event.RemainingHp = Hp;
		Event.Location = GetActorLocation();
		UValhallaCombatLibrary::BroadcastCombatEvent(this, Event);

		UE_LOG(LogValhallaCombat, Log, TEXT("dot tick %d on %s from '%s'"), DotDamage, *DisplayName, *DotSkill.ToString());

		if (Hp <= 0.f)
		{
			// NPCSystem.ts:460 — a DoT kill credits the caster of the DoT, which
			// is why the caster is carried on the buff at all.
			Die(DotCaster, Now);
			return;
		}
	}

	if (bChanged)
	{
		SyncBuffsToReplicatedView();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Aggro — NPCSystem.ts:493
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::UpdateAggro(double Now)
{
	// NPCSystem.ts:497 — canAggro defaults to true for enemies, false for
	// friendly NPCs. A shopkeeper does not chase you for walking past.
	const bool bCanAggro = CanEverAggro();
	if (!bCanAggro)
	{
		AggroTarget = nullptr;
		return;
	}

	// ── Phase 1: prune (NPCSystem.ts:503) ────────────────────────────────
	// A player who died or disconnected stops being a threat immediately,
	// otherwise the NPC stands there aggroed on a corpse.
	for (auto It = ThreatTable.CreateIterator(); It; ++It)
	{
		AActor* Candidate = It.Key().Get();
		if (!Candidate || !UValhallaCombatLibrary::IsAliveTarget(Candidate))
		{
			It.RemoveCurrent();
		}
	}

	// ── Phase 2: highest threat wins (NPCSystem.ts:511) ──────────────────
	if (ThreatTable.Num() > 0)
	{
		AActor* Best = nullptr;
		float BestThreat = -1.f;
		for (const TPair<TWeakObjectPtr<AActor>, float>& Entry : ThreatTable)
		{
			if (Entry.Value > BestThreat)
			{
				BestThreat = Entry.Value;
				Best = Entry.Key.Get();
			}
		}
		AggroTarget = Best;
		return;
	}

	// ── Phase 3: proximity, only when nobody has hit it yet ──────────────
	const float AggroRange = Template.AggroRange > 0.f ? Template.AggroRange : DefaultAggroRange;
	const double AggroRangeSq = static_cast<double>(AggroRange) * AggroRange;
	const FVector MyLocation = GetActorLocation();

	AActor* Nearest = nullptr;
	double NearestDistSq = AggroRangeSq;

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AValhallaCharacter> It(World); It; ++It)
		{
			AValhallaCharacter* Player = *It;
			if (!Player || !Player->IsAlive())
			{
				continue;
			}
			// A chase that gave up (stuck) does not restart by proximity on the
			// way back; a hit on it still does, through the threat table above.
			if (bReturning && GaveUpOn.Get() == Player)
			{
				continue;
			}

			FVector ToPlayer = Player->GetActorLocation() - MyLocation;
			ToPlayer.Z = 0.0;
			const double DistSq = ToPlayer.SizeSquared2D();
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Player;
			}
		}
	}

	if (Nearest)
	{
		// NPCSystem.ts:541 — seed 1 threat so a proximity-aggroed player is in
		// the table and can be legitimately out-threatened by someone who hits it.
		ThreatTable.Add(Nearest, 1.f);
		UE_LOG(LogValhallaCombat, Log, TEXT("%s aggro: %s entered aggro range (%.0f cm)."),
			*DisplayName, *UValhallaCombatLibrary::GetDisplayName(Nearest), FMath::Sqrt(NearestDistSq));
	}

	AggroTarget = Nearest;
}

void AValhallaNPC::StartReturn(const TCHAR* Why, AActor* InGaveUpOn)
{
	AggroTarget = nullptr;
	ThreatTable.Reset();
	MovePath.Reset();
	bCalledForHelp = false;
	bHoldingPosition = false;
	bReturning = true;
	GaveUpOn = InGaveUpOn;

	if (!bHasFightStart)
	{
		FightStart = HomeLocation;
		bHasFightStart = true;
	}

	UE_LOG(LogValhallaCombat, Log, TEXT("%s (%s) %s: walking back to %s (%.0f cm) at %.0f/%.0f hp."),
		*DisplayName, *GetName(), Why, *FightStart.ToCompactString(),
		FVector::Dist2D(GetActorLocation(), FightStart), Hp, MaxHp);
}

void AValhallaNPC::FinishReturn()
{
	// NPCSystem.ts:172 — a leashed NPC heals to full. Without it, a player could
	// pull an enemy, run out of leash range, and repeat until it died of
	// attrition without ever being able to fight back. 2.0 heals on arrival
	// rather than at the leash (Kevin, 2026-09-24), so one pulled again on
	// the way back is still hurt.
	const bool bWasReturning = bReturning;
	if (bWasReturning)
	{
		Hp = MaxHp;
	}

	bReturning = false;
	bHasFightStart = false;
	GaveUpOn = nullptr;
	MovePath.Reset();

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	if (bWasReturning)
	{
		UE_LOG(LogValhallaCombat, Log, TEXT("%s (%s) is back at %s and healed to %.0f."),
			*DisplayName, *GetName(), *GetActorLocation().ToCompactString(), MaxHp);
	}
}

void AValhallaNPC::TurnToward(const FVector& Point, float DeltaSeconds)
{
	const FVector To = Point - GetActorLocation();
	if (To.SizeSquared2D() < 1.0)
	{
		return;
	}

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float RatePerSecond = Movement ? Movement->RotationRate.Yaw : 540.f;

	FRotator Rotation = GetActorRotation();
	const float Desired = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)));
	const float NewYaw = FMath::FixedTurn(Rotation.Yaw, Desired, RatePerSecond * DeltaSeconds);
	if (!FMath::IsNearlyEqual(FRotator::NormalizeAxis(NewYaw - Rotation.Yaw), 0.f, 0.01f))
	{
		Rotation.Yaw = NewYaw;
		SetActorRotation(Rotation);
	}
}

bool AValhallaNPC::HasLineOfSightTo(const AActor* Target) const
{
	const UWorld* World = GetWorld();
	if (!World || !Target)
	{
		return false;
	}

	// The same channel and eye height social aggro and the players' fog use,
	// so a wall or a TH thicket that hides you also stops the arrow.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ValhallaNPCSight), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(Target);
	const FVector Eye(0.f, 0.f, ValhallaEyeHeight);
	return !World->LineTraceTestByChannel(
		GetActorLocation() + Eye, Target->GetActorLocation() + Eye, ValhallaVisionBlockerChannel, Params);
}

// ─────────────────────────────────────────────────────────────────────────────
//  B-10 — social aggro
// ─────────────────────────────────────────────────────────────────────────────

int32 AValhallaNPC::CallForHelp(AActor* Target)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || !Target)
	{
		return 0;
	}

	const FName Group = GetSocialGroup();
	const double Range = Template.SocialRange > 0.f ? Template.SocialRange
		: (Template.AggroRange > 0.f ? Template.AggroRange : DefaultAggroRange);
	const FVector MyLocation = GetActorLocation();

	int32 Joined = 0;
	for (TActorIterator<AValhallaNPC> It(World); It; ++It)
	{
		AValhallaNPC* Other = *It;
		if (!Other || Other == this)
		{
			continue;
		}

		FValhallaSocialAggro::FCandidate Candidate;
		Candidate.Group = Other->GetSocialGroup();
		Candidate.bAlive = Other->IsAlive();
		Candidate.bCanAggro = Other->CanEverAggro();
		Candidate.bEngaged = Other->IsEngaged();
		Candidate.DistanceSq = FVector::DistSquared2D(MyLocation, Other->GetActorLocation());

		// The cheap tests first; the trace only for NPCs that would otherwise join.
		Candidate.bLineOfSight = true;
		if (!FValhallaSocialAggro::ShouldAnswer(Group, Range, Candidate))
		{
			continue;
		}

		// Sight blockers (walls, cliffs, the TH thickets) stop the call, on the
		// same channel and at the same eye height the players' sight uses.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ValhallaSocialAggro), /*bTraceComplex*/ false);
		Params.AddIgnoredActor(this);
		Params.AddIgnoredActor(Other);
		const FVector Eye(0.f, 0.f, ValhallaEyeHeight);
		Candidate.bLineOfSight = !World->LineTraceTestByChannel(
			MyLocation + Eye, Other->GetActorLocation() + Eye, ValhallaVisionBlockerChannel, Params);
		if (!FValhallaSocialAggro::ShouldAnswer(Group, Range, Candidate))
		{
			continue;
		}

		Other->JoinFight(Target, this);
		++Joined;
	}

	if (Joined > 0)
	{
		UE_LOG(LogValhallaCombat, Log, TEXT("%s (%s) calls for help against %s: %d %s joined (group %s, %.0f cm)."),
			*DisplayName, *GetName(), *UValhallaCombatLibrary::GetDisplayName(Target), Joined,
			Joined == 1 ? TEXT("NPC") : TEXT("NPCs"), *Group.ToString(), Range);
	}
	return Joined;
}

void AValhallaNPC::JoinFight(AActor* Target, const AValhallaNPC* Caller)
{
	if (!HasAuthority() || !bAlive || !Target || IsEngaged() || !CanEverAggro())
	{
		return;
	}

	// The same token threat a proximity pull seeds (UpdateAggro), so whoever
	// actually hits this NPC takes it over.
	ThreatTable.Add(Target, 1.f);
	AggroTarget = Target;

	UE_LOG(LogValhallaCombat, Log, TEXT("%s (%s) answers %s's call for help against %s."),
		*DisplayName, *GetName(), Caller ? *Caller->GetName() : TEXT("?"), *UValhallaCombatLibrary::GetDisplayName(Target));
}

// ─────────────────────────────────────────────────────────────────────────────
//  B-16 — steering along a nav-mesh path
// ─────────────────────────────────────────────────────────────────────────────

FVector AValhallaNPC::PlanAndSteer(const FVector& Goal, double Now)
{
	const FVector From = GetActorLocation();

	if (FValhallaNPCPath::NeedsReplan(MovePath, Goal, Now))
	{
		UWorld* World = GetWorld();
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		const ANavigationData* NavData = Nav ? Nav->GetNavDataForProps(GetNavAgentPropertiesRef(), From) : nullptr;

		if (!NavData)
		{
			// No nav mesh in this world (L_GreyBox, tests): the pre-B-16 chase.
			FValhallaNPCPath::SetStraight(MovePath, FValhallaNPCPath::EMode::Fallback, Goal, Now);
		}
		else
		{
			// Clear walkable ground all the way: steer straight at the live goal,
			// which is exactly the old behaviour and keeps melee spacing exact.
			FVector HitLocation = FVector::ZeroVector;
			const bool bBlocked = NavData->Raycast(From, Goal, HitLocation, NavData->GetDefaultQueryFilter(), this);

			if (!bBlocked)
			{
				FValhallaNPCPath::SetStraight(MovePath, FValhallaNPCPath::EMode::Direct, Goal, Now);
			}
			else
			{
				FPathFindingQuery Query(this, *NavData, From, Goal, NavData->GetDefaultQueryFilter());
				Query.SetAllowPartialPaths(true);
				const FPathFindingResult Result = Nav->FindPathSync(GetNavAgentPropertiesRef(), Query);

				TArray<FVector> Points;
				if (Result.IsSuccessful() && Result.Path.IsValid())
				{
					for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
					{
						Points.Add(Point.Location);
					}
				}
				// Fewer than two points is stored as Fallback: steer straight.
				FValhallaNPCPath::SetPath(MovePath, Points, Result.IsPartial(), Goal, Now);
			}
		}
	}

	return FValhallaNPCPath::SteerDirection(MovePath, From, Goal);
}

// ─────────────────────────────────────────────────────────────────────────────
//  B-10 part 2 — idle movement: patrol routes, pairs, roaming
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::ConfigureIdleMovement(const FValhallaNPCPatrolSetup& Setup, AValhallaNPCSpawner* Leader)
{
	if (!HasAuthority())
	{
		return;
	}

	Patrol = Setup;
	FollowLeader = Leader;

	// One of the three, in the spawn point's order of precedence: a leader,
	// then a route, then roaming. A spawn point cannot lead its own NPC.
	if (Leader && Leader != Spawner.Get())
	{
		IdleMode = EValhallaNPCIdleMode::Follow;
	}
	else if (FValhallaNPCPatrolRules::HasRoute(Setup.Mode, Setup.Stops.Num()))
	{
		IdleMode = EValhallaNPCIdleMode::Route;
	}
	else if (Setup.WanderRadius > 0.f)
	{
		IdleMode = EValhallaNPCIdleMode::Wander;
	}
	else
	{
		IdleMode = EValhallaNPCIdleMode::None;
	}

	ResetIdleState();

	if (IdleMode != EValhallaNPCIdleMode::None)
	{
		FString What;
		if (IdleMode == EValhallaNPCIdleMode::Follow)
		{
			What = FString::Printf(TEXT("follows %s"), *GetNameSafe(Leader));
		}
		else if (IdleMode == EValhallaNPCIdleMode::Route)
		{
			What = FString::Printf(TEXT("%s route of %d stops"),
				Setup.Mode == EValhallaPatrolMode::Loop ? TEXT("loop") : TEXT("ping-pong"), Setup.Stops.Num());
		}
		else
		{
			What = FString::Printf(TEXT("roams %.0f cm"), Setup.WanderRadius);
		}
		UE_LOG(LogValhallaGame, Log, TEXT("%s (%s) idle: %s at %.0f cm/s, pauses %.1f-%.1f s."),
			*DisplayName, *GetName(), *What, GetIdleWalkSpeed(), Setup.PauseMinSeconds, Setup.PauseMaxSeconds);
	}
}

void AValhallaNPC::ResetIdleState()
{
	PatrolTargetIndex = 0;
	PatrolDirection = 1;
	IdlePauseUntil = 0.0;
	WanderGoal = FVector::ZeroVector;
	bHasWanderGoal = false;
	bFollowWalking = false;
}

float AValhallaNPC::GetIdleWalkSpeed() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float MaxSpeed = Movement ? Movement->MaxWalkSpeed : 60.f;
	return MaxSpeed * FMath::Clamp(Patrol.SpeedFraction, 0.1f, 1.f);
}

bool AValhallaNPC::TickIdleMovement(float FixedDeltaSeconds, double Now)
{
	switch (IdleMode)
	{
	case EValhallaNPCIdleMode::Route:
		TickPatrolRoute(Now);
		return true;
	case EValhallaNPCIdleMode::Follow:
		// With no living leader it waits at its own home: the caller's walk home.
		{
			AValhallaNPCSpawner* LeaderSpawner = FollowLeader.Get();
			const AValhallaNPC* Leader = LeaderSpawner ? LeaderSpawner->GetSpawnedNPC() : nullptr;
			if (!Leader || Leader == this || !Leader->IsAlive())
			{
				bFollowWalking = false;
				return false;
			}
		}
		TickFollow(FixedDeltaSeconds, Now);
		return true;
	case EValhallaNPCIdleMode::Wander:
		TickWander(Now);
		return true;
	default:
		return false;
	}
}

void AValhallaNPC::IdleStand()
{
	if (MovePath.Mode != FValhallaNPCPath::EMode::None || MovePath.StuckSince >= 0.0)
	{
		MovePath.Reset();
	}
}

bool AValhallaNPC::IdleWalkToward(const FVector& Goal, float InputScale, double Now)
{
	FVector ToGoal = Goal - GetActorLocation();
	ToGoal.Z = 0.0;
	const double Distance = ToGoal.Size2D();

	// The last half metre a little slower, so it settles on the stop rather
	// than stepping over it, as the walk back does. Never so slow that the
	// stuck clock (25 cm in 3 s) mistakes a stroll for being stuck.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float MaxSpeed = Movement ? Movement->MaxWalkSpeed : 60.f;
	const float MinScale = MaxSpeed > 0.f ? FMath::Min(1.f, 15.f / MaxSpeed) : 0.1f;
	const float Scale = FMath::Max(MinScale, InputScale * FMath::Clamp(static_cast<float>(Distance / 50.0), 0.4f, 1.f));

	const FVector Direction = PlanAndSteer(Goal, Now);
	AddMovementInput(Direction.IsNearlyZero() ? ToGoal.GetSafeNormal() : Direction, Scale);

	return FValhallaNPCPath::UpdateStuck(MovePath, GetActorLocation(), Now);
}

void AValhallaNPC::TickPatrolRoute(double Now)
{
	const int32 StopCount = Patrol.Stops.Num();
	if (!Patrol.Stops.IsValidIndex(PatrolTargetIndex))
	{
		PatrolTargetIndex = 0;
		PatrolDirection = 1;
	}

	if (Now < IdlePauseUntil)
	{
		IdleStand();
		return;
	}

	const FVector Stop = Patrol.Stops[PatrolTargetIndex];
	if (FValhallaNPCPatrolRules::HasArrived(GetActorLocation(), Stop))
	{
		// At a stop: stand a while, then on to the next. Stop 0 counts, so a
		// freshly spawned NPC stands its first pause on its spawn point too.
		const double Pause = FValhallaNPCPatrolRules::RollPause(Patrol.PauseMinSeconds, Patrol.PauseMaxSeconds, FMath::FRand());
		const int32 Reached = PatrolTargetIndex;
		IdlePauseUntil = Now + Pause;
		PatrolTargetIndex = FValhallaNPCPatrolRules::NextStop(Patrol.Mode, StopCount, PatrolTargetIndex, PatrolDirection);
		IdleStand();
		UE_LOG(LogValhallaGame, Verbose, TEXT("%s (%s) patrol: at stop %d, pausing %.1f s, then stop %d."),
			*DisplayName, *GetName(), Reached, Pause, PatrolTargetIndex);
		return;
	}

	if (IdleWalkToward(Stop, Patrol.SpeedFraction, Now))
	{
		// Walled in, off the nav mesh or blocked by another body: skip to the
		// next stop rather than warp (a patrol is not a leash), and stand a
		// second so a stop it can never reach does not become a grind.
		const int32 Skipped = PatrolTargetIndex;
		PatrolTargetIndex = FValhallaNPCPatrolRules::NextStop(Patrol.Mode, StopCount, PatrolTargetIndex, PatrolDirection);
		IdlePauseUntil = Now + 1.0;
		MovePath.Reset();
		UE_LOG(LogValhallaGame, Log, TEXT("%s (%s) is stuck on its patrol %.0f cm from stop %d; skipping to stop %d."),
			*DisplayName, *GetName(), FVector::Dist2D(GetActorLocation(), Stop), Skipped, PatrolTargetIndex);
	}
}

void AValhallaNPC::TickFollow(float FixedDeltaSeconds, double Now)
{
	AValhallaNPCSpawner* LeaderSpawner = FollowLeader.Get();
	const AValhallaNPC* Leader = LeaderSpawner ? LeaderSpawner->GetSpawnedNPC() : nullptr;
	if (!Leader)
	{
		IdleStand();
		return;
	}

	// The leader is fighting and this one is not (it would not be idle): hold
	// here. Social aggro, on the template, is what brings a pair in together;
	// walking after a leader into a fight it has not joined would be a pull
	// by proxy.
	if (Leader->IsEngaged())
	{
		bFollowWalking = false;
		IdleStand();
		return;
	}

	const FVector Spot = FValhallaNPCPatrolRules::FollowSpot(Leader->GetActorLocation(), Leader->GetActorRotation().Yaw);
	const double Distance = FVector::Dist2D(GetActorLocation(), Spot);
	bFollowWalking = FValhallaNPCPatrolRules::FollowerShouldMove(Distance, bFollowWalking);

	if (!bFollowWalking)
	{
		// Standing behind it: face the way it faces, as a pair standing
		// together would, and pause when it pauses.
		IdleStand();
		TurnToward(GetActorLocation() + Leader->GetActorForwardVector() * 100.0, FixedDeltaSeconds);
		return;
	}

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float MySpeed = Movement ? Movement->MaxWalkSpeed : 60.f;
	const double LeaderFraction = MySpeed > 0.f ? Leader->GetIdleWalkSpeed() / MySpeed : 1.0;
	const float Scale = static_cast<float>(FValhallaNPCPatrolRules::FollowSpeedScale(Distance, LeaderFraction));

	if (IdleWalkToward(Spot, Scale, Now))
	{
		// It cannot get behind its leader right now (a body in the way, a
		// corner): plan again and keep trying. No skip, no warp; the leader
		// walking on moves the spot anyway.
		MovePath.Reset();
	}
}

void AValhallaNPC::TickWander(double Now)
{
	if (Now < IdlePauseUntil)
	{
		IdleStand();
		return;
	}

	if (!bHasWanderGoal)
	{
		if (!PickWanderGoal(WanderGoal))
		{
			// No reachable point this time (a tiny nav island): try again after a pause.
			IdlePauseUntil = Now + FValhallaNPCPatrolRules::RollPause(Patrol.PauseMinSeconds, Patrol.PauseMaxSeconds, FMath::FRand());
			IdleStand();
			return;
		}
		bHasWanderGoal = true;
		MovePath.Reset();
	}

	if (FValhallaNPCPatrolRules::HasArrived(GetActorLocation(), WanderGoal))
	{
		bHasWanderGoal = false;
		IdlePauseUntil = Now + FValhallaNPCPatrolRules::RollPause(Patrol.PauseMinSeconds, Patrol.PauseMaxSeconds, FMath::FRand());
		IdleStand();
		return;
	}

	if (IdleWalkToward(WanderGoal, Patrol.SpeedFraction, Now))
	{
		// Pick somewhere else after a short stand.
		UE_LOG(LogValhallaGame, Verbose, TEXT("%s (%s) is stuck roaming toward %s; picking another spot."),
			*DisplayName, *GetName(), *WanderGoal.ToCompactString());
		bHasWanderGoal = false;
		IdlePauseUntil = Now + 1.0;
		MovePath.Reset();
	}
}

bool AValhallaNPC::PickWanderGoal(FVector& OutGoal) const
{
	const double Radius = Patrol.WanderRadius;
	if (Radius <= 0.0)
	{
		return false;
	}

	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const ANavigationData* NavData = Nav ? Nav->GetNavDataForProps(GetNavAgentPropertiesRef(), HomeLocation) : nullptr;

	if (!Nav || !NavData)
	{
		// No nav mesh (L_GreyBox, tests): anywhere on the disc, straight there.
		OutGoal = FValhallaNPCPatrolRules::WanderPoint(HomeLocation, Radius, FMath::FRand(), FMath::FRand());
		return true;
	}

	// Home is the capsule centre; the nav query wants a point on the mesh.
	FVector Origin = HomeLocation;
	FNavLocation HomeOnNav;
	if (Nav->ProjectPointToNavigation(HomeLocation, HomeOnNav, FVector(100.0, 100.0, 250.0)))
	{
		Origin = HomeOnNav.Location;
	}

	// Reachable, not merely inside the radius: a point across a river or
	// behind a palisade is inside the circle and a long way round.
	FNavLocation Found;
	if (Nav->GetRandomReachablePointInRadius(Origin, static_cast<float>(Radius), Found))
	{
		OutGoal = Found.Location;
		return true;
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  The state machine — NPCSystem.ts:111
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaNPC::ServerFixedTick(float FixedDeltaSeconds, double Now)
{
	if (!HasAuthority())
	{
		return;
	}

	// ── Respawn (NPCSystem.ts:124) ───────────────────────────────────────
	if (!bAlive)
	{
		if (RespawnAt > 0.0 && Now >= RespawnAt)
		{
			Respawn();
		}
		return;
	}

	// ── Aggro (NPCSystem.ts:150) ─────────────────────────────────────────
	// Only aggressive and patrol NPCs look for targets at all. A stationary or
	// passive one still keeps the threat table it was given by being hit, which
	// is how a passive NPC that is attacked fights back without wandering off.
	//
	// A leashed NPC walking back can be pulled again (Kevin, 2026-09-24), but
	// only once it is back within RepullLeashFraction of its leash range:
	// further out it would leash again on its very next step, so anything it
	// picks up out there (a hit's threat) is dropped.
	const bool bCanTakeTarget = !bReturning
		|| FValhallaNPCCombatRules::CanRepull(FVector::Dist2D(GetActorLocation(), FightStart), LeashRange);
	if (!bCanTakeTarget)
	{
		AggroTarget = nullptr;
		ThreatTable.Reset();
	}
	else if (Template.BehaviorType == EValhallaNPCBehavior::Aggressive || Template.BehaviorType == EValhallaNPCBehavior::Patrol)
	{
		UpdateAggro(Now);
	}

	AActor* Target = AggroTarget.Get();

	if (Target && bReturning)
	{
		// Pulled again on the way back. FightStart stays where the first fight
		// started, so the leash still measures from there, and it is not healed.
		bReturning = false;
		GaveUpOn = nullptr;
		MovePath.Reset();
		UE_LOG(LogValhallaCombat, Log, TEXT("%s (%s) pulled again by %s on its way back (%.0f/%.0f hp)."),
			*DisplayName, *GetName(), *UValhallaCombatLibrary::GetDisplayName(Target), Hp, MaxHp);
	}
	if (Target && !bHasFightStart)
	{
		FightStart = GetActorLocation();
		bHasFightStart = true;
	}

	// ── Social aggro (B-10) ─────────────────────────────────────────────
	// The first step of a fight: call same-group neighbours. Once per fight;
	// an NPC that answers calls its own neighbours on its next step, which is
	// how a chain spreads through a camp.
	if (!Target)
	{
		bCalledForHelp = false;
	}
	else if (!bCalledForHelp)
	{
		bCalledForHelp = true;
		if (Template.bCanSocialAggro && CanEverAggro() && UValhallaCombatLibrary::IsAliveTarget(Target))
		{
			CallForHelp(Target);
		}
	}

	if (Target)
	{
		if (!UValhallaCombatLibrary::IsAliveTarget(Target))
		{
			AggroTarget = nullptr;
			bHoldingPosition = false;
			TickBuffs(Now);
			return;
		}

		// A stationary NPC fights from where it stands: it turns to face its
		// target and attacks what is in range, but never chases or leashes.
		const bool bMobile = Template.BehaviorType != EValhallaNPCBehavior::Stationary;
		const bool bRanged = Template.AttackType == EValhallaNPCAttackType::Ranged;
		const FVector MyLocation = GetActorLocation();

		// ── Leash (NPCSystem.ts:164) ─────────────────────────────────────
		// Checked against *its own* distance from where the fight started, not
		// the target's: an NPC leashes because it has been led too far, not
		// because you are far away. 2.0 walks it back instead of teleporting.
		if (bMobile && FValhallaNPCCombatRules::IsBeyondLeash(FVector::Dist2D(MyLocation, FightStart), LeashRange))
		{
			StartReturn(TEXT("leashed"), nullptr);
			TickBuffs(Now);
			return;
		}

		FVector ToTarget = Target->GetActorLocation() - MyLocation;
		ToTarget.Z = 0.0;
		const double Distance = ToTarget.Size2D();

		// Distances in npc-templates.json are surface-to-surface, because 1.0's
		// entities were points on a plane that could stand on top of each other.
		// In 2.0 both parties are capsules, so two characters can never be closer
		// than the sum of their radii — 60 cm for the stock 30 cm capsules. A
		// template's attackRange of 40 would then be permanently unreachable and
		// the NPC would follow the player around forever without ever swinging.
		// Adding the radii back is what makes the authored numbers mean what they
		// meant in 1.0. (The player's own melee already does this: MELEE_RANGE +
		// PLAYER_COLLISION_RADIUS, SkillSystem.ts:474.)
		const double CapsuleGap = GetCapsuleComponent()->GetScaledCapsuleRadius() + [Target]() -> double
		{
			if (const ACharacter* TargetCharacter = Cast<ACharacter>(Target))
			{
				return TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
			}
			return 0.0;
		}();

		const double AttackRange = (Template.AttackRange > 0.f ? Template.AttackRange : (bRanged ? 600.f : 40.f)) + CapsuleGap;

		// A shot needs a clear line to the target (the same sight blockers that
		// stop social aggro); a swing at arm's length does not. Traced only
		// when a shot is possible at all.
		const bool bInSight = !bRanged || (Distance <= AttackRange && HasLineOfSightTo(Target));

		// ── Chase (NPCSystem.ts:177) ─────────────────────────────────────
		// Melee closes to StopChaseDistance; ranged stops once the target is in
		// range and in sight and holds there, shooting at any distance.
		const bool bChase = bMobile && FValhallaNPCCombatRules::ShouldChase(Template.AttackType, Distance,
			StopChaseDistance + CapsuleGap, AttackRange, bInSight, bHoldingPosition);
		bHoldingPosition = bRanged && !bChase;

		if (bChase)
		{
			// 1.0 wrote the position directly. Going through AddMovementInput
			// means the NPC collides with the world and with the player instead
			// of walking through both, at the same MaxWalkSpeed the template asks
			// for, so the tuning carries over and the behaviour improves.
			//
			// B-16: the direction comes off a nav-mesh path, so a wall between
			// the two is walked round rather than pushed against. The distance
			// checks above and below stay straight-line, so aggro, leash, stop
			// and attack ranges keep their authored meaning.
			const FVector Direction = PlanAndSteer(Target->GetActorLocation(), Now);
			AddMovementInput(Direction.IsNearlyZero() ? ToTarget.GetSafeNormal() : Direction, 1.f);

			// EverQuest's warp home: an NPC that cannot get to you gives up
			// rather than grinding against a wall until the leash fires. Not
			// near the target, where a pack crowding one player is normal.
			if (Distance > AttackRange + StuckIgnoreNearCm)
			{
				if (FValhallaNPCPath::UpdateStuck(MovePath, MyLocation, Now))
				{
					UE_LOG(LogValhallaCombat, Log, TEXT("%s is stuck chasing %s (%.0f cm away); giving up."),
						*DisplayName, *UValhallaCombatLibrary::GetDisplayName(Target), Distance);
					StartReturn(TEXT("gave up (stuck)"), Target);
					TickBuffs(Now);
					return;
				}
			}
			else
			{
				FValhallaNPCPath::ClearStuck(MovePath);
			}
		}
		else
		{
			// Standing its ground: orient-to-movement only turns a moving body,
			// so turn to face the target here, as a player has to.
			FValhallaNPCPath::ClearStuck(MovePath);
			TurnToward(Target->GetActorLocation(), FixedDeltaSeconds);
		}

		// ── Attack (NPCSystem.ts:200) ────────────────────────────────────
		// The same rule a player's auto-attack has: no hitting what is behind
		// you. A step that is only short on facing does not spend the attack,
		// so it lands the moment the turn above brings the target into the cone.
		const double AttackIntervalSeconds = (Template.AttackSpeedMs > 0.f ? Template.AttackSpeedMs : 1500.f) / 1000.0;
		const bool bFacing = UValhallaCombatLibrary::IsFacing(this, Target);

		if (FValhallaNPCCombatRules::CanAttack(Distance, AttackRange, bInSight, bFacing, Now, LastAttackTime + AttackIntervalSeconds))
		{
			LastAttackTime = Now;

			// The damage roll (2.0): a uniform roll in the template's
			// [minDamage, maxDamage], exactly like a player's weapon — or the
			// fixed 1.0 `damage` when the template has no range — plus, when the
			// NPC carries a weapon, that weapon's own roll on top.
			float MinDamage = 0.f, MaxDamage = 0.f;
			Template.GetDamageRange(MinDamage, MaxDamage);

			float WeaponMin = 0.f, WeaponMax = 0.f;
			bool bArmed = false;
			if (!Template.WeaponId.IsNone())
			{
				const UGameInstance* GameInstance = GetGameInstance();
				const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
				if (const FValhallaItemTemplate* Weapon = Data ? Data->FindItem(Template.WeaponId) : nullptr)
				{
					Weapon->GetDamageRange(WeaponMin, WeaponMax);
					bArmed = true;
				}
			}

			const double BaseDamage = Valhalla::Stats::RollNPCMeleeDamage(
				MinDamage, MaxDamage, FMath::FRand(), bArmed, WeaponMin, WeaponMax, FMath::FRand());

			UE_LOG(LogValhallaCombat, Log, TEXT("%s %s %s (dist %.0f <= %.0f, every %.0f ms) weapon=%s roll=%.1f"),
				*DisplayName, bRanged ? TEXT("shoots") : TEXT("attacks"),
				*UValhallaCombatLibrary::GetDisplayName(Target), Distance, AttackRange, Template.AttackSpeedMs,
				bArmed ? *Template.WeaponId.ToString() : TEXT("none"), BaseDamage);

			// The same pipeline a player's swing goes through, so an NPC's hit
			// can miss, be dodged, be blocked and be absorbed by a shield —
			// which in 1.0 it could not, because NPCSystem subtracted raw HP.
			UValhallaCombatLibrary::ApplyDamage(this, Target, BaseDamage, /*bMagical=*/false, TemplateId);

			if (!UValhallaCombatLibrary::IsAliveTarget(Target))
			{
				AggroTarget = nullptr;
			}
		}
	}
	else
	{
		bHoldingPosition = false;

		// ── Idle movement (B-10 part 2) ──────────────────────────────────
		// A patrol route, a leader to follow or a roam: only once any fight is
		// over *and* walked back from, so the leash, the heal on arrival and
		// the re-pull rules below are exactly what they were. A pulled patrol
		// walks back to where it was pulled (FightStart) and then picks the
		// route up at the stop it was heading for. False: no idle movement (or
		// a follower whose leader is dead), so it stands at home as before.
		if (!bHasFightStart && !bReturning && TickIdleMovement(FixedDeltaSeconds, Now))
		{
			TickBuffs(Now);
			return;
		}

		// ── Walk back (NPCSystem.ts:239) ─────────────────────────────────
		// To where the fight started (on its route, for a patrol), or home when
		// there was no fight. A leashed NPC goes at full speed and heals when it
		// gets there; one whose fight simply ended strolls back at 2/3.
		const FVector Spot = bHasFightStart ? FightStart : HomeLocation;
		FVector ToSpot = Spot - GetActorLocation();
		ToSpot.Z = 0.0;
		const double DistanceBack = ToSpot.Size2D();

		if (DistanceBack > FValhallaNPCCombatRules::ReturnArrivedCm)
		{
			// B-16: back by a path too, round whatever it chased you past. The
			// last half metre is taken slower so a full-speed return settles on
			// the spot instead of stepping over it.
			const FVector Direction = PlanAndSteer(Spot, Now);
			const float Speed = (bReturning ? 1.f : ReturnSpeedFraction) * FMath::Clamp(static_cast<float>(DistanceBack / 50.0), 0.2f, 1.f);
			AddMovementInput(Direction.IsNearlyZero() ? ToSpot.GetSafeNormal() : Direction, Speed);

			// The one teleport left: a return that makes no progress for 3 s
			// (walled in, off the nav mesh) would otherwise stand there forever.
			if (FValhallaNPCPath::UpdateStuck(MovePath, GetActorLocation(), Now))
			{
				UE_LOG(LogValhallaCombat, Log, TEXT("%s (%s) is stuck walking back (%.0f cm away); warping there."),
					*DisplayName, *GetName(), DistanceBack);
				SetActorLocation(Spot, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
				FinishReturn();
			}
		}
		else if (bHasFightStart || bReturning)
		{
			FinishReturn();
		}
		else if (MovePath.Mode != FValhallaNPCPath::EMode::None)
		{
			MovePath.Reset();
		}
	}

	// ── Buffs (NPCSystem.ts:266) ─────────────────────────────────────────
	TickBuffs(Now);
}
