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
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
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
#include "UObject/UObjectIterator.h"
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
	 */
	FAutoConsoleCommandWithWorldAndArgs GUICommand(
		TEXT("valhalla.UI"),
		TEXT("Dev only. valhalla.UI [@class] <inventory|skills|chat [text]|say <line>|loot|close|arm <skill>|filter <key>|tooltip <i>|reload|press <slot>> — drive a client's HUD."),
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
					if (AValhallaPlayerController* ValhallaPC = Cast<AValhallaPlayerController>(Hud.GetOwningPlayer()))
					{
						ValhallaPC->OnActionBarPressed(FMath::Clamp(FCString::Atoi(*Rest), 1, ValhallaActionBarSlots));
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
	if (WidgetTree->RootWidget)
	{
		return;
	}

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

void UValhallaHUDSlotWidget::Setup(float Size, const FLinearColor& Background, const FLinearColor& InBorder, const FLinearColor& Highlight)
{
	EnsureTree();
	CellSize = Size;
	BorderColour = InBorder;
	HighlightColour = Highlight;

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
		return;
	}
	// The sweep: an overlay whose height is the fraction left, draining
	// downwards, which is the 1.0 bar's vertical wipe.
	CooldownSizer->SetHeightOverride(FMath::Clamp(Fraction, 0.f, 1.f) * (CellSize - 2.f));
	CooldownFill->SetBrushColor(Colour);
	CooldownSizer->SetVisibility(ESlateVisibility::HitTestInvisible);
	CooldownText->SetText(AsText(Text));
	CooldownText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UValhallaHUDSlotWidget::SetDimmed(bool bDimmed)
{
	Stack->SetRenderOpacity(bDimmed ? 0.38f : 1.f);
}

void UValhallaHUDSlotWidget::SetSelected(bool bSelected)
{
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

void UValhallaGameHUDWidget::FBar::Set(float Fraction) const
{
	if (FillSizer)
	{
		FillSizer->SetWidthOverride(FMath::Max(0.f, FMath::Clamp(Fraction, 0.f, 1.f) * Width));
		FillSizer->SetVisibility(Fraction > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void UValhallaGameHUDWidget::FBar::SetOverlay(float Fraction) const
{
	if (OverlaySizer)
	{
		OverlaySizer->SetWidthOverride(FMath::Clamp(Fraction, 0.f, 1.f) * Width);
		OverlaySizer->SetVisibility(Fraction > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

UValhallaGameHUDWidget::FBar UValhallaGameHUDWidget::MakeBar(float Width, float Height, const FLinearColor& FillColour,
	const FLinearColor& Background, float BackgroundAlpha, bool bWithOverlay, int32 FontSize)
{
	UWidgetTree& Tree = *WidgetTree;
	FBar Bar;
	Bar.Width = Width;

	// 1.0's bar: a 2 px black frame (bg at bgAlpha), the fill, a 1 px white
	// stroke at half alpha. The stroke is the outer border here.
	UBorder* Stroke = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	Stroke->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, 0.5f));
	Stroke->SetPadding(FMargin(1.f));

	UBorder* Back = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	Back->SetBrushColor(WithAlpha(Background, BackgroundAlpha));
	Back->SetPadding(FMargin(1.f));
	Stroke->SetContent(Back);

	USizeBox* Box = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetWidthOverride(Width);
	Box->SetHeightOverride(Height);
	Back->SetContent(Box);

	UOverlay* Layers = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass());
	Box->SetContent(Layers);

	Bar.FillSizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Bar.FillBorder = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	Bar.FillBorder->SetBrushColor(FillColour);
	Bar.FillSizer->SetContent(Bar.FillBorder);
	Bar.FillSizer->SetWidthOverride(Width);
	UOverlaySlot* FillLayer = Layers->AddChildToOverlay(Bar.FillSizer);
	FillLayer->SetHorizontalAlignment(HAlign_Left);
	FillLayer->SetVerticalAlignment(VAlign_Fill);

	if (bWithOverlay)
	{
		// Shield of Faith's cyan wash over the HP bar (GameScene.ts:2161).
		Bar.OverlaySizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
		UBorder* Wash = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
		Wash->SetBrushColor(Srgb(0x00, 0xcc, 0xff, 0.45f));
		Bar.OverlaySizer->SetContent(Wash);
		Bar.OverlaySizer->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* WashLayer = Layers->AddChildToOverlay(Bar.OverlaySizer);
		WashLayer->SetHorizontalAlignment(HAlign_Left);
		WashLayer->SetVerticalAlignment(VAlign_Fill);
	}

	Bar.Label = MakeText(FString(), FontSize, FLinearColor::White, true, 1.f);
	UOverlaySlot* LabelLayer = Layers->AddChildToOverlay(Bar.Label);
	LabelLayer->SetHorizontalAlignment(HAlign_Center);
	LabelLayer->SetVerticalAlignment(VAlign_Center);

	Bar.Root = Stroke;
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
	UValhallaHUDSlotWidget* Cell = WidgetTree->ConstructWidget<UValhallaHUDSlotWidget>(UValhallaHUDSlotWidget::StaticClass());
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

void UValhallaGameHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("ValhallaHUDTree"));
	}
	if (!WidgetTree->RootWidget)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HUDRoot"));
		// Bare canvas lets clicks through to the world; panels stop them.
		RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = RootCanvas;
	}

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

	RootCanvas->ClearChildren();
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

	UE_LOG(LogValhallaHUD, Log, TEXT("HUD built from ui-config %s (sections %s): action bar %.0f px slots, chat %.0fx%.0f, inventory %dx%d @ %.0f px."),
		*Config.Version, Config.HasAllSections() ? TEXT("all 7") : TEXT("partial, defaults used"),
		Config.ActionBar.SlotSize, Config.Chat.MaxWidth, Config.Chat.Height,
		Config.Inventory.Cols, Config.Inventory.Rows, Config.Inventory.SlotSize);
}

void UValhallaGameHUDWidget::BuildAll()
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

	InventoryRoot->SetVisibility(bInventoryOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SkillsRoot->SetVisibility(bSkillsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	LootRoot->SetVisibility(LootBag.IsValid() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
		Plate.Bar.Label->SetVisibility(ESlateVisibility::Collapsed);
		Column->AddChildToVerticalBox(Plate.Bar.Root)->SetHorizontalAlignment(HAlign_Center);
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
	Place(HpBar.Root, FVector2D(0.f, 1.f), FVector2D(0.f, 0.f), FVector2D(H.HpX - 2.f, -H.HpYOffsetFromBottom - 2.f));

	// Mana and energy share a row, as in 1.0: a class has one or the other.
	ResourceBar = MakeBar(H.Mana.Width, H.Mana.Height, H.Mana.Color, H.Mana.BgColor, H.Mana.BgAlpha, false, 7);
	const float ResourceY = -H.HpYOffsetFromBottom - H.Mana.Height - H.ManaGapAboveHp - 2.f;
	Place(ResourceBar.Root, FVector2D(0.f, 1.f), FVector2D(0.f, 0.f), FVector2D(H.HpX - 2.f, ResourceY));

	ClassText = MakeText(FString(), PxToSlate(H.ClassFontSize), H.ClassColor, false, H.ClassStrokeThickness * 0.5f);
	Place(ClassText, FVector2D(0.f, 1.f), FVector2D(0.f, 1.f), FVector2D(H.HpX, ResourceY - 4.f));
}

void UValhallaGameHUDWidget::BuildActionBar()
{
	// actionBar.* — bottom centre, `bottomMargin` off the edge, a `padding`
	// box around eight `slotSize` cells `slotGap` apart.
	const FValhallaUIConfig::FActionBar& A = Config.ActionBar;

	UBorder* Panel = MakePanel(A.Bg, A.BgAlpha, A.Padding);
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Panel->SetContent(Row);

	for (int32 SlotNumber = 1; SlotNumber <= ValhallaActionBarSlots; ++SlotNumber)
	{
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Action, SlotNumber, A.SlotSize);
		Cell->SetKeyLabel(FString::FromInt(SlotNumber), A.KeyLabelColor);
		UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(Cell);
		CellSlot->SetPadding(FMargin(SlotNumber == 1 ? 0.f : A.SlotGap, 0.f, 0.f, 0.f));
		ActionCells.Add(Cell);
	}

	Place(Panel, FVector2D(0.5f, 1.f), FVector2D(0.5f, 1.f), FVector2D(0.f, -A.BottomMargin), 5);
	ActionBarRoot = Panel;
}

void UValhallaGameHUDWidget::BuildCastBar()
{
	// castBar.* — `yAboveActionBar` px above the action bar's top edge.
	const FValhallaUIConfig::FCastBar& C = Config.CastBar;
	const FValhallaUIConfig::FActionBar& A = Config.ActionBar;

	CastBar = MakeBar(C.Width, C.Height, C.Color, C.BgColor, C.BgAlpha, false, PxToSlate(C.FontSize));
	CastBar.Label->SetColorAndOpacity(FSlateColor(C.TextColor));
	CastBarRoot = CastBar.Root;

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

	TargetHp = MakeBar(240.f, 14.f, Config.Hud.HpHigh, Config.Hud.HpBg, Config.Hud.HpBgAlpha, false, 8);
	Column->AddChildToVerticalBox(TargetHp.Root)->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));

	TargetBuffs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(TargetBuffs)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	for (int32 Index = 0; Index < 8; ++Index)
	{
		UBorder* Token = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Token->SetPadding(FMargin(3.f, 1.f));
		UTextBlock* Label = MakeText(FString(), 7, FLinearColor::White, true, 1.f);
		Token->SetContent(Label);
		Token->SetVisibility(ESlateVisibility::Collapsed);
		TargetBuffs->AddChildToHorizontalBox(Token)->SetPadding(FMargin(0.f, 0.f, 3.f, 0.f));
		TargetBuffTokens.Add(Token);
		TargetBuffTexts.Add(Label);
	}

	TargetRoot = Panel;
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

	for (int32 Index = 0; Index < ValhallaPartyMaxMembers; ++Index)
	{
		UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UTextBlock* Name = MakeText(FString(), 8, I.ValueColor, false);
		Row->AddChildToVerticalBox(Name);
		FBar Bar = MakeBar(160.f, 8.f, Config.Hud.HpHigh, Config.Hud.HpBg, Config.Hud.HpBgAlpha, false, 6);
		Row->AddChildToVerticalBox(Bar.Root);
		Column->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		PartyRows.Add(Row);
		PartyNames.Add(Name);
		PartyBars.Add(Bar);
	}

	PartyRoot = Panel;
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
	InviteRoot = Prompt;
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
	Place(FilterMenu, FVector2D(1.f, 0.f), FVector2D(1.f, 0.f), FVector2D(-12.f, 12.f + Height + 4.f), 30);
	FilterMenu->SetVisibility(ESlateVisibility::Collapsed);
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
	constexpr int32 LootColumns = 4;
	constexpr int32 LootPool = 12;
	for (int32 Index = 0; Index < LootPool; ++Index)
	{
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Loot, Index, I.SlotSize);
		Grid->AddChildToUniformGrid(Cell, Index / LootColumns, Index % LootColumns);
		LootCells.Add(Cell);
	}

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	Buttons->AddChildToHorizontalBox(MakeButton(TEXT("Loot All"), EValhallaHUDButton::LootAll, 0, I.Highlight))->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	Buttons->AddChildToHorizontalBox(MakeButton(TEXT("Close"), EValhallaHUDButton::LootClose, 0, Srgb(0x88, 0x88, 0xaa)));

	LootRoot = Panel;
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

	SkillsRoot = Panel;
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
	CharColumn->AddChildToVerticalBox(MakeText(TEXT("Character  (B)"), 10, I.TitleColor, true))->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Equip, Index, 26.f);
		Row->AddChildToHorizontalBox(Cell);
		UTextBlock* Label = MakeText(FString(), 7, I.LabelColor);
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		CharColumn->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 1.f));
		EquipCells.Add(Cell);
		EquipLabels.Add(Label);
	}
	CharacterStats = MakeText(FString(), 7, I.ValueColor);
	CharacterStats->SetAutoWrapText(true);
	CharColumn->AddChildToVerticalBox(CharacterStats)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	Pair->AddChildToHorizontalBox(CharPanel)->SetPadding(FMargin(0.f, 0.f, I.PanelGap, 0.f));

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
	const int32 Cells = FMath::Clamp(I.Cols * I.Rows, 1, 64);
	for (int32 Index = 0; Index < Cells; ++Index)
	{
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Inventory, Index, I.SlotSize);
		Grid->AddChildToUniformGrid(Cell, Index / FMath::Max(1, I.Cols), Index % FMath::Max(1, I.Cols));
		InventoryCells.Add(Cell);
	}
	InvColumn->AddChildToVerticalBox(MakeText(
		TEXT("click: equip / unequip    right-click: drop    drag: move or swap"), 7, I.LabelColor))->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	Pair->AddChildToHorizontalBox(InvPanel);

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
	TooltipRoot = Panel;
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
	DropRoot = Panel;
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
	DeathRoot = Dim;
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
	HpBar.Set(HpFrac);
	// GameScene.ts:2156 — green over half, orange over a quarter, red below.
	HpBar.FillBorder->SetBrushColor(HpFrac > 0.5f ? H.HpHigh : (HpFrac > 0.25f ? H.HpMid : H.HpLow));
	HpBar.SetOverlay(PS->MaxHp > 0.f ? PS->ShieldHp / PS->MaxHp : 0.f);
	HpBar.Label->SetText(AsText(FString::Printf(TEXT("%.0f / %.0f"), PS->Hp, PS->MaxHp)));

	const bool bMana = PS->MaxMana > 0.f;
	const bool bEnergy = !bMana && PS->MaxEnergy > 0.f;
	ResourceBar.Root->SetVisibility(bMana || bEnergy ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bMana)
	{
		ResourceBar.Set(PS->Mana / PS->MaxMana);
		ResourceBar.FillBorder->SetBrushColor(H.Mana.Color);
		ResourceBar.Label->SetText(AsText(FString::Printf(TEXT("%.0f / %.0f"), PS->Mana, PS->MaxMana)));
	}
	else if (bEnergy)
	{
		ResourceBar.Set(PS->Energy / PS->MaxEnergy);
		ResourceBar.FillBorder->SetBrushColor(H.Energy.Color);
		ResourceBar.Label->SetText(AsText(FString::Printf(TEXT("%.0f / %.0f"), PS->Energy, PS->MaxEnergy)));
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
	CastBar.Set(Progress);

	FString Name = Skills->CastingSkillId.ToString();
	if (const UValhallaDataSubsystem* Data = GetData())
	{
		if (const FValhallaSkillTemplate* Skill = Data->FindSkill(Skills->CastingSkillId))
		{
			Name = Skill->Name;
		}
	}
	const float Left = (Skills->CastDurationMs / 1000.f) * (1.f - Progress);
	CastBar.Label->SetText(AsText(FString::Printf(TEXT("%s  %.1fs"), *Name, Left)));
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
	TargetHp.Set(Frac);
	TargetHp.FillBorder->SetBrushColor(Frac > 0.5f ? Config.Hud.HpHigh : (Frac > 0.25f ? Config.Hud.HpMid : Config.Hud.HpLow));
	TargetHp.Label->SetText(AsText(FString::Printf(TEXT("%.0f / %.0f"), Info.Hp, Info.MaxHp)));

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
		PartyBars[Index].Set(Frac);
		PartyBars[Index].FillBorder->SetBrushColor(Frac > 0.5f ? Config.Hud.HpHigh : (Frac > 0.25f ? Config.Hud.HpMid : Config.Hud.HpLow));
		PartyBars[Index].Label->SetText(AsText(Member ? FString::Printf(TEXT("%.0f/%.0f"), Member->Hp, Member->MaxHp) : FString()));
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

	FValhallaStatBlock Bonus;
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

		const FValhallaStatBlock& S = Template->StatBonuses;
		Bonus.Hp += S.Hp; Bonus.Mana += S.Mana; Bonus.Strength += S.Strength; Bonus.Stamina += S.Stamina;
		Bonus.Dexterity += S.Dexterity; Bonus.Intelligence += S.Intelligence; Bonus.Wisdom += S.Wisdom;
		Bonus.PhysicalResist += S.PhysicalResist; Bonus.SpellResist += S.SpellResist;
	}

	// The resolved block is server-only (AValhallaPlayerState's class
	// comment), so the panel shows the vitals the client *is* told and the
	// gear's own bonuses, which it can compute from items.json.
	CharacterStats->SetText(AsText(FString::Printf(
		TEXT("Level %d   XP %d\nHP %.0f/%.0f   %s\nGear: STR +%.0f  STA +%.0f  DEX +%.0f\n      INT +%.0f  WIS +%.0f  PR +%.0f  SR +%.0f"),
		PS->Level, PS->Xp, PS->Hp, PS->MaxHp,
		PS->MaxMana > 0.f ? *FString::Printf(TEXT("MP %.0f/%.0f"), PS->Mana, PS->MaxMana) : *FString::Printf(TEXT("EP %.0f/%.0f"), PS->Energy, PS->MaxEnergy),
		Bonus.Strength, Bonus.Stamina, Bonus.Dexterity, Bonus.Intelligence, Bonus.Wisdom, Bonus.PhysicalResist, Bonus.SpellResist)));
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
	if (!RootCanvas || !ChatPanel || !ChatSizer || !ActionBarRoot)
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
		Plate.Bar.Set(Frac);
		Plate.Bar.FillBorder->SetBrushColor(bHostile ? Config.Hud.HpLow : Config.Hud.HpHigh);
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
		PushCombatLog(FString::Printf(TEXT("Can't do that: %s"), *Event.Text), Srgb(0xff, 0x88, 0x44), TEXT("casts"));
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
	InventoryRoot->SetVisibility(bInventoryOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
	if (Item->AttackDamage > 0.f) { Body += FString::Printf(TEXT("\n%.0f damage"), Item->AttackDamage); }
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
		// filtered, no mips), then the 1.0 PNG straight off disk — so an icon
		// added to 1.0 shows up without a re-import, as every other piece of
		// 1.0 data does.
		const FString AssetPath = FString::Printf(TEXT("/Game/Valhalla/UI/Icons/Items/%s.%s"), *Base, *Base);
		Texture = LoadObject<UTexture2D>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);

		if (!Texture)
		{
			const FString DiskPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				Data->GetLoadedDataRoot(), TEXT("../../client/dist/assets/sprites/icons"), Item->InventoryIcon));
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
	if (USizeBox* Box = Cast<USizeBox>(ChatPanel->GetContent()))
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
	if (USizeBox* Box = Cast<USizeBox>(ChatPanel->GetContent()))
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
