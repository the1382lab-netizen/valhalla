// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaPlayerController.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaFogRenderer.h"
#include "ValhallaGame.h"
#include "ValhallaGameState.h"
#include "ValhallaGameHUDWidget.h"
#include "ValhallaHUD.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaLootBag.h"
#include "ValhallaNPC.h"
#include "ValhallaPartySubsystem.h"
#include "ValhallaPlayerState.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaVisuals.h"
#include "ValhallaZoneSubsystem.h"

namespace
{
	/**
	 * B-15 Wave 0 retired the Phase 4c black outline in favour of physically
	 * based materials and lighting. 1 puts PP_Outline back on the player camera
	 * (takes effect on the next possession), for side-by-side comparison.
	 */
	TAutoConsoleVariable<int32> CVarValhallaOutline(
		TEXT("valhalla.Visual.Outline"),
		0,
		TEXT("1: draw the Phase 4c black outline post-process on the player camera. 0 (default): off, the B-15 remaster look."),
		ECVF_Default);
}

AValhallaPlayerController::AValhallaPlayerController()
{
	// The cursor stays visible for click-to-target, loot and aoeGround skills
	// (hidden only during the right-mouse drag). It does not aim the character.
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void AValhallaPlayerController::ClientWasKicked_Implementation(const FText& KickReason)
{
	Super::ClientWasKicked_Implementation(KickReason);

	const FString Reason = KickReason.IsEmpty() ? FString(TEXT("You were disconnected by the server.")) : KickReason.ToString();
	UE_LOG(LogValhallaGame, Warning, TEXT("kicked by the server (%s): %s"),
		PlayerState ? *PlayerState->GetPlayerName() : TEXT("?"), *Reason);

	if (UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this))
	{
		Backend->SetDisconnectNotice(Reason);
	}
}

void AValhallaPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		// GameAndUI, not GameOnly: the Phase 8 action bar and inventory are UMG
		// and need to receive the same clicks the world does.
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);

		ApplyOutlinePostProcess();

		// Phase 5's fog. Spawned by the controller rather than placed in the
		// level so that a dedicated server never runs it, and owned by the
		// controller rather than the pawn so that what a player has explored
		// survives their death. Idempotent — see AValhallaFogRenderer::EnsureFor.
		AValhallaFogRenderer::EnsureFor(this);
	}
}

void AValhallaPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ApplyOutlinePostProcess();
}

void AValhallaPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);
	ApplyOutlinePostProcess();
	AValhallaFogRenderer::EnsureFor(this);
}

void AValhallaPlayerController::ApplyOutlinePostProcess()
{
	if (!IsLocalController())
	{
		// A post-process blendable is a property of *a view*, so it belongs to
		// the client that is rendering. Applying it server-side for a remote
		// player would set it on a camera nobody looks through.
		return;
	}

	AValhallaCharacter* OwnPawn = Cast<AValhallaCharacter>(GetPawn());
	UCameraComponent* Camera = OwnPawn ? OwnPawn->GetTopDownCamera() : nullptr;
	if (!Camera)
	{
		// Called again from OnPossess / AcknowledgePossession, so a controller
		// that has no pawn yet is not a failure.
		return;
	}

	UMaterialInterface* Outline = LoadObject<UMaterialInterface>(nullptr, OutlineMaterialPath);
	if (!Outline)
	{
		UE_LOG(LogValhallaVisual, Warning, TEXT("outline material %s is missing"), OutlineMaterialPath);
		return;
	}

	// Retired by default (B-15). Take it off if an earlier possession put it on.
	if (CVarValhallaOutline.GetValueOnGameThread() == 0)
	{
		Camera->PostProcessSettings.WeightedBlendables.Array.RemoveAll(
			[Outline](const FWeightedBlendable& Blendable) { return Blendable.Object == Outline; });
		return;
	}

	// Idempotent: possession can fire more than once for one pawn, and stacking
	// the same blendable would darken the line once per call. Compared by
	// object rather than by path — the same material reached twice is the same
	// pointer, and a path string would have to account for the `.Object` suffix
	// GetPathName appends.
	for (const FWeightedBlendable& Existing : Camera->PostProcessSettings.WeightedBlendables.Array)
	{
		if (Existing.Object == Outline)
		{
			return;
		}
	}

	// On the camera rather than on the camera manager, because this is the game's
	// look and not an effect: it should follow the pawn into any level, and it
	// should survive a view target change the way the boom and the FOV do. The
	// unbound PostProcessVolume in L_GreyBox carries the same material so the
	// editor viewport matches PIE; the two are idempotent with each other
	// because a volume and a camera blend the same material to the same value.
	Camera->PostProcessSettings.AddBlendable(Outline, 1.f);
	Camera->PostProcessBlendWeight = 1.f;

	UE_LOG(LogValhallaVisual, Log, TEXT("outline applied to %s"), *Camera->GetPathName());
}

void AValhallaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogValhallaGame, Error,
			TEXT("InputComponent is not a UEnhancedInputComponent. Check DefaultInputComponentClass in DefaultInput.ini."));
		return;
	}

	// ── Build the mapping context and the actions ───────────────────────
	ValhallaMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Valhalla"));

	// Move: Axis2D, X = strafe (A/D), Y = forward (W/S). A keyboard key produces
	// 1.0 on X, so W and S need a YXZ swizzle to land on the forward axis, and
	// S and A need a negate. This is the whole of 1.0's `up/down/left/right`
	// input flags, expressed as one vector.
	MoveAction = NewObject<UInputAction>(this, TEXT("IA_Move"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;

	{
		// W — screen-up.
		FEnhancedActionKeyMapping& MapW = ValhallaMappingContext->MapKey(MoveAction, EKeys::W);
		UInputModifierSwizzleAxis* SwizzleW = NewObject<UInputModifierSwizzleAxis>(this);
		SwizzleW->Order = EInputAxisSwizzle::YXZ;
		MapW.Modifiers.Add(SwizzleW);

		// S — screen-down.
		FEnhancedActionKeyMapping& MapS = ValhallaMappingContext->MapKey(MoveAction, EKeys::S);
		MapS.Modifiers.Add(NewObject<UInputModifierNegate>(this));
		UInputModifierSwizzleAxis* SwizzleS = NewObject<UInputModifierSwizzleAxis>(this);
		SwizzleS->Order = EInputAxisSwizzle::YXZ;
		MapS.Modifiers.Add(SwizzleS);

		// A — screen-left.
		FEnhancedActionKeyMapping& MapA = ValhallaMappingContext->MapKey(MoveAction, EKeys::A);
		MapA.Modifiers.Add(NewObject<UInputModifierNegate>(this));

		// D — screen-right. Already (+1, 0); no modifier needed.
		ValhallaMappingContext->MapKey(MoveAction, EKeys::D);
	}

	// Action bar 1-8. One action each so the key can be rebound independently,
	// one handler for all of them (the slot comes back off the source action).
	static const FKey BarKeys[ActionBarSlots] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight,
	};

	ActionBarActions.Reset();
	ActionBarActions.Reserve(ActionBarSlots);
	for (int32 Index = 0; Index < ActionBarSlots; ++Index)
	{
		UInputAction* BarAction = NewObject<UInputAction>(this, *FString::Printf(TEXT("IA_ActionBar%d"), Index + 1));
		BarAction->ValueType = EInputActionValueType::Boolean;
		ValhallaMappingContext->MapKey(BarAction, BarKeys[Index]);
		ActionBarActions.Add(BarAction);
	}

	ToggleSkillsAction = NewObject<UInputAction>(this, TEXT("IA_ToggleSkills"));
	ToggleSkillsAction->ValueType = EInputActionValueType::Boolean;
	ValhallaMappingContext->MapKey(ToggleSkillsAction, EKeys::K);

	PrimaryClickAction = NewObject<UInputAction>(this, TEXT("IA_PrimaryClick"));
	PrimaryClickAction->ValueType = EInputActionValueType::Boolean;
	ValhallaMappingContext->MapKey(PrimaryClickAction, EKeys::LeftMouseButton);

	// Camera: hold the right button and drag to orbit, wheel to zoom. 2.0 has a
	// perspective camera that can turn, so the 1.0 right-click (attack) moved
	// onto the action bar's auto-attack skills.
	CameraOrbitAction = NewObject<UInputAction>(this, TEXT("IA_CameraOrbit"));
	CameraOrbitAction->ValueType = EInputActionValueType::Boolean;
	ValhallaMappingContext->MapKey(CameraOrbitAction, EKeys::RightMouseButton);

	CameraLookAction = NewObject<UInputAction>(this, TEXT("IA_CameraLook"));
	CameraLookAction->ValueType = EInputActionValueType::Axis2D;
	ValhallaMappingContext->MapKey(CameraLookAction, EKeys::Mouse2D);

	CameraZoomAction = NewObject<UInputAction>(this, TEXT("IA_CameraZoom"));
	CameraZoomAction->ValueType = EInputActionValueType::Axis1D;
	ValhallaMappingContext->MapKey(CameraZoomAction, EKeys::MouseWheelAxis);

	// Phase 8b: the HUD's panels. I opens the combined character + inventory
	// panel (1.0 also bound B to it; B-07 dropped that); Enter opens the chat box, and
	// Escape closes whatever is on top. While the chat box has focus the
	// controller is in UI-only input mode, so none of these (nor WASD) fire.
	InventoryAction = NewObject<UInputAction>(this, TEXT("IA_Inventory"));
	InventoryAction->ValueType = EInputActionValueType::Boolean;
	ValhallaMappingContext->MapKey(InventoryAction, EKeys::I);

	ChatAction = NewObject<UInputAction>(this, TEXT("IA_Chat"));
	ChatAction->ValueType = EInputActionValueType::Boolean;
	ValhallaMappingContext->MapKey(ChatAction, EKeys::Enter);

	EscapeAction = NewObject<UInputAction>(this, TEXT("IA_Escape"));
	EscapeAction->ValueType = EInputActionValueType::Boolean;
	ValhallaMappingContext->MapKey(EscapeAction, EKeys::Escape);

	// ── Bind ────────────────────────────────────────────────────────────
	EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AValhallaPlayerController::HandleMove);

	for (const TObjectPtr<UInputAction>& BarAction : ActionBarActions)
	{
		EnhancedInput->BindAction(BarAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandleActionBar);
	}

	EnhancedInput->BindAction(ToggleSkillsAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandleToggleSkills);
	EnhancedInput->BindAction(PrimaryClickAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandlePrimaryClick);
	EnhancedInput->BindAction(CameraOrbitAction, ETriggerEvent::Started, this, &AValhallaPlayerController::BeginCameraOrbit);
	EnhancedInput->BindAction(CameraOrbitAction, ETriggerEvent::Completed, this, &AValhallaPlayerController::EndCameraOrbit);
	EnhancedInput->BindAction(CameraOrbitAction, ETriggerEvent::Canceled, this, &AValhallaPlayerController::EndCameraOrbit);
	EnhancedInput->BindAction(CameraLookAction, ETriggerEvent::Triggered, this, &AValhallaPlayerController::HandleCameraLook);
	EnhancedInput->BindAction(CameraZoomAction, ETriggerEvent::Triggered, this, &AValhallaPlayerController::HandleCameraZoom);
	EnhancedInput->BindAction(InventoryAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandleToggleInventory);
	EnhancedInput->BindAction(ChatAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandleOpenChat);
	EnhancedInput->BindAction(EscapeAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandleEscape);

	// ── Activate ────────────────────────────────────────────────────────
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(ValhallaMappingContext, MappingContextPriority);
			UE_LOG(LogValhallaGame, Log, TEXT("Enhanced Input ready: WASD + 1-8 + K + I + Enter + Esc + mouse + RMB orbit + wheel zoom (%d actions, built in code)."),
				ActionBarActions.Num() + 9);
		}
	}
}

void AValhallaPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// The mouse does not aim (controls rework): nothing here reads the cursor.
	// A camera-drag turn the 20 Hz throttle held back goes out once it may.
	if (IsLocalController())
	{
		if (AValhallaCharacter* ValhallaPawn = Cast<AValhallaCharacter>(GetPawn()))
		{
			ValhallaPawn->FlushFacingYaw(false);
		}
	}

	// The loot window follows the bag: it closes when the bag is emptied (and
	// destroyed) or when the player walks out of reach.
	if (IsLocalController() && !OpenLootBag.IsExplicitlyNull())
	{
		const AValhallaLootBag* Bag = OpenLootBag.Get();
		if (!Bag || !Bag->IsWithinReach(GetPawn()))
		{
			CloseLootWindow();
		}
	}

	// The invite prompt expires with the server's invite (ValhallaPartyInviteExpirySeconds).
	if (IsLocalController() && !PendingPartyInviter.IsEmpty() && GetWorld()
		&& GetWorld()->GetTimeSeconds() - PendingPartyInviteAt > ValhallaPartyInviteExpirySeconds)
	{
		PendingPartyInviter.Reset();
	}
}

bool AValhallaPlayerController::SampleCursorGroundPoint()
{
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	return SampleGroundPointAtScreen(FVector2D(MouseX, MouseY));
}

bool AValhallaPlayerController::SampleGroundPointAtScreen(const FVector2D& ScreenPosition)
{
	const AValhallaCharacter* ValhallaPawn = Cast<AValhallaCharacter>(GetPawn());
	if (!ValhallaPawn)
	{
		return false;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, RayOrigin, RayDirection))
	{
		return false;
	}

	// The whole game is played on one plane, so intersect the cursor ray with
	// it analytically instead of tracing: a trace would snap the aim point to
	// whatever prop happens to be under the cursor, and a skill aimed past a
	// tree must land past the tree, not on it.
	const FVector PawnLocation = ValhallaPawn->GetActorLocation();
	const double GroundZ = PawnLocation.Z - ValhallaPawn->GetDefaultHalfHeight();

	if (FMath::IsNearlyZero(RayDirection.Z))
	{
		return false;
	}

	const double Distance = (GroundZ - RayOrigin.Z) / RayDirection.Z;
	if (Distance <= 0.0)
	{
		// Cursor is above the horizon; keep the previous point.
		return false;
	}

	AimWorldPoint = RayOrigin + RayDirection * Distance;
	return true;
}

void AValhallaPlayerController::HandleMove(const FInputActionValue& Value)
{
	// Typing in the chat box. UI-only input mode already keeps the keys away
	// from Enhanced Input; this covers the frame the mode switches on.
	if (bUiTyping)
	{
		return;
	}

	ApplyMoveInput(Value.Get<FVector2D>());
}

void AValhallaPlayerController::ApplyMoveInput(const FVector2D& Axis)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || Axis.IsNearlyZero())
	{
		return;
	}

	// Frozen by an admin. The server has also stopped the movement component;
	// this keeps the client from predicting steps the server will refuse.
	if (const AValhallaPlayerState* ValhallaPS = GetValhallaPlayerState())
	{
		if (ValhallaPS->bAdminFrozen)
		{
			return;
		}
	}

	// MovementSystem.ts:30-34 rotated the input vector 45 degrees clockwise so
	// that W walked towards the top of an isometric screen. The same rotation is
	// what the camera boom's yaw already encodes, so take it from there: the
	// input and the view can never disagree, whatever the camera is retuned to.
	float CameraYaw = -45.f;
	if (const AValhallaCharacter* ValhallaPawn = Cast<AValhallaCharacter>(ControlledPawn))
	{
		CameraYaw = ValhallaPawn->GetCameraWorldYaw();
	}

	const FRotator YawOnly(0.f, CameraYaw, 0.f);
	const FRotationMatrix YawMatrix(YawOnly);

	// AddMovementInput accumulates; CharacterMovement normalises anything longer
	// than 1, which is exactly what `normalise(isoMx, isoMy)` did in 1.0.
	ControlledPawn->AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), Axis.Y);
	ControlledPawn->AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), Axis.X);
}

void AValhallaPlayerController::HandleActionBar(const FInputActionInstance& Instance)
{
	if (bUiTyping)
	{
		return;
	}

	const UInputAction* Source = Instance.GetSourceAction();
	const int32 Index = ActionBarActions.IndexOfByPredicate(
		[Source](const TObjectPtr<UInputAction>& Candidate) { return Candidate.Get() == Source; });

	if (Index != INDEX_NONE)
	{
		OnActionBarPressed(Index + 1);
	}
}

void AValhallaPlayerController::HandlePrimaryClick()
{
	OnPrimaryClick();
}

void AValhallaPlayerController::BeginCameraOrbit()
{
	if (bCameraOrbiting)
	{
		return;
	}

	bCameraOrbiting = true;

	float MouseX = 0.f;
	float MouseY = 0.f;
	if (GetMousePosition(MouseX, MouseY))
	{
		OrbitCursorRestore = FVector2D(MouseX, MouseY);
	}

	// Hiding the cursor while the button holds the viewport's capture puts the
	// viewport into relative mouse mode, so the drag never runs out of screen.
	bShowMouseCursor = false;
}

void AValhallaPlayerController::EndCameraOrbit()
{
	if (!bCameraOrbiting)
	{
		return;
	}

	bCameraOrbiting = false;
	bShowMouseCursor = true;
	SetMouseLocation(FMath::RoundToInt(OrbitCursorRestore.X), FMath::RoundToInt(OrbitCursorRestore.Y));

	FinishCameraOrbitTurn();
}

void AValhallaPlayerController::OrbitCameraBy(float DeltaYawDegrees, float DeltaPitchDegrees)
{
	AValhallaCharacter* ValhallaPawn = Cast<AValhallaCharacter>(GetPawn());
	if (!ValhallaPawn)
	{
		return;
	}

	ValhallaPawn->AddCameraOrbit(DeltaYawDegrees, DeltaPitchDegrees);

	// The body turns with the camera: it faces the way the camera looks. Yaw
	// only — pitch is the camera's alone. A no-op while walking, when
	// orient-to-movement owns the yaw (the walk bends with the new WASD basis).
	ValhallaPawn->SetFacingYawFromCamera(ValhallaPawn->GetCameraWorldYaw());
}

void AValhallaPlayerController::FinishCameraOrbitTurn()
{
	// The drag's last few degrees may sit inside the deadzone or the throttle
	// window; the server's yaw is what the facing check reads, so send it now.
	if (AValhallaCharacter* ValhallaPawn = Cast<AValhallaCharacter>(GetPawn()))
	{
		ValhallaPawn->FlushFacingYaw(true);
		UE_LOG(LogValhallaGame, Log, TEXT("cameraOrbit end: camera yaw %.1f, facing yaw %.1f"),
			ValhallaPawn->GetCameraWorldYaw(), ValhallaPawn->GetFacingYaw());
	}
}

void AValhallaPlayerController::HandleCameraLook(const FInputActionValue& Value)
{
	if (!bCameraOrbiting)
	{
		return;
	}

	// Mouse right turns the view right; mouse up raises the camera's gaze
	// towards the horizon (MMO convention).
	const FVector2D Delta = Value.Get<FVector2D>();
	OrbitCameraBy(Delta.X * CameraOrbitDegreesPerUnit, Delta.Y * CameraOrbitDegreesPerUnit);
}

void AValhallaPlayerController::HandleCameraZoom(const FInputActionValue& Value)
{
	if (AValhallaCharacter* ValhallaPawn = Cast<AValhallaCharacter>(GetPawn()))
	{
		// Wheel up (positive) zooms in.
		ValhallaPawn->AddCameraZoom(-Value.Get<float>());
	}
}

void AValhallaPlayerController::HandleToggleSkills()
{
	if (!bUiTyping)
	{
		OnToggleSkills();
	}
}

void AValhallaPlayerController::HandleToggleInventory()
{
	if (UValhallaGameHUDWidget* Hud = GetGameHUD(); Hud && !bUiTyping)
	{
		Hud->ToggleInventory();
	}
}

void AValhallaPlayerController::HandleOpenChat()
{
	if (UValhallaGameHUDWidget* Hud = GetGameHUD(); Hud && !Hud->IsChatOpen())
	{
		Hud->OpenChat();
	}
}

void AValhallaPlayerController::HandleEscape()
{
	// B-21: Escape closes the topmost thing that is open (the options menu
	// first); with nothing open it opens the options menu.
	if (UValhallaGameHUDWidget* Hud = GetGameHUD(); Hud && !Hud->CloseTopmost())
	{
		Hud->OpenOptions();
	}
}

const UInputMappingContext* AValhallaPlayerController::GetInputMappingContext() const
{
	return ValhallaMappingContext;
}

UValhallaGameHUDWidget* AValhallaPlayerController::GetGameHUD() const
{
	const AValhallaHUD* Hud = Cast<AValhallaHUD>(GetHUD());
	return Hud ? Hud->GetGameHUD() : nullptr;
}

// ── Targeting ───────────────────────────────────────────────────────────

UValhallaSkillComponent* AValhallaPlayerController::GetSkillComponent() const
{
	// Named ValhallaPawn, not Character: AController already has a `Character`
	// member, and shadowing it is a warning this project treats as an error.
	const AValhallaCharacter* ValhallaPawn = Cast<AValhallaCharacter>(GetPawn());
	return ValhallaPawn ? ValhallaPawn->GetSkillComponent() : nullptr;
}

AActor* AValhallaPlayerController::GetCurrentTarget() const
{
	const AValhallaPlayerState* ValhallaPS = GetPlayerState<AValhallaPlayerState>();
	return ValhallaPS ? ValhallaPS->GetTargetActor() : nullptr;
}

AActor* AValhallaPlayerController::TraceForTargetUnderCursor() const
{
	// Visibility, not a ground-plane intersection: this is asking "what is under
	// the cursor", which is exactly what a visibility trace answers. The aim
	// *point* still comes from the plane maths in SampleCursorGroundPoint, because a
	// skill aimed past a tree must land past the tree.
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex=*/false, Hit))
	{
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return nullptr;
	}

	// Only the two kinds of thing that have HP are targets. Clicking a rock,
	// a wall or the floor clears the selection, as in GameScene.ts:697.
	if (HitActor->IsA<AValhallaNPC>() || HitActor->IsA<AValhallaCharacter>())
	{
		return HitActor;
	}

	return nullptr;
}

AActor* AValhallaPlayerController::FindHostileAtScreen(const FVector2D& ScreenPosition) const
{
	const APawn* ValhallaPawn = GetPawn();
	if (!ValhallaPawn)
	{
		return nullptr;
	}

	auto IsAimableHostile = [ValhallaPawn](const AActor* Actor)
	{
		return Actor && Actor != ValhallaPawn
			&& (Actor->IsA<AValhallaNPC>() || Actor->IsA<AValhallaCharacter>())
			&& UValhallaCombatLibrary::IsAliveTarget(Actor)
			&& UValhallaCombatLibrary::AreHostile(ValhallaPawn, Actor);
	};

	// The cursor is on the enemy's body.
	FHitResult Hit;
	if (GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, /*bTraceComplex=*/false, Hit)
		&& IsAimableHostile(Hit.GetActor()))
	{
		return Hit.GetActor();
	}

	// The cursor is near the enemy's body: intersect the ray with a plane at
	// each hostile's own height and keep the closest within reach. A floor-plane
	// point would land behind the enemy by (height x cot(pitch)).
	FVector RayOrigin;
	FVector RayDirection;
	if (!DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, RayOrigin, RayDirection)
		|| FMath::IsNearlyZero(RayDirection.Z))
	{
		return nullptr;
	}

	constexpr double AimSlop = 25.0;
	const double Reach = Valhalla::PlayerCollisionRadius + AimSlop;
	AActor* Best = nullptr;
	double BestDistSq = Reach * Reach;

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		if (!IsAimableHostile(Candidate))
		{
			continue;
		}

		const FVector Location = Candidate->GetActorLocation();
		const double Distance = (Location.Z - RayOrigin.Z) / RayDirection.Z;
		if (Distance <= 0.0)
		{
			continue;
		}

		const FVector OnPlane = RayOrigin + RayDirection * Distance;
		const double DistSq = FVector::DistSquared2D(OnPlane, Location);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}

	return Best;
}

void AValhallaPlayerController::ServerSetTarget_Implementation(AActor* NewTarget)
{
	AValhallaPlayerState* ValhallaPS = GetPlayerState<AValhallaPlayerState>();
	if (!ValhallaPS)
	{
		return;
	}

	// Re-check server-side: the client's trace is a suggestion, and a client
	// that asks to target a brick wall or a projectile gets nothing.
	AActor* Validated = nullptr;
	if (NewTarget && (NewTarget->IsA<AValhallaNPC>() || NewTarget->IsA<AValhallaCharacter>()))
	{
		Validated = NewTarget;
	}

	ValhallaPS->SetTargetActor(Validated);
}

void AValhallaPlayerController::ServerSetTargetByName_Implementation(const FString& MemberName)
{
	AValhallaPlayerState* ValhallaPS = GetPlayerState<AValhallaPlayerState>();
	const UValhallaPartySubsystem* Party = UValhallaPartySubsystem::Get(this);
	if (!ValhallaPS || !Party)
	{
		return;
	}

	// Only this player's own party: a name is not a licence to select anyone
	// in the world. GetPartyMembers includes the caller (a click on your own
	// row targets yourself, for a heal).
	AValhallaPlayerState* Member = nullptr;
	for (AValhallaPlayerState* Candidate : Party->GetPartyMembers(ValhallaPS))
	{
		if (Candidate && Candidate->CharacterName == MemberName)
		{
			Member = Candidate;
			break;
		}
	}
	APawn* MemberPawn = Member ? Member->GetPawn() : nullptr;
	if (!MemberPawn || !MemberPawn->IsA<AValhallaCharacter>())
	{
		UE_LOG(LogValhallaGame, Log, TEXT("%s: party target '%s' refused (%s); target unchanged."), *ValhallaPS->CharacterName, *MemberName,
			!Member ? TEXT("not in this player's party") : TEXT("no body in this world"));
		return;
	}
	// SetTargetActor clears the target for something past the vision fog; a
	// party click that cannot select must not clear it.
	if (!ValhallaPS->CanSelectTarget(MemberPawn))
	{
		UE_LOG(LogValhallaGame, Log, TEXT("%s: party target '%s' refused (beyond the vision fog); target unchanged."), *ValhallaPS->CharacterName, *MemberName);
		return;
	}

	ValhallaPS->SetTargetActor(MemberPawn);
	UE_LOG(LogValhallaGame, Log, TEXT("%s: target -> party member %s (%s)"), *ValhallaPS->CharacterName, *MemberName, *MemberPawn->GetName());
}

void AValhallaPlayerController::ClientOnCombatEvent_Implementation(const FValhallaCombatEvent& Event)
{
	// Caster-private events land in the same list the multicast fills, so the
	// HUD has one place to read from and does not care how an event arrived.
	if (AValhallaGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AValhallaGameState>() : nullptr)
	{
		GameState->RecordCombatEvent(Event);
	}

	if (Event.Kind == EValhallaCombatEventKind::SkillFailed)
	{
		UE_LOG(LogValhallaGame, Log, TEXT("skillFailed: %s"), *Event.Text);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inventory, equipment and loot — GameRoom.ts:143-206
// ─────────────────────────────────────────────────────────────────────────────

AValhallaPlayerState* AValhallaPlayerController::GetValhallaPlayerState() const
{
	return GetPlayerState<AValhallaPlayerState>();
}

namespace
{
	/**
	 * The four things every inventory RPC needs, fetched once.
	 *
	 * Returns false when the player is dead or the data tables are missing, which
	 * is `if (player && player.alive)` plus the one check 1.0 did not have to
	 * make because its DataManager was a singleton that could not be absent.
	 */
	bool ResolveInventoryContext(
		const AValhallaPlayerController& Controller,
		AValhallaPlayerState*& OutPlayerState,
		const UValhallaDataSubsystem*& OutData,
		const FValhallaClassTemplate*& OutClassTemplate)
	{
		OutPlayerState = Controller.GetPlayerState<AValhallaPlayerState>();
		if (!OutPlayerState || !OutPlayerState->IsAlive())
		{
			return false;
		}

		const UGameInstance* GameInstance = Controller.GetGameInstance();
		OutData = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
		if (!OutData)
		{
			return false;
		}

		OutClassTemplate = OutData->FindClass(OutPlayerState->ClassId);
		return OutClassTemplate != nullptr;
	}

	/** A one-line dump of a stat block, for the equip/unequip log lines. */
	FString DescribeStats(const FValhallaResolvedStats& Stats)
	{
		return FString::Printf(
			TEXT("str=%.0f sta=%.0f dex=%.0f int=%.0f wis=%.0f pdef=%.0f sres=%.0f block=%.3f dodge=%.3f crit=%.3f hp=%.0f mana=%.0f"),
			Stats.Strength, Stats.Stamina, Stats.Dexterity, Stats.Intelligence, Stats.Wisdom,
			Stats.PhysicalDefense, Stats.SpellResist, Stats.BlockRating, Stats.DodgeRating, Stats.CritChance,
			Stats.MaxHp, Stats.MaxMana);
	}
}

void AValhallaPlayerController::ServerEquipItem_Implementation(int32 Slot)
{
	AValhallaPlayerState* ValhallaPS = nullptr;
	const UValhallaDataSubsystem* Data = nullptr;
	const FValhallaClassTemplate* ClassTemplate = nullptr;
	if (!ResolveInventoryContext(*this, ValhallaPS, Data, ClassTemplate))
	{
		return;
	}

	auto FindItem = [Data](FName ItemId) -> const FValhallaItemTemplate* { return Data->FindItem(ItemId); };

	TArray<FName> Equipment = ValhallaPS->GatherEquipment();
	const FValhallaResolvedStats Before = ValhallaPS->GetStats();
	FString Reason;

	if (!UValhallaInventoryLibrary::EquipItem(ValhallaPS->Inventory, Equipment, Slot, *ClassTemplate, FindItem, Reason))
	{
		FValhallaCombatEvent Failed;
		Failed.Kind = EValhallaCombatEventKind::SkillFailed;
		Failed.Target = GetPawn();
		Failed.Text = Reason;
		ClientOnCombatEvent(Failed);

		UE_LOG(LogValhallaInventory, Log, TEXT("equipItem refused for %s (slot %d): %s"),
			*ValhallaPS->CharacterName, Slot, *Reason);
		return;
	}

	ValhallaPS->ApplyEquipment(Equipment);
	ValhallaPS->RecomputeStats();

	UE_LOG(LogValhallaInventory, Log, TEXT("equipItem %s slot %d -> %s"),
		*ValhallaPS->CharacterName, Slot, *ValhallaPS->DescribeEquipment());
	UE_LOG(LogValhallaInventory, Log, TEXT("  stats before: %s"), *DescribeStats(Before));
	UE_LOG(LogValhallaInventory, Log, TEXT("  stats after : %s"), *DescribeStats(ValhallaPS->GetStats()));
}

void AValhallaPlayerController::ServerUnequipItem_Implementation(FName EquipSlot, int32 ToSlot)
{
	AValhallaPlayerState* ValhallaPS = nullptr;
	const UValhallaDataSubsystem* Data = nullptr;
	const FValhallaClassTemplate* ClassTemplate = nullptr;
	if (!ResolveInventoryContext(*this, ValhallaPS, Data, ClassTemplate))
	{
		return;
	}

	const EValhallaEquipSlot Slot = UValhallaInventoryLibrary::ParseEquipSlotName(EquipSlot);
	if (Slot == EValhallaEquipSlot::None)
	{
		UE_LOG(LogValhallaInventory, Log, TEXT("unequipItem: '%s' is not an equip slot."), *EquipSlot.ToString());
		return;
	}

	auto FindItem = [Data](FName ItemId) -> const FValhallaItemTemplate* { return Data->FindItem(ItemId); };

	TArray<FName> Equipment = ValhallaPS->GatherEquipment();
	const FValhallaResolvedStats Before = ValhallaPS->GetStats();

	// GameRoom.ts:153 — a target index is only meaningful when it is one, which
	// is why 1.0 tested `>= 0` rather than `!== undefined` alone.
	const bool bOk = (ToSlot >= 0)
		? UValhallaInventoryLibrary::UnequipItemToSlot(ValhallaPS->Inventory, Equipment, Slot, ToSlot, FindItem)
		: UValhallaInventoryLibrary::UnequipItem(ValhallaPS->Inventory, Equipment, Slot);

	if (!bOk)
	{
		UE_LOG(LogValhallaInventory, Log, TEXT("unequipItem refused for %s (slot %s, to %d)"),
			*ValhallaPS->CharacterName, *EquipSlot.ToString(), ToSlot);
		return;
	}

	ValhallaPS->ApplyEquipment(Equipment);
	ValhallaPS->RecomputeStats();

	UE_LOG(LogValhallaInventory, Log, TEXT("unequipItem %s %s -> %s"),
		*ValhallaPS->CharacterName, *EquipSlot.ToString(), *ValhallaPS->DescribeEquipment());
	UE_LOG(LogValhallaInventory, Log, TEXT("  stats before: %s"), *DescribeStats(Before));
	UE_LOG(LogValhallaInventory, Log, TEXT("  stats after : %s"), *DescribeStats(ValhallaPS->GetStats()));
}

void AValhallaPlayerController::ServerDropItem_Implementation(int32 Slot)
{
	AValhallaPlayerState* ValhallaPS = nullptr;
	const UValhallaDataSubsystem* Data = nullptr;
	const FValhallaClassTemplate* ClassTemplate = nullptr;
	if (!ResolveInventoryContext(*this, ValhallaPS, Data, ClassTemplate))
	{
		return;
	}

	const APawn* OwnPawn = GetPawn();
	if (!OwnPawn)
	{
		return;
	}

	FValhallaInventorySlot Dropped;
	if (!UValhallaInventoryLibrary::DropInventoryItem(ValhallaPS->Inventory, Slot, Dropped))
	{
		return;
	}

	// GameRoom.ts:174 — the bag lands where the player is standing, and merges
	// into whatever is already there.
	const TArray<FValhallaBagSlot> Items = { FValhallaBagSlot(Dropped.ItemId, Dropped.Quantity) };
	AValhallaLootBag::SpawnOrMerge(GetWorld(), OwnPawn->GetActorLocation(), Items, ValhallaPS->CharacterName);

	UE_LOG(LogValhallaInventory, Log, TEXT("dropItem %s dropped %s x%d from inventory slot %d"),
		*ValhallaPS->CharacterName, *Dropped.ItemId.ToString(), Dropped.Quantity, Slot);
}

void AValhallaPlayerController::ServerDropEquipped_Implementation(FName EquipSlot)
{
	AValhallaPlayerState* ValhallaPS = nullptr;
	const UValhallaDataSubsystem* Data = nullptr;
	const FValhallaClassTemplate* ClassTemplate = nullptr;
	if (!ResolveInventoryContext(*this, ValhallaPS, Data, ClassTemplate))
	{
		return;
	}

	const APawn* OwnPawn = GetPawn();
	if (!OwnPawn)
	{
		return;
	}

	const EValhallaEquipSlot Slot = UValhallaInventoryLibrary::ParseEquipSlotName(EquipSlot);
	TArray<FName> Equipment = ValhallaPS->GatherEquipment();

	FValhallaInventorySlot Dropped;
	if (!UValhallaInventoryLibrary::DropEquippedItem(Equipment, Slot, Dropped))
	{
		return;
	}

	ValhallaPS->ApplyEquipment(Equipment);
	ValhallaPS->RecomputeStats();

	const TArray<FValhallaBagSlot> Items = { FValhallaBagSlot(Dropped.ItemId, Dropped.Quantity) };
	AValhallaLootBag::SpawnOrMerge(GetWorld(), OwnPawn->GetActorLocation(), Items, ValhallaPS->CharacterName);

	UE_LOG(LogValhallaInventory, Log, TEXT("dropEquipped %s dropped %s from %s; stats now %s"),
		*ValhallaPS->CharacterName, *Dropped.ItemId.ToString(), *EquipSlot.ToString(),
		*DescribeStats(ValhallaPS->GetStats()));
}

void AValhallaPlayerController::ServerSwapInventory_Implementation(int32 A, int32 B)
{
	AValhallaPlayerState* ValhallaPS = nullptr;
	const UValhallaDataSubsystem* Data = nullptr;
	const FValhallaClassTemplate* ClassTemplate = nullptr;
	if (!ResolveInventoryContext(*this, ValhallaPS, Data, ClassTemplate))
	{
		return;
	}

	if (UValhallaInventoryLibrary::SwapInventorySlots(ValhallaPS->Inventory, A, B))
	{
		UE_LOG(LogValhallaInventory, Verbose, TEXT("swapInventory %s %d <-> %d"), *ValhallaPS->CharacterName, A, B);
	}
}

void AValhallaPlayerController::ServerLootItem_Implementation(AValhallaLootBag* Bag, int32 Slot)
{
	AValhallaPlayerState* ValhallaPS = nullptr;
	const UValhallaDataSubsystem* Data = nullptr;
	const FValhallaClassTemplate* ClassTemplate = nullptr;
	if (!ResolveInventoryContext(*this, ValhallaPS, Data, ClassTemplate))
	{
		return;
	}

	// LootBagSystem.ts:68 — the reach check happens *here*, on the server, with
	// the server's own positions. A client that asks to loot a bag across the map
	// is simply told nothing happened.
	if (!IsValid(Bag) || !Bag->IsWithinReach(GetPawn()))
	{
		return;
	}

	auto FindItem = [Data](FName ItemId) -> const FValhallaItemTemplate* { return Data->FindItem(ItemId); };

	if (!Bag->Items.IsValidIndex(Slot))
	{
		return;
	}

	const FName ItemId = Bag->Items[Slot].ItemId;
	const int32 Wanted = Bag->Items[Slot].Quantity;

	if (UValhallaInventoryLibrary::LootItem(Bag->Items, ValhallaPS->Inventory, Slot, Wanted, FindItem))
	{
		UE_LOG(LogValhallaInventory, Log, TEXT("lootItem %s took %s x%d (bag has %d slot(s) left)"),
			*ValhallaPS->CharacterName, *ItemId.ToString(), Wanted, Bag->Items.Num());
		Bag->DestroyIfEmpty();
	}
	else
	{
		UE_LOG(LogValhallaInventory, Log, TEXT("lootItem %s could not take %s — inventory full?"),
			*ValhallaPS->CharacterName, *ItemId.ToString());
	}
}

void AValhallaPlayerController::ServerLootAll_Implementation(AValhallaLootBag* Bag)
{
	AValhallaPlayerState* ValhallaPS = nullptr;
	const UValhallaDataSubsystem* Data = nullptr;
	const FValhallaClassTemplate* ClassTemplate = nullptr;
	if (!ResolveInventoryContext(*this, ValhallaPS, Data, ClassTemplate))
	{
		return;
	}

	if (!IsValid(Bag) || !Bag->IsWithinReach(GetPawn()))
	{
		return;
	}

	auto FindItem = [Data](FName ItemId) -> const FValhallaItemTemplate* { return Data->FindItem(ItemId); };

	const int32 Looted = UValhallaInventoryLibrary::LootAll(Bag->Items, ValhallaPS->Inventory, FindItem);

	FString InventoryLine;
	for (const FValhallaInventorySlot& InventorySlot : ValhallaPS->Inventory)
	{
		InventoryLine += FString::Printf(TEXT("%s x%d "), *InventorySlot.ItemId.ToString(), InventorySlot.Quantity);
	}

	UE_LOG(LogValhallaInventory, Log, TEXT("lootAll %s took %d slot(s); inventory now: %s"),
		*ValhallaPS->CharacterName, Looted,
		InventoryLine.IsEmpty() ? TEXT("<empty>") : *InventoryLine.TrimEnd());

	Bag->DestroyIfEmpty();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Party — GameRoom.ts:364-479
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaPlayerController::ServerPartyInvite_Implementation(const FString& TargetName)
{
	if (UValhallaPartySubsystem* Party = UValhallaPartySubsystem::Get(this))
	{
		Party->Invite(GetValhallaPlayerState(), TargetName);
	}
}

void AValhallaPlayerController::ServerPartyAccept_Implementation()
{
	if (UValhallaPartySubsystem* Party = UValhallaPartySubsystem::Get(this))
	{
		Party->Accept(GetValhallaPlayerState());
	}
}

void AValhallaPlayerController::ServerPartyDecline_Implementation()
{
	if (UValhallaPartySubsystem* Party = UValhallaPartySubsystem::Get(this))
	{
		Party->Decline(GetValhallaPlayerState());
	}
}

void AValhallaPlayerController::ServerPartyLeave_Implementation()
{
	if (UValhallaPartySubsystem* Party = UValhallaPartySubsystem::Get(this))
	{
		Party->Leave(GetValhallaPlayerState(), /*bSilent=*/false);
	}
}

void AValhallaPlayerController::ClientPartyInvite_Implementation(const FString& InviterName)
{
	PendingPartyInviter = InviterName;
	PendingPartyInviteAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	UE_LOG(LogValhallaGame, Log, TEXT("partyInvite received from %s"), *InviterName);
}

void AValhallaPlayerController::ClientPartyUpdate_Implementation(int32 InPartyId, const TArray<FString>& MemberNames)
{
	PendingPartyInviter.Reset();
	UE_LOG(LogValhallaGame, Log, TEXT("partyUpdate received: party %d with %d member(s): %s"),
		InPartyId, MemberNames.Num(), *FString::Join(MemberNames, TEXT(", ")));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Chat — GameRoom.ts:297
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** "general" -> EValhallaChatChannel::General. Unknown, and "system", give false. */
	bool ParseChatChannel(const FString& In, EValhallaChatChannel& Out)
	{
		const FString Lower = In.ToLower();
		if (Lower == TEXT("general")) { Out = EValhallaChatChannel::General; return true; }
		if (Lower == TEXT("world")) { Out = EValhallaChatChannel::World; return true; }
		if (Lower == TEXT("whisper") || Lower == TEXT("w")) { Out = EValhallaChatChannel::Whisper; return true; }
		if (Lower == TEXT("party") || Lower == TEXT("p")) { Out = EValhallaChatChannel::Party; return true; }
		// "system" is deliberately absent: it is the server's voice.
		return false;
	}

	const TCHAR* ChatChannelToString(EValhallaChatChannel Channel)
	{
		switch (Channel)
		{
		case EValhallaChatChannel::General:	return TEXT("general");
		case EValhallaChatChannel::World:	return TEXT("world");
		case EValhallaChatChannel::Whisper:	return TEXT("whisper");
		case EValhallaChatChannel::Party:	return TEXT("party");
		default:							return TEXT("system");
		}
	}

	/** GameRoom.ts:307 — trim, then cap. 200 characters, from 1.0. */
	constexpr int32 ChatMaxLength = 200;
}

void AValhallaPlayerController::DeliverChat(AValhallaPlayerController* To, const FValhallaChatMessage& Line)
{
	if (To)
	{
		To->ClientChatMessage(Line);
	}
}

void AValhallaPlayerController::ServerEmote_Implementation(FName Emote)
{
	AValhallaCharacter* Sitter = Cast<AValhallaCharacter>(GetPawn());
	const AValhallaPlayerState* ValhallaPS = GetPlayerState<AValhallaPlayerState>();
	if (!Sitter || !ValhallaPS || !ValhallaPS->IsAlive())
	{
		return;
	}

	const FString Verb = Emote.ToString().ToLower();
	if (Verb == TEXT("sit"))
	{
		Sitter->SetSitting(!Sitter->IsSitting());
		return;
	}
	if (Verb == TEXT("stand"))
	{
		Sitter->SetSitting(false);
		return;
	}

	EValhallaAnim Anim;
	if (Verb == TEXT("wave"))       { Anim = EValhallaAnim::EmoteWave; }
	else if (Verb == TEXT("cheer")) { Anim = EValhallaAnim::EmoteCheer; }
	else if (Verb == TEXT("bow"))   { Anim = EValhallaAnim::EmoteBow; }
	else
	{
		return;
	}

	const UValhallaSkillComponent* Skills = Sitter->FindComponentByClass<UValhallaSkillComponent>();
	if (Skills && !Skills->CastingSkillId.IsNone())
	{
		ServerChat_Implementation(TEXT("__system"), TEXT("You can't do that while casting."), FString());
		return;
	}

	Sitter->SetSitting(false);
	Sitter->MulticastEmote(static_cast<uint8>(Anim));
}

bool AValhallaPlayerController::TryHandleChatCommand(const FString& Raw)
{
	if (!Raw.StartsWith(TEXT("/")))
	{
		return false;
	}

	const FString Lower = Raw.ToLower();

	// B-15 A-030 — emotes, the same verbs the chat box parses.
	if (Lower == TEXT("/sit") || Lower == TEXT("/stand") || Lower == TEXT("/wave")
		|| Lower == TEXT("/cheer") || Lower == TEXT("/bow"))
	{
		ServerEmote_Implementation(FName(*Lower.Mid(1)));
		return true;
	}

	// GameScene.ts:5291 — /w and /whisper both take "<name> <message>".
	if (Lower.StartsWith(TEXT("/whisper ")) || Lower.StartsWith(TEXT("/w ")))
	{
		const int32 Skip = Lower.StartsWith(TEXT("/w ")) ? 3 : 9;
		const FString Rest = Raw.Mid(Skip);

		int32 SpaceIndex = INDEX_NONE;
		Rest.FindChar(TEXT(' '), SpaceIndex);
		if (SpaceIndex < 1)
		{
			ServerChat_Implementation(TEXT("__system"), TEXT("Usage: /w PlayerName message"), FString());
			return true;
		}

		const FString TargetName = Rest.Left(SpaceIndex);
		const FString Message = Rest.Mid(SpaceIndex + 1).TrimStartAndEnd();
		if (Message.IsEmpty())
		{
			ServerChat_Implementation(TEXT("__system"), TEXT("Usage: /w PlayerName message"), FString());
			return true;
		}

		ServerChat_Implementation(TEXT("whisper"), Message, TargetName);
		return true;
	}

	// GameScene.ts:5285 — /world <message>.
	if (Lower.StartsWith(TEXT("/world ")))
	{
		const FString Message = Raw.Mid(7).TrimStartAndEnd();
		if (!Message.IsEmpty())
		{
			ServerChat_Implementation(TEXT("world"), Message, FString());
		}
		return true;
	}

	// GameScene.ts:5310 — /party <message>. New in 2.0; see EValhallaChatChannel.
	if (Lower.StartsWith(TEXT("/party ")) || Lower.StartsWith(TEXT("/p ")))
	{
		const int32 Skip = Lower.StartsWith(TEXT("/p ")) ? 3 : 7;
		const FString Message = Raw.Mid(Skip).TrimStartAndEnd();
		if (!Message.IsEmpty())
		{
			ServerChat_Implementation(TEXT("party"), Message, FString());
		}
		return true;
	}

	// GameScene.ts:5301 — the four party commands.
	if (Lower.StartsWith(TEXT("/invite ")))
	{
		const FString Name = Raw.Mid(8).TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			ServerChat_Implementation(TEXT("__system"), TEXT("Usage: /invite PlayerName"), FString());
			return true;
		}
		ServerPartyInvite_Implementation(Name);
		return true;
	}

	if (Lower == TEXT("/accept"))
	{
		ServerPartyAccept_Implementation();
		return true;
	}

	if (Lower == TEXT("/decline"))
	{
		ServerPartyDecline_Implementation();
		return true;
	}

	if (Lower == TEXT("/leave"))
	{
		ServerPartyLeave_Implementation();
		return true;
	}

	// GameScene.ts:5307 — anything else beginning with a slash is a typo, and
	// broadcasting it would just show everyone the typo.
	int32 SpaceIndex = INDEX_NONE;
	const FString Verb = Raw.FindChar(TEXT(' '), SpaceIndex) ? Raw.Left(SpaceIndex) : Raw;

	FValhallaChatMessage Unknown;
	Unknown.Channel = EValhallaChatChannel::System;
	Unknown.Message = FString::Printf(TEXT("Unknown command: %s"), *Verb);
	Unknown.Timestamp = UValhallaCombatLibrary::GetServerTime(this);
	ClientChatMessage(Unknown);
	return true;
}

void AValhallaPlayerController::ServerChat_Implementation(const FString& Channel, const FString& Text, const FString& Target)
{
	AValhallaPlayerState* Sender = GetValhallaPlayerState();
	UWorld* World = GetWorld();
	if (!Sender || !World)
	{
		return;
	}

	// GameRoom.ts:306 — trim first, then cap, then drop an empty line. Doing it
	// in that order is what stops 200 spaces from being a message.
	const FString Trimmed = Text.TrimStartAndEnd().Left(ChatMaxLength);
	if (Trimmed.IsEmpty())
	{
		return;
	}

	const double Now = UValhallaCombatLibrary::GetServerTime(this);

	// The private escape hatch TryHandleChatCommand uses for its usage strings.
	// It is not a channel a real message can name, because ParseChatChannel does
	// not know it and the client never sends it.
	if (Channel == TEXT("__system"))
	{
		FValhallaChatMessage Line;
		Line.Channel = EValhallaChatChannel::System;
		Line.Message = Trimmed;
		Line.Timestamp = Now;
		ClientChatMessage(Line);
		return;
	}

	// Muted by an admin: tell them, deliver nothing. Checked after the
	// __system escape so usage lines still reach a muted player.
	if (Sender->AdminMutedUntil > Now)
	{
		FValhallaChatMessage Muted;
		Muted.Channel = EValhallaChatChannel::System;
		Muted.Message = FString::Printf(TEXT("You are muted for another %d minute(s)."),
			FMath::Max(1, FMath::CeilToInt((Sender->AdminMutedUntil - Now) / 60.0)));
		Muted.Timestamp = Now;
		ClientChatMessage(Muted);
		return;
	}

	EValhallaChatChannel Parsed;
	if (!ParseChatChannel(Channel, Parsed))
	{
		UE_LOG(LogValhallaGame, Log, TEXT("chat: %s asked for unknown channel '%s'; dropped."),
			*Sender->CharacterName, *Channel);
		return;
	}

	// The command grammar is 1.0's client-side parser, moved to the server. Only
	// `general` carries commands, exactly as in GameScene.ts:5276 — typing
	// "/accept" into a whisper sends the literal text, which is what 1.0 does.
	if (Parsed == EValhallaChatChannel::General && TryHandleChatCommand(Trimmed))
	{
		return;
	}

	FValhallaChatMessage Line;
	Line.Channel = Parsed;
	Line.SenderName = Sender->CharacterName;
	Line.Message = Trimmed;
	Line.TargetName = Target;
	Line.Timestamp = Now;

	UE_LOG(LogValhallaGame, Log, TEXT("chat [%s] %s%s: %s"),
		ChatChannelToString(Parsed), *Sender->CharacterName,
		Target.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" -> %s"), *Target), *Trimmed);

	switch (Parsed)
	{
	case EValhallaChatChannel::World:
	{
		// GameRoom.ts:318 — everyone connected, no filter at all.
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			DeliverChat(Cast<AValhallaPlayerController>(It->Get()), Line);
		}
		break;
	}

	case EValhallaChatChannel::General:
	{
		// GameRoom.ts:321 — same zone only.
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			AValhallaPlayerController* Controller = Cast<AValhallaPlayerController>(It->Get());
			const AValhallaPlayerState* Other = Controller ? Controller->GetPlayerState<AValhallaPlayerState>() : nullptr;
			if (Other && Other->ZoneId == Sender->ZoneId)
			{
				DeliverChat(Controller, Line);
			}
		}
		break;
	}

	case EValhallaChatChannel::Whisper:
	{
		// GameRoom.ts:331 — by character name, case-insensitively.
		if (Target.IsEmpty())
		{
			return;
		}

		AValhallaPlayerController* TargetController = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			AValhallaPlayerController* Controller = Cast<AValhallaPlayerController>(It->Get());
			const AValhallaPlayerState* Other = Controller ? Controller->GetPlayerState<AValhallaPlayerState>() : nullptr;
			if (Other && Other->CharacterName.Equals(Target, ESearchCase::IgnoreCase))
			{
				TargetController = Controller;
				break;
			}
		}

		if (!TargetController)
		{
			// GameRoom.ts:344 — the sender is told, and only the sender.
			FValhallaChatMessage NotFound;
			NotFound.Channel = EValhallaChatChannel::System;
			NotFound.Message = FString::Printf(TEXT("Player \"%s\" is not online."), *Target);
			NotFound.Timestamp = Now;
			ClientChatMessage(NotFound);
			return;
		}

		// GameRoom.ts:354 — both ends get the line; the client works out whether
		// it reads "To X" or "From X" from the sender name.
		ClientChatMessage(Line);
		if (TargetController != this)
		{
			DeliverChat(TargetController, Line);
		}
		break;
	}

	case EValhallaChatChannel::Party:
	{
		// New in 2.0. 1.0 had a party and no party channel; party talk went
		// through `general`, which meant it was heard by strangers standing
		// nearby and lost the moment someone crossed a zone line.
		UValhallaPartySubsystem* PartySubsystem = UValhallaPartySubsystem::Get(this);
		const TArray<AValhallaPlayerState*> Members = PartySubsystem
			? PartySubsystem->GetPartyMembers(Sender)
			: TArray<AValhallaPlayerState*>();

		if (Members.Num() == 0)
		{
			FValhallaChatMessage NoParty;
			NoParty.Channel = EValhallaChatChannel::System;
			NoParty.Message = TEXT("You are not in a party.");
			NoParty.Timestamp = Now;
			ClientChatMessage(NoParty);
			return;
		}

		for (AValhallaPlayerState* Member : Members)
		{
			DeliverChat(Cast<AValhallaPlayerController>(Member->GetOwner()), Line);
		}
		break;
	}

	default:
		break;
	}
}

void AValhallaPlayerController::AddLocalChatLine(const FValhallaChatMessage& Line)
{
	ChatLog.Add(Line);
	++ChatReceivedCount;
	while (ChatLog.Num() > ChatLogMaxLines)
	{
		ChatLog.RemoveAt(0);
	}
}

void AValhallaPlayerController::ClientChatMessage_Implementation(const FValhallaChatMessage& Line)
{
	ChatLog.Add(Line);
	++ChatReceivedCount;
	while (ChatLog.Num() > ChatLogMaxLines)
	{
		ChatLog.RemoveAt(0);
	}

	// GameScene.ts:5316 — the prefix is the channel, and a whisper reads
	// differently depending on which end of it you are.
	FString Prefix;
	switch (Line.Channel)
	{
	case EValhallaChatChannel::General:
		Prefix = FString::Printf(TEXT("[G] %s:"), *Line.SenderName);
		break;
	case EValhallaChatChannel::World:
		Prefix = FString::Printf(TEXT("[W] %s:"), *Line.SenderName);
		break;
	case EValhallaChatChannel::Party:
		Prefix = FString::Printf(TEXT("[P] %s:"), *Line.SenderName);
		break;
	case EValhallaChatChannel::Whisper:
	{
		const AValhallaPlayerState* Self = GetPlayerState<AValhallaPlayerState>();
		const bool bIsSender = Self && Self->CharacterName == Line.SenderName;
		Prefix = bIsSender
			? FString::Printf(TEXT("[To %s]:"), *Line.TargetName)
			: FString::Printf(TEXT("[From %s]:"), *Line.SenderName);
		break;
	}
	default:
		Prefix = TEXT("[system]");
		break;
	}

	UE_LOG(LogValhallaGame, Log, TEXT("chatReceived %s %s"), *Prefix, *Line.Message);
}

// ── Input hooks ─────────────────────────────────────────────────────────

void AValhallaPlayerController::OnActionBarPressed_Implementation(int32 Slot)
{
	UValhallaSkillComponent* Skills = GetSkillComponent();
	if (!Skills)
	{
		return;
	}

	// The client says which slot was pressed and what it has selected; the
	// server validates the cast. The cursor is read here and only here — once,
	// at the press, and only for a ground-targeted skill, which lands on the
	// point under it. Every other skill is sent the character's own position
	// (self and aoeSelf effects are drawn there when there is no target).
	FVector AimPoint = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
	AActor* CastTarget = GetCurrentTarget();

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(Skills->GetSlotSkillId(Slot)) : nullptr;
	if (Skill && Skill->TargetType == EValhallaSkillTargetType::AoeGround)
	{
		// A ground skill carries a target only when it was aimed *at* one: the
		// server then fires it at where that enemy is when the cast completes
		// (and a fireball follows them), rather than at the patch of floor they
		// were standing on when the key went down.
		CastTarget = nullptr;

		FVector2D ScreenPosition;
		bool bHaveScreen = AimScreenOverride.IsSet();
		if (bHaveScreen)
		{
			ScreenPosition = AimScreenOverride.GetValue();
		}
		else
		{
			float MouseX = 0.f;
			float MouseY = 0.f;
			bHaveScreen = GetMousePosition(MouseX, MouseY);
			ScreenPosition = FVector2D(MouseX, MouseY);
		}

		if (AActor* Hostile = bHaveScreen ? FindHostileAtScreen(ScreenPosition) : nullptr)
		{
			CastTarget = Hostile;
			AimPoint = Hostile->GetActorLocation();
			UE_LOG(LogValhallaGame, Log, TEXT("aoeGround '%s' aimed at %s %s"),
				*Skill->Id.ToString(), *UValhallaCombatLibrary::GetDisplayName(Hostile), *AimPoint.ToCompactString());
		}
		else if (bHaveScreen && SampleGroundPointAtScreen(ScreenPosition))
		{
			AimPoint = AimWorldPoint;
			UE_LOG(LogValhallaGame, Log, TEXT("aoeGround '%s' aimed at cursor point %s"),
				*Skill->Id.ToString(), *AimPoint.ToCompactString());
		}
		else
		{
			// No cursor over the world (it is outside the viewport, or above
			// the horizon): the selected target, as a player would have aimed,
			// else the caster's own position.
			if (AActor* Target = GetCurrentTarget())
			{
				AimPoint = Target->GetActorLocation();
				CastTarget = Target;
			}
			UE_LOG(LogValhallaGame, Log, TEXT("aoeGround '%s': no cursor point, aimed at %s"),
				*Skill->Id.ToString(), *AimPoint.ToCompactString());
		}
	}

	Skills->CastFromActionBar(Slot, CastTarget, AimPoint);
}

void AValhallaPlayerController::PressActionBarAimedAt(int32 Slot, const FVector2D& ScreenPosition)
{
	AimScreenOverride = ScreenPosition;
	OnActionBarPressed(Slot);
	AimScreenOverride.Reset();
}

AValhallaLootBag* AValhallaPlayerController::TraceForLootBagUnderCursor() const
{
	FHitResult Hit;
	if (!GetHitResultUnderCursor(AValhallaLootBag::InteractChannel, /*bTraceComplex=*/false, Hit))
	{
		return nullptr;
	}
	return Cast<AValhallaLootBag>(Hit.GetActor());
}

void AValhallaPlayerController::ShowLocalSystemMessage(const FString& Text)
{
	FValhallaChatMessage Line;
	Line.Channel = EValhallaChatChannel::System;
	Line.Message = Text;
	Line.Timestamp = UValhallaCombatLibrary::GetServerTime(this);
	// A client RPC called on the owning client runs locally: this is the same
	// path a server-sent system line takes into the chat log.
	ClientChatMessage(Line);
}

void AValhallaPlayerController::TryOpenLootBag(AValhallaLootBag* Bag)
{
	if (!Bag)
	{
		return;
	}

	// The client's reach test is presentational only — it decides whether to
	// open the window. The server re-runs it in ServerLootItem / ServerLootAll
	// against its own positions, and that one is the rule.
	if (!Bag->IsWithinReach(GetPawn()))
	{
		ShowLocalSystemMessage(TEXT("You are too far away to loot that."));
		return;
	}

	OpenLootBag = Bag;

	// Phase 8b: the window is the HUD's UMG loot panel.
	if (UValhallaGameHUDWidget* Hud = GetGameHUD())
	{
		Hud->OpenLootPanel(Bag);
	}
}

void AValhallaPlayerController::CloseLootWindow()
{
	OpenLootBag = nullptr;
	if (UValhallaGameHUDWidget* Hud = GetGameHUD(); Hud && Hud->IsLootPanelOpen())
	{
		Hud->CloseLootPanel();
	}
}

void AValhallaPlayerController::OnPrimaryClick_Implementation()
{
	// ── The bag labels first ────────────────────────────────────────────
	// The HUD draws a "Loot (n)" label over every bag, on top of the world, so
	// it wins over anything the cursor also happens to be over in 3D. The loot
	// window itself is the UMG panel (Phase 8b): a click on it is consumed by
	// Slate and never reaches this handler.
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (AValhallaHUD* ValhallaHUD = Cast<AValhallaHUD>(GetHUD()); ValhallaHUD && GetMousePosition(MouseX, MouseY))
	{
		if (AValhallaLootBag* Bag = ValhallaHUD->HitTestLootLabel(FVector2D(MouseX, MouseY)))
		{
			TryOpenLootBag(Bag);
			return;
		}
	}

	// ── A bag in the world, on the Interact channel ─────────────────────
	if (AValhallaLootBag* Bag = TraceForLootBagUnderCursor())
	{
		TryOpenLootBag(Bag);
		return;
	}

	// GameScene.ts:812 — left click selects what is under the cursor, and
	// clicking empty ground deselects. Clicking the same thing twice in 1.0 also
	// deselected; that is reproduced here so a second click is an "off" switch.
	AActor* Hit = TraceForTargetUnderCursor();

	if (Hit && Hit == GetCurrentTarget())
	{
		ServerSetTarget(nullptr);
		return;
	}

	ServerSetTarget(Hit);
}

void AValhallaPlayerController::OnToggleSkills_Implementation()
{
	// Phase 8b: the skills pane. The log listing stays, because it is what a
	// headless run can read.
	if (UValhallaGameHUDWidget* Hud = GetGameHUD())
	{
		Hud->ToggleSkills();
	}

	const UValhallaSkillComponent* Skills = GetSkillComponent();
	if (!Skills)
	{
		return;
	}

	for (int32 Slot = 1; Slot <= ValhallaActionBarSlots; ++Slot)
	{
		const FName SkillId = Skills->GetSlotSkillId(Slot);
		if (!SkillId.IsNone())
		{
			UE_LOG(LogValhallaGame, Log, TEXT("  slot %d: %s (%.1f s cooldown)"),
				Slot, *SkillId.ToString(), Skills->GetSlotCooldownRemaining(Slot));
		}
	}
}

// ── Dev console commands ────────────────────────────────────────────────
//
// PIE cannot deliver a mouse click to a specific one of several clients from a
// script, so the gate needs a way to drive the same server RPCs from the
// console. These are the only entry points that skip the input layer, and they
// still go through the same ServerCastSkill / ServerStartAutoAttack validation
// a real click would — they are a different *input*, not a different code path.

namespace
{
	/**
	 * Every player controller on the server, optionally narrowed to one class.
	 *
	 * Deliberately *not* limited to local controllers. Under an in-process PIE
	 * listen server the second client's controller is a remote connection in the
	 * server's world and a local one only in its own client world, so a
	 * local-only iteration can never reach the wizard from the editor console —
	 * and the client-world copy has no authority to act with anyway.
	 *
	 * Running on the server is also what makes these commands honest: every one
	 * of them ends in a Server RPC or a server-side write, which is exactly the
	 * path a real click would have taken once it arrived. The console is a
	 * different *input*, not a different code path.
	 *
	 * The class filter is what picks one of the two: an unfiltered command drives
	 * the warrior and the wizard at once.
	 */
	/**
	 * The world a dev command should act in. A client world has no authority;
	 * in a one-process PIE (Play As Client, Run Under One Process) the editor
	 * console hands commands to a *client* world, so the in-process server's
	 * PIE world is found and used instead. Null when there is none — a remote
	 * client really cannot act, and must not pretend to.
	 */
	UWorld* AuthorityWorldFor(UWorld* World)
	{
		if (World && World->GetNetMode() != NM_Client)
		{
			return World;
		}
#if WITH_EDITOR
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (Context.WorldType == EWorldType::PIE && Candidate && Candidate->GetNetMode() != NM_Client)
				{
					return Candidate;
				}
			}
		}
#endif
		return nullptr;
	}

	void ForEachValhallaController(UWorld* InWorld, const FString& ClassFilter, TFunctionRef<void(AValhallaPlayerController&)> Fn)
	{
		UWorld* World = AuthorityWorldFor(InWorld);
		if (!World)
		{
			// A client copy of the world has no authority; acting there would
			// silently do nothing, or worse, half of something.
			return;
		}

		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			AValhallaPlayerController* Controller = Cast<AValhallaPlayerController>(It->Get());
			if (!Controller)
			{
				continue;
			}

			if (!ClassFilter.IsEmpty())
			{
				const AValhallaPlayerState* ValhallaPS = Controller->GetPlayerState<AValhallaPlayerState>();
				if (!ValhallaPS || !ValhallaPS->ClassId.ToString().Equals(ClassFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}
			}

			Fn(*Controller);
		}
	}

	/** The optional trailing class-id argument, or empty. */
	FString ClassFilterFromArgs(const TArray<FString>& Args, int32 FilterIndex)
	{
		return Args.IsValidIndex(FilterIndex) ? Args[FilterIndex] : FString();
	}

	/**
	 * `valhalla.DebugCast <skillId> [classId]` — cast a skill by id at the
	 * current target and aim point, exactly as pressing its action bar key would.
	 *
	 * The aim point is only meaningful for ground-targeted skills, and on a
	 * client driven from the console the cursor is wherever the tester left it.
	 * So for an aoeGround skill the aim point is taken from the current target
	 * instead when there is one, which is what a player would have aimed at.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugCastCommand(
		TEXT("valhalla.DebugCast"),
		TEXT("Dev only. valhalla.DebugCast <skillId> [classId] — cast a skill at the current target / aim point."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugCast needs a skill id, e.g. 'valhalla.DebugCast wizard_fireball wizard'."));
				return;
			}

			const FName SkillId(*Args[0]);

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 1), [SkillId](AValhallaPlayerController& Controller)
			{
				UValhallaSkillComponent* Skills = Controller.GetSkillComponent();
				if (!Skills)
				{
					return;
				}

				AActor* Target = Controller.GetCurrentTarget();
				FVector AimPoint = Controller.GetAimWorldPoint();
				if (Target)
				{
					AimPoint = Target->GetActorLocation();
				}

				UE_LOG(LogValhallaGame, Log, TEXT("valhalla.DebugCast %s by %s (target %s, aim %s)"),
					*SkillId.ToString(),
					*UValhallaCombatLibrary::GetDisplayName(Controller.GetPawn()),
					*UValhallaCombatLibrary::GetDisplayName(Target),
					*AimPoint.ToCompactString());

				Skills->ServerCastSkill(SkillId, Target, AimPoint);
			});
		}));

	/** `valhalla.DebugAutoAttack [classId]` — toggle the auto-attack on the current target. */
	FAutoConsoleCommandWithWorldAndArgs GDebugAutoAttackCommand(
		TEXT("valhalla.DebugAutoAttack"),
		TEXT("Dev only. valhalla.DebugAutoAttack [classId] — toggle the auto-attack loop against the selected target."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ForEachValhallaController(World, ClassFilterFromArgs(Args, 0), [](AValhallaPlayerController& Controller)
			{
				UValhallaSkillComponent* Skills = Controller.GetSkillComponent();
				AActor* Target = Controller.GetCurrentTarget();
				if (!Skills)
				{
					return;
				}

				if (Skills->bAutoAttacking)
				{
					Skills->ServerStopAutoAttack();
					return;
				}

				if (!Target)
				{
					UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugAutoAttack: no target selected."));
					return;
				}

				Skills->ServerStartAutoAttack(Target);
			});
		}));

	/**
	 * `valhalla.DebugTeleport <x> <y> [classId]` — move a local player.
	 *
	 * Purely a way to get a character to where the NPCs are without walking. PIE
	 * cannot deliver WASD to one particular client from a script, and the two
	 * spawners are deliberately placed out at the corridor and north of the wall
	 * ring, so without this the gate would be unreachable. It writes the actor
	 * location on the listen-server host, which is authoritative.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugTeleportCommand(
		TEXT("valhalla.DebugTeleport"),
		TEXT("Dev only. valhalla.DebugTeleport <x> <y> [classId] — move a local player to a world XY."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugTeleport needs an x and a y, e.g. 'valhalla.DebugTeleport 450 220 warrior'."));
				return;
			}

			const double X = FCString::Atod(*Args[0]);
			const double Y = FCString::Atod(*Args[1]);

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 2), [X, Y](AValhallaPlayerController& Controller)
			{
				APawn* Pawn = Controller.GetPawn();
				if (!Pawn || !Pawn->HasAuthority())
				{
					UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugTeleport only works on the listen-server host."));
					return;
				}

				// B-06: stand on whatever is under (X, Y) - Eldmoor's Landscape is
				// not flat. The lowest walkable floor with room for the capsule
				// within 30 m above / 20 m below the pawn; the old height if
				// there is none (the flat grey box keeps working either way).
				FVector Destination(X, Y, Pawn->GetActorLocation().Z);
				if (const ACharacter* Character = Cast<ACharacter>(Pawn))
				{
					if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
					{
						double StandZ = 0.0;
						if (UValhallaZoneSubsystem::FindStandingZ(Pawn->GetWorld(), X, Y, Destination.Z + 3000.0,
								Destination.Z - 2000.0, Capsule->GetScaledCapsuleRadius(),
								Capsule->GetScaledCapsuleHalfHeight(), Pawn, StandZ))
						{
							Destination.Z = StandZ + 2.0;
						}
					}
				}
				Pawn->SetActorLocation(Destination, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

				UE_LOG(LogValhallaGame, Log, TEXT("valhalla.DebugTeleport: %s -> %s"),
					*UValhallaCombatLibrary::GetDisplayName(Pawn), *Destination.ToCompactString());
			});
		}));

	/**
	 * `valhalla.DebugSetHp <n>` — set the local player's HP.
	 *
	 * Purely so the death-and-respawn path can be demonstrated in a few seconds
	 * rather than by standing still for a minute while a 10-damage enemy chews
	 * through 150 HP. It writes the player state directly and therefore only
	 * works on a listen-server host, which is the only place it is ever used.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugSetHpCommand(
		TEXT("valhalla.DebugSetHp"),
		TEXT("Dev only. valhalla.DebugSetHp <n> [classId] — set a local player's HP, to test death and respawn."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugSetHp needs a number."));
				return;
			}

			const float NewHp = FCString::Atof(*Args[0]);

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 1), [NewHp](AValhallaPlayerController& Controller)
			{
				AValhallaPlayerState* ValhallaPS = Controller.GetPlayerState<AValhallaPlayerState>();
				if (!ValhallaPS || !ValhallaPS->HasAuthority())
				{
					UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugSetHp only works on the listen-server host."));
					return;
				}

				ValhallaPS->Hp = FMath::Clamp(NewHp, 0.f, ValhallaPS->MaxHp);
				UE_LOG(LogValhallaGame, Log, TEXT("valhalla.DebugSetHp: %s hp=%.0f/%.0f"),
					*ValhallaPS->CharacterName, ValhallaPS->Hp, ValhallaPS->MaxHp);
			});
		}));

	/**
	 * `valhalla.DebugTargetNearest` — select the nearest living NPC.
	 *
	 * A click would do this, but a click cannot be aimed at one particular PIE
	 * client from a script. This is the targeting half of the same problem the
	 * two commands above solve for casting.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugTargetNearestCommand(
		TEXT("valhalla.DebugTargetNearest"),
		TEXT("Dev only. valhalla.DebugTargetNearest [classId] — select the nearest living NPC."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ForEachValhallaController(World, ClassFilterFromArgs(Args, 0), [](AValhallaPlayerController& Controller)
			{
				const APawn* Pawn = Controller.GetPawn();
				if (!Pawn)
				{
					return;
				}

				AValhallaNPC* Nearest = nullptr;
				double NearestDistSq = TNumericLimits<double>::Max();

				for (TActorIterator<AValhallaNPC> It(Controller.GetWorld()); It; ++It)
				{
					if (!It->IsAlive())
					{
						continue;
					}
					const double DistSq = FVector::DistSquared2D(It->GetActorLocation(), Pawn->GetActorLocation());
					if (DistSq < NearestDistSq)
					{
						NearestDistSq = DistSq;
						Nearest = *It;
					}
				}

				if (Nearest)
				{
					UE_LOG(LogValhallaGame, Log, TEXT("valhalla.DebugTargetNearest -> %s at %.0f cm"),
						*Nearest->DisplayName, FMath::Sqrt(NearestDistSq));
					Controller.ServerSetTarget(Nearest);
				}
				else
				{
					UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugTargetNearest: no living NPC in the level."));
				}
			});
		}));

	// ── Phase 2c ────────────────────────────────────────────────────────
	//
	// Chat and party are the two features 2c adds that need *typing*, and PIE
	// has nowhere to type: there is no UMG chat box yet (Phase 8 owns it), and
	// APlayerController::InputKey character collection would mean building a
	// modal text-entry mode whose only consumer is about to be replaced. So the
	// input path for both is the console, and it is a thin one — each command
	// below calls the very ServerChat / ServerParty* RPC a chat box would, with
	// the same server-side parsing of `/invite`, `/accept`, `/decline` and
	// `/leave` out of a general-channel message that 1.0's client did on its
	// side. Nothing here is a second code path; it is a second keyboard.

	/**
	 * `valhalla.Chat <channel> <text...> [| target]` — send a chat message.
	 *
	 * The trailing text is joined back together with spaces, because the console
	 * splits on them. A whisper names its target after a bare `|`, which is the
	 * one character no chat line is likely to start with:
	 *
	 *     valhalla.Chat world Hello everyone
	 *     valhalla.Chat whisper psst | Player2
	 *     valhalla.Chat general /invite Player2
	 *
	 * The class filter the other commands take is replaced by the sender's
	 * channel and target here, so a `[classId]` argument would be ambiguous with
	 * the message text. Use `valhalla.ChatAs <classId> …` for one client.
	 */
	FAutoConsoleCommandWithWorldAndArgs GChatCommand(
		TEXT("valhalla.Chat"),
		TEXT("Dev only. valhalla.Chat <channel> <text...> [| target] — send chat from every local player. Channels: general, world, whisper, party."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.Chat needs a channel and a message, e.g. 'valhalla.Chat world Hello'."));
				return;
			}

			const FString Channel = Args[0];

			FString Text;
			FString Target;
			bool bAfterPipe = false;
			for (int32 Index = 1; Index < Args.Num(); ++Index)
			{
				if (Args[Index] == TEXT("|"))
				{
					bAfterPipe = true;
					continue;
				}
				FString& Sink = bAfterPipe ? Target : Text;
				if (!Sink.IsEmpty())
				{
					Sink += TEXT(" ");
				}
				Sink += Args[Index];
			}

			ForEachValhallaController(World, FString(), [&Channel, &Text, &Target](AValhallaPlayerController& Controller)
			{
				Controller.ServerChat(Channel, Text, Target);
			});
		}));

	/** `valhalla.ChatAs <classId> <channel> <text...> [| target]` — the same, from one client. */
	FAutoConsoleCommandWithWorldAndArgs GChatAsCommand(
		TEXT("valhalla.ChatAs"),
		TEXT("Dev only. valhalla.ChatAs <classId> <channel> <text...> [| target] — send chat from the player of one class."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 3)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.ChatAs needs a class, a channel and a message, e.g. 'valhalla.ChatAs warrior general /invite Player2'."));
				return;
			}

			const FString ClassFilter = Args[0];
			const FString Channel = Args[1];

			FString Text;
			FString Target;
			bool bAfterPipe = false;
			for (int32 Index = 2; Index < Args.Num(); ++Index)
			{
				if (Args[Index] == TEXT("|"))
				{
					bAfterPipe = true;
					continue;
				}
				FString& Sink = bAfterPipe ? Target : Text;
				if (!Sink.IsEmpty())
				{
					Sink += TEXT(" ");
				}
				Sink += Args[Index];
			}

			ForEachValhallaController(World, ClassFilter, [&Channel, &Text, &Target](AValhallaPlayerController& Controller)
			{
				Controller.ServerChat(Channel, Text, Target);
			});
		}));

	/**
	 * `valhalla.Party <invite|accept|decline|leave> [name] [classId]` — the four
	 * party RPCs, without going through the chat parser.
	 */
	FAutoConsoleCommandWithWorldAndArgs GPartyCommand(
		TEXT("valhalla.Party"),
		TEXT("Dev only. valhalla.Party <invite|accept|decline|leave> [name] [classId] — drive the party RPCs."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.Party needs a verb: invite, accept, decline or leave."));
				return;
			}

			const FString Verb = Args[0].ToLower();

			if (Verb == TEXT("invite"))
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.Party invite needs a character name."));
					return;
				}
				const FString TargetName = Args[1];
				ForEachValhallaController(World, ClassFilterFromArgs(Args, 2), [&TargetName](AValhallaPlayerController& Controller)
				{
					Controller.ServerPartyInvite(TargetName);
				});
				return;
			}

			const FString ClassFilter = ClassFilterFromArgs(Args, 1);

			if (Verb == TEXT("accept"))
			{
				ForEachValhallaController(World, ClassFilter, [](AValhallaPlayerController& Controller) { Controller.ServerPartyAccept(); });
			}
			else if (Verb == TEXT("decline"))
			{
				ForEachValhallaController(World, ClassFilter, [](AValhallaPlayerController& Controller) { Controller.ServerPartyDecline(); });
			}
			else if (Verb == TEXT("leave"))
			{
				ForEachValhallaController(World, ClassFilter, [](AValhallaPlayerController& Controller) { Controller.ServerPartyLeave(); });
			}
			else
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.Party: unknown verb '%s'."), *Args[0]);
			}
		}));

	/**
	 * `valhalla.DebugWalk <seconds> [classId]` — drive a character forward.
	 *
	 * The locomotion state is read from the pawn's velocity, so the only honest
	 * way to photograph a walk cycle is to actually walk. A teleport does not
	 * do it — it moves the capsule with zero velocity, which is what standing
	 * still looks like to the animation.
	 *
	 * This feeds AddMovementInput on a timer, which is the same input path a
	 * held W key produces, so what the screenshot shows is the real locomotion
	 * blend and not a pose forced for the camera.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugWalkCommand(
		TEXT("valhalla.DebugWalk"),
		TEXT("Dev only. valhalla.DebugWalk <seconds> [classId] — walk a character forward for a while."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 2.f;
			if (Seconds <= 0.f || !World)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugWalk needs a positive number of seconds."));
				return;
			}

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 1), [Seconds](AValhallaPlayerController& Controller)
			{
				APawn* Pawn = Controller.GetPawn();
				UWorld* World = Controller.GetWorld();
				if (!Pawn || !World)
				{
					return;
				}

				// The actor's forward, captured once. The character now turns to
				// face its walk anyway, so this walks straight on either way.
				const FVector Direction = Pawn->GetActorForwardVector();
				const TWeakObjectPtr<APawn> WeakPawn(Pawn);
				const TWeakObjectPtr<UWorld> WeakWorld(World);

				TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
				World->GetTimerManager().SetTimer(*Handle,
					FTimerDelegate::CreateLambda([WeakPawn, Direction]()
					{
						if (APawn* Walker = WeakPawn.Get())
						{
							Walker->AddMovementInput(Direction, 1.f);
						}
					}), 0.01f, /*bLoop=*/true);

				FTimerHandle StopHandle;
				World->GetTimerManager().SetTimer(StopHandle,
					FTimerDelegate::CreateLambda([WeakWorld, Handle]()
					{
						if (UWorld* Alive = WeakWorld.Get())
						{
							Alive->GetTimerManager().ClearTimer(*Handle);
						}
					}), Seconds, /*bLoop=*/false);

				UE_LOG(LogValhallaVisual, Log, TEXT("valhalla.DebugWalk: %s walking %s for %.1f s"),
					*Pawn->GetName(), *Direction.ToCompactString(), Seconds);
			});
		}));

	/**
	 * `valhalla.DebugFacing` — log every player's facing yaw in every PIE world.
	 *
	 * The controls-rework gate's instrument: the facing check reads the server's
	 * yaw, what the player sees is their own client's, and the two must agree.
	 * One line per character per world (server first), with the camera yaw
	 * where the character is locally controlled, its speed, and whether
	 * orient-to-movement currently owns the yaw.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugFacingCommand(
		TEXT("valhalla.DebugFacing"),
		TEXT("Dev only. valhalla.DebugFacing — log each player's facing yaw (and camera yaw) in every PIE world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* InWorld)
		{
			TArray<UWorld*> Worlds;
#if WITH_EDITOR
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::PIE && Context.World())
					{
						// Server first, so a line pair reads "server, client".
						if (Context.World()->GetNetMode() == NM_Client) { Worlds.Add(Context.World()); }
						else { Worlds.Insert(Context.World(), 0); }
					}
				}
			}
#endif
			if (Worlds.Num() == 0 && InWorld)
			{
				Worlds.Add(InWorld);
			}

			for (UWorld* World : Worlds)
			{
				const TCHAR* Mode = World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server");
				for (TActorIterator<AValhallaCharacter> It(World); It; ++It)
				{
					const AValhallaCharacter* Body = *It;
					const AValhallaPlayerState* BodyPS = Body->GetValhallaPlayerState();
					const FString Camera = Body->IsLocallyControlled()
						? FString::Printf(TEXT(" camera %.1f"), Body->GetCameraWorldYaw()) : FString();
					UE_LOG(LogValhallaGame, Log, TEXT("facing [%s %s] %s (%s) yaw %.1f%s speed %.0f%s"),
						Mode, *World->GetName(),
						BodyPS ? *BodyPS->CharacterName : *Body->GetName(),
						BodyPS ? *BodyPS->ClassId.ToString() : TEXT("?"),
						Body->GetFacingYaw(), *Camera, Body->GetVelocity().Size2D(),
						Body->IsWalkingForFacing() ? TEXT(" (walking)") : TEXT(""));
				}
			}
		}));

	/**
	 * `valhalla.DebugAnim` — log what every body in the level is playing.
	 *
	 * The animation is not replicated state and does not appear anywhere in the
	 * existing debug output, so without this the only evidence that a cast pose
	 * is held for the length of a cast is a photograph of it. Prints players and
	 * NPCs alike, because the point is that they run the same state machine.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugAnimCommand(
		TEXT("valhalla.DebugAnim"),
		TEXT("Dev only. valhalla.DebugAnim — log the animation every character and NPC is playing."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				const UValhallaAnimComponent* Anim = UValhallaAnimComponent::Find(*It);
				if (!Anim)
				{
					continue;
				}

				FString Who = It->GetName();
				if (const AValhallaCharacter* Character = Cast<AValhallaCharacter>(*It))
				{
					if (const AValhallaPlayerState* ValhallaPS = Character->GetValhallaPlayerState())
					{
						Who = FString::Printf(TEXT("%s[%s]"),
							*ValhallaPS->CharacterName, *ValhallaPS->ClassId.ToString());
					}
				}
				else if (const AValhallaNPC* Npc = Cast<AValhallaNPC>(*It))
				{
					Who = FString::Printf(TEXT("%s[npc %s]"),
						*Npc->DisplayName, *Npc->TemplateId.ToString());
				}

				UE_LOG(LogValhallaVisual, Log, TEXT("anim actor=%s playing=%s action=%s"),
					*Who, *Anim->DescribeCurrentAnim(),
					Anim->IsPlayingAction() ? TEXT("yes") : TEXT("no"));
			}
		}));

	/**
	 * `valhalla.DebugGiveItem <itemId> [classId]` — put an item in the bag.
	 *
	 * Phase 2c has no vendor, no loot table that drops everything and no
	 * character persistence, so the only route from "the art for
	 * `iron_gauntlets` exists" to "a character is wearing it" was to kill the
	 * right NPC repeatedly. This is that route, made direct. It writes the
	 * inventory on the authority exactly as looting does — the item has to be
	 * real (`items.json` is consulted) and the inventory cap still applies —
	 * and then logs the slot it landed in, because `valhalla.DebugEquip` takes
	 * an index rather than an id.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugGiveItemCommand(
		TEXT("valhalla.DebugGiveItem"),
		TEXT("Dev only. valhalla.DebugGiveItem <itemId> [classId] — add an item to a player's inventory."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogValhallaGame, Warning,
					TEXT("valhalla.DebugGiveItem needs an item id, e.g. 'valhalla.DebugGiveItem iron_helm warrior'."));
				return;
			}

			const FName ItemId(*Args[0]);

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 1), [ItemId, World](AValhallaPlayerController& Controller)
			{
				AValhallaPlayerState* ValhallaPS = Controller.GetPlayerState<AValhallaPlayerState>();
				const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
				const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
				if (!ValhallaPS || !Data)
				{
					return;
				}

				auto FindItem = [Data](FName Id) -> const FValhallaItemTemplate*
				{
					return Data->FindItem(Id);
				};

				if (!FindItem(ItemId))
				{
					UE_LOG(LogValhallaInventory, Warning,
						TEXT("valhalla.DebugGiveItem: '%s' is not in items.json."), *ItemId.ToString());
					return;
				}

				if (!UValhallaInventoryLibrary::AddItem(ValhallaPS->Inventory, ItemId, 1, FindItem))
				{
					UE_LOG(LogValhallaInventory, Warning,
						TEXT("valhalla.DebugGiveItem: %s could not take '%s' — inventory full."),
						*ValhallaPS->CharacterName, *ItemId.ToString());
					return;
				}

				const int32 Index = ValhallaPS->Inventory.IndexOfByPredicate(
					[ItemId](const FValhallaInventorySlot& Slot) { return Slot.ItemId == ItemId; });

				UE_LOG(LogValhallaInventory, Log, TEXT("valhalla.DebugGiveItem: %s got '%s' in slot %d"),
					*ValhallaPS->CharacterName, *ItemId.ToString(), Index);
			});
		}));

	/**
	 * `valhalla.DebugPreset <n> [classId]` — dress a character in one of four kits.
	 *
	 * Four sets, one per armour look the art ships, so that a screenshot of the
	 * paperdoll is one console line rather than fourteen. Each item is granted
	 * and then equipped through the ordinary Server RPCs, so an item the class
	 * is not allowed to wear is refused here exactly as it would be refused a
	 * player — a cleric asked for plate still ends up in what a cleric can wear.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugPresetCommand(
		TEXT("valhalla.DebugPreset"),
		TEXT("Dev only. valhalla.DebugPreset <1-4> [classId] — equip a full set: 1 warrior, 2 ranger, 3 wizard, 4 cleric."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			static const TArray<FName> Presets[] = {
				// 1 — warrior: iron everything, sword and kite shield.
				{ TEXT("iron_helm"), TEXT("chainmail"), TEXT("iron_greaves"), TEXT("iron_gauntlets"),
				  TEXT("iron_boots"), TEXT("iron_sword"), TEXT("iron_kite_shield") },
				// 2 — ranger: leather, a bow, and the one cloak in the game.
				{ TEXT("leather_helm"), TEXT("leather_tunic"), TEXT("leather_leggings"),
				  TEXT("leather_gloves"), TEXT("leather_boots"), TEXT("short_bow"), TEXT("travelers_cloak") },
				// 3 — wizard: cloth and a staff, which is the A_Cast attack cycle.
				{ TEXT("cloth_hood"), TEXT("cloth_robe"), TEXT("cloth_pants"), TEXT("cloth_wraps"),
				  TEXT("cloth_sandals"), TEXT("oak_staff") },
				// 4 — cleric: a deliberate mixture, to prove slots are independent.
				{ TEXT("chainmail"), TEXT("cloth_pants"), TEXT("cloth_wraps"), TEXT("iron_boots"),
				  TEXT("iron_mace"), TEXT("iron_buckler") },
			};

			const int32 Which = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			if (Which < 1 || Which > static_cast<int32>(UE_ARRAY_COUNT(Presets)))
			{
				UE_LOG(LogValhallaGame, Warning,
					TEXT("valhalla.DebugPreset needs 1 (warrior), 2 (ranger), 3 (wizard) or 4 (cleric)."));
				return;
			}

			const TArray<FName>& Preset = Presets[Which - 1];

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 1), [&Preset, World](AValhallaPlayerController& Controller)
			{
				AValhallaPlayerState* ValhallaPS = Controller.GetPlayerState<AValhallaPlayerState>();
				const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
				const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
				if (!ValhallaPS || !Data)
				{
					return;
				}

				auto FindItem = [Data](FName Id) -> const FValhallaItemTemplate*
				{
					return Data->FindItem(Id);
				};

				for (const FName ItemId : Preset)
				{
					if (!FindItem(ItemId))
					{
						UE_LOG(LogValhallaInventory, Warning,
							TEXT("valhalla.DebugPreset: '%s' is not in items.json."), *ItemId.ToString());
						continue;
					}

					UValhallaInventoryLibrary::AddItem(ValhallaPS->Inventory, ItemId, 1, FindItem);

					// Equip by index, immediately, so the next grant does not
					// shift the index out from under this one: equipping removes
					// the slot, and the array is dense.
					const int32 Index = ValhallaPS->Inventory.IndexOfByPredicate(
						[ItemId](const FValhallaInventorySlot& Slot) { return Slot.ItemId == ItemId; });
					if (Index != INDEX_NONE)
					{
						Controller.ServerEquipItem(Index);
					}
				}

				UE_LOG(LogValhallaVisual, Log, TEXT("valhalla.DebugPreset: %s [%s] -> %s"),
					*ValhallaPS->CharacterName, *ValhallaPS->ClassId.ToString(),
					*ValhallaPS->DescribeEquipment());
			});
		}));

	/** `valhalla.DebugEquip <inventorySlot> [classId]` — equip an inventory slot. */
	FAutoConsoleCommandWithWorldAndArgs GDebugEquipCommand(
		TEXT("valhalla.DebugEquip"),
		TEXT("Dev only. valhalla.DebugEquip <inventorySlot> [classId] — equip the item in an inventory slot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugEquip needs an inventory slot index."));
				return;
			}

			const int32 Slot = FCString::Atoi(*Args[0]);

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 1), [Slot](AValhallaPlayerController& Controller)
			{
				Controller.ServerEquipItem(Slot);
			});
		}));

	/** `valhalla.DebugUnequip <equipSlot> [classId]` — the other direction. */
	FAutoConsoleCommandWithWorldAndArgs GDebugUnequipCommand(
		TEXT("valhalla.DebugUnequip"),
		TEXT("Dev only. valhalla.DebugUnequip <equipSlot> [classId] — unequip a slot (weapon, helm, chest, …)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugUnequip needs an equip slot name."));
				return;
			}

			const FName Slot(*Args[0]);

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 1), [Slot](AValhallaPlayerController& Controller)
			{
				Controller.ServerUnequipItem(Slot, -1);
			});
		}));

	/**
	 * `valhalla.DebugLootNearest [classId]` — loot everything from the nearest bag.
	 *
	 * The click version needs a cursor over a particular client's viewport, which
	 * is the same problem valhalla.DebugTargetNearest solves for selection. This
	 * still goes through ServerLootAll and still fails the 200 cm reach check
	 * when the player is not actually next to the bag.
	 */
	FAutoConsoleCommandWithWorldAndArgs GDebugLootNearestCommand(
		TEXT("valhalla.DebugLootNearest"),
		TEXT("Dev only. valhalla.DebugLootNearest [classId] — loot all from the nearest loot bag."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ForEachValhallaController(World, ClassFilterFromArgs(Args, 0), [](AValhallaPlayerController& Controller)
			{
				const APawn* Pawn = Controller.GetPawn();
				if (!Pawn)
				{
					return;
				}

				AValhallaLootBag* Nearest = nullptr;
				double NearestDistSq = TNumericLimits<double>::Max();

				for (TActorIterator<AValhallaLootBag> It(Controller.GetWorld()); It; ++It)
				{
					const double DistSq = FVector::DistSquared2D(It->GetActorLocation(), Pawn->GetActorLocation());
					if (DistSq < NearestDistSq)
					{
						NearestDistSq = DistSq;
						Nearest = *It;
					}
				}

				if (!Nearest)
				{
					UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.DebugLootNearest: no loot bag in the level."));
					return;
				}

				UE_LOG(LogValhallaGame, Log, TEXT("valhalla.DebugLootNearest -> bag at %s, %.0f cm away, %d slot(s)"),
					*Nearest->GetActorLocation().ToCompactString(), FMath::Sqrt(NearestDistSq), Nearest->GetSlotCount());

				Controller.ServerLootAll(Nearest);
			});
		}));

	/** `valhalla.DebugInventory [classId]` — print the inventory, equipment and stats. */
	FAutoConsoleCommandWithWorldAndArgs GDebugInventoryCommand(
		TEXT("valhalla.DebugInventory"),
		TEXT("Dev only. valhalla.DebugInventory [classId] — log a player's inventory, equipment and resolved stats."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ForEachValhallaController(World, ClassFilterFromArgs(Args, 0), [](AValhallaPlayerController& Controller)
			{
				const AValhallaPlayerState* ValhallaPS = Controller.GetPlayerState<AValhallaPlayerState>();
				if (!ValhallaPS)
				{
					return;
				}

				UE_LOG(LogValhallaInventory, Log, TEXT("--- %s [%s lv%d]"),
					*ValhallaPS->CharacterName, *ValhallaPS->ClassId.ToString(), ValhallaPS->Level);
				UE_LOG(LogValhallaInventory, Log, TEXT("  equipment: %s"), *ValhallaPS->DescribeEquipment());

				for (int32 Index = 0; Index < ValhallaPS->Inventory.Num(); ++Index)
				{
					UE_LOG(LogValhallaInventory, Log, TEXT("  inv[%d] %s x%d"),
						Index, *ValhallaPS->Inventory[Index].ItemId.ToString(), ValhallaPS->Inventory[Index].Quantity);
				}
				if (ValhallaPS->Inventory.Num() == 0)
				{
					UE_LOG(LogValhallaInventory, Log, TEXT("  inventory is empty"));
				}

				UE_LOG(LogValhallaInventory, Log, TEXT("  stats: %s"), *DescribeStats(ValhallaPS->GetStats()));
				UE_LOG(LogValhallaInventory, Log, TEXT("  party %d: %s"),
					ValhallaPS->PartyId, *FString::Join(ValhallaPS->PartyMemberNames, TEXT(", ")));
			});
		}));

#if !UE_BUILD_SHIPPING
	/**
	 * `valhalla.Speed <cm/s> [classId]` — set a player's walk speed, to see the
	 * gaits (Project Settings > Valhalla > Locomotion) at any speed without
	 * editing classes.json. `valhalla.Speed 0` puts the class's baseSpeed back.
	 *
	 * MaxWalkSpeed is not replicated, so, like AValhallaCharacter::
	 * ApplyClassAppearance, it is written on both ends: the server's copy of
	 * the pawn (which the moves are checked against) and the owning client's
	 * (which predicts them). A one-process PIE has both worlds in this process;
	 * a standalone listen-server host is both ends at once. A remote client
	 * cannot reach the server's copy, so it refuses rather than set only its
	 * own and be corrected back every move.
	 */
	FAutoConsoleCommandWithWorldAndArgs GSpeedCommand(
		TEXT("valhalla.Speed"),
		TEXT("Dev only. valhalla.Speed <cm/s> [classId] — set a player's MaxWalkSpeed on server and owning client; 0 restores the class baseSpeed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
		{
			if (Args.Num() < 1 || !Args[0].IsNumeric() || FCString::Atof(*Args[0]) < 0.f)
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.Speed needs a speed in cm/s (0 = the class speed), e.g. 'valhalla.Speed 250 warrior'."));
				return;
			}
			if (!AuthorityWorldFor(InWorld))
			{
				UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.Speed only works on a listen-server host or in PIE (MaxWalkSpeed has to be set on the server too)."));
				return;
			}

			const float Requested = FCString::Atof(*Args[0]);
			const FString ClassFilter = ClassFilterFromArgs(Args, 1);

			TArray<UWorld*> Worlds;
#if WITH_EDITOR
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::PIE && Context.World())
					{
						Worlds.Add(Context.World());
					}
				}
			}
#endif
			if (Worlds.Num() == 0 && InWorld)
			{
				Worlds.Add(InWorld);
			}

			for (UWorld* World : Worlds)
			{
				const TCHAR* Mode = World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server");
				for (TActorIterator<AValhallaCharacter> It(World); It; ++It)
				{
					AValhallaCharacter* Body = *It;
					const AValhallaPlayerState* BodyPS = Body->GetValhallaPlayerState();
					UCharacterMovementComponent* Movement = Body->GetCharacterMovement();
					// The two copies that move the pawn; a simulated proxy only
					// replays replicated positions.
					if (!BodyPS || !Movement || !(Body->HasAuthority() || Body->IsLocallyControlled()))
					{
						continue;
					}
					if (!ClassFilter.IsEmpty() && !BodyPS->ClassId.ToString().Equals(ClassFilter, ESearchCase::IgnoreCase))
					{
						continue;
					}

					float Speed = Requested;
					if (Speed <= 0.f)
					{
						const UGameInstance* GameInstance = World->GetGameInstance();
						const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
						const FValhallaClassTemplate* ClassTemplate = Data ? Data->FindClass(BodyPS->ClassId) : nullptr;
						if (!ClassTemplate)
						{
							UE_LOG(LogValhallaGame, Warning, TEXT("valhalla.Speed [%s]: no class '%s' to restore the speed from."),
								Mode, *BodyPS->ClassId.ToString());
							continue;
						}
						Speed = ClassTemplate->BaseSpeed;
					}

					Movement->MaxWalkSpeed = Speed;
					Movement->MaxWalkSpeedCrouched = Speed;
					UE_LOG(LogValhallaGame, Log, TEXT("valhalla.Speed [%s]: %s (%s) MaxWalkSpeed=%.0f%s"),
						Mode, *BodyPS->CharacterName, *BodyPS->ClassId.ToString(), Speed,
						Requested <= 0.f ? TEXT(" (class baseSpeed)") : TEXT(""));
				}
			}
		}));
#endif // !UE_BUILD_SHIPPING
}
