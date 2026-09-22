// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaPlayerController.h"

#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaFogRenderer.h"
#include "ValhallaGame.h"
#include "ValhallaGameState.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaLootBag.h"
#include "ValhallaNPC.h"
#include "ValhallaPartySubsystem.h"
#include "ValhallaPlayerState.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaVisuals.h"

AValhallaPlayerController::AValhallaPlayerController()
{
	// 1.0 is a point-and-click RPG: the cursor is the aim reticle and never hides.
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;
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

	SecondaryClickAction = NewObject<UInputAction>(this, TEXT("IA_SecondaryClick"));
	SecondaryClickAction->ValueType = EInputActionValueType::Boolean;
	ValhallaMappingContext->MapKey(SecondaryClickAction, EKeys::RightMouseButton);

	// ── Bind ────────────────────────────────────────────────────────────
	EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AValhallaPlayerController::HandleMove);

	for (const TObjectPtr<UInputAction>& BarAction : ActionBarActions)
	{
		EnhancedInput->BindAction(BarAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandleActionBar);
	}

	EnhancedInput->BindAction(ToggleSkillsAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandleToggleSkills);
	EnhancedInput->BindAction(PrimaryClickAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandlePrimaryClick);
	EnhancedInput->BindAction(SecondaryClickAction, ETriggerEvent::Started, this, &AValhallaPlayerController::HandleSecondaryClick);

	// ── Activate ────────────────────────────────────────────────────────
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(ValhallaMappingContext, MappingContextPriority);
			UE_LOG(LogValhallaGame, Log, TEXT("Enhanced Input ready: WASD + 1-8 + K + mouse (%d actions, built in code)."),
				ActionBarActions.Num() + 4);
		}
	}
}

void AValhallaPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (IsLocalController())
	{
		UpdateAimFromCursor();
	}
}

void AValhallaPlayerController::UpdateAimFromCursor()
{
	AValhallaCharacter* ValhallaPawn = Cast<AValhallaCharacter>(GetPawn());
	if (!ValhallaPawn)
	{
		return;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return;
	}

	// The whole game is played on one plane, so intersect the cursor ray with
	// it analytically instead of tracing: a trace would snap the aim point to
	// whatever prop happens to be under the cursor, and a skill aimed past a
	// tree must land past the tree, not on it.
	const FVector PawnLocation = ValhallaPawn->GetActorLocation();
	const double GroundZ = PawnLocation.Z - ValhallaPawn->GetDefaultHalfHeight();

	if (FMath::IsNearlyZero(RayDirection.Z))
	{
		return;
	}

	const double Distance = (GroundZ - RayOrigin.Z) / RayDirection.Z;
	if (Distance <= 0.0)
	{
		// Cursor is above the horizon; keep the previous aim point.
		return;
	}

	AimWorldPoint = RayOrigin + RayDirection * Distance;

	const FVector ToAim(AimWorldPoint.X - PawnLocation.X, AimWorldPoint.Y - PawnLocation.Y, 0.0);
	if (ToAim.SizeSquared2D() < 1.0)
	{
		return;
	}

	AimYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(ToAim.Y, ToAim.X)));
	ValhallaPawn->UpdateAimYaw(AimYaw);
}

void AValhallaPlayerController::HandleMove(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	if (Axis.IsNearlyZero())
	{
		return;
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

void AValhallaPlayerController::HandleSecondaryClick()
{
	OnSecondaryClick();
}

void AValhallaPlayerController::HandleToggleSkills()
{
	OnToggleSkills();
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
	// *point* still comes from the plane maths in UpdateAimFromCursor, because a
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

void AValhallaPlayerController::ClientPartyUpdate_Implementation(int32 InPartyId, const TArray<FString>& MemberNames)
{
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

bool AValhallaPlayerController::TryHandleChatCommand(const FString& Raw)
{
	if (!Raw.StartsWith(TEXT("/")))
	{
		return false;
	}

	const FString Lower = Raw.ToLower();

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

void AValhallaPlayerController::ClientChatMessage_Implementation(const FValhallaChatMessage& Line)
{
	ChatLog.Add(Line);
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

	// The server casts what *it* has in the slot; the client only says which
	// slot was pressed. The aim point goes along for ground-targeted skills,
	// and the current selection for single-target ones.
	Skills->CastFromActionBar(Slot, GetCurrentTarget(), AimWorldPoint);
}

AValhallaLootBag* AValhallaPlayerController::TraceForLootBagUnderCursor() const
{
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex=*/false, Hit))
	{
		return nullptr;
	}

	AValhallaLootBag* Bag = Cast<AValhallaLootBag>(Hit.GetActor());
	if (!Bag)
	{
		return nullptr;
	}

	// The client's reach test is presentational only — it decides whether the
	// click is worth an RPC. The server re-runs it against its own positions in
	// ServerLootItem / ServerLootAll, and that one is the rule.
	return Bag->IsWithinReach(GetPawn()) ? Bag : nullptr;
}

void AValhallaPlayerController::OnPrimaryClick_Implementation()
{
	// A bag under the cursor beats a target under it: something you can pick up
	// is never something you want to select.
	//
	// 1.0 put both loot actions on the right button (GameScene's bag panel:
	// right-click a slot to take it, and a separate "Loot All" button). 2.0 has
	// no bag window to open yet, so the two actions are split across the two
	// buttons instead — primary takes everything, secondary takes one slot.
	// Phase 8's bag panel restores the 1.0 arrangement.
	if (AValhallaLootBag* Bag = TraceForLootBagUnderCursor())
	{
		ServerLootAll(Bag);
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

void AValhallaPlayerController::OnSecondaryClick_Implementation()
{
	// Right-clicking a bag takes one slot — the 1.0 gesture, minus the panel.
	// See OnPrimaryClick for why the two loot actions are split this way.
	if (AValhallaLootBag* Bag = TraceForLootBagUnderCursor())
	{
		ServerLootItem(Bag, 0);
		return;
	}

	UValhallaSkillComponent* Skills = GetSkillComponent();
	if (!Skills)
	{
		return;
	}

	// GameScene.ts:833 — a right click on an NPC also selects it, so attacking
	// something you have not selected is one click rather than two.
	AActor* Hit = TraceForTargetUnderCursor();
	if (Hit && Hit != GetCurrentTarget())
	{
		ServerSetTarget(Hit);
	}

	AActor* Target = Hit ? Hit : GetCurrentTarget();
	if (!Target || !UValhallaCombatLibrary::AreHostile(GetPawn(), Target))
	{
		return;
	}

	// The toggle: right-clicking an enemy you are already attacking stops.
	if (Skills->bAutoAttacking && Skills->AutoAttackTargetActor == Target)
	{
		Skills->ServerStopAutoAttack();
	}
	else
	{
		Skills->ServerStartAutoAttack(Target);
	}
}

void AValhallaPlayerController::OnToggleSkills_Implementation()
{
	// Phase 8 opens the skill book here. Until there is a UMG panel to open,
	// listing the bar in the log is the whole of the feature.
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
	void ForEachValhallaController(UWorld* World, const FString& ClassFilter, TFunctionRef<void(AValhallaPlayerController&)> Fn)
	{
		if (!World || World->GetNetMode() == NM_Client)
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

				// Keep the existing Z: the grey-box floor is flat, and picking a
				// new height would only risk dropping the capsule through it.
				const FVector Destination(X, Y, Pawn->GetActorLocation().Z);
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
			ForEachValhallaController(World, ClassFilterFromArgs(Args, 0), [World](AValhallaPlayerController& Controller)
			{
				const APawn* Pawn = Controller.GetPawn();
				if (!Pawn)
				{
					return;
				}

				AValhallaNPC* Nearest = nullptr;
				double NearestDistSq = TNumericLimits<double>::Max();

				for (TActorIterator<AValhallaNPC> It(World); It; ++It)
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

			ForEachValhallaController(World, ClassFilterFromArgs(Args, 1), [Seconds, World](AValhallaPlayerController& Controller)
			{
				APawn* Pawn = Controller.GetPawn();
				if (!Pawn)
				{
					return;
				}

				// The actor's forward, captured once: the character keeps facing
				// the cursor while it strafes, exactly as 1.0's aimAngle did, so
				// re-reading the forward each tick would make it curve.
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
			ForEachValhallaController(World, ClassFilterFromArgs(Args, 0), [World](AValhallaPlayerController& Controller)
			{
				const APawn* Pawn = Controller.GetPawn();
				if (!Pawn)
				{
					return;
				}

				AValhallaLootBag* Nearest = nullptr;
				double NearestDistSq = TNumericLimits<double>::Max();

				for (TActorIterator<AValhallaLootBag> It(World); It; ++It)
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
}
