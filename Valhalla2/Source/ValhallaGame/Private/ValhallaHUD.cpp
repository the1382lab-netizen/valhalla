// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "ValhallaGameHUDWidget.h"
#include "ValhallaCharacter.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGame.h"
#include "ValhallaGameState.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaLootBag.h"
#include "ValhallaNPC.h"
#include "ValhallaPartySubsystem.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSkillComponent.h"

namespace
{
	/** ENetRole as the three words that actually matter when reading a PIE log. */
	const TCHAR* NetRoleToString(ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_Authority:		return TEXT("Authority");
		case ROLE_AutonomousProxy:	return TEXT("AutonomousProxy");
		case ROLE_SimulatedProxy:	return TEXT("SimulatedProxy");
		default:					return TEXT("None");
		}
	}

	const FLinearColor ColourHeading(0.85f, 0.78f, 0.45f);
	const FLinearColor ColourBody(0.92f, 0.92f, 0.92f);
	const FLinearColor ColourDim(0.62f, 0.62f, 0.62f);
	const FLinearColor ColourDamage(0.96f, 0.94f, 0.90f);
	const FLinearColor ColourCrit(1.f, 0.62f, 0.15f);
	const FLinearColor ColourHeal(0.40f, 0.92f, 0.45f);
	const FLinearColor ColourMiss(0.70f, 0.70f, 0.78f);
	const FLinearColor ColourEnemy(0.85f, 0.22f, 0.22f);
	const FLinearColor ColourPanel(0.04f, 0.04f, 0.05f);

	/**
	 * Phase 8b: the Phase 2 canvas HUD is a developer tool now. Off by default;
	 * `valhalla.DebugHud 1` draws it again over the UMG HUD.
	 */
	TAutoConsoleVariable<int32> CVarDebugHud(
		TEXT("valhalla.DebugHud"),
		0,
		TEXT("1 draws the Phase 2 canvas debug HUD (stat block, inventory list, party, target pane, cast/action bar, chat, floaters) over the UMG HUD."),
		ECVF_Default);
}

AValhallaHUD::AValhallaHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AValhallaHUD::BeginPlay()
{
	Super::BeginPlay();

	// Only the local player's HUD has a viewport to draw into. On a listen
	// server a HUD exists only for the host anyway; this is belt and braces.
	if (PlayerOwner && PlayerOwner->IsLocalController() && !GameHUD)
	{
		GameHUD = CreateWidget<UValhallaGameHUDWidget>(PlayerOwner, UValhallaGameHUDWidget::StaticClass(), TEXT("ValhallaGameHUD"));
		if (GameHUD)
		{
			GameHUD->AddToViewport(/*ZOrder=*/0);
			UE_LOG(LogValhallaGame, Log, TEXT("game HUD widget created for %s"), *PlayerOwner->GetName());
		}
		else
		{
			UE_LOG(LogValhallaGame, Warning, TEXT("could not create the game HUD widget; falling back to the canvas debug HUD."));
		}
	}
}

void AValhallaHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GameHUD)
	{
		GameHUD->RemoveFromParent();
		GameHUD = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AValhallaHUD::DrawLine(const FString& Text, const FLinearColor& Colour, float& CursorY)
{
	DrawText(Text, Colour, MarginX, CursorY, GEngine ? GEngine->GetMediumFont() : nullptr, 1.f, false);
	CursorY += LineHeight;
}

void AValhallaHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	// The bag labels are gameplay UI (they are what a click opens a bag
	// through), so they are drawn whatever the debug setting.
	LootLabelRects.Reset();
	DrawLootBagLabels();

	// Everything else is the Phase 2 debug HUD: behind valhalla.DebugHud, or
	// as the fallback when there is no UMG HUD at all.
	if (CVarDebugHud.GetValueOnGameThread() == 0 && GameHUD)
	{
		return;
	}

	DrawDebugBlock();
	DrawInventoryBlock();
	DrawPartyBlock();
	DrawTargetPane();
	DrawNPCNameplates();
	DrawCastBar();
	DrawActionBar();
	DrawChatLog();
	DrawFloatingCombatText();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Debug block — Phase 2a, unchanged except for the new lines
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaHUD::DrawDebugBlock()
{
	if (!bShowDebugBlock)
	{
		return;
	}

	const AValhallaPlayerState* ValhallaPS = PlayerOwner ? PlayerOwner->GetPlayerState<AValhallaPlayerState>() : nullptr;
	float CursorY = MarginY;

	if (!ValhallaPS)
	{
		DrawLine(TEXT("Valhalla 2.0 — waiting for PlayerState"), FLinearColor::Yellow, CursorY);
		return;
	}

	const AValhallaCharacter* ValhallaPawn = PlayerOwner ? Cast<AValhallaCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const AValhallaGameState* ValhallaGS = GetWorld() ? GetWorld()->GetGameState<AValhallaGameState>() : nullptr;

	DrawLine(FString::Printf(TEXT("%s  —  %s  lv %d   xp %d"),
		*ValhallaPS->CharacterName, *ValhallaPS->ClassId.ToString(), ValhallaPS->Level, ValhallaPS->Xp), ColourHeading, CursorY);

	DrawLine(FString::Printf(TEXT("HP      %.0f / %.0f%s"),
		ValhallaPS->Hp, ValhallaPS->MaxHp,
		ValhallaPS->ShieldHp > 0.f ? *FString::Printf(TEXT("   shield %.0f"), ValhallaPS->ShieldHp) : TEXT("")),
		ValhallaPS->IsAlive() ? ColourBody : ColourEnemy, CursorY);

	DrawLine(FString::Printf(TEXT("Mana    %.0f / %.0f"), ValhallaPS->Mana, ValhallaPS->MaxMana), ColourBody, CursorY);
	DrawLine(FString::Printf(TEXT("Energy  %.0f / %.0f"), ValhallaPS->Energy, ValhallaPS->MaxEnergy), ColourBody, CursorY);

	if (!ValhallaPS->IsAlive())
	{
		DrawLine(TEXT("DEAD — respawning"), ColourEnemy, CursorY);
	}

	const UValhallaSkillComponent* Skills = ValhallaPawn ? ValhallaPawn->GetSkillComponent() : nullptr;
	if (Skills)
	{
		DrawLine(FString::Printf(TEXT("Attack  %.0f ms   auto %s"),
			Skills->GetAutoAttackIntervalMs(),
			Skills->bAutoAttacking ? TEXT("ON") : TEXT("off")), ColourDim, CursorY);
	}

	DrawLine(FString::Printf(TEXT("Vision  %.0f cm    Zone %s"),
		ValhallaPS->VisionRange, *ValhallaPS->ZoneId.ToString()), ColourDim, CursorY);

	DrawLine(FString::Printf(TEXT("Role    %s / remote %s    Ping %.0f ms"),
		ValhallaPawn ? NetRoleToString(ValhallaPawn->GetLocalRole()) : TEXT("no pawn"),
		ValhallaPawn ? NetRoleToString(ValhallaPawn->GetRemoteRole()) : TEXT("-"),
		ValhallaPS->GetPingInMilliseconds()), ColourDim, CursorY);

	DrawLine(FString::Printf(TEXT("Facing %.1f    ServerTime %.1f s"),
		ValhallaPawn ? ValhallaPawn->GetFacingYaw() : 0.f,
		ValhallaGS ? ValhallaGS->GetServerTime() : 0.0), ColourDim, CursorY);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inventory and equipment — Phase 2c
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaHUD::DrawInventoryBlock()
{
	if (!bShowDebugBlock)
	{
		return;
	}

	const AValhallaPlayerState* ValhallaPS = PlayerOwner ? PlayerOwner->GetPlayerState<AValhallaPlayerState>() : nullptr;
	if (!ValhallaPS)
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;

	// An item's display name when items.json knows it, its id otherwise. The id
	// is what every log line and console command uses, so falling back to it is
	// more useful than falling back to "unknown".
	auto ItemLabel = [Data](FName ItemId) -> FString
	{
		if (const FValhallaItemTemplate* Template = Data ? Data->FindItem(ItemId) : nullptr)
		{
			return Template->Name;
		}
		return ItemId.ToString();
	};

	float CursorY = InventoryBlockTop;

	DrawLine(TEXT("EQUIPMENT"), ColourHeading, CursorY);

	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		const EValhallaEquipSlot Slot = ValhallaEquipSlotFromIndex(Index);
		const FName ItemId = ValhallaPS->GetEquipped(Slot);
		const FString SlotName = UValhallaInventoryLibrary::EquipSlotToName(Slot).ToString();

		if (ItemId.IsNone())
		{
			DrawLine(FString::Printf(TEXT("  %-8s  —"), *SlotName), ColourDim, CursorY);
		}
		else
		{
			DrawLine(FString::Printf(TEXT("  %-8s  %s"), *SlotName, *ItemLabel(ItemId)), ColourBody, CursorY);
		}
	}

	CursorY += LineHeight * 0.5f;
	DrawLine(FString::Printf(TEXT("INVENTORY  %d / %d"),
		ValhallaPS->Inventory.Num(), Valhalla::InventoryMaxSlots), ColourHeading, CursorY);

	if (ValhallaPS->Inventory.Num() == 0)
	{
		DrawLine(TEXT("  (empty)"), ColourDim, CursorY);
		return;
	}

	for (int32 Index = 0; Index < ValhallaPS->Inventory.Num(); ++Index)
	{
		const FValhallaInventorySlot& Slot = ValhallaPS->Inventory[Index];

		// The index is printed because it is the argument valhalla.DebugEquip
		// and ServerSwapInventory take. A grid without indices would be prettier
		// and would make the console commands guesswork.
		FString Line = FString::Printf(TEXT("  [%d] %s"), Index, *ItemLabel(Slot.ItemId));
		if (Slot.Quantity > 1)
		{
			Line += FString::Printf(TEXT(" x%d"), Slot.Quantity);
		}

		DrawLine(Line, ColourBody, CursorY);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Party — Phase 2c
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaHUD::DrawPartyBlock()
{
	const AValhallaPlayerState* ValhallaPS = PlayerOwner ? PlayerOwner->GetPlayerState<AValhallaPlayerState>() : nullptr;
	if (!ValhallaPS || ValhallaPS->PartyId == 0 || ValhallaPS->PartyMemberNames.Num() == 0)
	{
		return;
	}

	constexpr float PanelWidth = 200.f;
	const float PanelHeight = LineHeight * (ValhallaPS->PartyMemberNames.Num() + 1) + 12.f;
	const float PanelX = Canvas->SizeX - PanelWidth - MarginRight;
	const float PanelY = MarginY;

	DrawRect(ColourPanel.CopyWithNewOpacity(0.72f), PanelX, PanelY, PanelWidth, PanelHeight);

	DrawText(FString::Printf(TEXT("PARTY %d  (%d/%d)"),
		ValhallaPS->PartyId, ValhallaPS->PartyMemberNames.Num(), ValhallaPartyMaxMembers),
		ColourHeading, PanelX + 8.f, PanelY + 6.f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.f, false);

	for (int32 Index = 0; Index < ValhallaPS->PartyMemberNames.Num(); ++Index)
	{
		const FString& Name = ValhallaPS->PartyMemberNames[Index];

		// Index 0 is the leader, per UValhallaPartySubsystem's ordering, and the
		// local player is marked so a two-warrior party is readable.
		FString Label = FString::Printf(TEXT("%s%s"), Index == 0 ? TEXT("* ") : TEXT("  "), *Name);
		const bool bIsSelf = Name == ValhallaPS->CharacterName;

		DrawText(Label, bIsSelf ? ColourBody : ColourDim,
			PanelX + 8.f, PanelY + 6.f + LineHeight * (Index + 1),
			GEngine ? GEngine->GetSmallFont() : nullptr, 1.f, false);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Chat — Phase 2c
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaHUD::DrawChatLog()
{
	const AValhallaPlayerController* Controller = Cast<AValhallaPlayerController>(PlayerOwner);
	if (!Controller)
	{
		return;
	}

	// The controller keeps 50 lines for the UMG chat box; the debug block shows
	// the last six, as it always did.
	const TArray<FValhallaChatMessage>& AllLines = Controller->GetChatLog();
	constexpr int32 DebugChatLines = 6;
	const TArray<FValhallaChatMessage> Lines(AllLines.GetData() + FMath::Max(0, AllLines.Num() - DebugChatLines),
		FMath::Min(AllLines.Num(), DebugChatLines));
	if (Lines.Num() == 0)
	{
		return;
	}

	const AValhallaPlayerState* ValhallaPS = PlayerOwner->GetPlayerState<AValhallaPlayerState>();

	// Bottom-left, growing upwards: the newest line is always in the same place,
	// which is what makes it readable when it is the only thing you are watching.
	const float BlockHeight = LineHeight * Lines.Num() + 10.f;
	const float BlockY = Canvas->SizeY - ChatBottomMargin - BlockHeight;

	DrawRect(ColourPanel.CopyWithNewOpacity(0.55f), MarginX - 6.f, BlockY, 460.f, BlockHeight);

	float CursorY = BlockY + 5.f;

	for (const FValhallaChatMessage& Line : Lines)
	{
		// The 1.0 colours, from GameScene.ts:5317-5326.
		FLinearColor Colour = ColourBody;
		FString Prefix;

		switch (Line.Channel)
		{
		case EValhallaChatChannel::General:
			Prefix = FString::Printf(TEXT("[G] %s:"), *Line.SenderName);
			Colour = FLinearColor::White;
			break;

		case EValhallaChatChannel::World:
			Prefix = FString::Printf(TEXT("[W] %s:"), *Line.SenderName);
			Colour = FLinearColor(1.f, 0.87f, 0.f);
			break;

		case EValhallaChatChannel::Party:
			Prefix = FString::Printf(TEXT("[P] %s:"), *Line.SenderName);
			Colour = FLinearColor(0.35f, 0.85f, 1.f);
			break;

		case EValhallaChatChannel::Whisper:
		{
			const bool bIsSender = ValhallaPS && ValhallaPS->CharacterName == Line.SenderName;
			Prefix = bIsSender
				? FString::Printf(TEXT("[To %s]:"), *Line.TargetName)
				: FString::Printf(TEXT("[From %s]:"), *Line.SenderName);
			Colour = FLinearColor(1.f, 0.53f, 0.80f);
			break;
		}

		default:
			Colour = FLinearColor(1.f, 0.67f, 0.27f);
			break;
		}

		const FString Text = Prefix.IsEmpty() ? Line.Message : FString::Printf(TEXT("%s %s"), *Prefix, *Line.Message);

		DrawText(Text, Colour, MarginX, CursorY, GEngine ? GEngine->GetSmallFont() : nullptr, 1.f, false);
		CursorY += LineHeight;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Loot bags — Phase 2c
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaHUD::DrawLootBagLabels()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APawn* Pawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;

	for (TActorIterator<AValhallaLootBag> It(World); It; ++It)
	{
		const AValhallaLootBag* Bag = *It;

		const FVector WorldPosition = Bag->GetActorLocation() + FVector(0.f, 0.f, BagLabelWorldHeight);
		const FVector Screen = Project(WorldPosition);
		if (Screen.Z <= 0.f)
		{
			continue;
		}

		// Bright when it can be looted from where the player is standing, dim
		// when it cannot — which is the 200 cm reach made visible, so a failed
		// click is explained before it happens rather than after. Drawn on a
		// backing plate over the world, so a bag behind an NPC or under its own
		// corpse is still seen, and the plate is itself a click target.
		const bool bInReach = Bag->IsWithinReach(Pawn);
		const FLinearColor Colour = bInReach ? FLinearColor(1.f, 0.82f, 0.30f) : ColourDim;

		const FString Label = FString::Printf(TEXT("Loot (%d)"), Bag->GetSlotCount());
		UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
		float TextW = 0.f, TextH = 0.f;
		GetTextSize(Label, TextW, TextH, Font, 1.f);

		const float PadX = 6.f, PadY = 2.f;
		const float BoxW = TextW + PadX * 2.f + 10.f;
		const float BoxH = TextH + PadY * 2.f;
		const float BoxX = Screen.X - BoxW * 0.5f;
		const float BoxY = Screen.Y - BoxH * 0.5f;

		DrawRect(ColourPanel.CopyWithNewOpacity(0.75f), BoxX, BoxY, BoxW, BoxH);
		// A small diamond-ish marker so the bag reads as an object, not text.
		DrawRect(Colour, BoxX + PadX, BoxY + BoxH * 0.5f - 3.f, 6.f, 6.f);
		DrawText(Label, Colour, BoxX + PadX + 10.f, BoxY + PadY, Font, 1.f, false);

		LootLabelRects.Emplace(FBox2D(FVector2D(BoxX, BoxY), FVector2D(BoxX + BoxW, BoxY + BoxH)),
			const_cast<AValhallaLootBag*>(Bag));
	}
}

AValhallaLootBag* AValhallaHUD::HitTestLootLabel(const FVector2D& ScreenPoint) const
{
	// The last drawn is on top, so search backwards.
	for (int32 Index = LootLabelRects.Num() - 1; Index >= 0; --Index)
	{
		if (LootLabelRects[Index].Key.IsInside(ScreenPoint))
		{
			if (AValhallaLootBag* Bag = LootLabelRects[Index].Value.Get())
			{
				return Bag;
			}
		}
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Target pane
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaHUD::DrawTargetPane()
{
	const AValhallaPlayerState* ValhallaPS = PlayerOwner ? PlayerOwner->GetPlayerState<AValhallaPlayerState>() : nullptr;
	AActor* Target = ValhallaPS ? ValhallaPS->GetTargetActor() : nullptr;
	if (!Target)
	{
		return;
	}

	const FValhallaCombatant Info = UValhallaCombatLibrary::DescribeCombatant(Target);
	if (!Info.bValid)
	{
		return;
	}

	constexpr float PaneWidth = 240.f;
	constexpr float PaneHeight = 46.f;
	const float PaneX = (Canvas->SizeX - PaneWidth) * 0.5f;
	const float PaneY = MarginY;

	DrawRect(ColourPanel.CopyWithNewOpacity(0.72f), PaneX, PaneY, PaneWidth, PaneHeight);

	DrawText(FString::Printf(TEXT("%s   lv %d"), *Info.DisplayName, Info.Level),
		UValhallaCombatLibrary::IsNpcTarget(Target) ? ColourEnemy : ColourHeading,
		PaneX + 8.f, PaneY + 5.f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.f, false);

	// The HP bar. Drawn from replicated Hp/MaxHp, which is all a client is told.
	constexpr float BarHeight = 12.f;
	const float BarWidth = PaneWidth - 16.f;
	const float BarY = PaneY + PaneHeight - BarHeight - 6.f;
	const float Fraction = Info.MaxHp > 0.0 ? FMath::Clamp(static_cast<float>(Info.Hp / Info.MaxHp), 0.f, 1.f) : 0.f;

	DrawRect(FLinearColor(0.12f, 0.03f, 0.03f, 0.9f), PaneX + 8.f, BarY, BarWidth, BarHeight);
	DrawRect(ColourEnemy, PaneX + 8.f, BarY, BarWidth * Fraction, BarHeight);

	DrawText(FString::Printf(TEXT("%.0f / %.0f"), Info.Hp, Info.MaxHp), ColourBody,
		PaneX + 12.f, BarY - 1.f, GEngine ? GEngine->GetSmallFont() : nullptr, 1.f, false);
}

void AValhallaHUD::DrawNPCNameplates()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 1.0 drew an NPC's name and HP over its sprite; the grey-box needs the same
	// thing to be readable at all, since every enemy is the same red rock.
	for (TActorIterator<AValhallaNPC> It(World); It; ++It)
	{
		const AValhallaNPC* Npc = *It;
		if (!Npc->IsAlive())
		{
			continue;
		}

		const FVector WorldPosition = Npc->GetActorLocation() + FVector(0.f, 0.f, NameplateWorldHeight);
		const FVector Screen = Project(WorldPosition);

		// Project returns a Z behind the camera as negative; those have to be
		// dropped or the nameplate appears mirrored on the far side of the view.
		if (Screen.Z <= 0.f)
		{
			continue;
		}

		const FString Label = FString::Printf(TEXT("%s  %.0f/%.0f"), *Npc->DisplayName, Npc->Hp, Npc->MaxHp);
		DrawText(Label, ColourEnemy, Screen.X - 40.f, Screen.Y, GEngine ? GEngine->GetSmallFont() : nullptr, 1.f, false);

		constexpr float BarWidth = 80.f;
		constexpr float BarHeight = 5.f;
		const float Fraction = Npc->MaxHp > 0.f ? FMath::Clamp(Npc->Hp / Npc->MaxHp, 0.f, 1.f) : 0.f;

		DrawRect(FLinearColor(0.1f, 0.02f, 0.02f, 0.85f), Screen.X - 40.f, Screen.Y + 13.f, BarWidth, BarHeight);
		DrawRect(ColourEnemy, Screen.X - 40.f, Screen.Y + 13.f, BarWidth * Fraction, BarHeight);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cast bar and action bar
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaHUD::DrawCastBar()
{
	const AValhallaCharacter* Character = PlayerOwner ? Cast<AValhallaCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const UValhallaSkillComponent* Skills = Character ? Character->GetSkillComponent() : nullptr;
	if (!Skills || Skills->CastingSkillId.IsNone())
	{
		return;
	}

	const float Progress = Skills->GetCastProgress();

	constexpr float BarWidth = 280.f;
	constexpr float BarHeight = 18.f;
	const float BarX = (Canvas->SizeX - BarWidth) * 0.5f;
	const float BarY = Canvas->SizeY - ActionBarBottomMargin - SlotSize - 40.f;

	DrawRect(ColourPanel.CopyWithNewOpacity(0.8f), BarX, BarY, BarWidth, BarHeight);
	DrawRect(FLinearColor(0.35f, 0.55f, 0.95f, 0.95f), BarX, BarY, BarWidth * Progress, BarHeight);

	FString SkillName = Skills->CastingSkillId.ToString();
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UValhallaDataSubsystem* Data = GameInstance->GetSubsystem<UValhallaDataSubsystem>())
		{
			if (const FValhallaSkillTemplate* Skill = Data->FindSkill(Skills->CastingSkillId))
			{
				SkillName = Skill->Name;
			}
		}
	}

	const float RemainingSeconds = (Skills->CastDurationMs / 1000.f) * (1.f - Progress);
	DrawText(FString::Printf(TEXT("%s   %.1fs"), *SkillName, RemainingSeconds), ColourBody,
		BarX + 8.f, BarY + 2.f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.f, false);
}

void AValhallaHUD::DrawActionBar()
{
	const AValhallaCharacter* Character = PlayerOwner ? Cast<AValhallaCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const UValhallaSkillComponent* Skills = Character ? Character->GetSkillComponent() : nullptr;
	if (!Skills)
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;

	const float TotalWidth = ValhallaActionBarSlots * SlotSize + (ValhallaActionBarSlots - 1) * SlotGap;
	const float StartX = (Canvas->SizeX - TotalWidth) * 0.5f;
	const float SlotY = Canvas->SizeY - ActionBarBottomMargin - SlotSize;

	for (int32 Slot = 1; Slot <= ValhallaActionBarSlots; ++Slot)
	{
		const float SlotX = StartX + (Slot - 1) * (SlotSize + SlotGap);
		const FName SkillId = Skills->GetSlotSkillId(Slot);

		DrawRect(ColourPanel.CopyWithNewOpacity(0.78f), SlotX, SlotY, SlotSize, SlotSize);

		// The slot's key, bottom-right, so the bar is usable without a manual.
		DrawText(FString::FromInt(Slot), ColourDim,
			SlotX + SlotSize - 10.f, SlotY + SlotSize - 12.f, GEngine ? GEngine->GetSmallFont() : nullptr, 1.f, false);

		if (SkillId.IsNone())
		{
			continue;
		}

		// skills.json authors a two-or-three letter `iconAbbrev` and an
		// `iconColor` for exactly this: a readable icon with no art.
		FString Abbrev = SkillId.ToString().Left(2).ToUpper();
		FLinearColor IconColour = ColourBody;

		if (const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(SkillId) : nullptr)
		{
			if (!Skill->IconAbbrev.IsEmpty())
			{
				Abbrev = Skill->IconAbbrev;
			}
			const FColor Srgb(
				static_cast<uint8>((Skill->IconColor >> 16) & 0xFF),
				static_cast<uint8>((Skill->IconColor >> 8) & 0xFF),
				static_cast<uint8>(Skill->IconColor & 0xFF));
			IconColour = FLinearColor::FromSRGBColor(Srgb);
		}

		DrawText(Abbrev, IconColour, SlotX + 6.f, SlotY + 6.f, GEngine ? GEngine->GetMediumFont() : nullptr, 1.f, false);

		// The cooldown: a dark wash over the slot plus the seconds left, which
		// is what makes a shared cooldown group visible — two slots darken at
		// once even though only one of them was pressed.
		const float Remaining = Skills->GetSlotCooldownRemaining(Slot);
		if (Remaining > 0.f)
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.62f), SlotX, SlotY, SlotSize, SlotSize);
			DrawText(FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(Remaining)), FLinearColor::White,
				SlotX + SlotSize * 0.5f - 6.f, SlotY + SlotSize * 0.5f - 8.f,
				GEngine ? GEngine->GetMediumFont() : nullptr, 1.f, false);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Floating combat text
// ─────────────────────────────────────────────────────────────────────────────

bool AValhallaHUD::DescribeEvent(const FValhallaCombatEvent& Event, FString& OutText, FLinearColor& OutColour)
{
	switch (Event.Kind)
	{
	case EValhallaCombatEventKind::PlayerHit:
	case EValhallaCombatEventKind::NpcHit:
		if (Event.Amount <= 0.f)
		{
			return false;
		}
		OutText = Event.bCrit
			? FString::Printf(TEXT("%.0f!"), Event.Amount)
			: FString::Printf(TEXT("%.0f"), Event.Amount);
		OutColour = Event.bCrit ? ColourCrit : ColourDamage;
		return true;

	case EValhallaCombatEventKind::SkillEffect:
		if (!Event.bHeal || Event.Amount <= 0.f)
		{
			return false;
		}
		OutText = FString::Printf(TEXT("+%.0f"), Event.Amount);
		OutColour = ColourHeal;
		return true;

	case EValhallaCombatEventKind::Missed:
		OutText = TEXT("miss");
		OutColour = ColourMiss;
		return true;

	case EValhallaCombatEventKind::Dodged:
		OutText = TEXT("dodge");
		OutColour = ColourMiss;
		return true;

	case EValhallaCombatEventKind::Blocked:
		OutText = TEXT("block");
		OutColour = ColourMiss;
		return true;

	case EValhallaCombatEventKind::SkillFailed:
		// Controls rework: the facing failure floats, like a miss.
		if (Event.Reason != UValhallaCombatLibrary::NotFacingReason())
		{
			return false;
		}
		OutText = TEXT("Not facing");
		OutColour = ColourMiss;
		return true;

	case EValhallaCombatEventKind::XpGained:
		OutText = FString::Printf(TEXT("+%.0f xp"), Event.Amount);
		OutColour = ColourHeading;
		return true;

	case EValhallaCombatEventKind::LevelUp:
		OutText = FString::Printf(TEXT("LEVEL %.0f"), Event.Amount);
		OutColour = ColourCrit;
		return true;

	default:
		// Everything else is a log line, not a floater: buffApplied, skillStarted
		// and the rest would only clutter the screen.
		return false;
	}
}

void AValhallaHUD::DrawFloatingCombatText()
{
	const AValhallaGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AValhallaGameState>() : nullptr;
	if (!GameState)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();

	for (const TPair<double, FValhallaCombatEvent>& Entry : GameState->GetRecentEvents())
	{
		const float Age = static_cast<float>(Now - Entry.Key);
		if (Age > AValhallaGameState::CombatEventLifetimeSeconds)
		{
			continue;
		}

		FString Text;
		FLinearColor Colour;
		if (!DescribeEvent(Entry.Value, Text, Colour))
		{
			continue;
		}

		// Prefer the actor's live position over the event's recorded one, so a
		// number follows a moving target instead of hanging where it was hit.
		FVector WorldPosition = Entry.Value.Location;
		if (const AActor* Target = Entry.Value.Target)
		{
			WorldPosition = Target->GetActorLocation();
		}
		WorldPosition.Z += NameplateWorldHeight;

		const FVector Screen = Project(WorldPosition);
		if (Screen.Z <= 0.f)
		{
			continue;
		}

		// Rise and fade over the one-second lifetime.
		const float Alpha = 1.f - (Age / AValhallaGameState::CombatEventLifetimeSeconds);
		const float RisenY = Screen.Y - FloaterRiseDistance * (1.f - Alpha) - 24.f;

		DrawText(Text, Colour.CopyWithNewOpacity(Alpha), Screen.X, RisenY,
			GEngine ? GEngine->GetMediumFont() : nullptr, 1.2f, false);
	}
}
