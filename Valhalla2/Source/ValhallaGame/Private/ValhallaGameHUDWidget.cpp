// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaGameHUDWidget.h"

#include "Blueprint/DragDropOperation.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "CoreGlobals.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "ValhallaCharacter.h"
#include "ValhallaChatCommands.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGameState.h"
#include "ValhallaInventoryLibrary.h"
#include "ValhallaLootBag.h"
#include "ValhallaNPC.h"
#include "ValhallaPartySubsystem.h"
#include "ValhallaPlayerController.h"
#include "ValhallaPlayerState.h"
#include "ValhallaSkillComponent.h"
#include "ValhallaStats.h"

DEFINE_LOG_CATEGORY(LogValhallaHUD);

/**
 * Act as this HUD's own PIE client for the rest of the scope.
 *
 * In a one-process PIE, code reached from the editor console (valhalla.UI) runs
 * with another PIE instance as the "current" one, and a Server RPC raised by a
 * client-world actor under the wrong instance id is executed *locally* instead
 * of being sent — the auto-attack "started" on the client and never on the
 * server. Every HUD entry point that ends in a Server RPC opens this scope, so
 * the call behaves exactly as a key press in that client's window. Compiled out
 * of anything that is not the editor.
 */
#if WITH_EDITOR
#define VALHALLA_HUD_PIE_SCOPE(WidgetPtr) \
	const UPackage* PiePackage_ = (WidgetPtr)->GetWorld() ? (WidgetPtr)->GetWorld()->GetOutermost() : nullptr; \
	FTemporaryPlayInEditorIDOverride PieScope_(PiePackage_ ? PiePackage_->GetPIEInstanceID() : INDEX_NONE)
#else
#define VALHALLA_HUD_PIE_SCOPE(WidgetPtr)
#endif

namespace
{
	/**
	 * 1.0's font sizes are CSS pixels; Slate's are points at 96 DPI, so a
	 * "12px" label is a 9 pt Slate font. Converted in one place so a size in
	 * ui-config.json means what it meant in the browser.
	 */
	int32 PxToSlate(int32 Px)
	{
		return FMath::Max(6, FMath::RoundToInt(Px * 0.75f));
	}

	FSlateFontInfo HudFont(int32 SlateSize, bool bBold, float Outline, const FLinearColor& OutlineColour = FLinearColor::Black)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), SlateSize);
		Font.OutlineSettings.OutlineSize = FMath::RoundToInt(Outline);
		Font.OutlineSettings.OutlineColor = OutlineColour;
		return Font;
	}

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float A = 1.f)
	{
		FLinearColor Out = FLinearColor::FromSRGBColor(FColor(R, G, B));
		Out.A = A;
		return Out;
	}

	FLinearColor Packed(int32 Rgb)
	{
		return FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>((Rgb >> 16) & 0xFF), static_cast<uint8>((Rgb >> 8) & 0xFF), static_cast<uint8>(Rgb & 0xFF)));
	}

	FLinearColor WithAlpha(FLinearColor In, float A)
	{
		In.A = A;
		return In;
	}

	FText AsText(const FString& In) { return FText::FromString(In); }

	/**
	 * The generated skill icon's tile colour, by skills.json `category`. 1.0
	 * drew the abbreviation in `iconColor` on a black slot; 2.0 keeps the code
	 * and puts it on a tile the category tints, so a bar reads at a glance as
	 * "two attacks, a heal, a buff".
	 */
	FLinearColor CategoryColour(EValhallaSkillCategory Category)
	{
		switch (Category)
		{
		case EValhallaSkillCategory::Offensive: return FLinearColor::FromSRGBColor(FColor(0xb8, 0x3a, 0x2e));
		case EValhallaSkillCategory::Defensive: return FLinearColor::FromSRGBColor(FColor(0x2e, 0x6d, 0xa4));
		case EValhallaSkillCategory::Healing:   return FLinearColor::FromSRGBColor(FColor(0x2f, 0x9e, 0x55));
		case EValhallaSkillCategory::Buff:      return FLinearColor::FromSRGBColor(FColor(0xc8, 0x96, 0x1a));
		case EValhallaSkillCategory::Debuff:    return FLinearColor::FromSRGBColor(FColor(0x84, 0x44, 0xa8));
		default:                                return FLinearColor::FromSRGBColor(FColor(0x6c, 0x74, 0x7a));
		}
	}

	/** The two-letter code: skills.json `iconAbbrev`, else the id's first two letters. */
	FString SkillCode(const FValhallaSkillTemplate& Skill)
	{
		return Skill.IconAbbrev.IsEmpty() ? Skill.Id.ToString().Left(2).ToUpper() : Skill.IconAbbrev;
	}

	/** items.ts `RARITY_COLORS`, the WoW convention 1.0 used. */
	FLinearColor RarityColour(EValhallaItemRarity Rarity)
	{
		switch (Rarity)
		{
		case EValhallaItemRarity::Uncommon:  return Srgb(0x1e, 0xff, 0x00);
		case EValhallaItemRarity::Rare:      return Srgb(0x00, 0x70, 0xdd);
		case EValhallaItemRarity::Epic:      return Srgb(0xa3, 0x35, 0xee);
		case EValhallaItemRarity::Legendary: return Srgb(0xff, 0x80, 0x00);
		default:                             return Srgb(0xff, 0xff, 0xff);
		}
	}

	const TCHAR* RarityName(EValhallaItemRarity Rarity)
	{
		switch (Rarity)
		{
		case EValhallaItemRarity::Uncommon:  return TEXT("Uncommon");
		case EValhallaItemRarity::Rare:      return TEXT("Rare");
		case EValhallaItemRarity::Epic:      return TEXT("Epic");
		case EValhallaItemRarity::Legendary: return TEXT("Legendary");
		default:                             return TEXT("Common");
		}
	}

	/**
	 * B-07 step 2: a hidden, unparented widget standing in for a designer part a
	 * Widget Blueprint left out, so the setters that write to it need no null
	 * checks. It is never added to a panel, so it never draws.
	 */
	template <typename T>
	void StandIn(UWidgetTree& Tree, TObjectPtr<T>& Member)
	{
		if (!Member)
		{
			Member = Tree.ConstructWidget<T>(T::StaticClass());
			Member->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	/** GameScene.ts CL_FILTER_LABELS, plus `casts`, which 2.0 adds so an instant cast is visible. */
	struct FFilterInfo { const TCHAR* Key; const TCHAR* Label; FLinearColor Colour; };
	const TArray<FFilterInfo>& FilterInfos()
	{
		static const TArray<FFilterInfo> Infos = {
			{ TEXT("outDmg"), TEXT("Your damage"),     Srgb(0xff, 0xcc, 0x44) },
			{ TEXT("inDmg"),  TEXT("Damage taken"),    Srgb(0xff, 0x66, 0x44) },
			{ TEXT("misses"), TEXT("Misses"),          Srgb(0x99, 0x99, 0x99) },
			{ TEXT("dodges"), TEXT("Dodges"),          Srgb(0xff, 0xff, 0xff) },
			{ TEXT("blocks"), TEXT("Blocks"),          Srgb(0x44, 0x88, 0xff) },
			{ TEXT("deaths"), TEXT("Deaths"),          Srgb(0xff, 0x44, 0x44) },
			{ TEXT("heals"),  TEXT("Healing"),         Srgb(0x44, 0xff, 0x44) },
			{ TEXT("buffs"),  TEXT("Buffs & debuffs"), Srgb(0x88, 0xcc, 0xff) },
			{ TEXT("xp"),     TEXT("XP"),              Srgb(0xff, 0xaa, 0x00) },
			{ TEXT("party"),  TEXT("Party combat"),    Srgb(0xaa, 0xbb, 0x88) },
			{ TEXT("casts"),  TEXT("Casts"),           Srgb(0xcc, 0xaa, 0xff) },
		};
		return Infos;
	}

	/** `valhalla.DebugHud` lives on AValhallaHUD; this is the HUD's rebuild command. */
	FAutoConsoleCommand GReloadUICommand(
		TEXT("valhalla.ReloadUI"),
		TEXT("Re-read ui-config.json and rebuild every game HUD in this process (the UI Layout editor's edits, live)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			int32 Count = 0;
			for (TObjectIterator<UValhallaGameHUDWidget> It; It; ++It)
			{
				if (It->GetWorld() && !It->HasAnyFlags(RF_ClassDefaultObject))
				{
					It->ReloadFromDisk();
					++Count;
				}
			}
			UE_LOG(LogValhallaHUD, Log, TEXT("valhalla.ReloadUI: rebuilt %d HUD(s)."), Count);
		}));

	/** The class id of the player a HUD belongs to, lower case, or empty. */
	FString HudClassId(const UValhallaGameHUDWidget& Hud)
	{
		const APlayerController* PC = Hud.GetOwningPlayer();
		const AValhallaPlayerState* PS = PC ? PC->GetPlayerState<AValhallaPlayerState>() : nullptr;
		return PS ? PS->ClassId.ToString().ToLower() : FString();
	}

	/**
	 * The HUDs a `valhalla.UI` command is for. `@<class>` picks the HUD of the
	 * player of that class (as `valhalla.Debug*`'s class filter picks a
	 * controller); otherwise the HUDs in `World`, and if that world has none —
	 * the editor console runs commands against the PIE *server* world, which
	 * has no HUD — every HUD in the process.
	 */
	void ForEachHudFor(UWorld* World, const FString& ClassFilter, TFunctionRef<void(UValhallaGameHUDWidget&)> Fn)
	{
		TArray<UValhallaGameHUDWidget*> All;
		for (TObjectIterator<UValhallaGameHUDWidget> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject) && It->GetWorld() && It->IsInViewport())
			{
				All.Add(*It);
			}
		}

		TArray<UValhallaGameHUDWidget*> Picked;
		for (UValhallaGameHUDWidget* Hud : All)
		{
			const bool bMatch = ClassFilter.IsEmpty() ? Hud->GetWorld() == World : HudClassId(*Hud) == ClassFilter;
			if (bMatch)
			{
				Picked.Add(Hud);
			}
		}
		if (Picked.Num() == 0 && ClassFilter.IsEmpty())
		{
			Picked = All;
		}
		if (Picked.Num() == 0)
		{
			UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI: no HUD matches '%s'."), *ClassFilter);
		}
		for (UValhallaGameHUDWidget* Hud : Picked)
		{
			Fn(*Hud);
		}
	}

	/**
	 * `valhalla.UI [@class] <verb> [args]` — drive a client's HUD. The
	 * client-side twin of the server's `valhalla.Debug*` commands: PIE cannot
	 * aim a key press at one of two clients from a script, but a console
	 * command can name the client by its class.
	 *
	 *   inventory | skills | chat [text] | say <line> | loot | close
	 *   arm <skillId> | filter <key> | tooltip <inventoryIndex> | reload | press <slot>
	 *   orbit <degrees>  (the right-mouse drag: camera and, standing, the body)
	 *   press <slot> <x> <y>  (with the aoeGround cursor read at viewport pixel x, y)
	 *   walk <w|a|s|d> <seconds>  (hold one movement key)
	 */
	FAutoConsoleCommandWithWorldAndArgs GUICommand(
		TEXT("valhalla.UI"),
		TEXT("Dev only. valhalla.UI [@class] <inventory|skills|chat [text]|say <line>|loot|close|arm <skill>|filter <key>|tooltip <i>|reload|press <slot> [x y]|orbit <deg>|walk <wasd> <s>> — drive a client's HUD."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			FString ClassFilter;
			if (Args.Num() > 0 && Args[0].StartsWith(TEXT("@")))
			{
				ClassFilter = Args[0].Mid(1).ToLower();
				Args.RemoveAt(0);
			}
			if (Args.Num() < 1)
			{
				UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI needs a verb."));
				return;
			}
			const FString Verb = Args[0].ToLower();
			FString Rest;
			for (int32 Index = 1; Index < Args.Num(); ++Index)
			{
				Rest += (Index > 1 ? TEXT(" ") : TEXT("")) + Args[Index];
			}

			ForEachHudFor(World, ClassFilter, [&](UValhallaGameHUDWidget& Hud)
			{
				VALHALLA_HUD_PIE_SCOPE(&Hud);
				if (Verb == TEXT("inventory"))     { Hud.ToggleInventory(); }
				else if (Verb == TEXT("skills"))   { Hud.ToggleSkills(); }
				else if (Verb == TEXT("chat"))     { Hud.OpenChat(Rest); }
				else if (Verb == TEXT("say"))      { Hud.SubmitChatLine(Rest); }
				else if (Verb == TEXT("close"))    { while (Hud.CloseTopmost()) {} }
				else if (Verb == TEXT("arm"))      { Hud.ArmSkill(FName(*Rest)); }
				else if (Verb == TEXT("filter"))   { Hud.ToggleLogFilter(FName(*Rest)); }
				else if (Verb == TEXT("tooltip"))  { Hud.PinInventoryTooltip(FCString::Atoi(*Rest)); }
				else if (Verb == TEXT("reload"))   { Hud.ReloadFromDisk(); }
				else if (Verb == TEXT("press"))
				{
					// An action bar slot, exactly as its key or a click on the
					// cell: OnActionBarPressed -> CastFromActionBar, so an auto
					// attack goes through ServerStartAutoAttackWith (Phase 9).
					// `press <slot> <x> <y>` also puts the aoeGround cursor read
					// at viewport pixel (x, y): typing into the editor console
					// takes the real cursor out of the PIE viewport.
					if (AValhallaPlayerController* ValhallaPC = Cast<AValhallaPlayerController>(Hud.GetOwningPlayer()))
					{
						const int32 Slot = FMath::Clamp(Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 0, 1, ValhallaActionBarSlots);
						if (Args.Num() >= 4)
						{
							ValhallaPC->PressActionBarAimedAt(Slot, FVector2D(FCString::Atof(*Args[2]), FCString::Atof(*Args[3])));
						}
						else
						{
							ValhallaPC->OnActionBarPressed(Slot);
						}
					}
				}
				else if (Verb == TEXT("walk"))
				{
					// Hold one WASD key for N seconds: HandleMove's body every
					// frame on the owning client (predicted, then sent to the
					// server as ordinary moves), exactly as a held key.
					AValhallaPlayerController* ValhallaPC = Cast<AValhallaPlayerController>(Hud.GetOwningPlayer());
					UWorld* HudWorld = Hud.GetWorld();
					const FString Key = Args.IsValidIndex(1) ? Args[1].ToLower() : FString();
					const float Seconds = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : 1.f;
					FVector2D Axis = FVector2D::ZeroVector;
					if (Key == TEXT("w")) { Axis = FVector2D(0.0, 1.0); }
					else if (Key == TEXT("s")) { Axis = FVector2D(0.0, -1.0); }
					else if (Key == TEXT("a")) { Axis = FVector2D(-1.0, 0.0); }
					else if (Key == TEXT("d")) { Axis = FVector2D(1.0, 0.0); }
					if (!ValhallaPC || !HudWorld || Axis.IsZero() || Seconds <= 0.f)
					{
						UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI walk needs w|a|s|d and a positive number of seconds."));
					}
					else
					{
						const TWeakObjectPtr<AValhallaPlayerController> WeakPC(ValhallaPC);
						TSharedRef<FTimerHandle> Hold = MakeShared<FTimerHandle>();
						FTimerManagerTimerParameters Params;
						Params.bLoop = true;
						Params.bMaxOncePerFrame = true;
						HudWorld->GetTimerManager().SetTimer(*Hold, FTimerDelegate::CreateLambda([WeakPC, Axis]()
						{
							if (AValhallaPlayerController* PC = WeakPC.Get()) { PC->ApplyMoveInput(Axis); }
						}), 0.001f, Params);

						auto LogFacing = [WeakPC, Key](const TCHAR* When)
						{
							const AValhallaPlayerController* PC = WeakPC.Get();
							const AValhallaCharacter* Body = PC ? Cast<AValhallaCharacter>(PC->GetPawn()) : nullptr;
							if (Body)
							{
								UE_LOG(LogValhallaHUD, Log, TEXT("walk %s %s: facing yaw %.1f camera yaw %.1f speed %.0f"),
									*Key, When, Body->GetFacingYaw(), Body->GetCameraWorldYaw(), Body->GetVelocity().Size2D());
							}
						};
						LogFacing(TEXT("start"));

						FTimerHandle Mid;
						HudWorld->GetTimerManager().SetTimer(Mid, FTimerDelegate::CreateLambda([LogFacing]() { LogFacing(TEXT("mid")); }),
							FMath::Max(0.05f, Seconds * 0.5f), false);

						const TWeakObjectPtr<UWorld> WeakWorld(HudWorld);
						FTimerHandle Stop;
						HudWorld->GetTimerManager().SetTimer(Stop, FTimerDelegate::CreateLambda([WeakWorld, Hold, LogFacing]()
						{
							if (UWorld* Alive = WeakWorld.Get()) { Alive->GetTimerManager().ClearTimer(*Hold); }
							LogFacing(TEXT("end"));
						}), Seconds, false);
					}
				}
				else if (Verb == TEXT("orbit"))
				{
					// The right-mouse drag, in 5-degree steps through the same
					// OrbitCameraBy the mouse delta drives, then the drag's end.
					if (AValhallaPlayerController* ValhallaPC = Cast<AValhallaPlayerController>(Hud.GetOwningPlayer()))
					{
						float Remaining = FCString::Atof(*Rest);
						while (!FMath::IsNearlyZero(Remaining))
						{
							const float Step = FMath::Clamp(Remaining, -5.f, 5.f);
							ValhallaPC->OrbitCameraBy(Step, 0.f);
							Remaining -= Step;
						}
						ValhallaPC->FinishCameraOrbitTurn();
					}
				}
				else if (Verb == TEXT("loot"))
				{
					APlayerController* PC = Hud.GetOwningPlayer();
					const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
					AValhallaLootBag* Nearest = nullptr;
					double Best = TNumericLimits<double>::Max();
					for (TActorIterator<AValhallaLootBag> BagIt(Hud.GetWorld()); BagIt && Pawn; ++BagIt)
					{
						const double D = FVector::DistSquared2D(BagIt->GetActorLocation(), Pawn->GetActorLocation());
						if (D < Best) { Best = D; Nearest = *BagIt; }
					}
					AValhallaPlayerController* ValhallaPC = Cast<AValhallaPlayerController>(PC);
					if (Nearest && ValhallaPC && Nearest->IsWithinReach(Pawn))
					{
						// The same path a click on the bag takes.
						ValhallaPC->TryOpenLootBag(Nearest);
					}
					else
					{
						UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI loot: no bag within reach."));
					}
				}
				else
				{
					UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI: unknown verb '%s'."), *Verb);
				}
			});
		}));
}

// ═════════════════════════════════════════════════════════════════════════════
//  UValhallaHUDSlotWidget
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaHUDSlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureTree();
}

void UValhallaHUDSlotWidget::EnsureTree()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("SlotTree"));
	}
	if (bTreeReady)
	{
		return;
	}
	bTreeReady = true;

	if (WidgetTree->RootWidget)
	{
		// B-07 step 2: a Widget Blueprint child laid the cell out, and its
		// widgets are already bound to the parts by name. Keep its look.
		bDesignerTree = true;
		bDesignerStack = Stack != nullptr;
		if (Frame)
		{
			DesignerFrameColour = Frame->GetBrushColor();
		}
		FillMissingParts();
		if (SelectionRing)
		{
			SelectionRing->SetVisibility(ESlateVisibility::Collapsed);
		}
		SetVisibility(ESlateVisibility::Visible);
		return;
	}

	// The code-built cell (no designer tree), as it has always been.
	Sizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	WidgetTree->RootWidget = Sizer;

	Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Frame->SetPadding(FMargin(1.f));
	Sizer->SetContent(Frame);

	Fill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Fill->SetPadding(FMargin(0.f));
	Frame->SetContent(Fill);

	Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	Fill->SetContent(Stack);

	auto AddLayer = [this](UWidget* Widget, EHorizontalAlignment H, EVerticalAlignment V, const FMargin& Pad = FMargin(0.f))
	{
		UOverlaySlot* Layer = Stack->AddChildToOverlay(Widget);
		Layer->SetHorizontalAlignment(H);
		Layer->SetVerticalAlignment(V);
		Layer->SetPadding(Pad);
	};

	Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	Icon->SetVisibility(ESlateVisibility::Collapsed);
	AddLayer(Icon, HAlign_Fill, VAlign_Fill, FMargin(2.f));

	SkillTile = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	SkillTile->SetVisibility(ESlateVisibility::Collapsed);
	AddLayer(SkillTile, HAlign_Fill, VAlign_Fill, FMargin(3.f));

	Abbrev = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Abbrev->SetFont(HudFont(11, true, 1.f));
	Abbrev->SetJustification(ETextJustify::Center);
	AddLayer(Abbrev, HAlign_Center, VAlign_Center);

	CooldownSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	CooldownFill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	CooldownSizer->SetContent(CooldownFill);
	CooldownSizer->SetVisibility(ESlateVisibility::Collapsed);
	AddLayer(CooldownSizer, HAlign_Fill, VAlign_Bottom);

	CooldownText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	CooldownText->SetFont(HudFont(12, true, 1.f));
	CooldownText->SetVisibility(ESlateVisibility::Collapsed);
	AddLayer(CooldownText, HAlign_Center, VAlign_Center);

	KeyLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	KeyLabel->SetFont(HudFont(7, false, 1.f));
	AddLayer(KeyLabel, HAlign_Right, VAlign_Bottom, FMargin(0.f, 0.f, 2.f, 0.f));

	QuantityText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	QuantityText->SetFont(HudFont(8, true, 1.f));
	AddLayer(QuantityText, HAlign_Right, VAlign_Top, FMargin(0.f, 0.f, 2.f, 0.f));

	SetVisibility(ESlateVisibility::Visible);
}

void UValhallaHUDSlotWidget::FillMissingParts()
{
	UWidgetTree& Tree = *WidgetTree;
	StandIn(Tree, Sizer);
	StandIn(Tree, Frame);
	StandIn(Tree, Fill);
	StandIn(Tree, Stack);
	StandIn(Tree, Icon);
	StandIn(Tree, SkillTile);
	StandIn(Tree, Abbrev);
	StandIn(Tree, CooldownSizer);
	StandIn(Tree, CooldownFill);
	StandIn(Tree, CooldownText);
	StandIn(Tree, KeyLabel);
	StandIn(Tree, QuantityText);
	// CooldownBar and SelectionRing stay optional: null means "not used".
}

void UValhallaHUDSlotWidget::Setup(float Size, const FLinearColor& Background, const FLinearColor& InBorder, const FLinearColor& Highlight)
{
	EnsureTree();
	CellSize = Size;
	BorderColour = InBorder;
	HighlightColour = Highlight;

	if (bDesignerTree)
	{
		// The designer's sizes and colours stand; only the sweep starts empty.
		CooldownSizer->SetHeightOverride(0.f);
		return;
	}

	if (Size > 0.f)
	{
		Sizer->SetWidthOverride(Size);
		Sizer->SetHeightOverride(Size);
	}
	Frame->SetBrushColor(InBorder);
	Fill->SetBrushColor(Background);
	CooldownSizer->SetHeightOverride(0.f);
}

void UValhallaHUDSlotWidget::SetContent(UWidget* Content)
{
	EnsureTree();
	UOverlaySlot* Layer = Stack->AddChildToOverlay(Content);
	Layer->SetHorizontalAlignment(HAlign_Fill);
	Layer->SetVerticalAlignment(VAlign_Fill);
}

void UValhallaHUDSlotWidget::SetIcon(UTexture2D* Texture)
{
	if (Texture)
	{
		Icon->SetBrushFromTexture(Texture, /*bMatchSize=*/false);
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		Icon->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UValhallaHUDSlotWidget::SetAbbrev(const FString& Text, const FLinearColor& Tint)
{
	Abbrev->SetText(AsText(Text));
	Abbrev->SetColorAndOpacity(FSlateColor(Tint));
	Abbrev->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UValhallaHUDSlotWidget::SetSkillIcon(const FString& Code, const FLinearColor& Category, const FLinearColor& SkillColour)
{
	// Rounded square, category fill, a 1.5 px rim in the skill's own colour.
	SkillTile->SetBrush(FSlateRoundedBoxBrush(Category, 6.f, SkillColour, 1.5f));
	SkillTile->SetBrushColor(FLinearColor::White);
	SkillTile->SetVisibility(ESlateVisibility::HitTestInvisible);
	SetAbbrev(Code, FLinearColor::White);
}

void UValhallaHUDSlotWidget::SetKeyLabel(const FString& Text, const FLinearColor& Colour)
{
	KeyLabel->SetText(AsText(Text));
	KeyLabel->SetColorAndOpacity(FSlateColor(Colour));
}

void UValhallaHUDSlotWidget::SetQuantity(int32 Quantity)
{
	QuantityText->SetText(AsText(Quantity > 1 ? FString::FromInt(Quantity) : FString()));
}

void UValhallaHUDSlotWidget::SetCooldown(float Fraction, const FString& Text, const FLinearColor& Colour)
{
	if (Fraction <= 0.f)
	{
		CooldownSizer->SetVisibility(ESlateVisibility::Collapsed);
		CooldownText->SetVisibility(ESlateVisibility::Collapsed);
		if (CooldownBar)
		{
			CooldownBar->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}
	// The sweep: an overlay whose height is the fraction left, draining
	// downwards, which is the 1.0 bar's vertical wipe. A designer cell sweeps
	// its own measured height (or a CooldownBar, if it has one).
	const float Left = FMath::Clamp(Fraction, 0.f, 1.f);
	float SweepHeight = CellSize - 2.f;
	if (bDesignerTree)
	{
		const float Measured = GetCachedGeometry().GetLocalSize().Y;
		SweepHeight = Measured > 0.f ? Measured : CellSize;
	}
	if (CooldownBar)
	{
		CooldownBar->SetPercent(Left);
		CooldownBar->SetFillColorAndOpacity(Colour);
		CooldownBar->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	CooldownSizer->SetHeightOverride(Left * SweepHeight);
	CooldownFill->SetBrushColor(Colour);
	CooldownSizer->SetVisibility(ESlateVisibility::HitTestInvisible);
	CooldownText->SetText(AsText(Text));
	CooldownText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UValhallaHUDSlotWidget::SetDimmed(bool bDimmed)
{
	// A designer cell without its own Stack dims as a whole.
	UWidget* Dims = (bDesignerTree && !bDesignerStack) ? WidgetTree->RootWidget.Get() : static_cast<UWidget*>(Stack.Get());
	if (Dims)
	{
		Dims->SetRenderOpacity(bDimmed ? 0.38f : 1.f);
	}
}

void UValhallaHUDSlotWidget::SetSelected(bool bSelected)
{
	if (bDesignerTree)
	{
		// The designer's ring if it drew one, else its frame in the highlight colour.
		if (SelectionRing)
		{
			SelectionRing->SetVisibility(bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		else
		{
			Frame->SetBrushColor(bSelected ? HighlightColour : DesignerFrameColour);
		}
		return;
	}
	Frame->SetBrushColor(bSelected ? HighlightColour : BorderColour);
	Frame->SetPadding(FMargin(bSelected ? 2.f : 1.f));
}

void UValhallaHUDSlotWidget::Clear()
{
	bFilled = false;
	Id = NAME_None;
	SetIcon(nullptr);
	SkillTile->SetVisibility(ESlateVisibility::Collapsed);
	SetAbbrev(FString(), FLinearColor::White);
	SetQuantity(0);
	SetCooldown(0.f, FString(), FLinearColor::Black);
	SetDimmed(false);
}

FReply UValhallaHUDSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (UValhallaGameHUDWidget* Owner = Hud.Get())
		{
			Owner->HandleSlotRightClicked(this, InMouseEvent.GetScreenSpacePosition());
		}
		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPressed = true;
		// Detect a drag, but only for cells that have something to drag; a
		// click on an empty cell (or the log) is still a click.
		if (bFilled && Kind != EValhallaHUDSlotKind::CombatLog)
		{
			return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
		}
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UValhallaHUDSlotWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bPressed)
	{
		bPressed = false;
		if (UValhallaGameHUDWidget* Owner = Hud.Get())
		{
			Owner->HandleSlotClicked(this);
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UValhallaHUDSlotWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (UValhallaGameHUDWidget* Owner = Hud.Get())
	{
		Owner->HandleSlotHovered(this, true);
	}
}

void UValhallaHUDSlotWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bPressed = false;
	if (UValhallaGameHUDWidget* Owner = Hud.Get())
	{
		Owner->HandleSlotHovered(this, false);
	}
}

void UValhallaHUDSlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	bPressed = false;

	UDragDropOperation* Operation = NewObject<UDragDropOperation>();
	Operation->Payload = this;
	Operation->Pivot = EDragPivot::CenterCenter;

	// The drag visual is a copy of what the cell shows, small.
	UTextBlock* Visual = NewObject<UTextBlock>(this);
	Visual->SetText(Abbrev->GetText().IsEmpty() ? AsText(Id.ToString()) : Abbrev->GetText());
	Visual->SetFont(HudFont(11, true, 2.f));
	Visual->SetColorAndOpacity(FSlateColor(HighlightColour));
	Operation->DefaultDragVisual = Visual;

	OutOperation = Operation;
}

bool UValhallaHUDSlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UValhallaHUDSlotWidget* Source = InOperation ? Cast<UValhallaHUDSlotWidget>(InOperation->Payload) : nullptr;
	UValhallaGameHUDWidget* Owner = Hud.Get();
	if (Source && Owner && Source != this)
	{
		Owner->HandleSlotDropped(Source, this);
		return true;
	}
	return false;
}

// ═════════════════════════════════════════════════════════════════════════════
//  UValhallaHUDButton
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaHUDButton::HandleClicked()
{
	if (UValhallaGameHUDWidget* Owner = Hud.Get())
	{
		Owner->HandleButton(Action, Index);
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  Bars
// ═════════════════════════════════════════════════════════════════════════════

// B-07 step 2: UValhallaHUDBarWidget is what MakeBar used to build as loose
// widgets in the HUD's own tree (FBar). The code-built tree is the same widgets.

float UValhallaHUDBarWidget::ClampFraction(float InFraction)
{
	return FMath::IsNaN(InFraction) ? 0.f : FMath::Clamp(InFraction, 0.f, 1.f);
}

void UValhallaHUDBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureTree();
}

void UValhallaHUDBarWidget::EnsureTree()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("BarTree"));
	}
	if (bTreeReady)
	{
		return;
	}
	bTreeReady = true;

	if (WidgetTree->RootWidget)
	{
		// A Widget Blueprint child laid the bar out; its parts are bound by name.
		bDesignerTree = true;
		if (OverlaySizer)
		{
			OverlaySizer->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (OverlayBar)
		{
			OverlayBar->SetVisibility(ESlateVisibility::Collapsed);
		}
		SetFraction(Fraction);
		return;
	}

	UWidgetTree& Tree = *WidgetTree;

	// 1.0's bar: a 2 px black frame (bg at bgAlpha), the fill, a 1 px white
	// stroke at half alpha. The stroke is the outer border here.
	Frame = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	Frame->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, 0.5f));
	Frame->SetPadding(FMargin(1.f));
	Tree.RootWidget = Frame;

	Background = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	Background->SetPadding(FMargin(1.f));
	Frame->SetContent(Background);

	Sizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Sizer->SetWidthOverride(Width);
	Background->SetContent(Sizer);

	UOverlay* Layers = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass());
	Sizer->SetContent(Layers);

	FillSizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Fill = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	FillSizer->SetContent(Fill);
	FillSizer->SetWidthOverride(Width);
	UOverlaySlot* FillLayer = Layers->AddChildToOverlay(FillSizer);
	FillLayer->SetHorizontalAlignment(HAlign_Left);
	FillLayer->SetVerticalAlignment(VAlign_Fill);

	// Shield of Faith's cyan wash over the HP bar (GameScene.ts:2161). Built
	// for every bar, shown only by one Setup with bWithOverlay.
	OverlaySizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
	OverlayFill = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	OverlayFill->SetBrushColor(Srgb(0x00, 0xcc, 0xff, 0.45f));
	OverlaySizer->SetContent(OverlayFill);
	OverlaySizer->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* WashLayer = Layers->AddChildToOverlay(OverlaySizer);
	WashLayer->SetHorizontalAlignment(HAlign_Left);
	WashLayer->SetVerticalAlignment(VAlign_Fill);

	Label = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Label->SetFont(HudFont(9, true, 1.f));
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	UOverlaySlot* LabelLayer = Layers->AddChildToOverlay(Label);
	LabelLayer->SetHorizontalAlignment(HAlign_Center);
	LabelLayer->SetVerticalAlignment(VAlign_Center);
}

void UValhallaHUDBarWidget::Setup(float InWidth, float InHeight, const FLinearColor& FillColour, const FLinearColor& BackgroundColour,
	float BackgroundAlpha, bool bWithOverlay, int32 FontSize)
{
	EnsureTree();
	Width = FMath::Max(0.f, InWidth);
	bOverlayEnabled = bWithOverlay;
	SetFillColour(FillColour);
	if (!bDesignerTree)
	{
		Background->SetBrushColor(WithAlpha(BackgroundColour, BackgroundAlpha));
		Sizer->SetWidthOverride(Width);
		Sizer->SetHeightOverride(InHeight);
		Label->SetFont(HudFont(FontSize, true, 1.f));
	}
	SetFraction(Fraction);
	SetOverlayFraction(OverlayFraction);
}

float UValhallaHUDBarWidget::FillWidth() const
{
	return bDesignerTree ? BarWidth : Width;
}

void UValhallaHUDBarWidget::SetFraction(float InFraction)
{
	Fraction = ClampFraction(InFraction);
	if (FillBar)
	{
		FillBar->SetPercent(Fraction);
	}
	if (FillSizer)
	{
		FillSizer->SetWidthOverride(Fraction * FillWidth());
		FillSizer->SetVisibility(Fraction > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void UValhallaHUDBarWidget::SetOverlayFraction(float InFraction)
{
	OverlayFraction = ClampFraction(InFraction);
	const ESlateVisibility Shown = OverlayFraction > 0.f && (bOverlayEnabled || bDesignerTree)
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (OverlayBar)
	{
		OverlayBar->SetPercent(OverlayFraction);
		OverlayBar->SetVisibility(Shown);
	}
	if (OverlaySizer)
	{
		OverlaySizer->SetWidthOverride(OverlayFraction * FillWidth());
		OverlaySizer->SetVisibility(Shown);
	}
}

void UValhallaHUDBarWidget::SetFillColour(const FLinearColor& Colour)
{
	if (Fill)
	{
		Fill->SetBrushColor(Colour);
	}
	if (FillBar)
	{
		FillBar->SetFillColorAndOpacity(Colour);
	}
}

void UValhallaHUDBarWidget::SetLabel(const FString& Text)
{
	if (Label)
	{
		Label->SetText(AsText(Text));
	}
}

void UValhallaHUDBarWidget::SetLabelColour(const FLinearColor& Colour)
{
	if (Label)
	{
		Label->SetColorAndOpacity(FSlateColor(Colour));
	}
}

void UValhallaHUDBarWidget::SetLabelVisible(bool bVisible)
{
	if (Label)
	{
		Label->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

UValhallaHUDBarWidget* UValhallaGameHUDWidget::MakeBar(float Width, float Height, const FLinearColor& FillColour,
	const FLinearColor& Background, float BackgroundAlpha, bool bWithOverlay, int32 FontSize)
{
	// B-07 step 2: one BarWidgetClass instance (a WBP_HUDBar child restyles
	// every bar) instead of loose widgets in this tree.
	UClass* BarClass = BarWidgetClass.Get() ? BarWidgetClass.Get() : UValhallaHUDBarWidget::StaticClass();
	UValhallaHUDBarWidget* Bar = WidgetTree->ConstructWidget<UValhallaHUDBarWidget>(BarClass);
	Bar->Setup(Width, Height, FillColour, Background, BackgroundAlpha, bWithOverlay, FontSize);
	return Bar;
}

UTextBlock* UValhallaGameHUDWidget::MakeText(const FString& Content, int32 Size, const FLinearColor& Colour, bool bBold, float Outline)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(AsText(Content));
	Text->SetFont(HudFont(Size, bBold, Outline));
	Text->SetColorAndOpacity(FSlateColor(Colour));
	return Text;
}

UBorder* UValhallaGameHUDWidget::MakePanel(const FLinearColor& Colour, float Alpha, float PanelPadding)
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetBrushColor(WithAlpha(Colour, Alpha));
	Panel->SetPadding(FMargin(PanelPadding));
	// A panel eats clicks, so clicking the inventory does not also select
	// whatever is standing behind it.
	Panel->SetVisibility(ESlateVisibility::Visible);
	return Panel;
}

UValhallaHUDButton* UValhallaGameHUDWidget::MakeButton(const FString& Caption, EValhallaHUDButton Action, int32 Index, const FLinearColor& Tint)
{
	UValhallaHUDButton* Button = WidgetTree->ConstructWidget<UValhallaHUDButton>(UValhallaHUDButton::StaticClass());
	Button->Action = Action;
	Button->Index = Index;
	Button->Hud = this;
	Button->SetBackgroundColor(Tint);
	Button->AddChild(MakeText(Caption, 9, FLinearColor::Black, true, 0.f));
	Button->OnClicked.AddDynamic(Button, &UValhallaHUDButton::HandleClicked);
	return Button;
}

UValhallaHUDSlotWidget* UValhallaGameHUDWidget::MakeCell(EValhallaHUDSlotKind Kind, int32 Index, float Size)
{
	// B-07 step 2: SlotWidgetClass (a WBP_HUDSlot child restyles every cell).
	// The combat log's frame is always the code-built cell: it is a panel.
	UClass* CellClass = (Kind != EValhallaHUDSlotKind::CombatLog && SlotWidgetClass.Get())
		? SlotWidgetClass.Get() : UValhallaHUDSlotWidget::StaticClass();
	UValhallaHUDSlotWidget* Cell = WidgetTree->ConstructWidget<UValhallaHUDSlotWidget>(CellClass);
	Cell->Kind = Kind;
	Cell->Index = Index;
	Cell->Hud = this;

	const bool bActionBar = Kind == EValhallaHUDSlotKind::Action;
	Cell->Setup(Size,
		bActionBar ? WithAlpha(Config.ActionBar.Bg, 1.f) : Config.Inventory.SlotBg,
		bActionBar ? Config.ActionBar.Border : Config.Inventory.Border,
		Config.Inventory.Highlight);
	Cell->Clear();
	return Cell;
}

UCanvasPanelSlot* UValhallaGameHUDWidget::Place(UWidget* Widget, const FVector2D& Anchor, const FVector2D& Alignment, const FVector2D& Position, int32 ZOrder)
{
	UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(Widget);
	CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
	CanvasSlot->SetAlignment(Alignment);
	CanvasSlot->SetPosition(Position);
	CanvasSlot->SetAutoSize(true);
	CanvasSlot->SetZOrder(ZOrder);
	return CanvasSlot;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═════════════════════════════════════════════════════════════════════════════

UValhallaGameHUDWidget::UValhallaGameHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SlotWidgetClass = UValhallaHUDSlotWidget::StaticClass();
	BarWidgetClass = UValhallaHUDBarWidget::StaticClass();
}

const TArray<FName>& UValhallaGameHUDWidget::GetRequiredPanelNames()
{
	// Keep in step with the meta = (BindWidget) members in the header.
	static const TArray<FName> Names = {
		TEXT("VitalsPanel"), TEXT("HpBar"), TEXT("ManaBar"), TEXT("ActionBarRow"),
		TEXT("ChatPanel"), TEXT("ChatScroll"), TEXT("ChatInput"),
	};
	return Names;
}

const TArray<FName>& UValhallaGameHUDWidget::GetOptionalPanelNames()
{
	// Keep in step with the meta = (BindWidgetOptional) members in the header.
	static const TArray<FName> Names = {
		TEXT("ClassText"), TEXT("CastBar"),
		TEXT("TargetFramePanel"), TEXT("TargetName"), TEXT("TargetHpBar"), TEXT("TargetBuffs"),
		TEXT("PartyPanel"), TEXT("PartyTitle"), TEXT("PartyList"), TEXT("InvitePanel"), TEXT("InviteText"),
		TEXT("CombatLogPanel"), TEXT("CombatLogTitle"), TEXT("CombatLogScroll"),
		TEXT("ChatChannelText"),
		TEXT("LootPanel"), TEXT("LootTitle"), TEXT("LootGrid"),
		TEXT("SkillsPanel"), TEXT("SkillsList"), TEXT("SkillsHint"),
		TEXT("CharacterPanel"), TEXT("EquipmentPanel"), TEXT("CharacterLevel"), TEXT("XpBar"), TEXT("CharacterStats"),
		TEXT("InventoryPanel"), TEXT("InventoryTitle"), TEXT("InventoryGrid"),
		TEXT("TooltipPanel"), TEXT("TooltipName"), TEXT("TooltipBody"),
		TEXT("DropConfirmPanel"), TEXT("DropText"),
		TEXT("DeathOverlay"), TEXT("DeathText"),
	};
	return Names;
}

bool UValhallaGameHUDWidget::WantsLayoutFromBlueprint() const
{
	// The C++ class (and its CDO) always builds itself.
	if (GetClass() == UValhallaGameHUDWidget::StaticClass() || !WidgetTree || !Cast<UCanvasPanel>(WidgetTree->RootWidget))
	{
		return false;
	}
	for (const FName Name : GetRequiredPanelNames())
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Name);
		if (!Property || !Property->GetObjectPropertyValue_InContainer(this))
		{
			return false;
		}
	}
	return true;
}

void UValhallaGameHUDWidget::ResetPanelPointers()
{
	for (const TArray<FName>* Names : { &GetRequiredPanelNames(), &GetOptionalPanelNames() })
	{
		for (const FName Name : *Names)
		{
			if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Name))
			{
				Property->SetObjectPropertyValue_InContainer(this, nullptr);
			}
		}
	}
}

void UValhallaGameHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("ValhallaHUDTree"));
	}

	// B-07 step 2: a Widget Blueprint child with a complete designer tree lays
	// the HUD out; anything else builds it in code, as it always has.
	bLayoutFromBlueprint = WantsLayoutFromBlueprint();
	if (bLayoutFromBlueprint)
	{
		RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
		UE_LOG(LogValhallaHUD, Log, TEXT("game HUD: layout from the Widget Blueprint %s."), *GetClass()->GetName());
	}
	else if (WidgetTree->RootWidget)
	{
		// A designer tree that is not usable: say what is missing, then drop it
		// (nothing has been handed to Slate yet) and build the code HUD.
		TArray<FString> Missing;
		for (const FName Name : GetRequiredPanelNames())
		{
			const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Name);
			if (!Property || !Property->GetObjectPropertyValue_InContainer(this))
			{
				Missing.Add(Name.ToString());
			}
		}
		UE_LOG(LogValhallaHUD, Error, TEXT("game HUD: %s's designer tree %s%s; building the code HUD instead."),
			*GetClass()->GetName(),
			Cast<UCanvasPanel>(WidgetTree->RootWidget) ? TEXT("") : TEXT("has no Canvas Panel root"),
			Missing.Num() > 0 ? *FString::Printf(TEXT(" lacks %s"), *FString::Join(Missing, TEXT(", "))) : TEXT(""));
		ResetPanelPointers();
		WidgetTree->RootWidget = nullptr;
	}

	if (!WidgetTree->RootWidget)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HUDRoot"));
		WidgetTree->RootWidget = RootCanvas;
	}
	// Bare canvas lets clicks through to the world; panels stop them.
	RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	for (const FFilterInfo& Info : FilterInfos())
	{
		LogFilters.Add(FName(Info.Key), true);
	}

	Rebuild();
}

void UValhallaGameHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	BindCombatEvents();
}

void UValhallaGameHUDWidget::NativeDestruct()
{
	if (AValhallaGameState* GameState = BoundGameState.Get())
	{
		GameState->OnCombatEvent.Remove(CombatEventHandle);
	}
	BoundGameState.Reset();
	Super::NativeDestruct();
}

void UValhallaGameHUDWidget::BindCombatEvents()
{
	AValhallaGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AValhallaGameState>() : nullptr;
	if (!GameState || BoundGameState.Get() == GameState)
	{
		return;
	}
	if (AValhallaGameState* Old = BoundGameState.Get())
	{
		Old->OnCombatEvent.Remove(CombatEventHandle);
	}
	CombatEventHandle = GameState->OnCombatEvent.AddUObject(this, &UValhallaGameHUDWidget::HandleCombatEvent);
	BoundGameState = GameState;
}

const UValhallaDataSubsystem* UValhallaGameHUDWidget::GetData() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
}

AValhallaPlayerController* UValhallaGameHUDWidget::GetValhallaController() const
{
	return Cast<AValhallaPlayerController>(GetOwningPlayer());
}

AValhallaPlayerState* UValhallaGameHUDWidget::GetValhallaPlayerState() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC ? PC->GetPlayerState<AValhallaPlayerState>() : nullptr;
}

AValhallaCharacter* UValhallaGameHUDWidget::GetValhallaPawn() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC ? Cast<AValhallaCharacter>(PC->GetPawn()) : nullptr;
}

void UValhallaGameHUDWidget::ReloadFromDisk()
{
	if (UValhallaDataSubsystem* Data = const_cast<UValhallaDataSubsystem*>(GetData()))
	{
		Data->ReloadUIConfig();
	}
	Rebuild();
}

void UValhallaGameHUDWidget::Rebuild()
{
	const UValhallaDataSubsystem* Data = GetData();
	Config = Data ? Data->GetUIConfig() : FValhallaUIConfig();

	if (!RootCanvas)
	{
		return;
	}

	const bool bChatWasOpen = bChatOpen;
	if (bChatOpen)
	{
		CloseChat();
	}

	if (bLayoutFromBlueprint)
	{
		// The designer's tree stays; only what C++ put into it goes.
		if (WorldLayer)
		{
			WorldLayer->RemoveFromParent();
		}
		if (FilterMenu)
		{
			FilterMenu->RemoveFromParent();
		}
		UPanelWidget* const Filled[] = { ActionBarRow.Get(), TargetBuffs.Get(), PartyList.Get(), LootGrid.Get(), EquipmentPanel.Get(), InventoryGrid.Get() };
		for (UPanelWidget* Container : Filled)
		{
			if (Container)
			{
				Container->ClearChildren();
			}
		}
	}
	else
	{
		RootCanvas->ClearChildren();
	}
	ActionCells.Reset();
	PartyRows.Reset();
	PartyNames.Reset();
	PartyBars.Reset();
	FilterLabels.Reset();
	ChatLineWidgets.Reset();
	LootCells.Reset();
	InventoryCells.Reset();
	EquipCells.Reset();
	EquipLabels.Reset();
	TargetBuffTokens.Reset();
	TargetBuffTexts.Reset();
	Plates.Reset();
	Floaters.Reset();
	PooledWidgets.Reset();
	NextFloater = 0;
	SkillsBuiltForClass = NAME_None;
	SkillsBuiltForLevel = -1;
	ChatSeenCount = -1;

	BuildAll();
	bBuilt = true;
	bCombatLogDirty = true;

	if (bChatWasOpen)
	{
		OpenChat();
	}

	UE_LOG(LogValhallaHUD, Log, TEXT("HUD built (%s) from ui-config %s (sections %s): action bar %.0f px slots, chat %.0fx%.0f, inventory %dx%d @ %.0f px."),
		bLayoutFromBlueprint ? *GetClass()->GetName() : TEXT("code layout"), *Config.Version, Config.HasAllSections() ? TEXT("all 7") : TEXT("partial, defaults used"),
		Config.ActionBar.SlotSize, Config.Chat.MaxWidth, Config.Chat.Height,
		Config.Inventory.Cols, Config.Inventory.Rows, Config.Inventory.SlotSize);
}

void UValhallaGameHUDWidget::BuildAll()
{
	if (bLayoutFromBlueprint)
	{
		BindDesignerPanels();
		PopulateDesignerPanels();
	}
	else
	{
		BuildWorldLayer();
		BuildVitals();
		BuildActionBar();
		BuildCastBar();
		BuildTargetFrame();
		BuildPartyFrame();
		BuildCombatLog();
		BuildChat();
		BuildLootPanel();
		BuildSkillsPane();
		BuildInventoryPanel();
		BuildDropConfirm();
		BuildTooltip();
		BuildDeathOverlay();
	}

	SetInventoryShown(bInventoryOpen);
	SkillsRoot->SetVisibility(bSkillsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	LootRoot->SetVisibility(LootBag.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UValhallaGameHUDWidget::SetInventoryShown(bool bShown)
{
	const ESlateVisibility Shown = bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (InventoryRoot)
	{
		InventoryRoot->SetVisibility(Shown);
	}
	// The code path's InventoryRoot holds both panels; a designer's may not.
	if (bLayoutFromBlueprint && CharacterPanel && CharacterPanel != InventoryRoot)
	{
		CharacterPanel->SetVisibility(Shown);
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  Designer path (B-07 step 2) — a WBP_GameHUD child owns the layout
// ═════════════════════════════════════════════════════════════════════════════

template <typename T>
void UValhallaGameHUDWidget::EnsureDesignerPart(TObjectPtr<T>& Member, const TCHAR* Name, UClass* ConcreteClass)
{
	if (Member)
	{
		return;
	}
	// Hidden and never parented: the feature's Tick code writes to it harmlessly.
	UClass* PartClass = ConcreteClass ? ConcreteClass : T::StaticClass();
	Member = WidgetTree->ConstructWidget<T>(PartClass);
	Member->SetVisibility(ESlateVisibility::Collapsed);
	const FName Key(Name);
	if (!WarnedMissingParts.Contains(Key))
	{
		WarnedMissingParts.Add(Key);
		UE_LOG(LogValhallaHUD, Warning, TEXT("game HUD: %s has no '%s' widget; that part of the HUD is hidden."), *GetClass()->GetName(), Name);
	}
}

void UValhallaGameHUDWidget::BindDesignerPanels()
{
	// The optional parts the designer left out: hidden stand-ins, so the Tick
	// functions need no null checks. Containers (TargetBuffs, PartyList,
	// LootGrid, SkillsList, EquipmentPanel, InventoryGrid, CombatLogScroll) get
	// none: no container, no cells.
	EnsureDesignerPart(ClassText, TEXT("ClassText"));
	EnsureDesignerPart(CastBar, TEXT("CastBar"));
	EnsureDesignerPart(TargetFramePanel, TEXT("TargetFramePanel"), UBorder::StaticClass());
	EnsureDesignerPart(TargetName, TEXT("TargetName"));
	EnsureDesignerPart(TargetHpBar, TEXT("TargetHpBar"));
	EnsureDesignerPart(PartyPanel, TEXT("PartyPanel"), UBorder::StaticClass());
	EnsureDesignerPart(PartyTitle, TEXT("PartyTitle"));
	EnsureDesignerPart(InvitePanel, TEXT("InvitePanel"), UBorder::StaticClass());
	EnsureDesignerPart(InviteText, TEXT("InviteText"));
	EnsureDesignerPart(CombatLogTitle, TEXT("CombatLogTitle"));
	EnsureDesignerPart(ChatChannelText, TEXT("ChatChannelText"));
	EnsureDesignerPart(LootPanel, TEXT("LootPanel"), UBorder::StaticClass());
	EnsureDesignerPart(LootTitle, TEXT("LootTitle"));
	EnsureDesignerPart(SkillsPanel, TEXT("SkillsPanel"), UBorder::StaticClass());
	EnsureDesignerPart(SkillsHint, TEXT("SkillsHint"));
	EnsureDesignerPart(InventoryPanel, TEXT("InventoryPanel"), UBorder::StaticClass());
	EnsureDesignerPart(InventoryTitle, TEXT("InventoryTitle"));
	EnsureDesignerPart(CharacterLevel, TEXT("CharacterLevel"));
	EnsureDesignerPart(XpBar, TEXT("XpBar"));
	EnsureDesignerPart(CharacterStats, TEXT("CharacterStats"));
	EnsureDesignerPart(TooltipPanel, TEXT("TooltipPanel"), UBorder::StaticClass());
	EnsureDesignerPart(TooltipName, TEXT("TooltipName"));
	EnsureDesignerPart(TooltipBody, TEXT("TooltipBody"));
	EnsureDesignerPart(DropConfirmPanel, TEXT("DropConfirmPanel"), UBorder::StaticClass());
	EnsureDesignerPart(DropText, TEXT("DropText"));
	EnsureDesignerPart(DeathOverlay, TEXT("DeathOverlay"), UBorder::StaticClass());
	EnsureDesignerPart(DeathText, TEXT("DeathText"));

	// The Tick / Toggle code works off these, whichever path filled them.
	ActionBarRoot = ActionBarRow;
	CastBarRoot = CastBar;
	TargetRoot = TargetFramePanel;
	PartyRoot = PartyPanel;
	InviteRoot = InvitePanel;
	LootRoot = LootPanel;
	SkillsRoot = SkillsPanel;
	InventoryRoot = InventoryPanel;
	TooltipRoot = TooltipPanel;
	DropRoot = DropConfirmPanel;
	DeathRoot = DeathOverlay;
	ChatSizer = nullptr;

	// Designer buttons (UValhallaHUDButton, Action set in the designer) and any
	// designer-placed cells report to this HUD.
	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		if (UValhallaHUDButton* Button = Cast<UValhallaHUDButton>(Widget))
		{
			Button->Hud = this;
			Button->OnClicked.AddUniqueDynamic(Button, &UValhallaHUDButton::HandleClicked);
		}
		else if (UValhallaHUDSlotWidget* Cell = Cast<UValhallaHUDSlotWidget>(Widget))
		{
			Cell->Hud = this;
		}
	});
	if (UValhallaHUDSlotWidget* LogCell = Cast<UValhallaHUDSlotWidget>(CombatLogPanel))
	{
		LogCell->Kind = EValhallaHUDSlotKind::CombatLog;
		LogCell->Index = 0;
	}

	// The chat box: the same commit handler as BuildChat's.
	ChatInput->OnTextCommitted.AddUniqueDynamic(this, &UValhallaGameHUDWidget::HandleChatCommitted);
}

void UValhallaGameHUDWidget::PopulateDesignerPanels()
{
	const FValhallaUIConfig::FHud& H = Config.Hud;
	const FValhallaUIConfig::FCastBar& C = Config.CastBar;
	const FValhallaUIConfig::FInventory& I = Config.Inventory;

	// Nameplates and floaters are C++'s own layer, under every designer panel.
	BuildWorldLayer();

	// Bars: a designer bar keeps its look, a code-built one takes ui-config's.
	HpBar->Setup(H.HpWidth, H.HpHeight, H.HpHigh, H.HpBg, H.HpBgAlpha, /*bWithOverlay=*/true, 7);
	ManaBar->Setup(H.Mana.Width, H.Mana.Height, H.Mana.Color, H.Mana.BgColor, H.Mana.BgAlpha, false, 7);
	CastBar->Setup(C.Width, C.Height, C.Color, C.BgColor, C.BgAlpha, false, PxToSlate(C.FontSize));
	CastBar->SetLabelColour(C.TextColor);
	TargetHpBar->Setup(240.f, 14.f, H.HpHigh, H.HpBg, H.HpBgAlpha, false, 8);
	XpBar->Setup(FMath::Max(40.f, I.CharPanelWidth - 8.f), 5.f, Srgb(0xff, 0xaa, 0x00), FLinearColor::Black, 0.8f, false, 6);
	XpBar->SetToolTipText(AsText(TEXT("Experience to the next level")));

	// The containers C++ fills.
	AddActionCells(ActionBarRow);
	if (TargetBuffs)    { AddTargetBuffTokens(TargetBuffs); }
	if (PartyList)      { AddPartyRows(PartyList); }
	if (LootGrid)       { AddLootCells(LootGrid); }
	if (EquipmentPanel) { AddEquipRows(EquipmentPanel); }
	if (InventoryGrid)  { AddInventoryCells(InventoryGrid); }
	// The filter menu is C++'s: top right under where the code-built log sits.
	BuildLogFilterMenu(FVector2D(-12.f, 12.f + 200.f + 4.f));

	// Clicks on empty space reach the world whatever the designer left set.
	ApplyClickThrough(RootCanvas);

	// Start states, as the code-built panels start.
	CastBarRoot->SetVisibility(ESlateVisibility::Collapsed);
	TargetRoot->SetVisibility(ESlateVisibility::Collapsed);
	PartyRoot->SetVisibility(ESlateVisibility::Collapsed);
	InviteRoot->SetVisibility(ESlateVisibility::Collapsed);
	TooltipRoot->SetVisibility(ESlateVisibility::Collapsed);
	DropRoot->SetVisibility(ESlateVisibility::Collapsed);
	DeathRoot->SetVisibility(ESlateVisibility::Collapsed);
	ChatPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ChatScroll->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ChatChannelText->SetVisibility(ESlateVisibility::Collapsed);
	ChatInput->SetVisibility(ESlateVisibility::Collapsed);
}

bool UValhallaGameHUDWidget::ApplyClickThrough(UWidget* Widget)
{
	if (!Widget || Widget == static_cast<UWidget*>(WorldLayer.Get()))
	{
		return false;
	}
	// A user widget minds its own hit-testing. Cells (and anything unknown)
	// count as clickable; a bar does not.
	if (const UUserWidget* Child = Cast<UUserWidget>(Widget))
	{
		return !Child->IsA<UValhallaHUDBarWidget>();
	}

	const bool bClickable = Widget->IsA<UButton>() || Widget->IsA<UEditableTextBox>() || Widget->IsA<UEditableText>()
		|| Widget->IsA<UMultiLineEditableTextBox>() || Widget->IsA<UMultiLineEditableText>() || Widget->IsA<UCheckBox>()
		|| Widget->IsA<USlider>() || Widget->IsA<USpinBox>() || Widget->IsA<UComboBoxString>() || Widget->IsA<UScrollBox>();

	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	if (!Panel)
	{
		// Leaves (text, images) are left as the designer set them.
		return bClickable;
	}

	bool bHoldsClickable = false;
	for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
	{
		bHoldsClickable |= ApplyClickThrough(Panel->GetChildAt(ChildIndex));
	}
	if (bClickable)
	{
		return true;
	}

	// Layout panels never need to eat clicks. A border (a panel background)
	// with something clickable in it keeps eating them, as the code-built
	// panels do, so a click on the inventory does not also walk the character.
	if (Widget->GetVisibility() == ESlateVisibility::Visible && !(bHoldsClickable && Widget->IsA<UBorder>()))
	{
		Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	return bHoldsClickable;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Runtime children — shared by the code-built and designer paths
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaGameHUDWidget::AddActionCells(UPanelWidget* Row)
{
	const FValhallaUIConfig::FActionBar& A = Config.ActionBar;
	for (int32 SlotNumber = 1; SlotNumber <= ValhallaActionBarSlots; ++SlotNumber)
	{
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Action, SlotNumber, A.SlotSize);
		Cell->SetKeyLabel(FString::FromInt(SlotNumber), A.KeyLabelColor);
		if (UHorizontalBoxSlot* CellSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Cell)))
		{
			CellSlot->SetPadding(FMargin(SlotNumber == 1 ? 0.f : A.SlotGap, 0.f, 0.f, 0.f));
		}
		ActionCells.Add(Cell);
	}
}

void UValhallaGameHUDWidget::AddTargetBuffTokens(UPanelWidget* Row)
{
	for (int32 Index = 0; Index < 8; ++Index)
	{
		UBorder* Token = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Token->SetPadding(FMargin(3.f, 1.f));
		UTextBlock* Label = MakeText(FString(), 7, FLinearColor::White, true, 1.f);
		Token->SetContent(Label);
		Token->SetVisibility(ESlateVisibility::Collapsed);
		if (UHorizontalBoxSlot* TokenSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Token)))
		{
			TokenSlot->SetPadding(FMargin(0.f, 0.f, 3.f, 0.f));
		}
		TargetBuffTokens.Add(Token);
		TargetBuffTexts.Add(Label);
	}
}

void UValhallaGameHUDWidget::AddPartyRows(UPanelWidget* Column)
{
	const FValhallaUIConfig::FInventory& I = Config.Inventory;
	for (int32 Index = 0; Index < ValhallaPartyMaxMembers; ++Index)
	{
		UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UTextBlock* Name = MakeText(FString(), 8, I.ValueColor, false);
		Row->AddChildToVerticalBox(Name);
		UValhallaHUDBarWidget* Bar = MakeBar(160.f, 8.f, Config.Hud.HpHigh, Config.Hud.HpBg, Config.Hud.HpBgAlpha, false, 6);
		Row->AddChildToVerticalBox(Bar);
		if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		}
		PartyRows.Add(Row);
		PartyNames.Add(Name);
		PartyBars.Add(Bar);
	}
}

void UValhallaGameHUDWidget::AddLootCells(UPanelWidget* Grid)
{
	const FValhallaUIConfig::FInventory& I = Config.Inventory;
	constexpr int32 LootColumns = 4;
	constexpr int32 LootPool = 12;
	UUniformGridPanel* Uniform = Cast<UUniformGridPanel>(Grid);
	for (int32 Index = 0; Index < LootPool; ++Index)
	{
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Loot, Index, I.SlotSize);
		if (Uniform)
		{
			Uniform->AddChildToUniformGrid(Cell, Index / LootColumns, Index % LootColumns);
		}
		else
		{
			Grid->AddChild(Cell);
		}
		LootCells.Add(Cell);
	}
}

void UValhallaGameHUDWidget::AddEquipRows(UPanelWidget* Column)
{
	const FValhallaUIConfig::FInventory& I = Config.Inventory;
	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Equip, Index, 26.f);
		Row->AddChildToHorizontalBox(Cell);
		UTextBlock* Label = MakeText(FString(), 7, I.LabelColor);
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 1.f));
		}
		EquipCells.Add(Cell);
		EquipLabels.Add(Label);
	}
}

void UValhallaGameHUDWidget::AddInventoryCells(UUniformGridPanel* Grid)
{
	const FValhallaUIConfig::FInventory& I = Config.Inventory;
	const int32 Cells = FMath::Clamp(I.Cols * I.Rows, 1, 64);
	for (int32 Index = 0; Index < Cells; ++Index)
	{
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Inventory, Index, I.SlotSize);
		Grid->AddChildToUniformGrid(Cell, Index / FMath::Max(1, I.Cols), Index % FMath::Max(1, I.Cols));
		InventoryCells.Add(Cell);
	}
}

void UValhallaGameHUDWidget::BuildLogFilterMenu(const FVector2D& Position)
{
	// The filter menu, right under the log.
	UBorder* Menu = MakePanel(Srgb(0x1a, 0x1a, 0x2e), 0.97f, 4.f);
	UVerticalBox* MenuColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Menu->SetContent(MenuColumn);
	MenuColumn->AddChildToVerticalBox(MakeText(TEXT("Show in combat log"), 8, Srgb(0xcc, 0xaa, 0x66), true))->SetPadding(FMargin(2.f));
	const TArray<FFilterInfo>& Infos = FilterInfos();
	for (int32 Index = 0; Index < Infos.Num(); ++Index)
	{
		UValhallaHUDButton* Row = WidgetTree->ConstructWidget<UValhallaHUDButton>(UValhallaHUDButton::StaticClass());
		Row->Action = EValhallaHUDButton::LogFilter;
		Row->Index = Index;
		Row->Hud = this;
		Row->SetBackgroundColor(Srgb(0x33, 0x33, 0x44));
		UTextBlock* Label = MakeText(FString(), 8, Infos[Index].Colour, false);
		Row->AddChild(Label);
		Row->OnClicked.AddDynamic(Row, &UValhallaHUDButton::HandleClicked);
		MenuColumn->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 1.f));
		FilterLabels.Add(Label);
	}
	MenuColumn->AddChildToVerticalBox(MakeButton(TEXT("Close"), EValhallaHUDButton::LogFilterClose, 0, Srgb(0x88, 0x88, 0xaa)))->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	FilterMenu = Menu;
	Place(FilterMenu, FVector2D(1.f, 0.f), FVector2D(1.f, 0.f), Position, 30);
	FilterMenu->SetVisibility(ESlateVisibility::Collapsed);
}

FReply UValhallaGameHUDWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Designer path: the combat log is a plain panel rather than a cell, so its
	// right click (the filter menu) bubbles up to here.
	if (bLayoutFromBlueprint && FilterMenu && CombatLogPanel && !CombatLogPanel->IsA<UValhallaHUDSlotWidget>()
		&& CombatLogPanel->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
	{
		if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			FilterMenu->SetVisibility(FilterMenu->GetVisibility() == ESlateVisibility::Collapsed
				? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			bCombatLogDirty = true;
			return FReply::Handled();
		}
		if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			FilterMenu->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Build — one function per ui-config section
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaGameHUDWidget::BuildWorldLayer()
{
	// Nameplates and floating numbers: below every panel, never hit-testable.
	WorldLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	WorldLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* LayerSlot = RootCanvas->AddChildToCanvas(WorldLayer);
	LayerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	LayerSlot->SetOffsets(FMargin(0.f));
	LayerSlot->SetZOrder(-10);

	const FValhallaUIConfig::FNameplates& NP = Config.Nameplates;
	constexpr int32 PlatePool = 48;
	for (int32 Index = 0; Index < PlatePool; ++Index)
	{
		FPlate Plate;
		UBorder* Back = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Back->SetBrushColor(WithAlpha(NP.BgColor, NP.BgAlpha));
		Back->SetPadding(FMargin(NP.BgPaddingX, NP.BgPaddingY));
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Back->SetContent(Column);
		Plate.Name = MakeText(FString(), PxToSlate(NP.FontSize), NP.Color,
			NP.FontWeight.Equals(TEXT("bold"), ESearchCase::IgnoreCase), NP.StrokeThickness * 0.5f);
		Plate.Name->SetJustification(ETextJustify::Center);
		Column->AddChildToVerticalBox(Plate.Name)->SetHorizontalAlignment(HAlign_Center);
		Plate.Bar = MakeBar(60.f, 4.f, Config.Hud.HpLow, FLinearColor::Black, 0.8f, false, 6);
		Plate.Bar->SetLabelVisible(false);
		Column->AddChildToVerticalBox(Plate.Bar)->SetHorizontalAlignment(HAlign_Center);
		Plate.Root = Back;

		UCanvasPanelSlot* PlateSlot = WorldLayer->AddChildToCanvas(Back);
		PlateSlot->SetAutoSize(true);
		PlateSlot->SetAlignment(FVector2D(0.5f, 1.f));
		Back->SetVisibility(ESlateVisibility::Collapsed);
		PooledWidgets.Add(Back);
		Plates.Add(Plate);
	}

	constexpr int32 FloaterPool = 40;
	for (int32 Index = 0; Index < FloaterPool; ++Index)
	{
		FFloater Floater;
		Floater.Text = MakeText(FString(), 14, FLinearColor::White, true, 2.f);
		UCanvasPanelSlot* FloatSlot = WorldLayer->AddChildToCanvas(Floater.Text);
		FloatSlot->SetAutoSize(true);
		FloatSlot->SetAlignment(FVector2D(0.5f, 1.f));
		Floater.Text->SetVisibility(ESlateVisibility::Collapsed);
		PooledWidgets.Add(Floater.Text);
		Floaters.Add(Floater);
	}
}

void UValhallaGameHUDWidget::BuildVitals()
{
	// hud.hpBar / manaBar / energyBar / classText, in GameScene.ts:2143's
	// geometry: the HP bar's top edge is `yOffsetFromBottom` px above the
	// bottom of the screen at x = hpBar.x; the resource bar sits
	// `height + gapAboveHp` above it; the class line 18 px above that.
	const FValhallaUIConfig::FHud& H = Config.Hud;

	HpBar = MakeBar(H.HpWidth, H.HpHeight, H.HpHigh, H.HpBg, H.HpBgAlpha, /*bWithOverlay=*/true, 7);
	Place(HpBar, FVector2D(0.f, 1.f), FVector2D(0.f, 0.f), FVector2D(H.HpX - 2.f, -H.HpYOffsetFromBottom - 2.f));

	// Mana and energy share a row, as in 1.0: a class has one or the other.
	ManaBar = MakeBar(H.Mana.Width, H.Mana.Height, H.Mana.Color, H.Mana.BgColor, H.Mana.BgAlpha, false, 7);
	const float ResourceY = -H.HpYOffsetFromBottom - H.Mana.Height - H.ManaGapAboveHp - 2.f;
	Place(ManaBar, FVector2D(0.f, 1.f), FVector2D(0.f, 0.f), FVector2D(H.HpX - 2.f, ResourceY));

	ClassText = MakeText(FString(), PxToSlate(H.ClassFontSize), H.ClassColor, false, H.ClassStrokeThickness * 0.5f);
	Place(ClassText, FVector2D(0.f, 1.f), FVector2D(0.f, 1.f), FVector2D(H.HpX, ResourceY - 4.f));

	// The code layout places the three straight on the canvas: there is no
	// vitals container, so VitalsPanel (the designer's) stays null here.
	VitalsPanel = nullptr;
}

void UValhallaGameHUDWidget::BuildActionBar()
{
	// actionBar.* — bottom centre, `bottomMargin` off the edge, a `padding`
	// box around eight `slotSize` cells `slotGap` apart.
	const FValhallaUIConfig::FActionBar& A = Config.ActionBar;

	UBorder* Panel = MakePanel(A.Bg, A.BgAlpha, A.Padding);
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Panel->SetContent(Row);
	AddActionCells(Row);
	ActionBarRow = Row;

	Place(Panel, FVector2D(0.5f, 1.f), FVector2D(0.5f, 1.f), FVector2D(0.f, -A.BottomMargin), 5);
	ActionBarRoot = Panel;
}

void UValhallaGameHUDWidget::BuildCastBar()
{
	// castBar.* — `yAboveActionBar` px above the action bar's top edge.
	const FValhallaUIConfig::FCastBar& C = Config.CastBar;
	const FValhallaUIConfig::FActionBar& A = Config.ActionBar;

	CastBar = MakeBar(C.Width, C.Height, C.Color, C.BgColor, C.BgAlpha, false, PxToSlate(C.FontSize));
	CastBar->SetLabelColour(C.TextColor);
	CastBarRoot = CastBar;

	const float ActionBarHeight = A.SlotSize + A.Padding * 2.f;
	Place(CastBarRoot, FVector2D(0.5f, 1.f), FVector2D(0.5f, 1.f),
		FVector2D(0.f, -(A.BottomMargin + ActionBarHeight + C.YAboveActionBar)), 6);
	CastBarRoot->SetVisibility(ESlateVisibility::Collapsed);
}

void UValhallaGameHUDWidget::BuildTargetFrame()
{
	// The target frame takes its type and panel colours from `nameplates`
	// (1.0 drew the selected target's plate larger, not a separate frame) and
	// its HP colours from `hud.hpBar`.
	const FValhallaUIConfig::FNameplates& NP = Config.Nameplates;

	UBorder* Panel = MakePanel(Config.Inventory.Bg, 0.85f, 6.f);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Column);

	TargetName = MakeText(FString(), PxToSlate(NP.FontSize) + 2, NP.Color, true, NP.StrokeThickness * 0.5f);
	Column->AddChildToVerticalBox(TargetName);

	TargetHpBar = MakeBar(240.f, 14.f, Config.Hud.HpHigh, Config.Hud.HpBg, Config.Hud.HpBgAlpha, false, 8);
	Column->AddChildToVerticalBox(TargetHpBar)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));

	UHorizontalBox* Buffs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(Buffs)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	AddTargetBuffTokens(Buffs);
	TargetBuffs = Buffs;

	TargetFramePanel = Panel;
	TargetRoot = TargetFramePanel;
	Place(TargetRoot, FVector2D(0.5f, 0.f), FVector2D(0.5f, 0.f), FVector2D(0.f, 12.f), 5);
	TargetRoot->SetVisibility(ESlateVisibility::Collapsed);
}

void UValhallaGameHUDWidget::BuildPartyFrame()
{
	const FValhallaUIConfig::FInventory& I = Config.Inventory;

	UBorder* Panel = MakePanel(Srgb(0x12, 0x12, 0x2a), 0.92f, 6.f);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Column);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(Header);
	PartyTitle = MakeText(TEXT("Party"), 9, I.TitleColor, true);
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(PartyTitle);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	Header->AddChildToHorizontalBox(MakeButton(TEXT("Leave"), EValhallaHUDButton::PartyLeave, 0, Srgb(0x88, 0x88, 0xaa)));

	AddPartyRows(Column);
	PartyList = Column;

	PartyPanel = Panel;
	PartyRoot = PartyPanel;
	Place(PartyRoot, FVector2D(0.f, 0.f), FVector2D(0.f, 0.f), FVector2D(12.f, 12.f), 5);
	PartyRoot->SetVisibility(ESlateVisibility::Collapsed);

	// The invite prompt — 1.0 only printed "Type /accept"; a button pair is the
	// 2.0 addition, wired to the same two RPCs.
	UBorder* Prompt = MakePanel(I.Bg, I.BgAlpha, 10.f);
	UVerticalBox* PromptColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Prompt->SetContent(PromptColumn);
	InviteText = MakeText(FString(), 10, I.TitleColor, true);
	PromptColumn->AddChildToVerticalBox(InviteText)->SetHorizontalAlignment(HAlign_Center);
	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	PromptColumn->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	Buttons->AddChildToHorizontalBox(MakeButton(TEXT("Accept"), EValhallaHUDButton::PartyAccept, 0, Srgb(0x44, 0xcc, 0x66)))->SetPadding(FMargin(4.f, 0.f));
	Buttons->AddChildToHorizontalBox(MakeButton(TEXT("Decline"), EValhallaHUDButton::PartyDecline, 0, Srgb(0xcc, 0x55, 0x55)))->SetPadding(FMargin(4.f, 0.f));
	InvitePanel = Prompt;
	InviteRoot = InvitePanel;
	Place(InviteRoot, FVector2D(0.5f, 0.f), FVector2D(0.5f, 0.f), FVector2D(0.f, 110.f), 20);
	InviteRoot->SetVisibility(ESlateVisibility::Collapsed);
}

void UValhallaGameHUDWidget::BuildCombatLog()
{
	// GameScene.ts:4788 — top right, CL_W x CL_H, right-click for filters.
	constexpr float Width = 320.f;
	constexpr float Height = 200.f;
	const FValhallaUIConfig::FChat& C = Config.Chat;

	UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::CombatLog, 0, 0.f);
	Cell->Setup(0.f, WithAlpha(C.BgColor, C.BgAlpha), WithAlpha(C.BorderColor, C.BorderAlpha), Config.Inventory.Highlight);

	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetWidthOverride(Width);
	Box->SetHeightOverride(Height);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Box->SetContent(Column);

	CombatLogTitle = MakeText(TEXT("Combat Log"), 8, Srgb(0xcc, 0xaa, 0x66), true);
	Column->AddChildToVerticalBox(CombatLogTitle)->SetPadding(FMargin(6.f, 3.f));

	CombatLogScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(CombatLogScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ScrollSlot->SetPadding(FMargin(6.f, 0.f, 4.f, 4.f));
	Cell->SetContent(Box);

	Place(Cell, FVector2D(1.f, 0.f), FVector2D(1.f, 0.f), FVector2D(-12.f, 12.f), 5);
	CombatLogPanel = Cell;

	BuildLogFilterMenu(FVector2D(-12.f, 12.f + Height + 4.f));
}

void UValhallaGameHUDWidget::BuildChat()
{
	// chat.* — bottom left at 1.0's CHAT_LEFT_X, which is the right edge of
	// the vitals plus 16 px, so the two never overlap whatever hpBar says.
	const FValhallaUIConfig::FChat& C = Config.Chat;
	const float LeftX = Config.Hud.HpX + Config.Hud.HpWidth + 16.f;

	ChatPanel = MakePanel(C.BgColor, 0.f, C.Padding);
	ChatPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetWidthOverride(C.MaxWidth);
	ChatPanel->SetContent(Box);
	ChatSizer = Box;
	LayoutForWidth = -1.f;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Box->SetContent(Column);

	ChatChannelText = MakeText(ValhallaChat::ChannelLabel(ChatChannel), PxToSlate(C.FontSize), C.System, true);
	ChatChannelText->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(ChatChannelText);

	ChatScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	ChatScroll->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(ChatScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	ChatInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ChatInput"));
	ChatInput->SetHintText(AsText(TEXT("Say something — /g /world /p /w <name> /invite /accept /decline /leave")));
	{
		// The engine default is a 24 pt font on a white field — a banner, not a
		// chat line. chat.fontSize / inputHeight, on the panel's own colours.
		FEditableTextBoxStyle InputStyle = ChatInput->GetWidgetStyle();
		FSlateFontInfo InputFont = InputStyle.TextStyle.Font;
		InputFont.Size = PxToSlate(C.FontSize);
		InputStyle.SetFont(InputFont);
		InputStyle.SetPadding(FMargin(4.f, FMath::Max(1.f, (C.InputHeight - InputFont.Size * 1.35f) * 0.5f)));
		InputStyle.SetBackgroundColor(FSlateColor(FLinearColor(C.BgColor.R, C.BgColor.G, C.BgColor.B, 0.95f)));
		InputStyle.SetForegroundColor(FSlateColor(C.General));
		InputStyle.SetFocusedForegroundColor(FSlateColor(C.General));
		ChatInput->SetWidgetStyle(InputStyle);
	}
	ChatInput->OnTextCommitted.AddDynamic(this, &UValhallaGameHUDWidget::HandleChatCommitted);
	ChatInput->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(ChatInput)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));

	Place(ChatPanel, FVector2D(0.f, 1.f), FVector2D(0.f, 1.f), FVector2D(LeftX, -C.BottomMargin), 8);
}

void UValhallaGameHUDWidget::BuildLootPanel()
{
	const FValhallaUIConfig::FInventory& I = Config.Inventory;

	UBorder* Panel = MakePanel(I.Bg, I.BgAlpha, 8.f);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Column);
	LootTitle = MakeText(TEXT("Loot"), 10, I.TitleColor, true);
	Column->AddChildToVerticalBox(LootTitle);
	Column->AddChildToVerticalBox(MakeText(TEXT("click an item to take it"), 7, I.LabelColor))->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
	Grid->SetSlotPadding(FMargin(I.SlotGap * 0.5f));
	Column->AddChildToVerticalBox(Grid);
	AddLootCells(Grid);
	LootGrid = Grid;

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	Buttons->AddChildToHorizontalBox(MakeButton(TEXT("Loot All"), EValhallaHUDButton::LootAll, 0, I.Highlight))->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	Buttons->AddChildToHorizontalBox(MakeButton(TEXT("Close"), EValhallaHUDButton::LootClose, 0, Srgb(0x88, 0x88, 0xaa)));

	LootPanel = Panel;
	LootRoot = LootPanel;
	Place(LootRoot, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 1.f), FVector2D(220.f, -40.f), 15);
}

void UValhallaGameHUDWidget::BuildSkillsPane()
{
	const FValhallaUIConfig::FInventory& I = Config.Inventory;

	UBorder* Panel = MakePanel(I.Bg, I.BgAlpha, 8.f);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Column);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(Header);
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(MakeText(TEXT("Skills  (K)"), 10, I.TitleColor, true));
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Header->AddChildToHorizontalBox(MakeButton(TEXT("X"), EValhallaHUDButton::SkillsClose, 0, Srgb(0x88, 0x88, 0xaa)));

	SkillsHint = MakeText(TEXT("Drag a skill to the action bar, or click it and then click a slot."), 7, I.LabelColor);
	SkillsHint->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(SkillsHint)->SetPadding(FMargin(0.f, 2.f, 0.f, 6.f));

	USizeBox* ListBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ListBox->SetWidthOverride(300.f);
	ListBox->SetMaxDesiredHeight(I.PanelHeight + 40.f);
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	ListBox->SetContent(Scroll);
	SkillsList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Scroll->AddChild(SkillsList);
	Column->AddChildToVerticalBox(ListBox);

	SkillsPanel = Panel;
	SkillsRoot = SkillsPanel;
	Place(SkillsRoot, FVector2D(0.f, 0.5f), FVector2D(0.f, 0.5f), FVector2D(16.f, -40.f), 12);
}

void UValhallaGameHUDWidget::BuildInventoryPanel()
{
	// inventory.* — GameScene.ts:2216's layout: the character panel
	// (charPanelWidth) and the cols x rows grid of slotSize cells slotGap
	// apart, panelGap between them, panelHeight tall, centred.
	const FValhallaUIConfig::FInventory& I = Config.Inventory;

	UHorizontalBox* Pair = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	// ── character / equipment ───────────────────────────────────────────
	UBorder* CharPanel = MakePanel(I.Bg, I.BgAlpha, 8.f);
	USizeBox* CharBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	CharBox->SetWidthOverride(I.CharPanelWidth);
	CharBox->SetMinDesiredHeight(I.PanelHeight);
	CharPanel->SetContent(CharBox);
	UVerticalBox* CharColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	CharBox->SetContent(CharColumn);
	CharColumn->AddChildToVerticalBox(MakeText(TEXT("Character  (I)"), 10, I.TitleColor, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	AddEquipRows(CharColumn);
	EquipmentPanel = CharColumn;
	// B-07: level + XP, a thin XP bar, then the owner's resolved stats
	// (AValhallaPlayerState::GetClientStats). Interim — step 3 rebuilds this
	// panel as a Widget Blueprint.
	CharacterLevel = MakeText(FString(), 7, I.ValueColor);
	CharColumn->AddChildToVerticalBox(CharacterLevel)->SetPadding(FMargin(0.f, 6.f, 0.f, 2.f));
	XpBar = MakeBar(FMath::Max(40.f, I.CharPanelWidth - 8.f), 5.f, Srgb(0xff, 0xaa, 0x00), FLinearColor::Black, 0.8f, false, 6);
	XpBar->SetToolTipText(AsText(TEXT("Experience to the next level")));
	CharColumn->AddChildToVerticalBox(XpBar)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	CharacterStats = MakeText(FString(), 7, I.ValueColor);
	CharacterStats->SetAutoWrapText(true);
	CharColumn->AddChildToVerticalBox(CharacterStats)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
	Pair->AddChildToHorizontalBox(CharPanel)->SetPadding(FMargin(0.f, 0.f, I.PanelGap, 0.f));
	CharacterPanel = CharPanel;

	// ── inventory grid ──────────────────────────────────────────────────
	UBorder* InvPanel = MakePanel(I.Bg, I.BgAlpha, 8.f);
	USizeBox* InvBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	InvBox->SetMinDesiredHeight(I.PanelHeight);
	InvPanel->SetContent(InvBox);
	UVerticalBox* InvColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	InvBox->SetContent(InvColumn);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	InvColumn->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	InventoryTitle = MakeText(TEXT("Inventory  (I)"), 10, I.TitleColor, true);
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(InventoryTitle);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Header->AddChildToHorizontalBox(MakeButton(TEXT("X"), EValhallaHUDButton::InventoryClose, 0, Srgb(0x88, 0x88, 0xaa)));

	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
	Grid->SetSlotPadding(FMargin(I.SlotGap * 0.5f));
	InvColumn->AddChildToVerticalBox(Grid);
	AddInventoryCells(Grid);
	InventoryGrid = Grid;
	InvColumn->AddChildToVerticalBox(MakeText(
		TEXT("click: equip / unequip    right-click: drop    drag: move or swap"), 7, I.LabelColor))->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	Pair->AddChildToHorizontalBox(InvPanel);
	InventoryPanel = InvPanel;

	InventoryRoot = Pair;
	Place(InventoryRoot, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(0.f, -30.f), 10);
}

void UValhallaGameHUDWidget::BuildTooltip()
{
	// GameScene.ts:6214's item tooltip: name in the rarity colour, then the
	// slot, the stats and the flavour text.
	const FValhallaUIConfig::FInventory& I = Config.Inventory;
	UBorder* Panel = MakePanel(Srgb(0x0c, 0x0c, 0x18), 0.97f, 8.f);
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetMaxDesiredWidth(240.f);
	Panel->SetContent(Box);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Box->SetContent(Column);
	TooltipName = MakeText(FString(), 10, FLinearColor::White, true);
	Column->AddChildToVerticalBox(TooltipName);
	TooltipBody = MakeText(FString(), 8, I.ValueColor);
	TooltipBody->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(TooltipBody)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	TooltipPanel = Panel;
	TooltipRoot = TooltipPanel;
	UCanvasPanelSlot* TipSlot = Place(TooltipRoot, FVector2D(0.f, 0.f), FVector2D(0.f, 0.f), FVector2D::ZeroVector, 50);
	TipSlot->SetAutoSize(true);
	TooltipRoot->SetVisibility(ESlateVisibility::Collapsed);
}

void UValhallaGameHUDWidget::BuildDropConfirm()
{
	const FValhallaUIConfig::FInventory& I = Config.Inventory;
	UBorder* Panel = MakePanel(I.Bg, 0.98f, 12.f);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Column);
	DropText = MakeText(FString(), 10, I.TitleColor, true);
	Column->AddChildToVerticalBox(DropText)->SetHorizontalAlignment(HAlign_Center);
	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	Buttons->AddChildToHorizontalBox(MakeButton(TEXT("Drop"), EValhallaHUDButton::DropConfirm, 0, Srgb(0xcc, 0x55, 0x55)))->SetPadding(FMargin(4.f, 0.f));
	Buttons->AddChildToHorizontalBox(MakeButton(TEXT("Cancel"), EValhallaHUDButton::DropCancel, 0, Srgb(0x88, 0x88, 0xaa)))->SetPadding(FMargin(4.f, 0.f));
	DropConfirmPanel = Panel;
	DropRoot = DropConfirmPanel;
	Place(DropRoot, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector, 40);
	DropRoot->SetVisibility(ESlateVisibility::Collapsed);
}

void UValhallaGameHUDWidget::BuildDeathOverlay()
{
	// deathOverlay.* — the whole screen dimmed to bgAlpha, the text centred.
	const FValhallaUIConfig::FDeathOverlay& D = Config.DeathOverlay;
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Dim->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, D.BgAlpha));
	Dim->SetHorizontalAlignment(HAlign_Center);
	Dim->SetVerticalAlignment(VAlign_Center);
	Dim->SetVisibility(ESlateVisibility::HitTestInvisible);
	DeathText = MakeText(FString(), PxToSlate(D.FontSize), D.TextColor, true, 2.f);
	DeathText->SetJustification(ETextJustify::Center);
	Dim->SetContent(DeathText);

	UCanvasPanelSlot* DimSlot = RootCanvas->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DimSlot->SetOffsets(FMargin(0.f));
	DimSlot->SetZOrder(100);
	DeathOverlay = Dim;
	DeathRoot = DeathOverlay;
	DeathRoot->SetVisibility(ESlateVisibility::Collapsed);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Tick
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaGameHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bBuilt)
	{
		return;
	}

	BindCombatEvents();

	TickLayout();
	TickVitals();
	TickActionBar();
	TickCastBar();
	TickTargetFrame();
	TickPartyFrame();
	TickChat(InDeltaTime);
	TickLootPanel();
	TickInventoryPanel();
	TickTooltip();
	TickDeathOverlay();
	TickWorldLayer(InDeltaTime);
	TickConfigWatcher(InDeltaTime);

	if (bCombatLogDirty)
	{
		RefreshCombatLog();
	}
	if (bSkillsOpen)
	{
		RefreshSkillsPane();
	}
}

void UValhallaGameHUDWidget::TickConfigWatcher(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	// The client-side half of Phase 6's hot reload. The server's watcher
	// reloads the *server's* data; a PIE client is its own game instance with
	// its own copy, and the HUD is the only thing on the client that cares
	// about ui-config.json — so the HUD watches it.
	ConfigWatchAccumulator += DeltaTime;
	if (ConfigWatchAccumulator < AValhallaGameState::DataWatchIntervalSeconds)
	{
		return;
	}
	ConfigWatchAccumulator = 0.0;

	const UValhallaDataSubsystem* Data = GetData();
	if (!Data)
	{
		return;
	}
	const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Data->GetUIConfigPath());
	if (ConfigTimestamp == FDateTime())
	{
		ConfigTimestamp = Stamp;
		return;
	}
	if (Stamp != ConfigTimestamp)
	{
		ConfigTimestamp = Stamp;
		UE_LOG(LogValhallaHUD, Log, TEXT("ui-config.json changed on disk — rebuilding the HUD."));
		ReloadFromDisk();
	}
#endif
}

void UValhallaGameHUDWidget::TickVitals()
{
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	if (!PS)
	{
		ClassText->SetText(AsText(TEXT("Waiting for the server…")));
		return;
	}

	const FValhallaUIConfig::FHud& H = Config.Hud;
	const float HpFrac = PS->MaxHp > 0.f ? PS->Hp / PS->MaxHp : 0.f;
	HpBar->SetFraction(HpFrac);
	// GameScene.ts:2156 — green over half, orange over a quarter, red below.
	HpBar->SetFillColour(HpFrac > 0.5f ? H.HpHigh : (HpFrac > 0.25f ? H.HpMid : H.HpLow));
	HpBar->SetOverlayFraction(PS->MaxHp > 0.f ? PS->ShieldHp / PS->MaxHp : 0.f);
	HpBar->SetLabel(FString::Printf(TEXT("%.0f / %.0f"), PS->Hp, PS->MaxHp));

	const bool bMana = PS->MaxMana > 0.f;
	const bool bEnergy = !bMana && PS->MaxEnergy > 0.f;
	ManaBar->SetVisibility(bMana || bEnergy ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bMana)
	{
		ManaBar->SetFraction(PS->Mana / PS->MaxMana);
		ManaBar->SetFillColour(H.Mana.Color);
		ManaBar->SetLabel(FString::Printf(TEXT("%.0f / %.0f"), PS->Mana, PS->MaxMana));
	}
	else if (bEnergy)
	{
		ManaBar->SetFraction(PS->Energy / PS->MaxEnergy);
		ManaBar->SetFillColour(H.Energy.Color);
		ManaBar->SetLabel(FString::Printf(TEXT("%.0f / %.0f"), PS->Energy, PS->MaxEnergy));
	}

	FString ClassName = PS->ClassId.ToString();
	if (const UValhallaDataSubsystem* Data = GetData())
	{
		if (const FValhallaClassTemplate* Template = Data->FindClass(PS->ClassId))
		{
			ClassName = Template->Name;
		}
	}

	// GameScene.ts:2199's line, word for word.
	FString Line = FString::Printf(TEXT("%s Lv.%d %s  HP: %.0f/%.0f"),
		*PS->CharacterName, PS->Level, *ClassName, PS->Hp, PS->MaxHp);
	if (bMana)   { Line += FString::Printf(TEXT("  MP: %.0f/%.0f"), PS->Mana, PS->MaxMana); }
	if (bEnergy) { Line += FString::Printf(TEXT("  EP: %.0f/%.0f"), FMath::FloorToFloat(PS->Energy), PS->MaxEnergy); }
	if (PS->ShieldHp > 0.f) { Line += FString::Printf(TEXT("  Shield: %.0f"), PS->ShieldHp); }
	ClassText->SetText(AsText(Line));
}

void UValhallaGameHUDWidget::TickActionBar()
{
	const AValhallaCharacter* Pawn = GetValhallaPawn();
	const UValhallaSkillComponent* Skills = Pawn ? Pawn->GetSkillComponent() : nullptr;
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	const FValhallaUIConfig::FActionBar& A = Config.ActionBar;

	for (UValhallaHUDSlotWidget* Cell : ActionCells)
	{
		const FName SkillId = Skills ? Skills->GetSlotSkillId(Cell->Index) : NAME_None;
		const FValhallaSkillTemplate* Skill = (Data && !SkillId.IsNone()) ? Data->FindSkill(SkillId) : nullptr;
		Cell->SetSelected(!ArmedSkill.IsNone());

		if (!Skill)
		{
			Cell->Clear();
			continue;
		}

		Cell->bFilled = true;
		Cell->Id = SkillId;

		// The generated icon: a category-tinted rounded tile with skills.json's
		// two-letter code, drawn by Slate rather than baked into 41 textures.
		Cell->SetSkillIcon(SkillCode(*Skill), CategoryColour(Skill->Category), Packed(Skill->IconColor));

		const float Remaining = Skills->GetSlotCooldownRemaining(Cell->Index);
		const float Total = FMath::Max(Skill->CooldownMs / 1000.f, Remaining);
		Cell->SetCooldown(Total > 0.f ? Remaining / Total : 0.f,
			FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(Remaining)),
			WithAlpha(A.CooldownOverlay, A.CooldownOverlayAlpha));

		bool bAffordable = PS && PS->IsAlive() && PS->Level >= Skill->LevelRequired;
		if (PS && Skill->ResourceType == EValhallaResourceType::Mana) { bAffordable &= PS->Mana >= Skill->ResourceCost; }
		if (PS && Skill->ResourceType == EValhallaResourceType::Energy) { bAffordable &= PS->Energy >= Skill->ResourceCost; }
		Cell->SetDimmed(!bAffordable);
	}
}

void UValhallaGameHUDWidget::TickCastBar()
{
	const AValhallaCharacter* Pawn = GetValhallaPawn();
	const UValhallaSkillComponent* Skills = Pawn ? Pawn->GetSkillComponent() : nullptr;
	if (!Skills || Skills->CastingSkillId.IsNone())
	{
		CastBarRoot->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const float Progress = Skills->GetCastProgress();
	CastBar->SetFraction(Progress);

	FString Name = Skills->CastingSkillId.ToString();
	if (const UValhallaDataSubsystem* Data = GetData())
	{
		if (const FValhallaSkillTemplate* Skill = Data->FindSkill(Skills->CastingSkillId))
		{
			Name = Skill->Name;
		}
	}
	const float Left = (Skills->CastDurationMs / 1000.f) * (1.f - Progress);
	CastBar->SetLabel(FString::Printf(TEXT("%s  %.1fs"), *Name, Left));
	CastBarRoot->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UValhallaGameHUDWidget::TickTargetFrame()
{
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	AActor* Target = PS ? PS->GetTargetActor() : nullptr;
	const FValhallaCombatant Info = UValhallaCombatLibrary::DescribeCombatant(Target);
	if (!Target || !Info.bValid)
	{
		TargetRoot->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	TargetRoot->SetVisibility(ESlateVisibility::Visible);

	const bool bHostile = UValhallaCombatLibrary::IsNpcTarget(Target);
	TargetName->SetText(AsText(FString::Printf(TEXT("%s   Lv %d"), *Info.DisplayName, Info.Level)));
	TargetName->SetColorAndOpacity(FSlateColor(bHostile ? Srgb(0xff, 0x66, 0x66) : Srgb(0x88, 0xcc, 0xff)));

	const float Frac = Info.MaxHp > 0.0 ? static_cast<float>(Info.Hp / Info.MaxHp) : 0.f;
	TargetHpBar->SetFraction(Frac);
	TargetHpBar->SetFillColour(Frac > 0.5f ? Config.Hud.HpHigh : (Frac > 0.25f ? Config.Hud.HpMid : Config.Hud.HpLow));
	TargetHpBar->SetLabel(FString::Printf(TEXT("%.0f / %.0f"), Info.Hp, Info.MaxHp));

	// Buffs: an NPC replicates its own censored view (SyncedBuffs); a player's
	// buffs are server-only, so the client keeps what buffApplied told it.
	struct FShown { FName SkillId; double ExpiresAt; };
	TArray<FShown> Shown;
	if (const AValhallaNPC* Npc = Cast<AValhallaNPC>(Target))
	{
		for (const FValhallaNPCBuffInfo& Buff : Npc->SyncedBuffs)
		{
			Shown.Add({ Buff.SkillId, Buff.ExpiresAt });
		}
	}
	else if (const TArray<FSeenBuff>* Seen = SeenBuffs.Find(Target))
	{
		for (const FSeenBuff& Buff : *Seen)
		{
			Shown.Add({ Buff.SkillId, Buff.ExpiresAt });
		}
	}

	const double Now = UValhallaCombatLibrary::GetServerTime(this);
	const UValhallaDataSubsystem* Data = GetData();
	int32 Used = 0;
	for (const FShown& Buff : Shown)
	{
		if (Used >= TargetBuffTokens.Num() || Buff.ExpiresAt <= Now)
		{
			continue;
		}
		const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(Buff.SkillId) : nullptr;
		TargetBuffTokens[Used]->SetBrushColor(Skill ? WithAlpha(Packed(Skill->IconColor), 0.9f) : FLinearColor::Gray);
		TargetBuffTexts[Used]->SetText(AsText(FString::Printf(TEXT("%s %.0f"),
			Skill && !Skill->IconAbbrev.IsEmpty() ? *Skill->IconAbbrev : *Buff.SkillId.ToString().Left(2),
			FMath::CeilToDouble(Buff.ExpiresAt - Now))));
		TargetBuffTokens[Used]->SetVisibility(ESlateVisibility::HitTestInvisible);
		++Used;
	}
	for (int32 Index = Used; Index < TargetBuffTokens.Num(); ++Index)
	{
		TargetBuffTokens[Index]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UValhallaGameHUDWidget::TickPartyFrame()
{
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	AValhallaPlayerController* PC = GetValhallaController();

	// The invite prompt.
	PendingInviter = PC ? PC->GetPendingPartyInviter() : FString();
	if (!PendingInviter.IsEmpty())
	{
		InviteText->SetText(AsText(FString::Printf(TEXT("%s invites you to a party"), *PendingInviter)));
		InviteRoot->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		InviteRoot->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (!PS || PS->PartyId == 0 || PS->PartyMemberNames.Num() == 0)
	{
		PartyRoot->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	PartyRoot->SetVisibility(ESlateVisibility::Visible);
	PartyTitle->SetText(AsText(FString::Printf(TEXT("Party  %d/%d"), PS->PartyMemberNames.Num(), ValhallaPartyMaxMembers)));

	// Every player state reaches every client (Phase 5's accepted leak), so a
	// member's HP is on hand whether or not their pawn is.
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	for (int32 Index = 0; Index < PartyRows.Num(); ++Index)
	{
		if (!PS->PartyMemberNames.IsValidIndex(Index))
		{
			PartyRows[Index]->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FString& Name = PS->PartyMemberNames[Index];
		const AValhallaPlayerState* Member = nullptr;
		if (GameState)
		{
			for (APlayerState* Candidate : GameState->PlayerArray)
			{
				const AValhallaPlayerState* Typed = Cast<AValhallaPlayerState>(Candidate);
				if (Typed && Typed->CharacterName == Name)
				{
					Member = Typed;
					break;
				}
			}
		}
		PartyRows[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
		PartyNames[Index]->SetText(AsText(FString::Printf(TEXT("%s%s%s"),
			Index == 0 ? TEXT("* ") : TEXT(""), *Name,
			Member ? *FString::Printf(TEXT("  Lv %d"), Member->Level) : TEXT(""))));
		const float Frac = Member && Member->MaxHp > 0.f ? Member->Hp / Member->MaxHp : 0.f;
		PartyBars[Index]->SetFraction(Frac);
		PartyBars[Index]->SetFillColour(Frac > 0.5f ? Config.Hud.HpHigh : (Frac > 0.25f ? Config.Hud.HpMid : Config.Hud.HpLow));
		PartyBars[Index]->SetLabel(Member ? FString::Printf(TEXT("%.0f/%.0f"), Member->Hp, Member->MaxHp) : FString());
	}
}

void UValhallaGameHUDWidget::TickChat(float /*DeltaTime*/)
{
	const AValhallaPlayerController* PC = GetValhallaController();
	if (!PC)
	{
		return;
	}

	const int32 Received = PC->GetChatReceivedCount();
	if (Received != ChatSeenCount)
	{
		// Arrival times for the idle fade, newest last, trimmed with the log.
		const int32 New = ChatSeenCount < 0 ? PC->GetChatLog().Num() : FMath::Max(0, Received - ChatSeenCount);
		const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
		for (int32 Index = 0; Index < New; ++Index)
		{
			ChatArrivalTimes.Add(ChatSeenCount < 0 ? Now - 60.0 : Now);
		}
		while (ChatArrivalTimes.Num() > PC->GetChatLog().Num())
		{
			ChatArrivalTimes.RemoveAt(0);
		}
		ChatSeenCount = Received;
		RefreshChatLines();
	}

	if (bChatOpen)
	{
		return;
	}

	// Idle: the last eight lines, each fading 10 s after it arrived.
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	const int32 Offset = ChatArrivalTimes.Num() - ChatLineWidgets.Num();
	for (int32 Index = 0; Index < ChatLineWidgets.Num(); ++Index)
	{
		const int32 TimeIndex = Offset + Index;
		const double Age = ChatArrivalTimes.IsValidIndex(TimeIndex) ? Now - ChatArrivalTimes[TimeIndex] : 999.0;
		const float Alpha = Age < 10.0 ? 1.f : FMath::Clamp(1.f - static_cast<float>((Age - 10.0) / 5.0), 0.f, 1.f);
		ChatLineWidgets[Index]->SetRenderOpacity(Alpha);
	}
}

void UValhallaGameHUDWidget::RefreshChatLines()
{
	const AValhallaPlayerController* PC = GetValhallaController();
	if (!PC || !ChatScroll)
	{
		return;
	}

	const TArray<FValhallaChatMessage>& Log = PC->GetChatLog();
	const int32 Show = bChatOpen ? FMath::Min(Log.Num(), Config.Chat.MaxMessages) : FMath::Min(Log.Num(), 8);

	ChatScroll->ClearChildren();
	ChatLineWidgets.Reset();
	for (int32 Index = Log.Num() - Show; Index < Log.Num(); ++Index)
	{
		UTextBlock* Line = MakeText(FormatChatLine(Log[Index]), PxToSlate(Config.Chat.FontSize), ChatColour(Log[Index].Channel), false, 1.f);
		Line->SetAutoWrapText(true);
		ChatScroll->AddChild(Line);
		ChatLineWidgets.Add(Line);
	}
	ChatScroll->ScrollToEnd();
}

FLinearColor UValhallaGameHUDWidget::ChatColour(EValhallaChatChannel Channel) const
{
	switch (Channel)
	{
	case EValhallaChatChannel::General: return Config.Chat.General;
	case EValhallaChatChannel::World:   return Config.Chat.World;
	case EValhallaChatChannel::Whisper: return Config.Chat.Whisper;
	// ui-config has no party colour; 1.0 had no party channel. The debug HUD's cyan.
	case EValhallaChatChannel::Party:   return Srgb(0x5a, 0xd8, 0xff);
	default:                            return Config.Chat.System;
	}
}

FString UValhallaGameHUDWidget::FormatChatLine(const FValhallaChatMessage& Line) const
{
	// GameScene.ts:5316 `receiveChatMessage`.
	switch (Line.Channel)
	{
	case EValhallaChatChannel::General: return FString::Printf(TEXT("[G] %s: %s"), *Line.SenderName, *Line.Message);
	case EValhallaChatChannel::World:   return FString::Printf(TEXT("[W] %s: %s"), *Line.SenderName, *Line.Message);
	case EValhallaChatChannel::Party:   return FString::Printf(TEXT("[P] %s: %s"), *Line.SenderName, *Line.Message);
	case EValhallaChatChannel::Whisper:
	{
		const AValhallaPlayerState* PS = GetValhallaPlayerState();
		const bool bSender = PS && PS->CharacterName == Line.SenderName;
		return bSender
			? FString::Printf(TEXT("[To %s]: %s"), *Line.TargetName, *Line.Message)
			: FString::Printf(TEXT("[From %s]: %s"), *Line.SenderName, *Line.Message);
	}
	default: return Line.Message;
	}
}

void UValhallaGameHUDWidget::TickLootPanel()
{
	AValhallaLootBag* Bag = LootBag.Get();
	const APawn* Pawn = GetValhallaPawn();
	if (!Bag || !Bag->IsWithinReach(Pawn) || Bag->Items.Num() == 0)
	{
		if (LootRoot->GetVisibility() != ESlateVisibility::Collapsed)
		{
			CloseLootPanel();
		}
		return;
	}

	LootTitle->SetText(AsText(FString::Printf(TEXT("Loot (%d)%s"), Bag->Items.Num(),
		Bag->OwnerName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" — %s"), *Bag->OwnerName))));
	const UValhallaDataSubsystem* Data = GetData();
	for (UValhallaHUDSlotWidget* Cell : LootCells)
	{
		if (!Bag->Items.IsValidIndex(Cell->Index))
		{
			Cell->Clear();
			Cell->SetVisibility(Cell->Index < 4 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			continue;
		}
		const FValhallaBagSlot& Item = Bag->Items[Cell->Index];
		const FValhallaItemTemplate* Template = Data ? Data->FindItem(Item.ItemId) : nullptr;
		Cell->SetVisibility(ESlateVisibility::Visible);
		Cell->bFilled = true;
		Cell->Id = Item.ItemId;
		UTexture2D* Texture = FindItemIcon(Item.ItemId);
		Cell->SetIcon(Texture);
		Cell->SetAbbrev(Texture ? FString() : (Template ? Template->Name.Left(3) : Item.ItemId.ToString().Left(3)),
			Template ? RarityColour(Template->Rarity) : FLinearColor::White);
		Cell->SetQuantity(Item.Quantity);
	}
}

void UValhallaGameHUDWidget::TickInventoryPanel()
{
	if (!bInventoryOpen)
	{
		return;
	}
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PS || !Data)
	{
		return;
	}

	InventoryTitle->SetText(AsText(FString::Printf(TEXT("Inventory  %d/%d  (I)"), PS->Inventory.Num(), Valhalla::InventoryMaxSlots)));

	for (UValhallaHUDSlotWidget* Cell : InventoryCells)
	{
		if (!PS->Inventory.IsValidIndex(Cell->Index))
		{
			Cell->Clear();
			continue;
		}
		const FValhallaInventorySlot& Item = PS->Inventory[Cell->Index];
		const FValhallaItemTemplate* Template = Data->FindItem(Item.ItemId);
		Cell->bFilled = true;
		Cell->Id = Item.ItemId;
		UTexture2D* Texture = FindItemIcon(Item.ItemId);
		Cell->SetIcon(Texture);
		Cell->SetAbbrev(Texture ? FString() : (Template ? Template->Name.Left(3) : Item.ItemId.ToString().Left(3)),
			Template ? RarityColour(Template->Rarity) : FLinearColor::White);
		Cell->SetQuantity(Item.Quantity);
	}

	for (UValhallaHUDSlotWidget* Cell : EquipCells)
	{
		const EValhallaEquipSlot EquipSlot = ValhallaEquipSlotFromIndex(Cell->Index);
		const FName ItemId = PS->GetEquipped(EquipSlot);
		const FValhallaItemTemplate* Template = ItemId.IsNone() ? nullptr : Data->FindItem(ItemId);
		const FString SlotName = UValhallaInventoryLibrary::EquipSlotToName(EquipSlot).ToString();
		if (!Template)
		{
			Cell->Clear();
			EquipLabels[Cell->Index]->SetText(AsText(FString::Printf(TEXT("%s  —"), *SlotName)));
			EquipLabels[Cell->Index]->SetColorAndOpacity(FSlateColor(Config.Inventory.LabelColor));
			continue;
		}
		Cell->bFilled = true;
		Cell->Id = ItemId;
		UTexture2D* Texture = FindItemIcon(ItemId);
		Cell->SetIcon(Texture);
		Cell->SetAbbrev(Texture ? FString() : Template->Name.Left(2), RarityColour(Template->Rarity));
		EquipLabels[Cell->Index]->SetText(AsText(FString::Printf(TEXT("%s  %s"), *SlotName, *Template->Name)));
		EquipLabels[Cell->Index]->SetColorAndOpacity(FSlateColor(RarityColour(Template->Rarity)));
	}

	// B-07: the owner's read-only copy of its resolved stats (class base +
	// level growth + gear, exactly what the server fights with). Rates are
	// 0-1 decimals; the resists and defense are flat ratings, not percentages.
	const FValhallaResolvedStats& S = PS->GetClientStats();
	auto Pct = [](float Rate) { return FString::Printf(TEXT("%.0f%%"), Rate * 100.f); };

	const int32 XpNeeded = PS->GetXpToNextLevel();
	const float XpFraction = PS->GetXpFraction();
	CharacterLevel->SetText(AsText(XpNeeded > 0
		? FString::Printf(TEXT("Level %d   XP %d / %d (%.0f%%)"), PS->Level, PS->Xp, XpNeeded, XpFraction * 100.f)
		: FString::Printf(TEXT("Level %d   XP max"), PS->Level)));
	XpBar->SetFraction(XpFraction);

	// The main-hand weapon's roll and swing, from items.json (EquipWeapon is
	// public), the swing with this player's DEX applied as the server does.
	FString WeaponLine = TEXT("Weapon  —");
	if (const FValhallaItemTemplate* Weapon = PS->EquipWeapon.IsNone() ? nullptr : Data->FindItem(PS->EquipWeapon))
	{
		float MinDamage = 0.f, MaxDamage = 0.f;
		Weapon->GetDamageRange(MinDamage, MaxDamage);
		const FString Damage = MaxDamage <= 0.f ? FString(TEXT("—"))
			: FMath::IsNearlyEqual(MinDamage, MaxDamage) ? FString::Printf(TEXT("%.0f"), MaxDamage)
			: FString::Printf(TEXT("%.0f–%.0f"), MinDamage, MaxDamage);
		WeaponLine = FString::Printf(TEXT("Weapon  %s dmg"), *Damage);
		if (Weapon->bHasAttackSpeed && Weapon->AttackSpeedMs > 0.f)
		{
			const double Swing = Valhalla::Stats::ComputeAutoAttackSpeed(Weapon->AttackSpeedMs, S.Dexterity);
			WeaponLine += FString::Printf(TEXT("   %.1fs (%.1fs w/ DEX)"), Weapon->AttackSpeedMs / 1000.f, Swing / 1000.0);
		}
	}

	const FString Pool = PS->MaxMana > 0.f
		? FString::Printf(TEXT("Mana %.0f/%.0f"), PS->Mana, PS->MaxMana)
		: FString::Printf(TEXT("Energy %.0f/%.0f"), PS->Energy, PS->MaxEnergy);

	CharacterStats->SetText(AsText(FString::Printf(
		TEXT("HP %.0f/%.0f   %s\n")
		TEXT("STR %.0f  STA %.0f  DEX %.0f  INT %.0f  WIS %.0f\n")
		TEXT("Phys Def %.0f   Phys Resist %.0f   Spell Resist %.0f\n")
		TEXT("Block %s   Dodge %s   Crit %s / x%.2f\n")
		TEXT("%s\n")
		TEXT("Speed %.0f   Vision %.0f"),
		PS->Hp, PS->MaxHp, *Pool,
		S.Strength, S.Stamina, S.Dexterity, S.Intelligence, S.Wisdom,
		S.PhysicalDefense, S.PhysicalResist, S.SpellResist,
		*Pct(S.BlockRating), *Pct(S.DodgeRating), *Pct(S.CritChance), 1.f + S.CritDamage,
		*WeaponLine,
		S.Speed, PS->VisionRange)));
}

void UValhallaGameHUDWidget::TickTooltip()
{
	if (TooltipRoot->GetVisibility() == ESlateVisibility::Collapsed || bTooltipPinned)
	{
		return;
	}
	const FVector2D Mouse = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
	if (UCanvasPanelSlot* TipSlot = Cast<UCanvasPanelSlot>(TooltipRoot->Slot))
	{
		// Below-right of the cursor, flipped to the other side of it when that
		// would run off the canvas (the action bar is at the very bottom).
		FVector2D Position = Mouse + FVector2D(18.f, 18.f);
		const FVector2D Canvas = RootCanvas ? RootCanvas->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
		const FVector2D Tip = TooltipRoot->GetDesiredSize();
		if (Canvas.X > 0.f && Canvas.Y > 0.f)
		{
			if (Position.X + Tip.X > Canvas.X) { Position.X = FMath::Max(0.f, Mouse.X - 12.f - Tip.X); }
			if (Position.Y + Tip.Y > Canvas.Y) { Position.Y = FMath::Max(0.f, Mouse.Y - 12.f - Tip.Y); }
		}
		TipSlot->SetPosition(Position);
	}
}

void UValhallaGameHUDWidget::TickLayout()
{
	// A designer tree places the chat itself; this is the code layout's fix-up.
	if (bLayoutFromBlueprint || !RootCanvas || !ChatPanel || !ChatSizer || !ActionBarRoot)
	{
		return;
	}
	const float Width = RootCanvas->GetCachedGeometry().GetLocalSize().X;
	const float BarWidth = ActionBarRoot->GetDesiredSize().X;
	if (Width <= 0.f || BarWidth <= 0.f || FMath::IsNearlyEqual(Width, LayoutForWidth, 0.5f))
	{
		return;
	}
	LayoutForWidth = Width;

	UCanvasPanelSlot* ChatSlot = Cast<UCanvasPanelSlot>(ChatPanel->Slot);
	if (!ChatSlot)
	{
		return;
	}

	const FValhallaUIConfig::FChat& C = Config.Chat;
	const FValhallaUIConfig::FHud& H = Config.Hud;
	constexpr float Gap = 8.f;
	constexpr float MinReadableWidth = 220.f;

	// Beside the vitals (ui-config's own place), as wide as the gap to the bar allows.
	const float LeftX = H.HpX + H.HpWidth + 16.f;
	const float Room = (Width - BarWidth) * 0.5f - LeftX - Gap;
	if (Room >= MinReadableWidth)
	{
		ChatSizer->SetWidthOverride(FMath::Min(C.MaxWidth, Room));
		ChatSlot->SetPosition(FVector2D(LeftX, -C.BottomMargin));
		return;
	}

	// Too narrow: full width, stacked above the vitals block (class line top).
	const float VitalsTop = H.HpYOffsetFromBottom + H.Mana.Height + H.ManaGapAboveHp + 2.f + 4.f + 18.f;
	ChatSizer->SetWidthOverride(FMath::Min(C.MaxWidth, FMath::Max(MinReadableWidth, Width * 0.5f - H.HpX - Gap)));
	ChatSlot->SetPosition(FVector2D(H.HpX, -(VitalsTop + Gap)));
}

void UValhallaGameHUDWidget::TickDeathOverlay()
{
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	const bool bAlive = !PS || PS->IsAlive();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (!bAlive && bWasAlive)
	{
		DeathStartedAt = Now;
	}
	bWasAlive = bAlive;

	if (bAlive)
	{
		DeathRoot->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// GameScene.ts:4442 — the seconds left, rounded up, from the 1.0 constant.
	const double Left = FMath::Max(0.0, Valhalla::RespawnTimeMs / 1000.0 - (Now - DeathStartedAt));
	DeathText->SetText(AsText(FString::Printf(TEXT("YOU DIED\nRespawning in %d s…"), FMath::CeilToInt(Left))));
	DeathRoot->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UValhallaGameHUDWidget::TickWorldLayer(float /*DeltaTime*/)
{
	APlayerController* PC = GetOwningPlayer();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}

	// ── Nameplates ──────────────────────────────────────────────────────
	// 1.0's nameplates.yOffset (-44) is measured from the sprite's origin; the
	// 2.0 plate is projected from the head (origin + 90 cm) and then shifted
	// by yOffset + 44, so the default reads as "just above the head" and a
	// designer's edit moves it by the same pixels it moved 1.0's.
	const APawn* OwnPawn = PC->GetPawn();
	const float ScreenOffset = Config.Nameplates.YOffset + 44.f;
	int32 Used = 0;

	auto ShowPlate = [&](const AActor* Actor, const FString& Name, float Hp, float MaxHp, bool bHostile)
	{
		if (Used >= Plates.Num())
		{
			return;
		}
		FVector2D Screen;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Actor->GetActorLocation() + FVector(0.f, 0.f, 90.f), Screen, false))
		{
			return;
		}
		FPlate& Plate = Plates[Used++];
		Plate.Name->SetText(AsText(Name));
		const float Frac = MaxHp > 0.f ? Hp / MaxHp : 0.f;
		Plate.Bar->SetFraction(Frac);
		Plate.Bar->SetFillColour(bHostile ? Config.Hud.HpLow : Config.Hud.HpHigh);
		if (UCanvasPanelSlot* PlateSlot = Cast<UCanvasPanelSlot>(Plate.Root->Slot))
		{
			PlateSlot->SetPosition(Screen + FVector2D(0.f, ScreenOffset));
		}
		Plate.Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	};

	for (TActorIterator<AValhallaNPC> It(World); It; ++It)
	{
		if (It->IsAlive())
		{
			ShowPlate(*It, It->DisplayName, It->Hp, It->MaxHp, true);
		}
	}
	for (TActorIterator<AValhallaCharacter> It(World); It; ++It)
	{
		const AValhallaPlayerState* Other = It->GetValhallaPlayerState();
		if (*It != OwnPawn && Other && Other->IsAlive())
		{
			ShowPlate(*It, Other->CharacterName, Other->Hp, Other->MaxHp, false);
		}
	}
	for (int32 Index = Used; Index < Plates.Num(); ++Index)
	{
		Plates[Index].Root->SetVisibility(ESlateVisibility::Collapsed);
	}

	// ── Floating combat text ────────────────────────────────────────────
	const double Now = World->GetRealTimeSeconds();
	for (FFloater& Floater : Floaters)
	{
		if (Floater.StartedAt < 0.0)
		{
			continue;
		}
		const float T = static_cast<float>((Now - Floater.StartedAt) / Floater.Lifetime);
		if (T >= 1.f)
		{
			Floater.StartedAt = -1.0;
			Floater.Text->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FVector Base = Floater.Anchor.IsValid() ? Floater.Anchor->GetActorLocation() : Floater.Location;
		FVector2D Screen;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Base + FVector(0.f, 0.f, 110.f), Screen, false))
		{
			Floater.Text->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		if (UCanvasPanelSlot* FloatSlot = Cast<UCanvasPanelSlot>(Floater.Text->Slot))
		{
			FloatSlot->SetPosition(Screen + FVector2D(Floater.XJitter, -18.f - 56.f * T));
		}
		Floater.Text->SetRenderOpacity(1.f - T * T);
		Floater.Text->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  Combat events: the log and the floaters
// ═════════════════════════════════════════════════════════════════════════════

const TArray<FName>& UValhallaGameHUDWidget::GetLogFilterKeys()
{
	static TArray<FName> Keys;
	if (Keys.Num() == 0)
	{
		for (const FFilterInfo& Info : FilterInfos())
		{
			Keys.Add(FName(Info.Key));
		}
	}
	return Keys;
}

void UValhallaGameHUDWidget::HandleCombatEvent(const FValhallaCombatEvent& Event)
{
	const AActor* Me = GetValhallaPawn();
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();

	auto Name = [](const AActor* Actor) { return UValhallaCombatLibrary::GetDisplayName(Actor); };
	auto SkillName = [Data](FName SkillId) -> FString
	{
		const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(SkillId) : nullptr;
		return Skill ? Skill->Name : SkillId.ToString();
	};
	auto IsPartyMember = [PS](const AActor* Actor)
	{
		if (!PS || !Actor) { return false; }
		const FString ActorName = UValhallaCombatLibrary::GetDisplayName(Actor);
		return PS->PartyMemberNames.Contains(ActorName);
	};

	const bool bByMe = Me && Event.Instigator == Me;
	const bool bOnMe = Me && Event.Target == Me;
	const bool bParty = IsPartyMember(Event.Instigator) || IsPartyMember(Event.Target);
	const FString Crit = Event.bCrit ? TEXT(" (critical)") : TEXT("");

	// Client-side buff view for player targets.
	if (Event.Kind == EValhallaCombatEventKind::BuffApplied && Event.Target)
	{
		TArray<FSeenBuff>& List = SeenBuffs.FindOrAdd(Event.Target);
		List.RemoveAll([&Event](const FSeenBuff& B) { return B.SkillId == Event.SkillId; });
		List.Add({ Event.SkillId, UValhallaCombatLibrary::GetServerTime(this) + Event.Amount / 1000.0 });
	}
	else if (Event.Kind == EValhallaCombatEventKind::BuffRemoved && Event.Target)
	{
		if (TArray<FSeenBuff>* List = SeenBuffs.Find(Event.Target))
		{
			List->RemoveAll([&Event](const FSeenBuff& B) { return B.SkillId == Event.SkillId; });
		}
	}

	// GameScene.ts:1354-1677's wording, event for event.
	switch (Event.Kind)
	{
	case EValhallaCombatEventKind::Blocked:
		// The block itself; the hit that follows it carries "(blocked)" and the damage.
		if (bOnMe)      { PushCombatLog(FString::Printf(TEXT("You blocked %s's attack"), *Name(Event.Instigator)), Srgb(0x44, 0x88, 0xff), TEXT("blocks")); }
		else if (bByMe) { PushCombatLog(FString::Printf(TEXT("%s blocked your attack"), *Name(Event.Target)), Srgb(0x44, 0x88, 0xff), TEXT("blocks")); }
		else if (bParty){ PushCombatLog(FString::Printf(TEXT("%s blocked %s's attack"), *Name(Event.Target), *Name(Event.Instigator)), Srgb(0x66, 0x88, 0xaa), TEXT("party")); }
		break;
	case EValhallaCombatEventKind::PlayerHit:
	case EValhallaCombatEventKind::NpcHit:
	{
		const FString Blocked = Event.bBlocked ? TEXT(" (blocked)") : TEXT("");
		const FString Skill = Data && Data->FindSkill(Event.SkillId) && !Data->FindSkill(Event.SkillId)->bIsAutoAttack
			? FString::Printf(TEXT(" [%s]"), *SkillName(Event.SkillId)) : FString();
		if (bOnMe)
		{
			PushCombatLog(FString::Printf(TEXT("%s hits you for %.0f%s%s"), *Name(Event.Instigator), Event.Amount, *Crit, *Blocked),
				Srgb(0xff, 0x66, 0x44), Blocked.IsEmpty() ? TEXT("inDmg") : TEXT("blocks"));
		}
		else if (bByMe)
		{
			PushCombatLog(FString::Printf(TEXT("You hit %s for %.0f%s%s%s"), *Name(Event.Target), Event.Amount, *Crit, *Skill, *Blocked),
				Srgb(0xff, 0xcc, 0x44), Blocked.IsEmpty() ? TEXT("outDmg") : TEXT("blocks"));
		}
		else if (bParty)
		{
			PushCombatLog(FString::Printf(TEXT("%s hits %s for %.0f%s"), *Name(Event.Instigator), *Name(Event.Target), Event.Amount, *Crit),
				Srgb(0xaa, 0xbb, 0x88), TEXT("party"));
		}
		break;
	}
	case EValhallaCombatEventKind::Missed:
		if (bByMe)      { PushCombatLog(FString::Printf(TEXT("Your attack missed %s"), *Name(Event.Target)), Srgb(0x99, 0x99, 0x99), TEXT("misses")); }
		else if (bOnMe) { PushCombatLog(FString::Printf(TEXT("%s's attack missed you"), *Name(Event.Instigator)), Srgb(0x99, 0x99, 0x99), TEXT("misses")); }
		else if (bParty){ PushCombatLog(FString::Printf(TEXT("%s's attack missed %s"), *Name(Event.Instigator), *Name(Event.Target)), Srgb(0x88, 0x88, 0x88), TEXT("party")); }
		break;
	case EValhallaCombatEventKind::Dodged:
		if (bOnMe)      { PushCombatLog(FString::Printf(TEXT("You dodged %s's attack"), *Name(Event.Instigator)), Srgb(0xff, 0xff, 0xff), TEXT("dodges")); }
		else if (bByMe) { PushCombatLog(FString::Printf(TEXT("%s dodged your attack"), *Name(Event.Target)), Srgb(0xcc, 0xcc, 0xcc), TEXT("dodges")); }
		else if (bParty){ PushCombatLog(FString::Printf(TEXT("%s dodged %s's attack"), *Name(Event.Target), *Name(Event.Instigator)), Srgb(0xaa, 0xaa, 0xaa), TEXT("party")); }
		break;
	case EValhallaCombatEventKind::NpcDied:
		if (bByMe) { PushCombatLog(FString::Printf(TEXT("You killed %s"), *Name(Event.Target)), Srgb(0x44, 0xff, 0x44), TEXT("deaths")); }
		else if (bParty) { PushCombatLog(FString::Printf(TEXT("%s killed %s"), *Name(Event.Instigator), *Name(Event.Target)), Srgb(0x88, 0xcc, 0x88), TEXT("party")); }
		break;
	case EValhallaCombatEventKind::PlayerDied:
		if (bOnMe) { PushCombatLog(FString::Printf(TEXT("You were killed by %s"), *Name(Event.Instigator)), Srgb(0xff, 0x44, 0x44), TEXT("deaths")); }
		else       { PushCombatLog(FString::Printf(TEXT("%s was killed by %s"), *Name(Event.Target), *Name(Event.Instigator)), Srgb(0xff, 0x66, 0x66), TEXT("deaths")); }
		break;
	case EValhallaCombatEventKind::PlayerRespawned:
		if (bOnMe) { PushCombatLog(TEXT("You have respawned"), Srgb(0x44, 0xff, 0x44), TEXT("deaths")); }
		break;
	case EValhallaCombatEventKind::XpGained:
		PushCombatLog(FString::Printf(TEXT("+%.0f XP"), Event.Amount), Srgb(0xff, 0xaa, 0x00), TEXT("xp"));
		break;
	case EValhallaCombatEventKind::LevelUp:
		PushCombatLog(FString::Printf(TEXT("You reached level %.0f!"), Event.Amount), Srgb(0xff, 0xaa, 0x00), TEXT("xp"));
		break;
	case EValhallaCombatEventKind::SkillEffect:
		if (Event.bHeal && Event.Amount > 0.f)
		{
			PushCombatLog(Event.Target == Event.Instigator
				? FString::Printf(TEXT("%s healed self for %.0f [%s]"), *Name(Event.Instigator), Event.Amount, *SkillName(Event.SkillId))
				: FString::Printf(TEXT("%s healed %s for %.0f [%s]"), *Name(Event.Instigator), *Name(Event.Target), Event.Amount, *SkillName(Event.SkillId)),
				Srgb(0x44, 0xff, 0x44), TEXT("heals"));
		}
		else if (bByMe)
		{
			PushCombatLog(FString::Printf(TEXT("You cast %s"), *SkillName(Event.SkillId)), Srgb(0xcc, 0xaa, 0xff), TEXT("casts"));
		}
		else if (bParty)
		{
			PushCombatLog(FString::Printf(TEXT("%s casts %s"), *Name(Event.Instigator), *SkillName(Event.SkillId)), Srgb(0xaa, 0x99, 0xcc), TEXT("party"));
		}
		break;
	case EValhallaCombatEventKind::SkillStarted:
		if (bByMe && Event.Amount > 0.f)
		{
			PushCombatLog(FString::Printf(TEXT("You begin casting %s (%.1fs)"), *SkillName(Event.SkillId), Event.Amount / 1000.f), Srgb(0xaa, 0x99, 0xdd), TEXT("casts"));
		}
		break;
	case EValhallaCombatEventKind::SkillInterrupted:
		if (bByMe) { PushCombatLog(FString::Printf(TEXT("%s interrupted"), *SkillName(Event.SkillId)), Srgb(0xff, 0x88, 0x44), TEXT("casts")); }
		break;
	case EValhallaCombatEventKind::SkillFailed:
		if (Event.Reason == UValhallaCombatLibrary::NotFacingReason())
		{
			// Controls rework: the server's own sentence, word for word.
			PushCombatLog(Event.Text, Srgb(0xff, 0x88, 0x44), TEXT("casts"));
		}
		else
		{
			PushCombatLog(FString::Printf(TEXT("Can't do that: %s"), *Event.Text), Srgb(0xff, 0x88, 0x44), TEXT("casts"));
		}
		break;
	case EValhallaCombatEventKind::BuffApplied:
		if (bOnMe)      { PushCombatLog(FString::Printf(TEXT("+%s applied (%.0fs)"), *SkillName(Event.SkillId), Event.Amount / 1000.f), Srgb(0x88, 0xcc, 0xff), TEXT("buffs")); }
		else if (bByMe) { PushCombatLog(FString::Printf(TEXT("%s applied to %s"), *SkillName(Event.SkillId), *Name(Event.Target)), Srgb(0x88, 0xff, 0x44), TEXT("buffs")); }
		break;
	case EValhallaCombatEventKind::BuffRemoved:
		if (bOnMe) { PushCombatLog(FString::Printf(TEXT("-%s faded"), *SkillName(Event.SkillId)), Srgb(0x88, 0x88, 0x88), TEXT("buffs")); }
		break;
	default:
		break;
	}

	SpawnFloater(Event);
}

void UValhallaGameHUDWidget::PushCombatLog(const FString& Text, const FLinearColor& Colour, FName Filter)
{
	CombatLogLines.Add({ Text, Colour, Filter });
	while (CombatLogLines.Num() > CombatLogMaxLines)
	{
		CombatLogLines.RemoveAt(0);
	}
	bCombatLogDirty = true;
	UE_LOG(LogValhallaHUD, Log, TEXT("combatLog [%s] %s"), *Filter.ToString(), *Text);
}

void UValhallaGameHUDWidget::RefreshCombatLog()
{
	bCombatLogDirty = false;
	if (!CombatLogScroll)
	{
		return;
	}

	CombatLogScroll->ClearChildren();
	int32 Shown = 0;
	for (const FValhallaCombatLogLine& Line : CombatLogLines)
	{
		const bool* bOn = LogFilters.Find(Line.Filter);
		if (bOn && !*bOn)
		{
			continue;
		}
		UTextBlock* Text = MakeText(Line.Text, 7, Line.Colour, false, 1.f);
		Text->SetAutoWrapText(true);
		CombatLogScroll->AddChild(Text);
		++Shown;
	}
	CombatLogScroll->ScrollToEnd();

	int32 Hidden = 0;
	for (const TPair<FName, bool>& Pair : LogFilters)
	{
		Hidden += Pair.Value ? 0 : 1;
	}
	CombatLogTitle->SetText(AsText(Hidden > 0
		? FString::Printf(TEXT("Combat Log  (%d filter%s off — right-click)"), Hidden, Hidden == 1 ? TEXT("") : TEXT("s"))
		: FString(TEXT("Combat Log  (right-click to filter)"))));

	const TArray<FFilterInfo>& Infos = FilterInfos();
	for (int32 Index = 0; Index < FilterLabels.Num() && Index < Infos.Num(); ++Index)
	{
		const bool bOn = LogFilters.FindRef(FName(Infos[Index].Key));
		FilterLabels[Index]->SetText(AsText(FString::Printf(TEXT("%s  %s"), bOn ? TEXT("[x]") : TEXT("[  ]"), Infos[Index].Label)));
	}
}

bool UValhallaGameHUDWidget::ToggleLogFilter(FName Key)
{
	bool* bOn = LogFilters.Find(Key);
	if (!bOn)
	{
		UE_LOG(LogValhallaHUD, Warning, TEXT("combat log: no filter '%s'."), *Key.ToString());
		return false;
	}
	*bOn = !*bOn;
	bCombatLogDirty = true;
	UE_LOG(LogValhallaHUD, Log, TEXT("combat log filter %s -> %s"), *Key.ToString(), *bOn ? TEXT("on") : TEXT("off"));
	return true;
}

void UValhallaGameHUDWidget::SpawnFloater(const FValhallaCombatEvent& Event)
{
	FString Text;
	FLinearColor Colour = FLinearColor::White;
	int32 Size = 14;
	AActor* Anchor = Event.Target;

	switch (Event.Kind)
	{
	case EValhallaCombatEventKind::PlayerHit:
	case EValhallaCombatEventKind::NpcHit:
	case EValhallaCombatEventKind::Blocked:
		if (Event.Amount <= 0.f) { return; }
		Text = Event.bCrit ? FString::Printf(TEXT("%.0f!"), Event.Amount) : FString::Printf(TEXT("%.0f"), Event.Amount);
		Colour = Event.bCrit ? Srgb(0xff, 0x9e, 0x26) : (Event.Kind == EValhallaCombatEventKind::PlayerHit ? Srgb(0xff, 0x55, 0x44) : Srgb(0xf6, 0xf0, 0xe6));
		Size = Event.bCrit ? 22 : 15;
		if (Event.bBlocked || Event.Kind == EValhallaCombatEventKind::Blocked) { Text += TEXT(" (block)"); }
		break;
	case EValhallaCombatEventKind::SkillEffect:
		if (!Event.bHeal || Event.Amount <= 0.f) { return; }
		Text = FString::Printf(TEXT("+%.0f"), Event.Amount);
		Colour = Srgb(0x66, 0xee, 0x77);
		Size = 15;
		break;
	case EValhallaCombatEventKind::Missed: Text = TEXT("miss");  Colour = Srgb(0xb3, 0xb3, 0xc7); break;
	case EValhallaCombatEventKind::Dodged: Text = TEXT("dodge"); Colour = Srgb(0xb3, 0xb3, 0xc7); break;
	case EValhallaCombatEventKind::SkillFailed:
		// Only the facing failure floats, over the player, like miss / dodge.
		// Every other failure is a log line alone.
		if (Event.Reason != UValhallaCombatLibrary::NotFacingReason()) { return; }
		Text = TEXT("Not facing");
		Colour = Srgb(0xff, 0x88, 0x44);
		Anchor = GetValhallaPawn();
		break;
	case EValhallaCombatEventKind::XpGained:
		Text = FString::Printf(TEXT("+%.0f xp"), Event.Amount);
		Colour = Srgb(0xd9, 0xc7, 0x73);
		Anchor = GetValhallaPawn();
		break;
	case EValhallaCombatEventKind::LevelUp:
		Text = FString::Printf(TEXT("LEVEL %.0f"), Event.Amount);
		Colour = Srgb(0xff, 0x9e, 0x26);
		Size = 22;
		Anchor = GetValhallaPawn();
		break;
	default:
		return;
	}

	if (Floaters.Num() == 0)
	{
		return;
	}
	FFloater& Floater = Floaters[NextFloater];
	NextFloater = (NextFloater + 1) % Floaters.Num();
	Floater.Text->SetText(AsText(Text));
	Floater.Text->SetFont(HudFont(Size, true, 2.f));
	Floater.Text->SetColorAndOpacity(FSlateColor(Colour));
	Floater.Anchor = Anchor;
	Floater.Location = Event.Location;
	Floater.StartedAt = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	Floater.Lifetime = Size >= 22 ? 1.6f : 1.2f;
	Floater.XJitter = FMath::FRandRange(-18.f, 18.f);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Skills pane
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaGameHUDWidget::RefreshSkillsPane()
{
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PS || !Data || !SkillsList)
	{
		return;
	}

	if (SkillsBuiltForClass != PS->ClassId || SkillsBuiltForLevel != PS->Level)
	{
		SkillsBuiltForClass = PS->ClassId;
		SkillsBuiltForLevel = PS->Level;
		SkillsList->ClearChildren();

		const FValhallaUIConfig::FInventory& I = Config.Inventory;
		for (const FName SkillId : Data->GetClassSkills(PS->ClassId))
		{
			const FValhallaSkillTemplate* Skill = Data->FindSkill(SkillId);
			if (!Skill)
			{
				continue;
			}
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Skill, INDEX_NONE, 36.f);
			Cell->bFilled = true;
			Cell->Id = SkillId;
			Cell->SetSkillIcon(SkillCode(*Skill), CategoryColour(Skill->Category), Packed(Skill->IconColor));
			const bool bLocked = PS->Level < Skill->LevelRequired;
			Cell->SetDimmed(bLocked);
			Row->AddChildToHorizontalBox(Cell);

			UVerticalBox* Text = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			Text->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s%s"), *Skill->Name,
				bLocked ? *FString::Printf(TEXT("   (Lv %d)"), Skill->LevelRequired) : TEXT("")), 8,
				bLocked ? I.LabelColor : I.ValueColor, true));
			const TCHAR* Resource = Skill->ResourceType == EValhallaResourceType::Mana ? TEXT("mana")
				: (Skill->ResourceType == EValhallaResourceType::Energy ? TEXT("energy") : TEXT(""));
			Text->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s%s  cast %.1fs  cd %.0fs"),
				Skill->ResourceCost > 0.f ? *FString::Printf(TEXT("%.0f %s"), Skill->ResourceCost, Resource) : TEXT("free"), TEXT(""),
				Skill->CastTimeMs / 1000.f, Skill->CooldownMs / 1000.f), 7, I.LabelColor));
			UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
			TextSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			SkillsList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 2.f));
		}
	}

	// The armed skill's ring.
	for (UWidget* Child : SkillsList->GetAllChildren())
	{
		if (const UHorizontalBox* Row = Cast<UHorizontalBox>(Child))
		{
			if (UValhallaHUDSlotWidget* Cell = Cast<UValhallaHUDSlotWidget>(Row->GetChildAt(0)))
			{
				Cell->SetSelected(Cell->Id == ArmedSkill && !ArmedSkill.IsNone());
			}
		}
	}
	SkillsHint->SetText(AsText(ArmedSkill.IsNone()
		? FString(TEXT("Drag a skill to the action bar, or click it and then click a slot."))
		: FString::Printf(TEXT("Now click an action bar slot for %s (Esc to cancel)."), *ArmedSkill.ToString())));
}

void UValhallaGameHUDWidget::ArmSkill(FName SkillId)
{
	ArmedSkill = SkillId;
	UE_LOG(LogValhallaHUD, Log, TEXT("skills pane: armed %s"), *SkillId.ToString());
}

// ═════════════════════════════════════════════════════════════════════════════
//  Interaction
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaGameHUDWidget::HandleSlotClicked(UValhallaHUDSlotWidget* Cell)
{
	VALHALLA_HUD_PIE_SCOPE(this);
	AValhallaPlayerController* PC = GetValhallaController();
	AValhallaCharacter* Pawn = GetValhallaPawn();
	UValhallaSkillComponent* Skills = Pawn ? Pawn->GetSkillComponent() : nullptr;
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	if (!Cell || !PC)
	{
		return;
	}

	switch (Cell->Kind)
	{
	case EValhallaHUDSlotKind::Action:
		if (!ArmedSkill.IsNone() && Skills)
		{
			// Click-to-assign: GameRoom.ts:270 SET_ACTION_BAR.
			UE_LOG(LogValhallaHUD, Log, TEXT("action bar: assign %s to slot %d"), *ArmedSkill.ToString(), Cell->Index);
			Skills->ServerSetActionBar(Cell->Index, ArmedSkill);
			ArmedSkill = NAME_None;
			// The hover tooltip still names the skill that was in the slot.
			HideTooltip();
		}
		else
		{
			// A click is the key: the same OnActionBarPressed the 1-8 keys call.
			PC->OnActionBarPressed(Cell->Index);
		}
		break;

	case EValhallaHUDSlotKind::Skill:
		ArmSkill(ArmedSkill == Cell->Id ? NAME_None : Cell->Id);
		break;

	case EValhallaHUDSlotKind::Inventory:
		if (PS && PS->Inventory.IsValidIndex(Cell->Index))
		{
			const UValhallaDataSubsystem* Data = GetData();
			const FValhallaItemTemplate* Item = Data ? Data->FindItem(PS->Inventory[Cell->Index].ItemId) : nullptr;
			if (Item && Item->EquipSlot != EValhallaEquipSlot::None)
			{
				UE_LOG(LogValhallaHUD, Log, TEXT("inventory: equip slot %d (%s)"), Cell->Index, *Item->Name);
				PC->ServerEquipItem(Cell->Index);
				HideTooltip();
			}
		}
		break;

	case EValhallaHUDSlotKind::Equip:
		if (Cell->bFilled)
		{
			const FName SlotName = UValhallaInventoryLibrary::EquipSlotToName(ValhallaEquipSlotFromIndex(Cell->Index));
			UE_LOG(LogValhallaHUD, Log, TEXT("equipment: unequip %s"), *SlotName.ToString());
			PC->ServerUnequipItem(SlotName, -1);
			HideTooltip();
		}
		break;

	case EValhallaHUDSlotKind::Loot:
		if (AValhallaLootBag* Bag = LootBag.Get())
		{
			if (Bag->Items.IsValidIndex(Cell->Index))
			{
				UE_LOG(LogValhallaHUD, Log, TEXT("loot: take slot %d (%s)"), Cell->Index, *Bag->Items[Cell->Index].ItemId.ToString());
				PC->ServerLootItem(Bag, Cell->Index);
			}
		}
		break;

	case EValhallaHUDSlotKind::CombatLog:
		FilterMenu->SetVisibility(ESlateVisibility::Collapsed);
		break;

	default:
		break;
	}
}

void UValhallaGameHUDWidget::HandleSlotRightClicked(UValhallaHUDSlotWidget* Cell, const FVector2D& /*ScreenPosition*/)
{
	VALHALLA_HUD_PIE_SCOPE(this);
	if (!Cell)
	{
		return;
	}
	switch (Cell->Kind)
	{
	case EValhallaHUDSlotKind::CombatLog:
		// GameScene.ts:5013 — the filter context menu.
		FilterMenu->SetVisibility(FilterMenu->GetVisibility() == ESlateVisibility::Collapsed
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		bCombatLogDirty = true;
		break;
	case EValhallaHUDSlotKind::Inventory:
	case EValhallaHUDSlotKind::Equip:
		if (Cell->bFilled)
		{
			RequestDrop(Cell->Kind, Cell->Index);
		}
		break;
	case EValhallaHUDSlotKind::Action:
		// Right-click clears a slot, as dragging it off the bar did in 1.0.
		if (AValhallaCharacter* Pawn = GetValhallaPawn())
		{
			if (UValhallaSkillComponent* Skills = Pawn->GetSkillComponent())
			{
				Skills->ServerSetActionBar(Cell->Index, NAME_None);
			}
		}
		break;
	default:
		break;
	}
}

void UValhallaGameHUDWidget::HandleSlotHovered(UValhallaHUDSlotWidget* Cell, bool bHovered)
{
	if (bTooltipPinned)
	{
		return;
	}
	if (!bHovered || !Cell || !Cell->bFilled)
	{
		HideTooltip();
		return;
	}

	int32 Quantity = 1;
	if (Cell->Kind == EValhallaHUDSlotKind::Inventory)
	{
		const AValhallaPlayerState* PS = GetValhallaPlayerState();
		if (PS && PS->Inventory.IsValidIndex(Cell->Index)) { Quantity = PS->Inventory[Cell->Index].Quantity; }
	}

	if (Cell->Kind == EValhallaHUDSlotKind::Inventory || Cell->Kind == EValhallaHUDSlotKind::Equip || Cell->Kind == EValhallaHUDSlotKind::Loot)
	{
		ShowItemTooltip(Cell->Id, Quantity);
	}
	else if (Cell->Kind == EValhallaHUDSlotKind::Action || Cell->Kind == EValhallaHUDSlotKind::Skill)
	{
		const UValhallaDataSubsystem* Data = GetData();
		if (const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(Cell->Id) : nullptr)
		{
			TooltipName->SetText(AsText(Skill->Name));
			TooltipName->SetColorAndOpacity(FSlateColor(Packed(Skill->IconColor)));
			TooltipBody->SetText(AsText(FString::Printf(TEXT("Level %d\n%.0f %s   cast %.1fs   cooldown %.0fs\n\n%s"),
				Skill->LevelRequired, Skill->ResourceCost,
				Skill->ResourceType == EValhallaResourceType::Mana ? TEXT("mana") : TEXT("energy"),
				Skill->CastTimeMs / 1000.f, Skill->CooldownMs / 1000.f, *Skill->Description)));
			TooltipRoot->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void UValhallaGameHUDWidget::HandleSlotDropped(UValhallaHUDSlotWidget* Source, UValhallaHUDSlotWidget* Target)
{
	VALHALLA_HUD_PIE_SCOPE(this);
	AValhallaPlayerController* PC = GetValhallaController();
	AValhallaCharacter* Pawn = GetValhallaPawn();
	UValhallaSkillComponent* Skills = Pawn ? Pawn->GetSkillComponent() : nullptr;
	if (!Source || !Target || !PC)
	{
		return;
	}

	using K = EValhallaHUDSlotKind;
	UE_LOG(LogValhallaHUD, Log, TEXT("drag: %d/%d -> %d/%d"), static_cast<int32>(Source->Kind), Source->Index,
		static_cast<int32>(Target->Kind), Target->Index);

	if (Source->Kind == K::Inventory && Target->Kind == K::Inventory)
	{
		// GameRoom.ts:206 SWAP_INVENTORY. The array is dense, so a drop on an
		// empty cell past the end moves the item to the end.
		const AValhallaPlayerState* PS = GetValhallaPlayerState();
		const int32 To = PS ? FMath::Min(Target->Index, PS->Inventory.Num() - 1) : Target->Index;
		PC->ServerSwapInventory(Source->Index, To);
	}
	else if (Source->Kind == K::Inventory && Target->Kind == K::Equip)
	{
		PC->ServerEquipItem(Source->Index);
	}
	else if (Source->Kind == K::Equip && Target->Kind == K::Inventory)
	{
		PC->ServerUnequipItem(UValhallaInventoryLibrary::EquipSlotToName(ValhallaEquipSlotFromIndex(Source->Index)), Target->Index);
	}
	else if (Source->Kind == K::Loot && Target->Kind == K::Inventory)
	{
		if (AValhallaLootBag* Bag = LootBag.Get())
		{
			PC->ServerLootItem(Bag, Source->Index);
		}
	}
	else if ((Source->Kind == K::Skill || Source->Kind == K::Action) && Target->Kind == K::Action && Skills)
	{
		const FName Moving = Source->Id;
		const FName Displaced = Target->Id;
		Skills->ServerSetActionBar(Target->Index, Moving);
		if (Source->Kind == K::Action)
		{
			// Action to action is a swap, so nothing falls off the bar.
			Skills->ServerSetActionBar(Source->Index, Displaced);
		}
	}
}

void UValhallaGameHUDWidget::RequestDrop(EValhallaHUDSlotKind Kind, int32 Index)
{
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	if (!PS || !Data)
	{
		return;
	}

	FName ItemId;
	if (Kind == EValhallaHUDSlotKind::Inventory && PS->Inventory.IsValidIndex(Index))
	{
		ItemId = PS->Inventory[Index].ItemId;
	}
	else if (Kind == EValhallaHUDSlotKind::Equip)
	{
		ItemId = PS->GetEquipped(ValhallaEquipSlotFromIndex(Index));
	}
	if (ItemId.IsNone())
	{
		return;
	}

	const FValhallaItemTemplate* Item = Data->FindItem(ItemId);
	DropKind = Kind;
	DropIndex = Index;
	DropText->SetText(AsText(FString::Printf(TEXT("Drop %s on the ground?"), Item ? *Item->Name : *ItemId.ToString())));
	DropRoot->SetVisibility(ESlateVisibility::Visible);
}

void UValhallaGameHUDWidget::HandleButton(EValhallaHUDButton Action, int32 Index)
{
	VALHALLA_HUD_PIE_SCOPE(this);
	AValhallaPlayerController* PC = GetValhallaController();
	if (!PC)
	{
		return;
	}
	UE_LOG(LogValhallaHUD, Log, TEXT("button %d (%d)"), static_cast<int32>(Action), Index);

	switch (Action)
	{
	case EValhallaHUDButton::LootAll:
		if (AValhallaLootBag* Bag = LootBag.Get())
		{
			PC->ServerLootAll(Bag);
		}
		break;
	case EValhallaHUDButton::LootClose:
		CloseLootPanel();
		break;
	case EValhallaHUDButton::PartyAccept:
		PC->ServerPartyAccept();
		PC->ClearPendingPartyInvite();
		break;
	case EValhallaHUDButton::PartyDecline:
		PC->ServerPartyDecline();
		PC->ClearPendingPartyInvite();
		break;
	case EValhallaHUDButton::PartyLeave:
		PC->ServerPartyLeave();
		break;
	case EValhallaHUDButton::DropConfirm:
		if (DropKind == EValhallaHUDSlotKind::Inventory)
		{
			PC->ServerDropItem(DropIndex);
		}
		else if (DropKind == EValhallaHUDSlotKind::Equip)
		{
			PC->ServerDropEquipped(UValhallaInventoryLibrary::EquipSlotToName(ValhallaEquipSlotFromIndex(DropIndex)));
		}
		DropRoot->SetVisibility(ESlateVisibility::Collapsed);
		DropKind = EValhallaHUDSlotKind::None;
		break;
	case EValhallaHUDButton::DropCancel:
		DropRoot->SetVisibility(ESlateVisibility::Collapsed);
		DropKind = EValhallaHUDSlotKind::None;
		break;
	case EValhallaHUDButton::LogFilter:
		if (GetLogFilterKeys().IsValidIndex(Index))
		{
			ToggleLogFilter(GetLogFilterKeys()[Index]);
		}
		break;
	case EValhallaHUDButton::LogFilterClose:
		FilterMenu->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EValhallaHUDButton::InventoryClose:
		if (bInventoryOpen) { ToggleInventory(); }
		break;
	case EValhallaHUDButton::SkillsClose:
		if (bSkillsOpen) { ToggleSkills(); }
		break;
	default:
		break;
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  Panels
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaGameHUDWidget::ToggleInventory()
{
	bInventoryOpen = !bInventoryOpen;
	SetInventoryShown(bInventoryOpen);
	if (!bInventoryOpen)
	{
		bTooltipPinned = false;
		HideTooltip();
	}
	UE_LOG(LogValhallaHUD, Log, TEXT("inventory panel %s"), bInventoryOpen ? TEXT("open") : TEXT("closed"));
}

void UValhallaGameHUDWidget::ToggleSkills()
{
	bSkillsOpen = !bSkillsOpen;
	SkillsRoot->SetVisibility(bSkillsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!bSkillsOpen)
	{
		ArmedSkill = NAME_None;
	}
	SkillsBuiltForClass = NAME_None;
	UE_LOG(LogValhallaHUD, Log, TEXT("skills pane %s"), bSkillsOpen ? TEXT("open") : TEXT("closed"));
}

void UValhallaGameHUDWidget::OpenLootPanel(AValhallaLootBag* Bag)
{
	LootBag = Bag;
	LootRoot->SetVisibility(Bag ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	UE_LOG(LogValhallaHUD, Log, TEXT("loot panel open on %s (%d slot(s))"), Bag ? *Bag->GetName() : TEXT("-"), Bag ? Bag->GetSlotCount() : 0);
}

void UValhallaGameHUDWidget::CloseLootPanel()
{
	LootBag.Reset();
	if (LootRoot)
	{
		LootRoot->SetVisibility(ESlateVisibility::Collapsed);
	}
	// Keep the controller's idea of the open bag in step. CloseLootWindow
	// clears it before asking the panel to close, so this cannot recurse.
	if (AValhallaPlayerController* PC = GetValhallaController(); PC && PC->GetOpenLootBag())
	{
		PC->CloseLootWindow();
	}
}

bool UValhallaGameHUDWidget::IsLootPanelOpen() const
{
	return LootRoot && LootRoot->GetVisibility() != ESlateVisibility::Collapsed;
}

bool UValhallaGameHUDWidget::CloseTopmost()
{
	if (bChatOpen) { CloseChat(); return true; }
	if (DropRoot->GetVisibility() != ESlateVisibility::Collapsed) { HandleButton(EValhallaHUDButton::DropCancel, 0); return true; }
	if (FilterMenu->GetVisibility() != ESlateVisibility::Collapsed) { FilterMenu->SetVisibility(ESlateVisibility::Collapsed); return true; }
	if (!ArmedSkill.IsNone()) { ArmedSkill = NAME_None; return true; }
	if (LootBag.IsValid()) { CloseLootPanel(); return true; }
	if (bSkillsOpen) { ToggleSkills(); return true; }
	if (bInventoryOpen) { ToggleInventory(); return true; }
	return false;
}

void UValhallaGameHUDWidget::ShowItemTooltip(FName ItemId, int32 Quantity)
{
	const UValhallaDataSubsystem* Data = GetData();
	const FValhallaItemTemplate* Item = Data ? Data->FindItem(ItemId) : nullptr;
	if (!Item)
	{
		HideTooltip();
		return;
	}

	TooltipName->SetText(AsText(Quantity > 1 ? FString::Printf(TEXT("%s  x%d"), *Item->Name, Quantity) : Item->Name));
	TooltipName->SetColorAndOpacity(FSlateColor(RarityColour(Item->Rarity)));

	FString Body = RarityName(Item->Rarity);
	if (Item->EquipSlot != EValhallaEquipSlot::None)
	{
		Body += FString::Printf(TEXT("  %s"), *UValhallaInventoryLibrary::EquipSlotToName(Item->EquipSlot).ToString());
	}
	const FValhallaStatBlock& S = Item->StatBonuses;
	auto Stat = [&Body](const TCHAR* Label, float Value)
	{
		if (!FMath::IsNearlyZero(Value))
		{
			Body += FString::Printf(TEXT("\n%s%.0f %s"), Value > 0.f ? TEXT("+") : TEXT(""), Value, Label);
		}
	};
	Stat(TEXT("HP"), S.Hp); Stat(TEXT("Mana"), S.Mana); Stat(TEXT("Strength"), S.Strength); Stat(TEXT("Stamina"), S.Stamina);
	Stat(TEXT("Dexterity"), S.Dexterity); Stat(TEXT("Intelligence"), S.Intelligence); Stat(TEXT("Wisdom"), S.Wisdom);
	Stat(TEXT("Physical resist"), S.PhysicalResist); Stat(TEXT("Spell resist"), S.SpellResist);
	Stat(TEXT("Physical defense"), S.PhysicalDefense);
	// Rates are 0-1 decimals (items.json): +0.03 blockRating reads "+3% Block".
	auto Rate = [&Body](const TCHAR* Label, float Value)
	{
		if (!FMath::IsNearlyZero(Value))
		{
			Body += FString::Printf(TEXT("\n%s%.0f%% %s"), Value > 0.f ? TEXT("+") : TEXT(""), Value * 100.f, Label);
		}
	};
	Rate(TEXT("Block"), S.BlockRating); Rate(TEXT("Dodge"), S.DodgeRating);
	Rate(TEXT("Crit chance"), S.CritChance); Rate(TEXT("Crit damage"), S.CritDamage);
	{
		float MinDamage = 0.f, MaxDamage = 0.f;
		Item->GetDamageRange(MinDamage, MaxDamage);
		if (MaxDamage > 0.f)
		{
			Body += FMath::IsNearlyEqual(MinDamage, MaxDamage)
				? FString::Printf(TEXT("\n%.0f damage"), MaxDamage)
				: FString::Printf(TEXT("\n%.0f-%.0f damage"), MinDamage, MaxDamage);
		}
	}
	if (Item->bHasAttackSpeed) { Body += FString::Printf(TEXT("\n%.1fs attack speed"), Item->AttackSpeedMs / 1000.f); }
	if (!Item->Description.IsEmpty()) { Body += FString::Printf(TEXT("\n\n%s"), *Item->Description); }
	TooltipBody->SetText(AsText(Body));
	TooltipRoot->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UValhallaGameHUDWidget::HideTooltip()
{
	if (TooltipRoot)
	{
		TooltipRoot->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UValhallaGameHUDWidget::PinInventoryTooltip(int32 InventoryIndex)
{
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	if (!PS || !PS->Inventory.IsValidIndex(InventoryIndex))
	{
		bTooltipPinned = false;
		HideTooltip();
		return;
	}
	if (!bInventoryOpen)
	{
		ToggleInventory();
	}
	bTooltipPinned = false;
	ShowItemTooltip(PS->Inventory[InventoryIndex].ItemId, PS->Inventory[InventoryIndex].Quantity);
	bTooltipPinned = true;

	// Beside the cell it describes.
	if (InventoryCells.IsValidIndex(InventoryIndex))
	{
		const FGeometry& Geometry = InventoryCells[InventoryIndex]->GetCachedGeometry();
		FVector2D Pixel, Viewport;
		USlateBlueprintLibrary::AbsoluteToViewport(this, Geometry.GetAbsolutePositionAtCoordinates(FVector2D(1.f, 0.f)), Pixel, Viewport);
		if (UCanvasPanelSlot* TipSlot = Cast<UCanvasPanelSlot>(TooltipRoot->Slot))
		{
			TipSlot->SetPosition(Viewport + FVector2D(6.f, 0.f));
		}
	}
}

UTexture2D* UValhallaGameHUDWidget::FindItemIcon(FName ItemId)
{
	if (TObjectPtr<UTexture2D>* Cached = IconCache.Find(ItemId))
	{
		return *Cached;
	}

	UTexture2D* Texture = nullptr;
	const UValhallaDataSubsystem* Data = GetData();
	const FValhallaItemTemplate* Item = Data ? Data->FindItem(ItemId) : nullptr;
	if (Item && !Item->InventoryIcon.IsEmpty())
	{
		const FString Base = FPaths::GetBaseFilename(Item->InventoryIcon);

		// The imported copy first (/Game/Valhalla/UI/Icons/Items, nearest
		// filtered, no mips), then the source PNG in Import/UI/Icons straight
		// off disk — so a new icon shows up in the editor before anyone
		// re-runs import_ui_icons.py.
		const FString AssetPath = FString::Printf(TEXT("/Game/Valhalla/UI/Icons/Items/%s.%s"), *Base, *Base);
		Texture = LoadObject<UTexture2D>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);

		if (!Texture)
		{
			const FString DiskPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				UValhallaDataSettings::Get()->GetResolvedDataRoot(), TEXT("../../Import/UI/Icons"), Item->InventoryIcon));
			Texture = FImageUtils::ImportFileAsTexture2D(DiskPath);
			if (Texture)
			{
				Texture->Filter = TF_Nearest;
				Texture->UpdateResource();
			}
			else
			{
				UE_LOG(LogValhallaHUD, Verbose, TEXT("no icon for %s (%s)"), *ItemId.ToString(), *DiskPath);
			}
		}
	}

	IconCache.Add(ItemId, Texture);
	return Texture;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Chat
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaGameHUDWidget::SetInputForTyping(bool bTyping)
{
	AValhallaPlayerController* PC = GetValhallaController();
	if (!PC)
	{
		return;
	}
	PC->SetUiTyping(bTyping);

	if (bTyping)
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(ChatInput->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		ChatInput->SetKeyboardFocus();
	}
	else
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
}

void UValhallaGameHUDWidget::OpenChat(const FString& Prefill)
{
	if (!ChatInput)
	{
		return;
	}
	bChatOpen = true;
	const FValhallaUIConfig::FChat& C = Config.Chat;
	ChatPanel->SetBrushColor(WithAlpha(C.BgColor, C.BgAlpha));
	ChatPanel->SetVisibility(ESlateVisibility::Visible);
	// The code layout grows the box to chat.height while typing; a designer's
	// chat keeps the size it was given.
	if (USizeBox* Box = bLayoutFromBlueprint ? nullptr : Cast<USizeBox>(ChatPanel->GetContent()))
	{
		Box->SetHeightOverride(C.Height);
	}
	ChatChannelText->SetText(AsText(ValhallaChat::ChannelLabel(ChatChannel)));
	ChatChannelText->SetColorAndOpacity(FSlateColor(ChatColour(ChatChannel)));
	ChatChannelText->SetVisibility(ESlateVisibility::HitTestInvisible);
	ChatScroll->SetVisibility(ESlateVisibility::Visible);
	ChatInput->SetText(AsText(Prefill));
	ChatInput->SetVisibility(ESlateVisibility::Visible);
	for (UTextBlock* Line : ChatLineWidgets)
	{
		Line->SetRenderOpacity(1.f);
	}
	RefreshChatLines();
	SetInputForTyping(true);
	UE_LOG(LogValhallaHUD, Log, TEXT("chat open %s"), *ValhallaChat::ChannelLabel(ChatChannel));
}

void UValhallaGameHUDWidget::CloseChat()
{
	if (!bChatOpen)
	{
		return;
	}
	bChatOpen = false;
	ChatPanel->SetBrushColor(WithAlpha(Config.Chat.BgColor, 0.f));
	ChatPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (USizeBox* Box = bLayoutFromBlueprint ? nullptr : Cast<USizeBox>(ChatPanel->GetContent()))
	{
		Box->ClearHeightOverride();
	}
	ChatChannelText->SetVisibility(ESlateVisibility::Collapsed);
	ChatScroll->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ChatInput->SetVisibility(ESlateVisibility::Collapsed);
	RefreshChatLines();
	SetInputForTyping(false);
}

void UValhallaGameHUDWidget::HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (!bChatOpen)
	{
		return;
	}
	if (CommitMethod == ETextCommit::OnEnter)
	{
		const FString Line = Text.ToString();
		ChatInput->SetText(FText::GetEmpty());
		CloseChat();
		SubmitChatLine(Line);
	}
	else if (CommitMethod == ETextCommit::OnCleared)
	{
		CloseChat();
	}
}

void UValhallaGameHUDWidget::SubmitChatLine(const FString& Line)
{
	VALHALLA_HUD_PIE_SCOPE(this);
	AValhallaPlayerController* PC = GetValhallaController();
	if (!PC)
	{
		return;
	}

	const FValhallaChatParse Parsed = ValhallaChat::Parse(Line, ChatChannel);
	if (Parsed.bSetsChannel)
	{
		ChatChannel = Parsed.Channel;
	}

	switch (Parsed.Action)
	{
	case EValhallaChatAction::Send:
		UE_LOG(LogValhallaHUD, Log, TEXT("chat send [%s]%s %s"), ValhallaChat::ChannelToWire(Parsed.Channel),
			Parsed.Target.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" -> %s"), *Parsed.Target), *Parsed.Text);
		PC->ServerChat(ValhallaChat::ChannelToWire(Parsed.Channel), Parsed.Text, Parsed.Target);
		break;
	case EValhallaChatAction::PartyInvite:  PC->ServerPartyInvite(Parsed.Target); break;
	case EValhallaChatAction::PartyAccept:  PC->ServerPartyAccept();  PC->ClearPendingPartyInvite(); break;
	case EValhallaChatAction::PartyDecline: PC->ServerPartyDecline(); PC->ClearPendingPartyInvite(); break;
	case EValhallaChatAction::PartyLeave:   PC->ServerPartyLeave();   break;
	case EValhallaChatAction::Usage:
	case EValhallaChatAction::Unknown:
	{
		FValhallaChatMessage Local;
		Local.Channel = EValhallaChatChannel::System;
		Local.Message = Parsed.Text;
		PC->AddLocalChatLine(Local);
		break;
	}
	default:
		break;
	}
}

FReply UValhallaGameHUDWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bChatOpen)
	{
		if (InKeyEvent.GetKey() == EKeys::Tab)
		{
			ChatChannel = ValhallaChat::NextChannel(ChatChannel);
			ChatChannelText->SetText(AsText(ValhallaChat::ChannelLabel(ChatChannel)));
			ChatChannelText->SetColorAndOpacity(FSlateColor(ChatColour(ChatChannel)));
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			CloseChat();
			return FReply::Handled();
		}
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
