// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaTypes.h"
#include "ValhallaVisibilitySubsystem.h"
#include "ValhallaVisuals.h"

namespace
{
	/** The `BaseColor` parameter on M_ValhallaToon, which is what the skin tint writes. */
	const FName BaseColorParameter(TEXT("BaseColor"));

	/** Which section of the body mesh is skin. Section 0 is `M_Skin`; 1 is `M_Hair`. */
	constexpr int32 SkinMaterialIndex = 0;

	/**
	 * Configure one follower: no collision, no shadow of its own, and the body's
	 * pose rather than an animation.
	 *
	 * The leader pose link is what makes the paperdoll a paperdoll. Without it
	 * each piece would need its own anim instance playing the same sequence at
	 * the same time, which is seven extra pose evaluations per character and
	 * seven chances to be a frame out of step with the body.
	 */
	void SetUpFollower(USkeletalMeshComponent* Follower, USkeletalMeshComponent* Leader)
	{
		if (!Follower)
		{
			return;
		}

		Follower->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Follower->SetCollisionProfileName(TEXT("NoCollision"));
		Follower->SetGenerateOverlapEvents(false);
		// The body already casts the silhouette; seven more shadow casters for
		// one character is seven times the cost for no visible difference.
		Follower->SetCastShadow(false);
		Follower->bUseAttachParentBound = true;
		Follower->SetLeaderPoseComponent(Leader);
	}

	/**
	 * The 1.0 spelling of an equip slot, which is what items.json uses and what
	 * the Phase 4c gate greps the log for. Not `UEnum::GetDisplayValueAsText`:
	 * that returns the UMETA display name, which is capitalised, and the point
	 * of this line is that it matches the data.
	 */
	FString EquipSlotName(EValhallaEquipSlot Slot)
	{
		switch (Slot)
		{
		case EValhallaEquipSlot::Weapon:  return TEXT("weapon");
		case EValhallaEquipSlot::Offhand: return TEXT("offhand");
		case EValhallaEquipSlot::Helm:    return TEXT("helm");
		case EValhallaEquipSlot::Chest:   return TEXT("chest");
		case EValhallaEquipSlot::Legs:    return TEXT("legs");
		case EValhallaEquipSlot::Boots:   return TEXT("boots");
		case EValhallaEquipSlot::Gloves:  return TEXT("gloves");
		case EValhallaEquipSlot::Back:    return TEXT("back");
		case EValhallaEquipSlot::Ring:    return TEXT("ring");
		default:                          return TEXT("none");
		}
	}

	/** Configure a held prop: rigid, socket-attached, no collision. */
	void SetUpProp(UStaticMeshComponent* Prop)
	{
		if (!Prop)
		{
			return;
		}

		Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Prop->SetCollisionProfileName(TEXT("NoCollision"));
		Prop->SetGenerateOverlapEvents(false);
		Prop->SetCastShadow(false);
		// A held prop is small and always moving, so it has no business in the
		// distance-field scene; and the bow's flat mesh gives a degenerate
		// distance-field volume that spams "non-invertible matrix" errors every
		// frame it is held (seen since the bow was first equipped on 09-23).
		Prop->bAffectDistanceFieldLighting = false;
	}
}

AValhallaCharacter::AValhallaCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// ── Rotation: motion, or the camera drag ────────────────────────────
	// WASD faces the way the character walks (orient-to-movement, predicted
	// and replicated by CharacterMovement). The controller rotation is not a
	// source: the right-mouse drag writes the yaw directly through
	// SetFacingYawFromCamera / ServerSetFacingYaw, and only while standing.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->bUseControllerDesiredRotation = false;
	Movement->RotationRate = FRotator(0.f, FacingTurnRateDegrees, 0.f);

	// ── No jumping, no crouching ────────────────────────────────────────
	// 1.0 is a flat 2D plane; there is no vertical input anywhere in the game.
	JumpMaxCount = 0;
	JumpMaxHoldTime = 0.f;
	Movement->JumpZVelocity = 0.f;
	Movement->GetNavAgentPropertiesRef().bCanJump = false;
	Movement->GetNavAgentPropertiesRef().bCanCrouch = false;
	Movement->GetNavAgentPropertiesRef().bCanSwim = false;
	Movement->GetNavAgentPropertiesRef().bCanFly = false;

	// ── Movement feel ───────────────────────────────────────────────────
	// MovementSystem.ts applies `speed * dt` with no acceleration curve at all:
	// a key press is full speed on the next tick. High acceleration and braking
	// reproduce that inside CharacterMovement without leaving prediction behind.
	Movement->MaxAcceleration = 8192.f;
	Movement->BrakingDecelerationWalking = 8192.f;
	Movement->GroundFriction = 12.f;
	Movement->bUseSeparateBrakingFriction = false;
	// Placeholder until ApplyClassAppearance reads classes.json.
	Movement->MaxWalkSpeed = 110.f;

	// The simulated proxies' facing only looks right if the replicated yaw is
	// finer than the default byte quantisation (~1.4 degrees).
	FRepMovement& RepMovementSettings = GetReplicatedMovement_Mutable();
	RepMovementSettings.RotationQuantizationLevel = ERotatorQuantization::ShortComponents;

	// ── Camera ──────────────────────────────────────────────────────────
	// The boom uses an absolute rotation: the actor turns as it walks, and the
	// camera must not spin with it or the world would swim around. Only the
	// right-mouse drag turns the camera (and the body with it).
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 1500.f;
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-45.f, -45.f, 0.f));
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;
	// A long boom plus a narrow FOV flattens the perspective until it reads as
	// the isometric projection 1.0 rendered with an orthographic camera.
	TopDownCamera->FieldOfView = 35.f;

	// ── Body ────────────────────────────────────────────────────────────
	// ACharacter's own mesh is the leader. Both numbers below are measured from
	// the import rather than guessed: the body mesh's bounds run Z 0..120 with
	// the origin at 60, so dropping it by the capsule half-height stands it on
	// the capsule's bottom cap; and `socket_back` sits at component-space
	// Y = -14, so the model faces +Y and needs -90 of yaw to face the actor's
	// +X. See UValhallaVisuals::MeshZOffset / MeshYaw.
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

	// ── Head ────────────────────────────────────────────────────────────
	// Only the MetaHuman body has a separate head; it follows the body by
	// leader pose and casts its own shadow (it is part of the silhouette).
	HeadMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(BodyMesh);
	SetUpFollower(HeadMesh, BodyMesh);
	HeadMesh->SetCastShadow(true);
	if (!UValhallaVisuals::ActiveHeadMeshPath().IsEmpty())
	{
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> HeadAsset(*UValhallaVisuals::ActiveHeadMeshPath());
		if (HeadAsset.Succeeded())
		{
			HeadMesh->SetSkeletalMeshAsset(HeadAsset.Object);
		}
	}

	// ── Paperdoll followers ─────────────────────────────────────────────
	// Created empty. RefreshEquipmentVisuals fills them from the player state's
	// nine Equip* fields; a slot with nothing in it keeps a null mesh and draws
	// nothing, which is cheaper than a hidden component.
	const TCHAR* FollowerNames[] = {
		TEXT("HairMesh"), TEXT("HelmMesh"), TEXT("ChestMesh"), TEXT("LegsMesh"),
		TEXT("GlovesMesh"), TEXT("BootsMesh"), TEXT("BackMesh")
	};
	TObjectPtr<USkeletalMeshComponent>* Followers[] = {
		&HairMesh, &HelmMesh, &ChestMesh, &LegsMesh, &GlovesMesh, &BootsMesh, &BackMesh
	};

	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Followers)); ++Index)
	{
		USkeletalMeshComponent* Follower =
			CreateDefaultSubobject<USkeletalMeshComponent>(FollowerNames[Index]);
		Follower->SetupAttachment(BodyMesh);
		SetUpFollower(Follower, BodyMesh);
		*Followers[Index] = Follower;
	}

	// ── Held props ──────────────────────────────────────────────────────
	// Socketed to bones of the shared skeleton, so they inherit the animated
	// hand transforms without being skinned.
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(BodyMesh, UValhallaVisuals::WeaponSocket());
	SetUpProp(WeaponMesh);

	OffhandMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OffhandMesh"));
	OffhandMesh->SetupAttachment(BodyMesh, UValhallaVisuals::OffhandSocket());
	SetUpProp(OffhandMesh);

	// ── Animation ───────────────────────────────────────────────────────
	AnimComponent = CreateDefaultSubobject<UValhallaAnimComponent>(TEXT("AnimComponent"));

	// ── Skills ──────────────────────────────────────────────────────────
	// On the character, not the player state, because casting is range-checked
	// and interrupted by movement — both of which need a body. See the component.
	SkillComponent = CreateDefaultSubobject<UValhallaSkillComponent>(TEXT("SkillComponent"));

	// The cursor trace has to be able to pick a player out of the world.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	bReplicates = true;
	SetReplicateMovement(true);

	// ── Phase 5 relevancy ───────────────────────────────────────────────
	//
	// Not always relevant, and effectively never distance-culled: the two
	// together hand the whole decision to IsNetRelevantFor below. The cull
	// distance is left enormous rather than set to a ranger's 1800 cm because
	// NetCullDistanceSquared is a *3D* radius applied before the override runs,
	// and a party member three zones up a hill must not drop out of a party
	// because of a number that knows nothing about parties.
	bAlwaysRelevant = false;
	SetNetCullDistanceSquared(1.0e12f);
}

bool AValhallaCharacter::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& /*SrcLocation*/) const
{
	return UValhallaVisibilitySubsystem::IsRelevantForViewer(this, RealViewer, ViewTarget);
}

void AValhallaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AValhallaCharacter, bDeathPresentation);
}

void AValhallaCharacter::OnRep_DeathPresentation()
{
	SetDeathPresentation(bDeathPresentation);
}

void AValhallaCharacter::SetDeathPresentation(bool bDead)
{
	if (HasAuthority())
	{
		bDeathPresentation = bDead;
	}

	// The body stays on screen and plays `A_Death`, holding its last frame.
	// Phase 2b hid the actor instead, for want of a death pose to hold.
	if (AnimComponent)
	{
		AnimComponent->SetDead(bDead);
	}

	SetActorEnableCollision(!bDead);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (bDead)
		{
			// Stop first, then disable: disabling a movement mode that still has
			// velocity leaves the capsule drifting with no way to stop it.
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		else
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
}

void AValhallaCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Before BeginPlay, so the anim component starts on the right skeleton.
	if (UValhallaVisuals::ApplyActiveBody(BodyMesh))
	{
		UValhallaVisuals::ApplyActiveHead(HeadMesh, BodyMesh);
		UValhallaVisuals::AttachHeldProp(WeaponMesh, BodyMesh, EValhallaGrip::OneHand);
		UValhallaVisuals::AttachHeldProp(OffhandMesh, BodyMesh, EValhallaGrip::Shield);
	}
}

void AValhallaCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (AnimComponent)
	{
		AnimComponent->SetBodyMesh(BodyMesh);
	}

	ApplyClassAppearance();
	RefreshEquipmentVisuals();
}

void AValhallaCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server side: the player state is assigned by now, so the class is known.
	ApplyClassAppearance();
	RefreshEquipmentVisuals();
}

void AValhallaCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Client side: this is the first moment ClassId and the nine Equip* fields
	// are readable, and the equipment OnReps that fired before the player state
	// was attached to this pawn had no pawn to draw on.
	ApplyClassAppearance();
	RefreshEquipmentVisuals();
}

AValhallaPlayerState* AValhallaCharacter::GetValhallaPlayerState() const
{
	return GetPlayerState<AValhallaPlayerState>();
}

bool AValhallaCharacter::IsAlive() const
{
	const AValhallaPlayerState* ValhallaPS = GetValhallaPlayerState();
	return ValhallaPS ? ValhallaPS->IsAlive() : false;
}

float AValhallaCharacter::GetCameraWorldYaw() const
{
	return CameraBoom ? static_cast<float>(CameraBoom->GetComponentRotation().Yaw) : -45.f;
}

void AValhallaCharacter::AddCameraOrbit(float DeltaYawDegrees, float DeltaPitchDegrees)
{
	if (!CameraBoom)
	{
		return;
	}

	// The boom uses an absolute rotation, so its relative rotation *is* its
	// world rotation — the actor turning as it walks never drags it.
	FRotator Rotation = CameraBoom->GetRelativeRotation();
	Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + DeltaYawDegrees);
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + DeltaPitchDegrees, CameraPitchMin, CameraPitchMax);
	Rotation.Roll = 0.f;
	CameraBoom->SetRelativeRotation(Rotation);
}

void AValhallaCharacter::AddCameraZoom(float Steps)
{
	if (!CameraBoom || FMath::IsNearlyZero(Steps))
	{
		return;
	}

	CameraBoom->TargetArmLength = FMath::Clamp(
		CameraBoom->TargetArmLength + Steps * CameraArmStep, CameraArmMin, CameraArmMax);
}

void AValhallaCharacter::ApplyClassAppearance()
{
	const AValhallaPlayerState* ValhallaPS = GetValhallaPlayerState();
	if (!ValhallaPS || ValhallaPS->ClassId.IsNone())
	{
		return;
	}

	if (AppliedClassId == ValhallaPS->ClassId)
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	if (!Data)
	{
		return;
	}

	const FValhallaClassTemplate* ClassTemplate = Data->FindClass(ValhallaPS->ClassId);
	if (!ClassTemplate)
	{
		UE_LOG(LogValhallaGame, Warning, TEXT("ApplyClassAppearance: unknown class '%s'"), *ValhallaPS->ClassId.ToString());
		return;
	}

	AppliedClassId = ValhallaPS->ClassId;

	// classes.json `baseSpeed` is in 1.0 pixels per second on a 64 px tile; the
	// grassland kit's tiles are 64 cm, so the number carries over unchanged and
	// the conversion factor is exactly 1. This is the one place it is applied.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = ClassTemplate->BaseSpeed;
		Movement->MaxWalkSpeedCrouched = ClassTemplate->BaseSpeed;
	}

	// ── Skin ────────────────────────────────────────────────────────────
	// The class's `bodyId` picks one of the five 1.0 skin tones. Nothing in
	// classes.json names one today, so every class is `body_fair` — which is
	// also what 1.0 rendered a character with no body chosen as, so the
	// fallback is the 1.0 behaviour rather than a placeholder. External bodies
	// (the MetaHuman) carry their own baked skin and are not tinted.
	if (BodyMesh && BodyMesh->GetSkeletalMeshAsset() && !UValhallaVisuals::UseExternalBody())
	{
		if (!SkinMaterial)
		{
			if (UMaterialInterface* Source = BodyMesh->GetMaterial(SkinMaterialIndex))
			{
				SkinMaterial = UMaterialInstanceDynamic::Create(Source, this);
				BodyMesh->SetMaterial(SkinMaterialIndex, SkinMaterial);
			}
		}

		if (SkinMaterial)
		{
			SkinMaterial->SetVectorParameterValue(
				BaseColorParameter, UValhallaVisuals::SkinTintForBodyId(ClassTemplate->BodyId));
		}
	}

	// The action bar's default loadout is `classSkills[classId]`, so this is the
	// first moment the server can fill it in. BeginPlay is too early: the player
	// state may not have been assigned yet.
	if (HasAuthority() && SkillComponent)
	{
		SkillComponent->InitializeActionBarFromClass();
	}

	UE_LOG(LogValhallaGame, Verbose, TEXT("%s: class '%s' applied, MaxWalkSpeed=%.0f"),
		*GetName(), *AppliedClassId.ToString(), ClassTemplate->BaseSpeed);
}

bool AValhallaCharacter::IsWalkingForFacing() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return false;
	}

	// Input first: acceleration is what orient-to-movement turns towards, so
	// while there is any, CharacterMovement is (or is about to be) writing the
	// yaw. Velocity second, for the frame or two of braking after a key lifts.
	return !Movement->GetCurrentAcceleration().IsNearlyZero()
		|| GetVelocity().SizeSquared2D() > FMath::Square(FacingStillSpeed);
}

void AValhallaCharacter::SetFacingYawFromCamera(float NewYaw)
{
	if (!IsLocallyControlled() || bDeathPresentation || IsWalkingForFacing())
	{
		// Walking: orient-to-movement wins. The drag still turns the camera,
		// and with it the WASD basis, so the walk bends round with the view.
		return;
	}

	NewYaw = FRotator::NormalizeAxis(NewYaw);

	// Apply locally first: the local player's own turn must never wait on a
	// round trip.
	if (!FMath::IsNearlyEqual(FRotator::NormalizeAxis(GetActorRotation().Yaw - NewYaw), 0.f, 0.01f))
	{
		SetActorRotation(FRotator(0.f, NewYaw, 0.f));
	}

	if (HasAuthority())
	{
		// A listen-server host is already authoritative — nothing to send.
		return;
	}

	bFacingSendPending = true;
	FlushFacingYaw(false);
}

void AValhallaCharacter::FlushFacingYaw(bool bForce)
{
	if (!bFacingSendPending || !IsLocallyControlled() || HasAuthority())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Yaw = FRotator::NormalizeAxis(GetActorRotation().Yaw);

	// Two gates, from the 1.0 input cadence: don't send what the server cannot
	// tell apart (2 degrees), and don't send faster than 20 Hz. The end of a
	// drag (bForce) skips the deadzone but not the need to have turned at all.
	const float Moved = FMath::Abs(FRotator::NormalizeAxis(Yaw - LastSentFacingYaw));
	if (!bForce && Moved < FacingYawDeadzoneDegrees)
	{
		return;
	}
	if (Moved < 0.01f)
	{
		bFacingSendPending = false;
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (!bForce && Now - LastFacingSendTime < (1.0 / FacingSendRateHz))
	{
		// Held back; the controller calls again next frame.
		return;
	}

	// Moves still waiting to be sent go first, so the server has already
	// stopped this character before it is told which way it now faces — a
	// late move carrying the last of the walk's acceleration would otherwise
	// turn it back towards the walk after the facing arrived.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->FlushServerMoves();
	}

	LastFacingSendTime = Now;
	LastSentFacingYaw = Yaw;
	bFacingSendPending = false;
	ServerSetFacingYaw(Yaw);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Paperdoll
// ─────────────────────────────────────────────────────────────────────────────

bool AValhallaCharacter::ApplySlotVisual(EValhallaEquipSlot Slot, FName ItemId)
{
	const int32 Index = static_cast<int32>(Slot);
	if (!AppliedEquipment.IsValidIndex(Index))
	{
		AppliedEquipment.SetNum(static_cast<int32>(EValhallaEquipSlot::Ring) + 1);
	}

	if (AppliedEquipment[Index] == ItemId)
	{
		return false;
	}
	AppliedEquipment[Index] = ItemId;

	// Which component this slot draws into. Ring has no art and no component,
	// which is a data fact rather than an omission — see EquipmentAssetPath.
	USkeletalMeshComponent* Skeletal = nullptr;
	UStaticMeshComponent* Static = nullptr;

	switch (Slot)
	{
	case EValhallaEquipSlot::Helm:    Skeletal = HelmMesh;   break;
	case EValhallaEquipSlot::Chest:   Skeletal = ChestMesh;  break;
	case EValhallaEquipSlot::Legs:    Skeletal = LegsMesh;   break;
	case EValhallaEquipSlot::Gloves:  Skeletal = GlovesMesh; break;
	case EValhallaEquipSlot::Boots:   Skeletal = BootsMesh;  break;
	case EValhallaEquipSlot::Back:    Skeletal = BackMesh;   break;
	case EValhallaEquipSlot::Weapon:  Static = WeaponMesh;   break;
	case EValhallaEquipSlot::Offhand: Static = OffhandMesh;  break;
	default:                          return true;  // Ring, None.
	}

	// `weapon`, `chest`, … — the 1.0 spelling of the slot, which is what
	// items.json uses and what the gate log is grepped for.
	const FString SlotName = EquipSlotName(Slot);

	if (ItemId.IsNone())
	{
		if (Skeletal) { Skeletal->SetSkeletalMeshAsset(nullptr); }
		if (Static)   { Static->SetStaticMesh(nullptr); }
		UE_LOG(LogValhallaVisual, Log, TEXT("slot=%s asset=<none>"), *SlotName);
		return true;
	}

	bool bIsSkeletal = true;
	const FString AssetPath = UValhallaVisuals::EquipmentAssetPathForItem(this, ItemId, bIsSkeletal);

	if (AssetPath.IsEmpty())
	{
		if (Skeletal) { Skeletal->SetSkeletalMeshAsset(nullptr); }
		if (Static)   { Static->SetStaticMesh(nullptr); }
		UE_LOG(LogValhallaVisual, Log, TEXT("slot=%s item=%s asset=<no art>"),
			*SlotName, *ItemId.ToString());
		return true;
	}

	if (bIsSkeletal && Skeletal)
	{
		USkeletalMesh* LoadedMesh = LoadObject<USkeletalMesh>(nullptr, *AssetPath);
		if (!LoadedMesh)
		{
			UE_LOG(LogValhallaVisual, Warning, TEXT("slot=%s item=%s asset=%s MISSING"),
				*SlotName, *ItemId.ToString(), *AssetPath);
			return true;
		}

		// A piece skinned to another rig (today: every piece, all built for
		// SK_Valhalla_Skeleton) cannot follow the active body. It is left off
		// until it is rebuilt for this body's skeleton; then it simply appears.
		if (!UValhallaVisuals::CanFollowBody(LoadedMesh, BodyMesh))
		{
			Skeletal->SetSkeletalMeshAsset(nullptr);
			UE_LOG(LogValhallaVisual, Log, TEXT("slot=%s item=%s asset=%s hidden (built for another skeleton)"),
				*SlotName, *ItemId.ToString(), *AssetPath);
			return true;
		}

		Skeletal->SetSkeletalMeshAsset(LoadedMesh);
		// Re-established every time the mesh changes: SetSkeletalMeshAsset
		// rebuilds the component's instance data and drops the link, and a
		// follower that has lost it renders its own reference pose — a T-posed
		// breastplate standing next to a walking character.
		Skeletal->SetLeaderPoseComponent(BodyMesh);
		UE_LOG(LogValhallaVisual, Log, TEXT("slot=%s item=%s asset=%s"),
			*SlotName, *ItemId.ToString(), *AssetPath);
	}
	else if (!bIsSkeletal && Static)
	{
		UStaticMesh* LoadedMesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
		if (!LoadedMesh)
		{
			UE_LOG(LogValhallaVisual, Warning, TEXT("slot=%s item=%s asset=%s MISSING"),
				*SlotName, *ItemId.ToString(), *AssetPath);
			return true;
		}

		Static->SetStaticMesh(LoadedMesh);

		// Per-socket correction. socket_weapon_r is already aligned with the way
		// the props are modelled, so a sword needs nothing; socket_offhand_l is
		// an identity bone and needs a roll to stand a shield on edge. See
		// UValhallaVisuals::OffhandSocketRoll for the measurement.
		FRotator PropRotation = FRotator::ZeroRotator;
		if (UValhallaVisuals::UseExternalBody())
		{
			// External body: the grip decides bone and frame (a bow moves to the left hand).
			UValhallaVisuals::AttachHeldProp(Static, BodyMesh, Static == OffhandMesh
				? EValhallaGrip::Shield
				: UValhallaVisuals::GripForWeapon(this, ItemId));
			PropRotation = Static->GetRelativeRotation();
		}
		else if (Static == OffhandMesh)
		{
			PropRotation.Roll = UValhallaVisuals::OffhandSocketRoll;
		}
		else if (UValhallaVisuals::AttackCycleForEquippedWeapon(this, ItemId) == EValhallaAttackCycle::Shoot)
		{
			// A bow in the right hand, which is where the weapon slot puts it.
			PropRotation.Pitch = UValhallaVisuals::BowWeaponSocketPitch;
		}
		Static->SetRelativeRotation(PropRotation);

		UE_LOG(LogValhallaVisual, Log, TEXT("slot=%s item=%s asset=%s"),
			*SlotName, *ItemId.ToString(), *AssetPath);
	}

	return true;
}

void AValhallaCharacter::RefreshEquipmentVisuals()
{
	const AValhallaPlayerState* ValhallaPS = GetValhallaPlayerState();
	if (!ValhallaPS)
	{
		// Before the player state replicates there is nothing to draw. The
		// OnRep that brings it will call back here.
		return;
	}

	static const EValhallaEquipSlot Slots[] = {
		EValhallaEquipSlot::Helm, EValhallaEquipSlot::Chest, EValhallaEquipSlot::Legs,
		EValhallaEquipSlot::Gloves, EValhallaEquipSlot::Boots, EValhallaEquipSlot::Back,
		EValhallaEquipSlot::Weapon, EValhallaEquipSlot::Offhand, EValhallaEquipSlot::Ring,
	};

	for (const EValhallaEquipSlot Slot : Slots)
	{
		ApplySlotVisual(Slot, ValhallaPS->GetEquipped(Slot));
	}

	// ── Hair ────────────────────────────────────────────────────────────
	// A helm replaces the hair rather than being worn over it. That is 1.0's
	// rule (the paperdoll drew helm *instead of* hair, never both) and it is
	// also why the art ships a bald cap: a hood has to sit on something.
	const bool bHasHelm = !ValhallaPS->GetEquipped(EValhallaEquipSlot::Helm).IsNone();
	if (HairMesh)
	{
		if (!HairMesh->GetSkeletalMeshAsset())
		{
			const FString HairPath = UValhallaVisuals::HairMeshPath(TEXT("SK_Hair_Brown_Short"));
			USkeletalMesh* Hair = LoadObject<USkeletalMesh>(nullptr, *HairPath);
			// Same rule as armour: hair built for another rig stays off until
			// it is rebuilt for the active body.
			if (Hair && UValhallaVisuals::CanFollowBody(Hair, BodyMesh))
			{
				HairMesh->SetSkeletalMeshAsset(Hair);
				HairMesh->SetLeaderPoseComponent(BodyMesh);
				UE_LOG(LogValhallaVisual, Log, TEXT("slot=hair asset=%s"), *HairPath);
			}
		}
		HairMesh->SetVisibility(!bHasHelm);
	}

	// ── Attack cycle ────────────────────────────────────────────────────
	// paperdoll.ts:48 — the *weapon* decides which attack animation plays, so
	// this has to be re-read whenever the weapon slot changes.
	if (AnimComponent)
	{
		const FName WeaponId = ValhallaPS->GetEquipped(EValhallaEquipSlot::Weapon);
		AnimComponent->SetAttackCycle(UValhallaVisuals::AttackCycleForEquippedWeapon(this, WeaponId));

		// What the hands hold decides the stance: a fist around the weapon, a
		// fist around a bow (left), a raised shield arm. A bow takes the left
		// hand, so a shield is not carried while one is held.
		const bool bHasWeapon = !WeaponId.IsNone();
		const EValhallaGrip Grip = UValhallaVisuals::GripForWeapon(this, WeaponId);
		const bool bBow = bHasWeapon && Grip == EValhallaGrip::Bow;
		// A bow and a staff need both hands (the staff attack is a two-handed
		// thrust), so a shield is neither carried nor drawn while one is held.
		const bool bTwoHanded = bHasWeapon && (bBow || Grip == EValhallaGrip::Staff);
		const bool bShield = !bTwoHanded && !ValhallaPS->GetEquipped(EValhallaEquipSlot::Offhand).IsNone();
		AnimComponent->SetWeaponLoadout(UValhallaVisuals::AttackAnimForWeapon(this, WeaponId),
			bHasWeapon && !bBow, bBow, bShield);
		if (OffhandMesh && UValhallaVisuals::UseExternalBody())
		{
			OffhandMesh->SetVisibility(!bTwoHanded);
		}
	}
}

void AValhallaCharacter::ServerSetFacingYaw_Implementation(float NewYaw)
{
	// The server is the only writer of the replicated rotation. A client may
	// turn on the spot as it likes — that is the right-mouse drag, and it costs
	// nothing a player could not do by walking a step — so the yaw is not
	// validated. What facing *gates* (IsFacing on swings and targeted casts) is
	// always read here, from this value, never from anything the client claims.
	//
	// A dead body does not turn.
	if (bDeathPresentation)
	{
		return;
	}
	SetActorRotation(FRotator(0.f, FRotator::NormalizeAxis(NewYaw), 0.f));
}
