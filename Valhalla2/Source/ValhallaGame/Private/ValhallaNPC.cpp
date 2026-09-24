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

	if (UValhallaVisuals::ApplyActiveBody(BodyMesh))
	{
		UValhallaVisuals::ApplyActiveHead(HeadMesh, BodyMesh);
		UValhallaVisuals::AttachHeldProp(WeaponMesh, BodyMesh, EValhallaGrip::OneHand);
	}
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
		// ActiveBodyScale normalises the active body to 122 cm (1 for the Valhalla body).
		BodyMesh->SetRelativeScale3D(FVector(Scale * UValhallaVisuals::ActiveBodyScale()));
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
	if (UValhallaVisuals::UseExternalBody())
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

int32 AValhallaNPC::ApplyDamageFromAttacker(AActor* Attacker, int32 Damage, double Now, bool& bOutDied)
{
	bOutDied = false;

	if (!HasAuthority() || !bAlive || Damage <= 0)
	{
		return 0;
	}

	Hp = FMath::Max(0.f, Hp - Damage);
	LastAttacker = Attacker;

	// NPCSystem.ts:286 — damage dealt is threat generated, and this is the only
	// place threat is created by combat. A `canAggro: false` NPC takes the damage
	// but never builds a table, so it never fights back.
	const bool bCanAggro = Template.bCanAggro || Template.Type == EValhallaNPCType::Enemy;
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
	const bool bCanAggro = Template.bCanAggro || Template.Type == EValhallaNPCType::Enemy;
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
	const bool bCanAggro = Template.bCanAggro || Template.Type == EValhallaNPCType::Enemy;
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

void AValhallaNPC::ResetToHome()
{
	AggroTarget = nullptr;
	ThreatTable.Reset();

	SetActorLocation(HomeLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// NPCSystem.ts:172 — a leashed NPC heals to full. Without it, a player could
	// pull an enemy, run out of leash range, and repeat until it died of
	// attrition without ever being able to fight back.
	Hp = MaxHp;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	UE_LOG(LogValhallaCombat, Log, TEXT("%s leashed: reset to %s and healed to %.0f."),
		*DisplayName, *HomeLocation.ToCompactString(), MaxHp);
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
	if (Template.BehaviorType == EValhallaNPCBehavior::Aggressive || Template.BehaviorType == EValhallaNPCBehavior::Patrol)
	{
		UpdateAggro(Now);
	}

	AActor* Target = AggroTarget.Get();

	if (Target && Template.BehaviorType != EValhallaNPCBehavior::Stationary)
	{
		if (!UValhallaCombatLibrary::IsAliveTarget(Target))
		{
			AggroTarget = nullptr;
			TickBuffs(Now);
			return;
		}

		const FVector MyLocation = GetActorLocation();

		// ── Leash (NPCSystem.ts:164) ─────────────────────────────────────
		// Checked against *its own* distance from home, not the target's: an NPC
		// leashes because it has been led too far, not because you are far away.
		FVector FromHome = MyLocation - HomeLocation;
		FromHome.Z = 0.0;
		if (FromHome.SizeSquared2D() > static_cast<double>(LeashRange) * LeashRange)
		{
			ResetToHome();
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

		// ── Chase (NPCSystem.ts:177) ─────────────────────────────────────
		if (Distance > StopChaseDistance + CapsuleGap)
		{
			// 1.0 wrote the position directly. Going through AddMovementInput
			// means the NPC collides with the world and with the player instead
			// of walking through both, at the same MaxWalkSpeed the template asks
			// for, so the tuning carries over and the behaviour improves.
			AddMovementInput(ToTarget.GetSafeNormal(), 1.f);
		}

		// ── Attack (NPCSystem.ts:200) ────────────────────────────────────
		const double AttackRange = (Template.AttackRange > 0.f ? Template.AttackRange : 40.f) + CapsuleGap;
		const double AttackIntervalSeconds = (Template.AttackSpeedMs > 0.f ? Template.AttackSpeedMs : 1500.f) / 1000.0;

		if (Distance <= AttackRange && Now >= LastAttackTime + AttackIntervalSeconds)
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

			UE_LOG(LogValhallaCombat, Log, TEXT("%s attacks %s (dist %.0f <= %.0f, every %.0f ms) weapon=%s roll=%.1f"),
				*DisplayName, *UValhallaCombatLibrary::GetDisplayName(Target), Distance, AttackRange, Template.AttackSpeedMs,
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
	else if (!Target)
	{
		// ── Walk home (NPCSystem.ts:239) ─────────────────────────────────
		FVector ToHome = HomeLocation - GetActorLocation();
		ToHome.Z = 0.0;
		const double DistanceHome = ToHome.Size2D();

		if (DistanceHome > 2.0)
		{
			AddMovementInput(ToHome.GetSafeNormal(), ReturnSpeedFraction);
		}
	}

	// ── Buffs (NPCSystem.ts:266) ─────────────────────────────────────────
	TickBuffs(Now);
}
