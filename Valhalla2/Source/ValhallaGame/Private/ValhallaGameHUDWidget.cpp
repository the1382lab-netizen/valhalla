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
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Widgets/SViewport.h"
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
#include "ValhallaOptionsMenuWidget.h"
#include "ValhallaStats.h"
#include "ValhallaUserSettingsSubsystem.h"

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
	 *   B-21 (the player's UI settings, this HUD's game instance):
	 *   settings  (print the JSON) | resetlayout | panels  (log each movable panel's geometry)
	 *   movepanel <Key> <x> <y>  (place a panel at canvas offset x, y from its designer anchor: dev, until edit mode)
	 *   hidepanel <Key> | showpanel <Key> | uiscale <0.5..2> | opacity <0.2..1> | border <1..12, 0 = default>
	 *   B-21 steps 3-4: lock <0|1>  (0 = edit mode) | options  (toggle the options menu)
	 *   dragtest <Key> <dx> <dy> [resize]  (press, move, release through the edit-mode drag code)
	 */
	FAutoConsoleCommandWithWorldAndArgs GUICommand(
		TEXT("valhalla.UI"),
		TEXT("Dev only. valhalla.UI [@class] <inventory|skills|chat [text]|say <line>|loot|close|arm <skill>|filter <key>|tooltip <i>|reload|press <slot> [x y]|orbit <deg>|walk <wasd> <s>")
		TEXT("|settings|resetlayout|panels|movepanel <key> <x> <y>|hidepanel <key>|showpanel <key>|uiscale <s>|opacity <a>|lock <0|1>|options|dragtest <key> <dx> <dy> [resize]> — drive a client's HUD."),
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
				else if (Verb == TEXT("options"))  { Hud.ToggleOptions(); }
				else if (Verb == TEXT("dragtest"))
				{
					// B-21 step 3: the edit-mode drag, through the same Begin / Update /
					// EndPanelDrag the overlay's mouse handlers call (the commit
					// follows two ticks later).
					if (Args.Num() < 4)
					{
						UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI dragtest needs <key> <dx> <dy> [resize]."));
						return;
					}
					const bool bResize = Args.IsValidIndex(4) && Args[4].Equals(TEXT("resize"), ESearchCase::IgnoreCase);
					Hud.DragPanelForTest(FName(*Args[1]), FVector2D(FCString::Atof(*Args[2]), FCString::Atof(*Args[3])), bResize);
				}
				else if (Verb == TEXT("settings") || Verb == TEXT("lock") || Verb == TEXT("resetlayout") || Verb == TEXT("panels") || Verb == TEXT("movepanel")
					|| Verb == TEXT("hidepanel") || Verb == TEXT("showpanel") || Verb == TEXT("uiscale") || Verb == TEXT("opacity") || Verb == TEXT("border"))
				{
					// B-21: the player's UI settings for this HUD's character.
					UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(&Hud);
					if (!UserSettings)
					{
						UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI %s: no UI settings subsystem."), *Verb);
						return;
					}
					if (Verb == TEXT("settings"))
					{
						UE_LOG(LogValhallaHUD, Log, TEXT("UI settings for character %d (%s, %s):\n%s"), UserSettings->GetCharacterId(),
							*UValhallaUserSettingsSubsystem::CachePathFor(UserSettings->GetCharacterId()),
							UserSettings->IsDirty() ? TEXT("unsaved changes") : TEXT("saved"), *UserSettings->Get().ToJsonString());
					}
					else if (Verb == TEXT("resetlayout"))
					{
						UserSettings->ResetToDefaults(EValhallaUISettingsSection::Layout);
					}
					else if (Verb == TEXT("panels"))
					{
						Hud.LogPanelGeometry();
					}
					else if (Verb == TEXT("lock"))
					{
						const bool bLock = Rest.IsEmpty() ? !UserSettings->Get().bLocked : FCString::Atoi(*Rest) != 0;
						UserSettings->Mutate([bLock](FValhallaUserUISettings& S) { S.bLocked = bLock; });
					}
					else if (Verb == TEXT("border"))
					{
						// Framed panels' border, px; 0 = the HUD's default.
						const float Value = FCString::Atof(*Rest);
						UserSettings->Mutate([Value](FValhallaUserUISettings& S) { S.PanelBorder = Value; });
					}
					else if (Verb == TEXT("uiscale") || Verb == TEXT("opacity"))
					{
						const float Value = FCString::Atof(*Rest);
						const bool bScale = Verb == TEXT("uiscale");
						UserSettings->Mutate([bScale, Value](FValhallaUserUISettings& S) { (bScale ? S.UiScale : S.PanelOpacity) = Value; });
					}
					else
					{
						const FName Key = Args.IsValidIndex(1) ? FName(*Args[1]) : NAME_None;
						const FValhallaPanelLayout* Designer = Hud.GetDesignerLayout(Key);
						if (!UValhallaGameHUDWidget::FindMovablePanel(Key) || !Designer)
						{
							UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI %s: '%s' is not a movable panel on this HUD."), *Verb, *Key.ToString());
							return;
						}
						const FValhallaPanelLayout Base = *Designer;
						const bool bMove = Verb == TEXT("movepanel");
						if (bMove && Args.Num() < 4)
						{
							UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI movepanel needs <key> <x> <y>."));
							return;
						}
						const FVector2D Position = bMove ? FVector2D(FCString::Atof(*Args[2]), FCString::Atof(*Args[3])) : FVector2D::ZeroVector;
						UserSettings->Mutate([&](FValhallaUserUISettings& S)
						{
							FValhallaPanelLayout* Existing = S.Panels.Find(Key);
							FValhallaPanelLayout Layout = Existing && Existing->bSet ? *Existing : Base;
							Layout.bSet = true;
							if (bMove) { Layout.Position = Position; }
							else { Layout.bVisible = Verb == TEXT("showpanel"); }
							S.Panels.Add(Key, Layout);
						});
					}
				}
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
//  Frame art (B-15 Wave 4)
// ═════════════════════════════════════════════════════════════════════════════

namespace ValhallaHudArt
{
	/**
	 * A frame texture's size in its own pixels: the imported (source) size.
	 * GetSizeX / Y are the built platform data, which in the editor is a
	 * 32 x 32 placeholder while the texture is still compiling after startup
	 * (a 24 px margin then comes out as 0.75 of the texture instead of 24/256).
	 */
	static FVector2D TextureSize(const UTexture2D* Texture)
	{
		const FIntPoint Imported = Texture->GetImportedSize();
		if (Imported.X > 0 && Imported.Y > 0)
		{
			return FVector2D(Imported.X, Imported.Y);
		}
		return FVector2D(FMath::Max(1, Texture->GetSizeX()), FMath::Max(1, Texture->GetSizeY()));
	}

	/**
	 * A nine-slice brush from a frame texture. `MarginPx` is the border in the
	 * texture's own pixels (x, y); `BorderPx` is how wide that border draws on
	 * screen, which sets the brush's image size.
	 */
	static FSlateBrush BoxBrush(UTexture2D* Texture, const FVector2D& MarginPx, float BorderPx)
	{
		FSlateBrush Brush;
		if (!Texture)
		{
			return Brush;
		}
		const FVector2D Size = TextureSize(Texture);
		const float Scale = BorderPx / FMath::Max(1.0, MarginPx.X);
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Size * Scale;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = FMargin(MarginPx.X / Size.X, MarginPx.Y / Size.Y);
		Brush.TintColor = FSlateColor(FLinearColor::White);
		return Brush;
	}

	static FSlateBrush ImageBrush(UTexture2D* Texture)
	{
		FSlateBrush Brush;
		if (Texture)
		{
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Brush.DrawAs = ESlateBrushDrawType::Image;
		}
		return Brush;
	}
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
		// The designer's look stands, at the size C++ asks for (B-07 step 4:
		// one WBP_HUDSlot serves the 44 px action cells, the 48 px inventory and
		// loot cells, the 36 px skills and the 26 px equipment slots). Its Sizer
		// is the whole cell, as the code-built one is. The sweep starts empty.
		if (Size > 0.f)
		{
			Sizer->SetWidthOverride(Size);
			Sizer->SetHeightOverride(Size);
		}
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

void UValhallaHUDSlotWidget::SetSkillIcon(const FString& Code, const FLinearColor& Category, const FLinearColor& SkillColour, UTexture2D* Texture)
{
	if (Texture)
	{
		// The painted icon (Import/UI/Icons/Skills) fills the well; no code.
		SkillTile->SetVisibility(ESlateVisibility::Collapsed);
		SetAbbrev(FString(), FLinearColor::White);
		SetIcon(Texture);
		return;
	}
	SetIcon(nullptr);
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

void UValhallaHUDSlotWidget::SetFrameArt(UTexture2D* SlotTexture, const FVector2D& MarginPx, float BorderPx, float FramePadding)
{
	EnsureTree();
	if (!SlotTexture)
	{
		return;
	}
	bFrameArt = true;
	// By default T_UI_Slot: 10 px of the 64 px texture is rim, drawn 4 px wide
	// whatever the cell size. The combat log passes the panel art instead.
	Frame->SetBrush(ValhallaHudArt::BoxBrush(SlotTexture, MarginPx, BorderPx));
	Frame->SetBrushColor(FLinearColor::White);
	Frame->SetPadding(FMargin(FramePadding));
	Fill->SetBrushColor(FLinearColor::Transparent);
}

void UValhallaHUDSlotWidget::SetHighlightColour(const FLinearColor& Colour)
{
	HighlightColour = Colour;
	if (bSelectedState && bTreeReady)
	{
		SetSelected(true);
	}
}

void UValhallaHUDSlotWidget::SetSelected(bool bSelected)
{
	bSelectedState = bSelected;
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
	if (bFrameArt)
	{
		Frame->SetBrushColor(bSelected ? HighlightColour : FLinearColor::White);
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

	UValhallaHUDDragOperation* Operation = NewObject<UValhallaHUDDragOperation>();
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
	if (Source == this)
	{
		// Let go where it started: handled, so the drag is not "cancelled" (which
		// for an action bar cell would take the skill off the bar).
		return true;
	}
	if (Source && Owner)
	{
		Owner->HandleSlotDropped(Source, this);
		return true;
	}
	return false;
}

void UValhallaHUDDragOperation::DragCancelled_Implementation(const FPointerEvent& PointerEvent)
{
	Super::DragCancelled_Implementation(PointerEvent);
	UValhallaHUDSlotWidget* Source = Cast<UValhallaHUDSlotWidget>(Payload);
	if (Source && Source->Kind == EValhallaHUDSlotKind::Action)
	{
		UValhallaGameHUDWidget* Owner = Source->Hud.Get();
		// Let go in a gap between two bar cells: still on the bar, so kept.
		if (Owner && !Owner->IsOverActionBar(PointerEvent.GetScreenSpacePosition()))
		{
			Owner->HandleSlotDraggedOff(Source);
		}
	}
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

UValhallaPartyRowButton::UValhallaPartyRowButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Keyboard focus stays with the game viewport (WASD keeps walking).
	InitIsFocusable(false);
	Action = EValhallaHUDButton::PartySelect;
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

void UValhallaHUDBarWidget::SetBarSize(float InWidth, float InHeight)
{
	EnsureTree();
	Width = FMath::Max(0.f, InWidth);
	if (bDesignerTree)
	{
		// B-07 step 4: the designer's Sizer is the inside of the frame, as the
		// code-built bar's is, so a WBP_HUDBar made by C++ (party, nameplates)
		// comes out at the size C++ asks for; the fill spans the same width.
		BarWidth = FMath::Max(1.f, Width);
	}
	if (Sizer)
	{
		Sizer->SetWidthOverride(Width);
		Sizer->SetHeightOverride(FMath::Max(0.f, InHeight));
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

void UValhallaHUDBarWidget::SetFrameArt(UTexture2D* FrameTexture, UTexture2D* FillTexture)
{
	EnsureTree();
	if (bDesignerTree)
	{
		return;
	}
	if (FrameTexture)
	{
		// An iron frame (6 px of 128 x 24), drawn 3 px wide.
		Frame->SetBrush(ValhallaHudArt::BoxBrush(FrameTexture, FVector2D(6.f, 6.f), 3.f));
		Frame->SetBrushColor(FLinearColor::White);
		Frame->SetPadding(FMargin(2.f));
	}
	if (FillTexture)
	{
		// A glossy grey ramp; the bar colour tints it (SetFillColour sets the brush colour).
		Fill->SetBrush(ValhallaHudArt::ImageBrush(FillTexture));
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
	// A bar C++ makes (party, nameplates) is C++'s size, designer tree or not.
	Bar->SetBarSize(Width, Height);
	Bar->SetFrameArt(FindUiTexture(TEXT("T_UI_BarFrame")), FindUiTexture(TEXT("T_UI_BarFill")));
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

UBorder* UValhallaGameHUDWidget::MakePanel(const FLinearColor& Colour, float Alpha, float PanelPadding, bool bFramed)
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetBrushColor(WithAlpha(Colour, Alpha));
	Panel->SetPadding(FMargin(PanelPadding));
	const float Border = EffectivePanelBorder();
	UTexture2D* PanelArt = nullptr;
	if (bFramed && Alpha > 0.f)
	{
		PanelArt = FindUiTexture(PanelFrameTextureName(FMath::RoundToInt(Border)));
		PanelArt = PanelArt ? PanelArt : FindUiTexture(TEXT("T_UI_Panel"));
	}
	if (PanelArt)
	{
		// Leather in bronze trim, drawn at the panel border thickness (HUD
		// Style, or the player's Options -> Layout).
		// Nearly opaque: the trim should not look washed out over the world.
		FSlateBrush Brush = ValhallaHudArt::ImageBrush(PanelArt);
		SetPanelFrameThickness(Brush, Border);
		Brush.TintColor = FSlateColor(FLinearColor::White);
		Panel->SetBrush(Brush);
		Panel->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, FMath::Max(Alpha, 0.94f)));
		Panel->SetPadding(FMargin(PanelPadding + 10.f));
	}
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
	if (UTexture2D* ButtonArt = FindUiTexture(TEXT("T_UI_Button")))
	{
		// A bronze plate: 10 px of the 64 x 32 texture is bevel, drawn 5 px.
		FButtonStyle Style = Button->GetStyle();
		FSlateBrush Plate = ValhallaHudArt::BoxBrush(ButtonArt, FVector2D(10.f, 10.f), 5.f);
		Style.SetNormal(Plate);
		Plate.TintColor = FSlateColor(FLinearColor(1.25f, 1.2f, 1.1f));
		Style.SetHovered(Plate);
		Plate.TintColor = FSlateColor(FLinearColor(0.75f, 0.72f, 0.68f));
		Style.SetPressed(Plate);
		Style.SetNormalPadding(FMargin(8.f, 2.f));
		Style.SetPressedPadding(FMargin(8.f, 3.f, 8.f, 1.f));
		Button->SetStyle(Style);
	}
	Button->AddChild(MakeText(Caption, 9, FLinearColor::Black, true, 0.f));
	Button->OnClicked.AddDynamic(Button, &UValhallaHUDButton::HandleClicked);
	return Button;
}

UValhallaHUDSlotWidget* UValhallaGameHUDWidget::MakeCell(EValhallaHUDSlotKind Kind, int32 Index, float Size)
{
	// B-07: SlotWidgetClass (WBP_HUDSlot restyles every cell); Setup sizes it.
	UClass* CellClass = SlotWidgetClass.Get() ? SlotWidgetClass.Get() : UValhallaHUDSlotWidget::StaticClass();
	UValhallaHUDSlotWidget* Cell = WidgetTree->ConstructWidget<UValhallaHUDSlotWidget>(CellClass);
	Cell->Kind = Kind;
	Cell->Index = Index;
	Cell->Hud = this;

	// The flat colours only show on a code-built cell without the T_UI_Slot art
	// (1.0's action bar #1a1a1a / #666666, inventory #2a2a3e / #333355).
	const bool bActionBar = Kind == EValhallaHUDSlotKind::Action;
	Cell->Setup(Size,
		bActionBar ? Srgb(0x1a, 0x1a, 0x1a) : Srgb(0x2a, 0x2a, 0x3e),
		bActionBar ? Srgb(0x66, 0x66, 0x66) : Srgb(0x33, 0x33, 0x55),
		EffectiveHighlightColour());
	if (!Cell->HasDesignerTree())
	{
		Cell->SetFrameArt(FindUiTexture(TEXT("T_UI_Slot")));
	}
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

	// B-07 step 4: the values ui-config.json used to carry for these (1.0's
	// DEFAULT_UI_CONFIG), now WBP_GameHUD's Class Defaults.
	HpHighColour = Srgb(0x44, 0xff, 0x44);
	HpMidColour = Srgb(0xff, 0xaa, 0x00);
	HpLowColour = Srgb(0xff, 0x44, 0x44);
	ManaColour = Srgb(0x44, 0x88, 0xff);
	EnergyColour = Srgb(0xdd, 0xaa, 0x00);
	CastBarColour = Srgb(0xff, 0xaa, 0x00);
	CastBarTextColour = Srgb(0xff, 0xff, 0xff);
	CooldownColour = Srgb(0x88, 0x00, 0x00, 0.6f);
	KeyLabelColour = Srgb(0x88, 0x88, 0x88);
	HighlightColour = Srgb(0xff, 0xaa, 0x00);
	LabelColour = Srgb(0xaa, 0xaa, 0xcc);
	ValueColour = Srgb(0xff, 0xff, 0xff);
	ChatGeneralColour = Srgb(0xff, 0xff, 0xff);
	ChatWorldColour = Srgb(0xff, 0xdd, 0x00);
	ChatWhisperColour = Srgb(0xff, 0x88, 0xcc);
	ChatPartyColour = Srgb(0x5a, 0xd8, 0xff);
	ChatSystemColour = Srgb(0xff, 0xaa, 0x44);
	ChatOpenBackground = Srgb(0x0a, 0x0a, 0x1a, 0.72f);
	// B-21: white leaves the designer's panel colours as they are.
	PanelTintColour = FLinearColor::White;
	// B-21 step 4: WBP_GameHUD's Class Defaults name WBP_OptionsMenu; the C++
	// class alone has no layout (it warns and shows nothing).
	OptionsMenuClass = UValhallaOptionsMenuWidget::StaticClass();
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
		TEXT("OptionsButton"),
	};
	return Names;
}

const TArray<FName>& UValhallaGameHUDWidget::GetClickEatingPanelNames()
{
	// A click on either used to fall through to the world: clicking the target
	// frame (nothing in it is a button) cleared the target.
	static const TArray<FName> Names = { TEXT("TargetFramePanel"), TEXT("PartyPanel") };
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

	// B-07: a Widget Blueprint child with a complete designer tree lays the HUD
	// out (WBP_GameHUD, Project Settings > Valhalla > UI > Game HUD Class). The
	// code-built layout is gone (step 4): anything else gets a blank HUD, only
	// the nameplates and floating text, and an error saying how to fix it.
	bLayoutFromBlueprint = WantsLayoutFromBlueprint();
	if (bLayoutFromBlueprint)
	{
		RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
		UE_LOG(LogValhallaHUD, Log, TEXT("game HUD: layout from the Widget Blueprint %s."), *GetClass()->GetName());
	}
	else if (WidgetTree->RootWidget)
	{
		// A designer tree that is not usable: say what is missing, then drop it
		// (nothing has been handed to Slate yet) and run without a layout.
		TArray<FString> Missing;
		for (const FName Name : GetRequiredPanelNames())
		{
			const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Name);
			if (!Property || !Property->GetObjectPropertyValue_InContainer(this))
			{
				Missing.Add(Name.ToString());
			}
		}
		UE_LOG(LogValhallaHUD, Error, TEXT("game HUD: %s's designer tree %s%s; the HUD is blank except nameplates. Fix the Widget Blueprint, or set Project Settings > Valhalla > UI > Game HUD Class to /Game/Valhalla/UI/HUD/WBP_GameHUD."),
			*GetClass()->GetName(),
			Cast<UCanvasPanel>(WidgetTree->RootWidget) ? TEXT("") : TEXT("has no Canvas Panel root"),
			Missing.Num() > 0 ? *FString::Printf(TEXT(" lacks %s"), *FString::Join(Missing, TEXT(", "))) : TEXT(""));
		ResetPanelPointers();
		WidgetTree->RootWidget = nullptr;
	}
	else if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		UE_LOG(LogValhallaHUD, Error,
			TEXT("game HUD: %s has no Widget Blueprint layout (the code-built HUD was removed in B-07), so the HUD is blank except nameplates. ")
			TEXT("Set Project Settings > Valhalla > UI > Game HUD Class to /Game/Valhalla/UI/HUD/WBP_GameHUD (or valhalla.HudClass for one session)."),
			*GetClass()->GetName());
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

	// B-21: this character's UI settings (the disk cache now, the backend's
	// when it answers: OnChanged, bound in NativeConstruct, re-applies them).
	if (UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this))
	{
		UserSettings->EnsureLoadedForCurrentCharacter();
	}

	Rebuild();
}

void UValhallaGameHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	BindCombatEvents();

	if (UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this); UserSettings && !UserSettingsHandle.IsValid())
	{
		UserSettingsHandle = UserSettings->OnChanged.AddUObject(this, &UValhallaGameHUDWidget::HandleUserSettingsChanged);
		// A load may have finished between NativeOnInitialized and now.
		HandleUserSettingsChanged(UserSettings->Get());
	}
}

void UValhallaGameHUDWidget::NativeDestruct()
{
	if (AValhallaGameState* GameState = BoundGameState.Get())
	{
		GameState->OnCombatEvent.Remove(CombatEventHandle);
	}
	BoundGameState.Reset();
	// B-21: a change still waiting for its debounce is saved now (EndPlay,
	// travel, the HUD being replaced).
	if (UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this))
	{
		UserSettings->OnChanged.Remove(UserSettingsHandle);
		UserSettings->FlushPendingSave();
	}
	UserSettingsHandle.Reset();
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

	UE_LOG(LogValhallaHUD, Log, TEXT("HUD built (%s) with ui-config %s (sections %s): action cells %.0f px, inventory %dx%d @ %.0f px, chat %d open / %d idle lines, cells %s, bars %s."),
		bLayoutFromBlueprint ? *GetClass()->GetName() : TEXT("no layout"), *Config.Version, Config.HasAllSections() ? TEXT("all 3") : TEXT("partial, defaults used"),
		ActionSlotSize, Config.Inventory.Cols, Config.Inventory.Rows, InventorySlotSize,
		Config.Chat.MaxMessages, Config.Chat.VisibleLines, *GetNameSafe(SlotWidgetClass.Get()), *GetNameSafe(BarWidgetClass.Get()));
}

void UValhallaGameHUDWidget::BuildAll()
{
	// B-07 step 4: the designer's tree is the only layout. Without one (the bare
	// C++ class, or an incomplete Blueprint) every panel is a hidden stand-in,
	// so the HUD runs, blank but for the nameplates and floating text.
	BindDesignerPanels();
	PopulateDesignerPanels();

	// B-21: the player's layout over the designer's (the designer's is
	// recorded the first time, after click-through has settled visibility).
	CaptureDesignerDefaults();
	if (const UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this))
	{
		ApplyUserLayout(UserSettings->Get());
		ApplyUserStyle(UserSettings->Get());
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
	// The character panel opens and closes with the inventory (I).
	if (CharacterPanel && CharacterPanel != InventoryRoot)
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
	// Without a layout at all, NativeOnInitialized's one error says it all.
	if (bLayoutFromBlueprint && !WarnedMissingParts.Contains(Key))
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
	// none: no container, no cells. The required parts only need stand-ins
	// when there is no layout at all (the bare C++ class).
	EnsureDesignerPart(VitalsPanel, TEXT("VitalsPanel"), UVerticalBox::StaticClass());
	EnsureDesignerPart(HpBar, TEXT("HpBar"));
	EnsureDesignerPart(ManaBar, TEXT("ManaBar"));
	EnsureDesignerPart(ActionBarRow, TEXT("ActionBarRow"), UHorizontalBox::StaticClass());
	EnsureDesignerPart(ChatPanel, TEXT("ChatPanel"));
	EnsureDesignerPart(ChatScroll, TEXT("ChatScroll"));
	EnsureDesignerPart(ChatInput, TEXT("ChatInput"));
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
	EnsureDesignerPart(OptionsButton, TEXT("OptionsButton"));

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

	// The chat's Size Box (ChatPanel > Size Box > ...): TickLayout narrows it on
	// a narrow viewport and OpenChat jumps it to its max height. What the
	// designer set is read once, before TickLayout first moves anything.
	ChatSizer = Cast<USizeBox>(ChatPanel->GetContent());
	if (!bChatDesignKnown && ChatSizer)
	{
		if (const UCanvasPanelSlot* ChatSlot = Cast<UCanvasPanelSlot>(ChatPanel->Slot))
		{
			bChatDesignKnown = true;
			ChatDesignPosition = ChatSlot->GetPosition();
			ChatDesignWidth = ChatSizer->GetWidthOverride();
			ChatOpenHeight = ChatSizer->GetMaxDesiredHeight();
		}
	}
	LayoutForWidth = -1.f;

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

	// The chat box. By default a text box clears keyboard focus after its commit
	// handler runs, which undoes CloseChat's hand-back to the game viewport:
	// Enter then reached nothing until the player clicked the screen.
	ChatInput->OnTextCommitted.AddUniqueDynamic(this, &UValhallaGameHUDWidget::HandleChatCommitted);
	ChatInput->SetClearKeyboardFocusOnCommit(false);
}

void UValhallaGameHUDWidget::PopulateDesignerPanels()
{
	// Nameplates and floaters are C++'s own layer, under every designer panel.
	BuildWorldLayer();

	// Bars: a designer bar keeps its look and size (WBP_HUDBar in a Size Box);
	// C++ gives it its fill colour. The numbers only size a code-built bar
	// (a bare UValhallaHUDBarWidget placed in the designer).
	const FLinearColor BarBackground = FLinearColor::Black;
	HpBar->Setup(200.f, 12.f, EffectiveHpHighColour(), BarBackground, 0.7f, /*bWithOverlay=*/true, 7);
	ManaBar->Setup(200.f, 12.f, EffectiveManaColour(), BarBackground, 0.7f, false, 7);
	CastBar->Setup(260.f, 16.f, EffectiveCastBarColour(), BarBackground, 0.6f, false, 8);
	CastBar->SetLabelColour(CastBarTextColour);
	TargetHpBar->Setup(240.f, 14.f, EffectiveHpHighColour(), BarBackground, 0.7f, false, 8);
	XpBar->Setup(194.f, 4.f, Srgb(0xff, 0xaa, 0x00), BarBackground, 0.8f, false, 6);
	XpBar->SetToolTipText(AsText(TEXT("Experience to the next level")));

	// The containers C++ fills.
	AddActionCells(ActionBarRow);
	if (TargetBuffs)    { AddTargetBuffTokens(TargetBuffs); }
	if (PartyList)      { AddPartyRows(PartyList); }
	if (LootGrid)       { AddLootCells(LootGrid); }
	if (EquipmentPanel) { AddEquipRows(EquipmentPanel); }
	if (InventoryGrid)  { AddInventoryCells(InventoryGrid); }
	// The filter menu is C++'s: top right, under WBP_GameHUD's combat log.
	BuildLogFilterMenu(FVector2D(-12.f, 12.f + 200.f + 4.f));

	// Clicks on empty space reach the world whatever the designer left set.
	ApplyClickThrough(RootCanvas);

	// Start states.
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

	// The target and party frames eat clicks over their whole area (their
	// children were settled above): hit-testable here, and a left press on them
	// is handled in NativeOnMouseButtonDown, so it never reaches the world.
	if (GetClickEatingPanelNames().Contains(Widget->GetFName()))
	{
		if (Widget->GetVisibility() != ESlateVisibility::Collapsed && Widget->GetVisibility() != ESlateVisibility::Hidden)
		{
			Widget->SetVisibility(ESlateVisibility::Visible);
		}
		return true;
	}

	// Layout panels never need to eat clicks. A border (a panel background)
	// with something clickable in it keeps eating them (as MakePanel's do), so
	// a click on the inventory does not also walk the character.
	if (Widget->GetVisibility() == ESlateVisibility::Visible && !(bHoldsClickable && Widget->IsA<UBorder>()))
	{
		Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	return bHoldsClickable;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Player layout (B-21) — the settings subsystem's Panels over the designer's
// ═════════════════════════════════════════════════════════════════════════════

namespace
{
	/** `Widget`, or the ancestor of it that sits directly on `Canvas`. */
	UWidget* CanvasChildOf(UWidget* Widget, const UCanvasPanel* Canvas)
	{
		while (Widget && Widget->GetParent() && Widget->GetParent() != Canvas)
		{
			Widget = Widget->GetParent();
		}
		return Widget && Widget->GetParent() == Canvas ? Widget : nullptr;
	}

	/** A Size Box override: a positive value sets it, anything else clears it (auto). */
	void SetWidthOrClear(USizeBox* Box, float Value)
	{
		if (Value > 0.f) { Box->SetWidthOverride(Value); } else { Box->ClearWidthOverride(); }
	}
	void SetHeightOrClear(USizeBox* Box, float Value)
	{
		if (Value > 0.f) { Box->SetHeightOverride(Value); } else { Box->ClearHeightOverride(); }
	}
	void SetMaxHeightOrClear(USizeBox* Box, float Value)
	{
		if (Value > 0.f) { Box->SetMaxDesiredHeight(Value); } else { Box->ClearMaxDesiredHeight(); }
	}

	// An unset override reads 0 (its default), which the setters above treat as "auto".
	float BoxWidth(const USizeBox* Box) { return Box->GetWidthOverride(); }
	float BoxHeight(const USizeBox* Box) { return Box->GetHeightOverride(); }
	float BoxMaxHeight(const USizeBox* Box) { return Box->GetMaxDesiredHeight(); }
}

const TArray<FValhallaMovablePanel>& UValhallaGameHUDWidget::GetMovablePanels()
{
	// Keep the members in step with the header's BindWidget(Optional) panels
	// (Valhalla.Game.UI.MovablePanels checks). Flowing: the Size Box named here
	// takes Size; scaled: fixed content, the player scales it.
	using ESizing = EValhallaPanelSizing;
	static const TArray<FValhallaMovablePanel> Panels = {
		{ TEXT("Vitals"),      TEXT("VitalsPanel"),      ESizing::Scaled,           NAME_None,              true  },
		{ TEXT("ActionBar"),   TEXT("ActionBarRow"),     ESizing::Scaled,           NAME_None,              true  },
		{ TEXT("CastBar"),     TEXT("CastBar"),          ESizing::Scaled,           NAME_None,              true  },
		{ TEXT("TargetFrame"), TEXT("TargetFramePanel"), ESizing::Scaled,           NAME_None,              true  },
		{ TEXT("Party"),       TEXT("PartyPanel"),       ESizing::Scaled,           NAME_None,              true  },
		{ TEXT("CombatLog"),   TEXT("CombatLogPanel"),   ESizing::FlowingBox,       TEXT("CombatLogSize"),  true  },
		{ TEXT("Chat"),        TEXT("ChatPanel"),        ESizing::FlowingMaxHeight, TEXT("ChatSize"),       true  },
		{ TEXT("Loot"),        TEXT("LootPanel"),        ESizing::Scaled,           NAME_None,              false },
		{ TEXT("Skills"),      TEXT("SkillsPanel"),      ESizing::FlowingMaxHeight, TEXT("SkillsSize"),     false },
		{ TEXT("Character"),   TEXT("CharacterPanel"),   ESizing::Scaled,           NAME_None,              false },
		{ TEXT("Inventory"),   TEXT("InventoryPanel"),   ESizing::Scaled,           NAME_None,              false },
	};
	return Panels;
}

const FValhallaMovablePanel* UValhallaGameHUDWidget::FindMovablePanel(FName Key)
{
	return GetMovablePanels().FindByPredicate([Key](const FValhallaMovablePanel& Info) { return Info.Key == Key; });
}

FValhallaPanelLayout UValhallaGameHUDWidget::ResolvePanelLayout(const FValhallaPanelLayout& Designer, const FValhallaPanelLayout* User, float UiScale)
{
	const bool bUser = User && User->bSet;
	const float Scale = FMath::IsFinite(UiScale)
		? FMath::Clamp(UiScale, FValhallaUserUISettings::MinUiScale, FValhallaUserUISettings::MaxUiScale) : 1.f;
	FValhallaPanelLayout Out = bUser ? *User : Designer;
	Out.Position = Out.Position * Scale;
	Out.Scale = Scale * (bUser ? FMath::Clamp(User->Scale, FValhallaPanelLayout::MinScale, FValhallaPanelLayout::MaxScale) : 1.f);
	Out.Size = FVector2D(
		bUser && User->Size.X > 0.0 ? User->Size.X : Designer.Size.X,
		bUser && User->Size.Y > 0.0 ? User->Size.Y : Designer.Size.Y);
	Out.bVisible = bUser ? User->bVisible : true;
	Out.bSet = bUser;
	return Out;
}

const FValhallaPanelLayout* UValhallaGameHUDWidget::GetDesignerLayout(FName Key) const
{
	const FPanelState* State = PanelStates.Find(Key);
	return State ? &State->Designer : nullptr;
}

void UValhallaGameHUDWidget::CaptureDesignerDefaults()
{
	// Once per HUD: Rebuild keeps the designer's tree, whose canvas slots carry
	// the player's layout after the first ApplyUserLayout.
	if (bDesignerDefaultsCaptured || !bLayoutFromBlueprint || !RootCanvas)
	{
		return;
	}
	bDesignerDefaultsCaptured = true;

	TSet<UWidget*> Claimed;
	for (const FValhallaMovablePanel& Info : GetMovablePanels())
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Info.Member);
		UWidget* Member = Property ? Cast<UWidget>(Property->GetObjectPropertyValue_InContainer(this)) : nullptr;
		UWidget* Widget = CanvasChildOf(Member, RootCanvas);
		UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
		if (!CanvasSlot)
		{
			// A hidden stand-in (the designer left it out) is on no canvas.
			continue;
		}
		if (Claimed.Contains(Widget))
		{
			// Two panels in one canvas child (a pre-B-21 WBP_GameHUD's InventoryPair): the first one moves it.
			UE_LOG(LogValhallaHUD, Warning, TEXT("game HUD: movable panel %s shares %s with another panel; it cannot be moved on its own."),
				*Info.Key.ToString(), *Widget->GetName());
			continue;
		}
		Claimed.Add(Widget);

		FPanelState State;
		State.Widget = Widget;
		const FAnchors Anchors = CanvasSlot->GetAnchors();
		State.Designer.AnchorMin = FVector2D(Anchors.Minimum);
		State.Designer.AnchorMax = FVector2D(Anchors.Maximum);
		State.Designer.Alignment = FVector2D(CanvasSlot->GetAlignment());
		State.Designer.Position = FVector2D(CanvasSlot->GetPosition());
		State.Designer.Scale = 1.f;
		State.Designer.bVisible = true;
		State.Designer.bSet = false;
		State.DesignerVisibility = Widget->GetVisibility();
		State.DesignerTransform = Widget->GetRenderTransform();
		State.DesignerPivot = FVector2D(Widget->GetRenderTransformPivot());

		if (Info.Sizing != EValhallaPanelSizing::Scaled)
		{
			USizeBox* Box = Info.Key == TEXT("Chat") ? ChatSizer.Get() : Cast<USizeBox>(WidgetTree->FindWidget(Info.SizeBox));
			if (Box)
			{
				State.SizeBox = Box;
				State.Designer.Size = Info.Sizing == EValhallaPanelSizing::FlowingBox
					? FVector2D(BoxWidth(Box), BoxHeight(Box))
					: FVector2D(BoxWidth(Box), BoxMaxHeight(Box));
			}
			else
			{
				UE_LOG(LogValhallaHUD, Warning, TEXT("game HUD: %s has no '%s' Size Box; the %s panel cannot be resized."),
					*GetClass()->GetName(), *Info.SizeBox.ToString(), *Info.Key.ToString());
			}
		}

		// Its background(s): the canvas child when it is a border, and a border
		// straight inside it (the combat log's well). The chat's brush is
		// OpenChat / CloseChat's (ChatOpenBrush).
		if (Info.Key != TEXT("Chat"))
		{
			if (UBorder* Frame = Cast<UBorder>(Widget))
			{
				State.Backgrounds.Emplace(Frame, Frame->GetBrushColor());
				if (UBorder* Inner = Cast<UBorder>(Frame->GetContent()))
				{
					State.Backgrounds.Emplace(Inner, Inner->GetBrushColor());
				}
			}
		}
		PanelStates.Add(Info.Key, MoveTemp(State));
	}
	ChatDesignerSize = FVector2D(ChatDesignWidth, ChatOpenHeight);
	UE_LOG(LogValhallaHUD, Log, TEXT("game HUD: %d of %d movable panels on the canvas."), PanelStates.Num(), GetMovablePanels().Num());
}

int32 UValhallaGameHUDWidget::ApplyUserLayout(const FValhallaUserUISettings& Settings)
{
	if (!bLayoutFromBlueprint || !RootCanvas)
	{
		return 0;
	}
	CaptureDesignerDefaults();
	if (PanelStates.Num() == 0)
	{
		return 0;
	}

	AppliedUiScale = FMath::IsFinite(Settings.UiScale)
		? FMath::Clamp(Settings.UiScale, FValhallaUserUISettings::MinUiScale, FValhallaUserUISettings::MaxUiScale) : 1.f;
	AppliedPanelOpacity = FMath::IsFinite(Settings.PanelOpacity)
		? FMath::Clamp(Settings.PanelOpacity, FValhallaUserUISettings::MinPanelOpacity, FValhallaUserUISettings::MaxPanelOpacity) : 1.f;

	int32 Moved = 0;
	for (const FValhallaMovablePanel& Info : GetMovablePanels())
	{
		FPanelState* State = PanelStates.Find(Info.Key);
		UWidget* Widget = State ? State->Widget.Get() : nullptr;
		UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
		if (!CanvasSlot)
		{
			continue;
		}
		const FValhallaPanelLayout Resolved = ResolvePanelLayout(State->Designer, Settings.FindSetPanel(Info.Key), AppliedUiScale);

		// Where.
		CanvasSlot->SetAnchors(FAnchors(Resolved.AnchorMin.X, Resolved.AnchorMin.Y, Resolved.AnchorMax.X, Resolved.AnchorMax.Y));
		CanvasSlot->SetAlignment(Resolved.Alignment);
		CanvasSlot->SetPosition(Resolved.Position);

		// How big: a flowing panel's Size Box.
		if (USizeBox* Box = State->SizeBox.Get())
		{
			if (Info.Key == TEXT("Chat"))
			{
				// TickLayout and OpenChat read these; TickLayout narrows from the
				// width unless the player placed the chat.
				ChatDesignPosition = Resolved.Position;
				ChatDesignWidth = Resolved.Size.X;
				ChatOpenHeight = Resolved.Size.Y;
				SetWidthOrClear(Box, ChatDesignWidth);
				SetMaxHeightOrClear(Box, ChatOpenHeight);
				if (bChatOpen && ChatOpenHeight > 0.f)
				{
					Box->SetHeightOverride(ChatOpenHeight);
				}
			}
			else if (Info.Sizing == EValhallaPanelSizing::FlowingBox)
			{
				SetWidthOrClear(Box, Resolved.Size.X);
				SetHeightOrClear(Box, Resolved.Size.Y);
			}
			else
			{
				SetWidthOrClear(Box, Resolved.Size.X);
				SetMaxHeightOrClear(Box, Resolved.Size.Y);
			}
		}

		// Scale: a render transform about the alignment point, so the anchored
		// corner stays put. Nothing set and UiScale 1: the designer's transform.
		if (!Resolved.bSet && FMath::IsNearlyEqual(Resolved.Scale, 1.f))
		{
			Widget->SetRenderTransformPivot(State->DesignerPivot);
			Widget->SetRenderTransform(State->DesignerTransform);
		}
		else
		{
			FWidgetTransform Transform = State->DesignerTransform;
			Transform.Scale = FVector2D(Resolved.Scale, Resolved.Scale);
			Widget->SetRenderTransformPivot(Resolved.Alignment);
			Widget->SetRenderTransform(Transform);
		}
		State->AppliedScale = Resolved.Scale;

		// Shown: a hideable panel the player hid stays collapsed (EnforceUserHiddenPanels);
		// one they show again gets the designer's visibility back (the Tick code
		// then shows or hides it as the game needs).
		const bool bHide = Info.bHideable && !Resolved.bVisible;
		if (State->bUserHidden && !bHide)
		{
			Widget->SetVisibility(Info.Key == TEXT("Chat")
				? (bChatOpen ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible)
				: State->DesignerVisibility);
		}
		State->bUserHidden = bHide;
		State->bUserSet = Resolved.bSet;
		Moved += Resolved.bSet ? 1 : 0;
	}
	// Background opacity (and B-21 step 5's tint): the designer's brush colours.
	ApplyPanelBackgrounds();
	if (bChatOpen && ChatPanel)
	{
		ChatPanel->SetBrushColor(ChatOpenBrush());
	}
	LayoutForWidth = -1.f; // TickLayout measures again
	EnforceUserHiddenPanels();

	// B-21 step 3: unlocked = edit mode (the overlays), locked = none.
	SetEditMode(!Settings.bLocked);

	// A slider drag in the options menu applies every frame; log what changed only.
	const FString Summary = FString::Printf(TEXT("%d panel(s) placed, UI scale %.2f, panel opacity %.2f, %s"),
		Moved, AppliedUiScale, AppliedPanelOpacity, Settings.bLocked ? TEXT("locked") : TEXT("unlocked"));
	if (LastLayoutLog != Summary)
	{
		LastLayoutLog = Summary;
		UE_LOG(LogValhallaHUD, Log, TEXT("game HUD: player layout applied (%s)."), *Summary);
	}
	return Moved;
}

void UValhallaGameHUDWidget::ApplyPanelBackgrounds()
{
	const FLinearColor Tint = EffectivePanelTint();
	for (TPair<FName, FPanelState>& Entry : PanelStates)
	{
		for (const TPair<TWeakObjectPtr<UBorder>, FLinearColor>& Background : Entry.Value.Backgrounds)
		{
			if (UBorder* Border = Background.Key.Get())
			{
				const FLinearColor& Designer = Background.Value;
				Border->SetBrushColor(FLinearColor(Designer.R * Tint.R, Designer.G * Tint.G, Designer.B * Tint.B,
					Designer.A * Tint.A * AppliedPanelOpacity));
			}
		}
	}
}

FString UValhallaGameHUDWidget::PanelFrameTextureName(int32 Px)
{
	const int32 Clamped = FMath::Clamp(Px, FMath::RoundToInt(FValhallaUserUISettings::MinPanelBorder), FMath::RoundToInt(FValhallaUserUISettings::MaxPanelBorder));
	return FString::Printf(TEXT("T_UI_PanelFrame_%02d"), Clamped);
}

bool UValhallaGameHUDWidget::IsPanelFrameArtName(FName Name)
{
	const FString Text = Name.ToString();
	return Text == TEXT("T_UI_Panel") || Text.StartsWith(TEXT("T_UI_PanelFrame_"));
}

float UValhallaGameHUDWidget::ResolvePanelBorder(float PlayerSetting, float HudDefault)
{
	const float Chosen = (FMath::IsFinite(PlayerSetting) && PlayerSetting > 0.f) ? PlayerSetting
		: (FMath::IsFinite(HudDefault) && HudDefault > 0.f ? HudDefault : 4.f);
	// Whole pixels: there is one frame texture per thickness.
	return FMath::RoundToFloat(FMath::Clamp(Chosen, FValhallaUserUISettings::MinPanelBorder, FValhallaUserUISettings::MaxPanelBorder));
}

bool UValhallaGameHUDWidget::SetPanelFrameThickness(FSlateBrush& Brush, float Thickness, UTexture2D* FrameTexture)
{
	if (FrameTexture)
	{
		Brush.SetResourceObject(FrameTexture);
	}
	const UTexture2D* Texture = Cast<UTexture2D>(Brush.GetResourceObject());
	if (!Texture)
	{
		return false;
	}
	// Slate draws a box brush's margin at (margin x the texture's pixel size)
	// Slate units, whatever ImageSize says, and a margin past 0.5 (what
	// WBP_GameHUD stored before) is squeezed into whatever room the widget has:
	// thick, doubled trim down a tall panel's sides. So: the frame art made
	// for this thickness, and a margin of exactly `Px` of its texels.
	const FVector2D Size = ValhallaHudArt::TextureSize(Texture);
	const float Px = FMath::Min(FMath::Clamp(Thickness, FValhallaUserUISettings::MinPanelBorder, FValhallaUserUISettings::MaxPanelBorder),
		static_cast<float>(FMath::Min(Size.X, Size.Y)) * 0.45f);
	Brush.DrawAs = ESlateBrushDrawType::Box;
	Brush.Margin = FMargin(Px / Size.X, Px / Size.Y);
	Brush.ImageSize = Size;
	return true;
}

float UValhallaGameHUDWidget::EffectivePanelBorder() const
{
	return ResolvePanelBorder(UserPanelBorder, PanelBorderThickness);
}

int32 UValhallaGameHUDWidget::ApplyPanelBorders()
{
	const float Thickness = EffectivePanelBorder();
	// The art made for this thickness; without it (not imported, no PNG) the
	// panels keep their texture and only the margin changes.
	UTexture2D* FrameArt = FindUiTexture(PanelFrameTextureName(FMath::RoundToInt(Thickness)));
	TSet<const UObject*> KnownArt = { FindUiTexture(TEXT("T_UI_Panel")) };
	for (int32 Px = FMath::RoundToInt(FValhallaUserUISettings::MinPanelBorder); Px <= FMath::RoundToInt(FValhallaUserUISettings::MaxPanelBorder); ++Px)
	{
		KnownArt.Add(FindUiTexture(PanelFrameTextureName(Px)));
	}
	KnownArt.Remove(nullptr);
	int32 Count = 0;
	// The HUD's own tree, then the user widgets in it (the options menu, the
	// bars), which keep their own trees.
	TArray<const UWidgetTree*> Trees = { WidgetTree };
	TSet<const UWidgetTree*> Seen;
	while (Trees.Num() > 0)
	{
		const UWidgetTree* Tree = Trees.Pop(EAllowShrinking::No);
		if (!Tree || Seen.Contains(Tree))
		{
			continue;
		}
		Seen.Add(Tree);
		Tree->ForEachWidget([&](UWidget* Widget)
		{
			if (const UUserWidget* Inner = Cast<UUserWidget>(Widget))
			{
				Trees.Add(Inner->WidgetTree);
				return;
			}
			UBorder* Border = Cast<UBorder>(Widget);
			if (!Border)
			{
				return;
			}
			const UObject* Resource = Border->Background.GetResourceObject();
			if (!Resource || (!KnownArt.Contains(Resource) && !IsPanelFrameArtName(Resource->GetFName())))
			{
				return;
			}
			FSlateBrush Brush = Border->Background;
			if (SetPanelFrameThickness(Brush, Thickness, FrameArt))
			{
				Border->SetBrush(Brush);
				++Count;
			}
		});
	}
	if (Count > 0 && !FMath::IsNearlyEqual(Thickness, AppliedPanelBorderLogged))
	{
		AppliedPanelBorderLogged = Thickness;
		UE_LOG(LogValhallaHUD, Log, TEXT("game HUD: %d framed panel(s) at a %.0f px border."), Count, Thickness);
	}
	return Count;
}

void UValhallaGameHUDWidget::EnforceUserHiddenPanels()
{
	// Edit mode shows hidden panels faintly instead (TickEditMode).
	if (bEditMode)
	{
		return;
	}
	for (TPair<FName, FPanelState>& Entry : PanelStates)
	{
		UWidget* Widget = Entry.Value.Widget.Get();
		if (!Entry.Value.bUserHidden || !Widget)
		{
			continue;
		}
		// Typing shows the chat even when the player hid it idle.
		if (Entry.Key == TEXT("Chat") && bChatOpen)
		{
			if (Widget->GetVisibility() == ESlateVisibility::Collapsed)
			{
				Widget->SetVisibility(ESlateVisibility::Visible);
			}
			continue;
		}
		if (Widget->GetVisibility() != ESlateVisibility::Collapsed)
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UValhallaGameHUDWidget::HandleUserSettingsChanged(const FValhallaUserUISettings& Settings)
{
	if (!bBuilt)
	{
		return; // Rebuild applies them
	}
	ApplyUserLayout(Settings);
	ApplyUserStyle(Settings);
	if (OptionsMenu)
	{
		OptionsMenu->SyncFromSettings(Settings);
	}
}

FLinearColor UValhallaGameHUDWidget::ChatOpenBrush() const
{
	return WithAlpha(ChatOpenBackground, ChatOpenBackground.A * AppliedPanelOpacity);
}

void UValhallaGameHUDWidget::LogPanelGeometry() const
{
	const FVector2D CanvasSize = RootCanvas ? FVector2D(RootCanvas->GetCachedGeometry().GetLocalSize()) : FVector2D::ZeroVector;
	UE_LOG(LogValhallaHUD, Log, TEXT("game HUD panels (canvas %.0f x %.0f, UI scale %.2f):"), CanvasSize.X, CanvasSize.Y, AppliedUiScale);
	for (const FValhallaMovablePanel& Info : GetMovablePanels())
	{
		const FPanelState* State = PanelStates.Find(Info.Key);
		const UWidget* Widget = State ? State->Widget.Get() : nullptr;
		const UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
		if (!CanvasSlot)
		{
			UE_LOG(LogValhallaHUD, Log, TEXT("  %-11s (not on the canvas)"), *Info.Key.ToString());
			continue;
		}
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D TopLeft = RootCanvas ? FVector2D(RootCanvas->GetCachedGeometry().AbsoluteToLocal(Geometry.GetAbsolutePosition())) : FVector2D::ZeroVector;
		const FVector2D Drawn = FVector2D(Geometry.GetAbsoluteSize()) / FMath::Max(0.0001f, RootCanvas ? RootCanvas->GetCachedGeometry().Scale : 1.f);
		const FAnchors Anchors = CanvasSlot->GetAnchors();
		// The member itself when the designer framed it (ActionBarRow in ActionBarFrame, ...).
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Info.Member);
		const UWidget* Member = Property ? Cast<UWidget>(Property->GetObjectPropertyValue_InContainer(this)) : nullptr;
		const FString MemberSize = Member && Member != Widget
			? FString::Printf(TEXT(" (%s %.0fx%.0f)"), *Member->GetName(), Member->GetDesiredSize().X, Member->GetDesiredSize().Y) : FString();
		UE_LOG(LogValhallaHUD, Log, TEXT("  %-11s %-18s anchor (%.2f,%.2f) align (%.2f,%.2f) pos (%.1f,%.1f) desired %.0fx%.0f%s drawn %.0fx%.0f at (%.0f,%.0f) scale %.2f %s%s%s"),
			*Info.Key.ToString(), *Widget->GetName(), Anchors.Minimum.X, Anchors.Minimum.Y,
			CanvasSlot->GetAlignment().X, CanvasSlot->GetAlignment().Y, CanvasSlot->GetPosition().X, CanvasSlot->GetPosition().Y,
			Widget->GetDesiredSize().X, Widget->GetDesiredSize().Y, *MemberSize, Drawn.X, Drawn.Y, TopLeft.X, TopLeft.Y, State->AppliedScale,
			*UEnum::GetValueAsString(Widget->GetVisibility()), State->bUserSet ? TEXT(" [player]") : TEXT(""), State->bUserHidden ? TEXT(" [hidden]") : TEXT(""));
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  Runtime children — what C++ adds to the designer's containers
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaGameHUDWidget::AddActionCells(UPanelWidget* Row)
{
	for (int32 SlotNumber = 1; SlotNumber <= ValhallaActionBarSlots; ++SlotNumber)
	{
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Action, SlotNumber, ActionSlotSize);
		Cell->SetKeyLabel(FString::FromInt(SlotNumber), EffectiveKeyLabelColour());
		if (UHorizontalBoxSlot* CellSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Cell)))
		{
			CellSlot->SetPadding(FMargin(SlotNumber == 1 ? 0.f : ActionSlotGap, 0.f, 0.f, 0.f));
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
	// Each row is a button (name over HP bar): a click targets that member
	// (HandleButton PartySelect). No plate, only a faint wash on hover / press.
	FButtonStyle RowStyle;
	FSlateBrush Clear;
	Clear.DrawAs = ESlateBrushDrawType::NoDrawType;
	RowStyle.SetNormal(Clear);
	RowStyle.SetDisabled(Clear);
	RowStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.08f), 2.f));
	RowStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.16f), 2.f));
	RowStyle.SetNormalPadding(FMargin(0.f));
	RowStyle.SetPressedPadding(FMargin(0.f));
	for (int32 Index = 0; Index < ValhallaPartyMaxMembers; ++Index)
	{
		UValhallaPartyRowButton* Row = WidgetTree->ConstructWidget<UValhallaPartyRowButton>(UValhallaPartyRowButton::StaticClass());
		Row->Action = EValhallaHUDButton::PartySelect;
		Row->Index = Index;
		Row->Hud = this;
		Row->SetStyle(RowStyle);
		Row->SetBackgroundColor(FLinearColor::White);
		Row->OnClicked.AddDynamic(Row, &UValhallaHUDButton::HandleClicked);
		UVerticalBox* RowColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Row->SetContent(RowColumn);
		UTextBlock* Name = MakeText(FString(), 8, EffectiveValueColour(), false);
		Name->SetVisibility(ESlateVisibility::HitTestInvisible);
		RowColumn->AddChildToVerticalBox(Name);
		UValhallaHUDBarWidget* Bar = MakeBar(160.f, 8.f, EffectiveHpHighColour(), FLinearColor::Black, 0.7f, false, 6);
		Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
		RowColumn->AddChildToVerticalBox(Bar);
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
	constexpr int32 LootColumns = 4;
	constexpr int32 LootPool = 12;
	UUniformGridPanel* Uniform = Cast<UUniformGridPanel>(Grid);
	for (int32 Index = 0; Index < LootPool; ++Index)
	{
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Loot, Index, InventorySlotSize);
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
	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Equip, Index, EquipSlotSize);
		Row->AddChildToHorizontalBox(Cell);
		UTextBlock* Label = MakeText(FString(), 7, EffectiveLabelColour());
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
		UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Inventory, Index, InventorySlotSize);
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

	// A left press on the target or party frame (outside its buttons, which
	// handle their own) stops here. Unhandled, it would bubble to the game
	// viewport and become a world click, and a world click on nothing clears
	// the target. Right presses still pass (the camera orbit).
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && IsOverClickEatingPanel(InMouseEvent.GetScreenSpacePosition()))
	{
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

bool UValhallaGameHUDWidget::IsOverClickEatingPanel(const FVector2D& ScreenPosition) const
{
	for (const UWidget* Frame : { static_cast<const UWidget*>(TargetFramePanel.Get()), static_cast<const UWidget*>(PartyPanel.Get()) })
	{
		if (Frame && Frame->IsVisible() && Frame->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return true;
		}
	}
	return false;
}

// ═════════════════════════════════════════════════════════════════════════════
//  World layer — nameplates and floating text, still built in C++ (ui-config `nameplates`)
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
		// B-21 step 5: the player's nameplate font size when set.
		Plate.Name = MakeText(FString(), PxToSlate(EffectiveNameplateFontPx()), NP.Color,
			NP.FontWeight.Equals(TEXT("bold"), ESearchCase::IgnoreCase), NP.StrokeThickness * 0.5f);
		Plate.Name->SetJustification(ETextJustify::Center);
		Column->AddChildToVerticalBox(Plate.Name)->SetHorizontalAlignment(HAlign_Center);
		Plate.Bar = MakeBar(60.f, 4.f, EffectiveHpLowColour(), FLinearColor::Black, 0.8f, false, 6);
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

	AppliedNameplateFontPx = EffectiveNameplateFontPx();

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
	// B-21: last, so a panel the player hid stays hidden whatever the Tick code set.
	EnforceUserHiddenPanels();
	// B-21 step 3: a drag's commit, and (unlocked) the edit overlays and ghosts.
	TickEditMode();

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

	const float HpFrac = PS->MaxHp > 0.f ? PS->Hp / PS->MaxHp : 0.f;
	HpBar->SetFraction(HpFrac);
	// GameScene.ts:2156 — green over half, orange over a quarter, red below.
	HpBar->SetFillColour(HpColourFor(HpFrac));
	HpBar->SetOverlayFraction(PS->MaxHp > 0.f ? PS->ShieldHp / PS->MaxHp : 0.f);
	HpBar->SetLabel(FString::Printf(TEXT("%.0f / %.0f"), PS->Hp, PS->MaxHp));

	const bool bMana = PS->MaxMana > 0.f;
	const bool bEnergy = !bMana && PS->MaxEnergy > 0.f;
	ManaBar->SetVisibility(bMana || bEnergy ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bMana)
	{
		ManaBar->SetFraction(PS->Mana / PS->MaxMana);
		ManaBar->SetFillColour(EffectiveManaColour());
		ManaBar->SetLabel(FString::Printf(TEXT("%.0f / %.0f"), PS->Mana, PS->MaxMana));
	}
	else if (bEnergy)
	{
		ManaBar->SetFraction(PS->Energy / PS->MaxEnergy);
		ManaBar->SetFillColour(EffectiveEnergyColour());
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

		// The painted icon (B-15 Wave 4) when there is one, else the generated
		// tile: category colour with skills.json's two-letter code.
		Cell->SetSkillIcon(SkillCode(*Skill), CategoryColour(Skill->Category), Packed(Skill->IconColor), FindSkillIcon(SkillId));

		const float Remaining = Skills->GetSlotCooldownRemaining(Cell->Index);
		const float Total = FMath::Max(Skill->CooldownMs / 1000.f, Remaining);
		Cell->SetCooldown(Total > 0.f ? Remaining / Total : 0.f,
			FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(Remaining)),
			CooldownColour);

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
		// Nothing casting: no cast bar, except while the HUD is unlocked, when a
		// placeholder bar stands in so the panel can be moved and scaled
		// (TickEditMode ghosts it; SetEditMode(false) collapses it again).
		if (bEditMode)
		{
			CastBar->SetFraction(0.6f);
			CastBar->SetLabel(TEXT("Cast bar"));
			CastBarRoot->SetVisibility(ESlateVisibility::HitTestInvisible);
			return;
		}
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

	// B-06: a friendly NPC is not an enemy, so its name is not red.
	const AValhallaNPC* TargetNpc = Cast<AValhallaNPC>(Target);
	const bool bHostile = UValhallaCombatLibrary::IsNpcTarget(Target) && !(TargetNpc && TargetNpc->bFriendly);
	TargetName->SetText(AsText(FString::Printf(TEXT("%s   Lv %d"), *Info.DisplayName, Info.Level)));
	TargetName->SetColorAndOpacity(FSlateColor(bHostile ? Srgb(0xff, 0x66, 0x66) : Srgb(0x88, 0xcc, 0xff)));

	const float Frac = Info.MaxHp > 0.0 ? static_cast<float>(Info.Hp / Info.MaxHp) : 0.f;
	TargetHpBar->SetFraction(Frac);
	TargetHpBar->SetFillColour(HpColourFor(Frac));
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
		// Visible: the row is a button, a click targets the member.
		PartyRows[Index]->SetVisibility(ESlateVisibility::Visible);
		PartyNames[Index]->SetText(AsText(FString::Printf(TEXT("%s%s%s"),
			Index == 0 ? TEXT("* ") : TEXT(""), *Name,
			Member ? *FString::Printf(TEXT("  Lv %d"), Member->Level) : TEXT(""))));
		const float Frac = Member && Member->MaxHp > 0.f ? Member->Hp / Member->MaxHp : 0.f;
		PartyBars[Index]->SetFraction(Frac);
		PartyBars[Index]->SetFillColour(HpColourFor(Frac));
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
			// B-21: the wall clock for "[hh:mm] " (unknown for lines from before this HUD).
			ChatArrivalClock.Add(ChatSeenCount < 0 ? FDateTime::MinValue() : FDateTime::Now());
		}
		while (ChatArrivalTimes.Num() > PC->GetChatLog().Num())
		{
			ChatArrivalTimes.RemoveAt(0);
		}
		while (ChatArrivalClock.Num() > PC->GetChatLog().Num())
		{
			ChatArrivalClock.RemoveAt(0);
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
	// ui-config chat.maxMessages while typing, chat.visibleLines while idle
	// (B-21: the player's idle line count and font size win when set).
	const int32 Show = FMath::Min(Log.Num(), FMath::Max(0, bChatOpen ? Config.Chat.MaxMessages : EffectiveChatVisibleLines()));
	const int32 FontSize = EffectiveChatFontSize();
	const int32 ClockOffset = Log.Num() - ChatArrivalClock.Num();

	ChatScroll->ClearChildren();
	ChatLineWidgets.Reset();
	for (int32 Index = Log.Num() - Show; Index < Log.Num(); ++Index)
	{
		FString Text = FormatChatLine(Log[Index]);
		const int32 ClockIndex = Index - ClockOffset;
		if (bChatTimestamps && ChatArrivalClock.IsValidIndex(ClockIndex) && ChatArrivalClock[ClockIndex] > FDateTime::MinValue())
		{
			Text = FString::Printf(TEXT("[%02d:%02d] %s"), ChatArrivalClock[ClockIndex].GetHour(), ChatArrivalClock[ClockIndex].GetMinute(), *Text);
		}
		UTextBlock* Line = MakeText(Text, FontSize, ChatColour(Log[Index].Channel), false, 1.f);
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
	case EValhallaChatChannel::General: return StyleColour(TEXT("ChatGeneralColour"), ChatGeneralColour);
	case EValhallaChatChannel::World:   return StyleColour(TEXT("ChatWorldColour"), ChatWorldColour);
	case EValhallaChatChannel::Whisper: return StyleColour(TEXT("ChatWhisperColour"), ChatWhisperColour);
	case EValhallaChatChannel::Party:   return StyleColour(TEXT("ChatPartyColour"), ChatPartyColour);
	default:                            return StyleColour(TEXT("ChatSystemColour"), ChatSystemColour);
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
			EquipLabels[Cell->Index]->SetColorAndOpacity(FSlateColor(EffectiveLabelColour()));
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

namespace
{

	/** A canvas slot pinned to the bottom-left corner by its bottom-left point. */
	bool IsBottomLeft(const UCanvasPanelSlot* CanvasSlot)
	{
		const FAnchors Anchors = CanvasSlot->GetAnchors();
		return Anchors.Minimum.Equals(FVector2D(0.f, 1.f)) && Anchors.Maximum.Equals(FVector2D(0.f, 1.f))
			&& FMath::IsNearlyEqual(CanvasSlot->GetAlignment().Y, 1.f);
	}
}

void UValhallaGameHUDWidget::TickLayout()
{
	// B-07 step 4: the code layout's chat fix-up, on WBP_GameHUD's chat. Only a
	// chat laid out the way WBP_GameHUD has it (bottom-left anchored, a Size Box
	// inside ChatPanel) is moved; anything else is left as the designer put it.
	if (!bLayoutFromBlueprint || !bChatDesignKnown || !RootCanvas || !ChatPanel || !ChatSizer || !ActionBarRoot)
	{
		return;
	}
	// B-21: the player placed the chat; leave it where they put it.
	const FPanelState* ChatState = PanelStates.Find(TEXT("Chat"));
	if ((ChatState && ChatState->bUserSet) || DragKey == TEXT("Chat") || PendingCommitKey == TEXT("Chat"))
	{
		return;
	}
	auto AppliedScaleOf = [this](const TCHAR* Key)
	{
		const FPanelState* State = PanelStates.Find(Key);
		return State ? State->AppliedScale : 1.f;
	};
	UCanvasPanelSlot* ChatSlot = Cast<UCanvasPanelSlot>(ChatPanel->Slot);
	const UWidget* ActionBarPanel = CanvasChildOf(ActionBarRoot, RootCanvas);
	UWidget* VitalsBlock = CanvasChildOf(VitalsPanel, RootCanvas);
	const UCanvasPanelSlot* VitalsSlot = VitalsBlock ? Cast<UCanvasPanelSlot>(VitalsBlock->Slot) : nullptr;
	if (!ChatSlot || !ActionBarPanel || !VitalsSlot || !IsBottomLeft(ChatSlot) || !IsBottomLeft(VitalsSlot) || ChatDesignWidth <= 0.f)
	{
		return;
	}

	const float Width = RootCanvas->GetCachedGeometry().GetLocalSize().X;
	// Desired sizes are before the render scale (UiScale x the panel's) the canvas draws them at.
	const float ChatScale = FMath::Max(0.01f, AppliedScaleOf(TEXT("Chat")));
	const float BarWidth = ActionBarPanel->GetDesiredSize().X * AppliedScaleOf(TEXT("ActionBar"));
	const float VitalsHeight = VitalsBlock->GetDesiredSize().Y * AppliedScaleOf(TEXT("Vitals"));
	if (Width <= 0.f || BarWidth <= 0.f || VitalsHeight <= 0.f || FMath::IsNearlyEqual(Width, LayoutForWidth, 0.5f))
	{
		return;
	}
	LayoutForWidth = Width;

	constexpr float Gap = 8.f;
	constexpr float MinReadableWidth = 220.f;

	// Where the designer put it (beside the vitals), as wide as the gap to the
	// centred action bar allows.
	const float LeftX = ChatDesignPosition.X;
	const float Room = (Width - BarWidth) * 0.5f - LeftX - Gap;
	if (Room >= MinReadableWidth)
	{
		ChatSizer->SetWidthOverride(FMath::Min(ChatDesignWidth, Room / ChatScale));
		ChatSlot->SetPosition(ChatDesignPosition);
		return;
	}

	// Too narrow: stacked above the vitals block, as wide as half the screen.
	const FVector2D VitalsPosition = VitalsSlot->GetPosition();
	const float VitalsTop = VitalsHeight - VitalsPosition.Y; // px above the bottom edge
	ChatSizer->SetWidthOverride(FMath::Min(ChatDesignWidth, FMath::Max(MinReadableWidth, Width * 0.5f - VitalsPosition.X - Gap) / ChatScale));
	ChatSlot->SetPosition(FVector2D(VitalsPosition.X, -(VitalsTop + Gap)));
	UE_LOG(LogValhallaHUD, Verbose, TEXT("chat stacked above the vitals (%.0f units wide, action bar %.0f)."), Width, BarWidth);
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

	// B-06: a friendly NPC (template `type: "npc"`) gets a green name and bar.
	const FLinearColor FriendlyNameColour = Srgb(0x8c, 0xe6, 0x8c);

	auto ShowPlate = [&](const AActor* Actor, const FString& Name, float Hp, float MaxHp, bool bHostile, const FLinearColor& NameColour)
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
		Plate.Name->SetColorAndOpacity(FSlateColor(NameColour));
		const float Frac = MaxHp > 0.f ? Hp / MaxHp : 0.f;
		Plate.Bar->SetFraction(Frac);
		Plate.Bar->SetFillColour(bHostile ? EffectiveHpLowColour() : EffectiveHpHighColour());
		if (UCanvasPanelSlot* PlateSlot = Cast<UCanvasPanelSlot>(Plate.Root->Slot))
		{
			PlateSlot->SetPosition(Screen + FVector2D(0.f, ScreenOffset));
		}
		Plate.Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	};

	// B-21 step 5: the player's nameplate switches.
	for (TActorIterator<AValhallaNPC> It(World); It && bShowNpcNameplates; ++It)
	{
		// B-06: an NPC hidden by the zone's vision fog has no plate either.
		if (It->IsAlive() && !It->IsHidden())
		{
			ShowPlate(*It, It->DisplayName, It->Hp, It->MaxHp, !It->bFriendly,
				It->bFriendly ? FriendlyNameColour : Config.Nameplates.Color);
		}
	}
	for (TActorIterator<AValhallaCharacter> It(World); It && bShowPlayerNameplates; ++It)
	{
		const AValhallaPlayerState* Other = It->GetValhallaPlayerState();
		if (*It != OwnPawn && Other && Other->IsAlive() && !It->IsHidden())
		{
			ShowPlate(*It, Other->CharacterName, Other->Hp, Other->MaxHp, false, Config.Nameplates.Color);
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
			// Kevin (2026-09-25): no "You cast <skill>" line. It came after the
			// effect's own line ("... healed ... for N", the hit, the buff) and
			// said nothing new; "You begin casting" still marks a cast-time
			// start. The event itself still drives the VFX, the cooldown mirror
			// and the floaters. This branch also keeps a party member's own casts
			// out of the "X casts Y" line below (PartyMemberNames includes them).
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
	return SetLogFilter(Key, !*bOn);
}

bool UValhallaGameHUDWidget::SetLogFilter(FName Key, bool bOnNow)
{
	bool* bOn = LogFilters.Find(Key);
	if (!bOn)
	{
		UE_LOG(LogValhallaHUD, Warning, TEXT("combat log: no filter '%s'."), *Key.ToString());
		return false;
	}
	*bOn = bOnNow;
	bCombatLogDirty = true;
	UE_LOG(LogValhallaHUD, Log, TEXT("combat log filter %s -> %s"), *Key.ToString(), bOnNow ? TEXT("on") : TEXT("off"));
	// B-21 step 5: saved with the player's settings (restored at the next login).
	if (UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this))
	{
		UserSettings->Mutate([Key, bOnNow](FValhallaUserUISettings& S) { S.LogFilters.Add(Key, bOnNow); });
	}
	return true;
}

bool UValhallaGameHUDWidget::IsLogFilterOn(FName Key) const
{
	const bool* bOn = LogFilters.Find(Key);
	return !bOn || *bOn;
}

FString UValhallaGameHUDWidget::GetLogFilterLabel(FName Key)
{
	for (const FFilterInfo& Info : FilterInfos())
	{
		if (Key == FName(Info.Key))
		{
			return Info.Label;
		}
	}
	return Key.ToString();
}

void UValhallaGameHUDWidget::SpawnFloater(const FValhallaCombatEvent& Event)
{
	// B-21 step 5: the player turned floating combat text off.
	if (!bShowFloatingText)
	{
		return;
	}
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

		for (const FName SkillId : Data->GetClassSkills(PS->ClassId))
		{
			const FValhallaSkillTemplate* Skill = Data->FindSkill(SkillId);
			if (!Skill)
			{
				continue;
			}
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UValhallaHUDSlotWidget* Cell = MakeCell(EValhallaHUDSlotKind::Skill, INDEX_NONE, SkillSlotSize);
			Cell->bFilled = true;
			Cell->Id = SkillId;
			Cell->SetSkillIcon(SkillCode(*Skill), CategoryColour(Skill->Category), Packed(Skill->IconColor), FindSkillIcon(SkillId));
			const bool bLocked = PS->Level < Skill->LevelRequired;
			Cell->SetDimmed(bLocked);
			Row->AddChildToHorizontalBox(Cell);

			UVerticalBox* Text = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			Text->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s%s"), *Skill->Name,
				bLocked ? *FString::Printf(TEXT("   (Lv %d)"), Skill->LevelRequired) : TEXT("")), 8,
				bLocked ? EffectiveLabelColour() : EffectiveValueColour(), true));
			const TCHAR* Resource = Skill->ResourceType == EValhallaResourceType::Mana ? TEXT("mana")
				: (Skill->ResourceType == EValhallaResourceType::Energy ? TEXT("energy") : TEXT(""));
			Text->AddChildToVerticalBox(MakeText(FString::Printf(TEXT("%s%s  cast %.1fs  cd %.0fs"),
				Skill->ResourceCost > 0.f ? *FString::Printf(TEXT("%.0f %s"), Skill->ResourceCost, Resource) : TEXT("free"), TEXT(""),
				Skill->CastTimeMs / 1000.f, Skill->CooldownMs / 1000.f), 7, EffectiveLabelColour()));
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
		? FString(TEXT("Drag a skill to the action bar, or click it and then click a slot. Drag a skill off the bar to remove it."))
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
		// A skill not unlocked yet cannot go on the bar.
		if (ArmedSkill != Cell->Id && Skills && !Skills->CanPlaceOnActionBar(Cell->Id))
		{
			ExplainSkillNotPlaceable(Cell->Id);
			break;
		}
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
	else if (Source->Kind == K::Skill && Target->Kind == K::Action && Skills && !Skills->CanPlaceOnActionBar(Source->Id))
	{
		// Not unlocked yet (the pane shows it dimmed with its level): nothing changes.
		UE_LOG(LogValhallaHUD, Log, TEXT("drag: %s is not unlocked; the bar is unchanged."), *Source->Id.ToString());
		ExplainSkillNotPlaceable(Source->Id);
	}
	else if (Source->Kind == K::Action && Target->Kind != K::Action)
	{
		// Off the bar onto some other cell (the skills pane, the inventory): removed.
		HandleSlotDraggedOff(Source);
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

void UValhallaGameHUDWidget::HandleSlotDraggedOff(UValhallaHUDSlotWidget* Source)
{
	VALHALLA_HUD_PIE_SCOPE(this);
	AValhallaCharacter* Pawn = GetValhallaPawn();
	UValhallaSkillComponent* Skills = Pawn ? Pawn->GetSkillComponent() : nullptr;
	if (!Source || Source->Kind != EValhallaHUDSlotKind::Action || !Skills)
	{
		return;
	}
	UE_LOG(LogValhallaHUD, Log, TEXT("action bar: %s dragged off slot %d"), *Source->Id.ToString(), Source->Index);
	Skills->ServerSetActionBar(Source->Index, NAME_None);
	HideTooltip();
}

bool UValhallaGameHUDWidget::IsOverActionBar(const FVector2D& ScreenPosition) const
{
	return ActionBarRoot && ActionBarRoot->GetCachedGeometry().IsUnderLocation(ScreenPosition);
}

void UValhallaGameHUDWidget::ExplainSkillNotPlaceable(FName SkillId)
{
	AValhallaPlayerController* PC = GetValhallaController();
	const AValhallaPlayerState* PS = GetValhallaPlayerState();
	const UValhallaDataSubsystem* Data = GetData();
	const FValhallaSkillTemplate* Skill = Data ? Data->FindSkill(SkillId) : nullptr;
	if (!PC)
	{
		return;
	}
	const FString SkillName = Skill ? Skill->Name : SkillId.ToString();
	PC->ShowLocalSystemMessage((Skill && PS && PS->Level < Skill->LevelRequired)
		? FString::Printf(TEXT("You must be level %d to use %s."), Skill->LevelRequired, *SkillName)
		: FString::Printf(TEXT("You cannot use %s."), *SkillName));
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
	case EValhallaHUDButton::Options:
		ToggleOptions();
		break;
	case EValhallaHUDButton::PartySelect:
		// The server resolves the name to the member's pawn (it may not be
		// relevant to this client) and leaves the target alone when it cannot.
		if (const AValhallaPlayerState* PS = GetValhallaPlayerState(); PS && PS->PartyMemberNames.IsValidIndex(Index))
		{
			UE_LOG(LogValhallaHUD, Log, TEXT("party row %d clicked: target %s"), Index, *PS->PartyMemberNames[Index]);
			PC->ServerSetTargetByName(PS->PartyMemberNames[Index]);
		}
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
	// B-21 step 4: the options menu first (its colour editor before the menu).
	if (IsOptionsOpen())
	{
		if (!OptionsMenu->CloseSubPanel())
		{
			CloseOptions();
		}
		return true;
	}
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
				// The 1.0 icons are 32 px pixel art; the Wave 4 renders are 128 px.
				Texture->Filter = Texture->GetSizeX() <= 32 ? TF_Nearest : TF_Bilinear;
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

UTexture2D* UValhallaGameHUDWidget::FindSkillIcon(FName SkillId)
{
	return SkillId.IsNone() ? nullptr : LoadUiTexture(TEXT("Icons/Skills"), TEXT("Icons/Skills"), SkillId.ToString(), false);
}

UTexture2D* UValhallaGameHUDWidget::FindUiTexture(const FString& Name)
{
	return LoadUiTexture(TEXT("Frames"), TEXT("Frames"), Name, false);
}

UTexture2D* UValhallaGameHUDWidget::LoadUiTexture(const FString& AssetFolder, const FString& DiskFolder, const FString& Name, bool bPixelArtFilter)
{
	const FString Key = AssetFolder + TEXT("/") + Name;
	if (TObjectPtr<UTexture2D>* Cached = UiTextureCache.Find(Key))
	{
		return *Cached;
	}

	// The imported copy first (import_ui_icons.py), then the PNG on disk, as
	// FindItemIcon does, so new art shows in the editor before an import.
	const FString AssetPath = FString::Printf(TEXT("/Game/Valhalla/UI/%s/%s.%s"), *AssetFolder, *Name, *Name);
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Texture)
	{
		const FString DiskPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			UValhallaDataSettings::Get()->GetResolvedDataRoot(), TEXT("../../Import/UI"), DiskFolder, Name + TEXT(".png")));
		if (FPaths::FileExists(DiskPath))
		{
			Texture = FImageUtils::ImportFileAsTexture2D(DiskPath);
			if (Texture)
			{
				Texture->Filter = bPixelArtFilter ? TF_Nearest : TF_Bilinear;
				Texture->UpdateResource();
			}
		}
	}
	UiTextureCache.Add(Key, Texture);
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
		// SetInputMode only queues the viewport focus; it is applied on the
		// viewport's next input event, and a keyboard event can't reach the
		// viewport until it has focus. Hand focus back now so Enter reopens chat.
		// Use this player's own viewport: Slate's "game viewport" is whichever
		// was registered last, which in a two-client PIE is the other window.
		UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> ViewportWidget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;
		if (ViewportWidget.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetKeyboardFocus(ViewportWidget, EFocusCause::SetDirectly);
		}
	}
}

void UValhallaGameHUDWidget::OpenChat(const FString& Prefill)
{
	// No layout (the bare C++ class): the box is a hidden stand-in, and UI-only
	// input focused on it would leave the player typing into nothing.
	if (!ChatInput || !bLayoutFromBlueprint)
	{
		return;
	}
	bChatOpen = true;
	ChatPanel->SetBrushColor(ChatOpenBrush());
	ChatPanel->SetVisibility(ESlateVisibility::Visible);
	// Typing, the box jumps to its full height (the designer Size Box's Max
	// Desired Height) rather than growing line by line; idle, it fits its lines.
	if (ChatSizer && ChatOpenHeight > 0.f)
	{
		ChatSizer->SetHeightOverride(ChatOpenHeight);
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
	ChatPanel->SetBrushColor(WithAlpha(ChatOpenBackground, 0.f));
	ChatPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (ChatSizer && ChatOpenHeight > 0.f)
	{
		ChatSizer->ClearHeightOverride();
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
	case EValhallaChatAction::Emote:        PC->ServerEmote(FName(*Parsed.Text)); break;
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
	// B-21: a slider or check box in the options menu has keyboard focus, so
	// Escape reaches the HUD before the player controller's binding.
	if (InKeyEvent.GetKey() == EKeys::Escape && IsOptionsOpen() && !bChatOpen)
	{
		CloseTopmost();
		return FReply::Handled();
	}
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

// ═════════════════════════════════════════════════════════════════════════════
//  B-21 step 5 — the player's style (colours, chat, nameplates)
// ═════════════════════════════════════════════════════════════════════════════

const TArray<FValhallaStyleColourKey>& UValhallaGameHUDWidget::GetStyleColourKeys()
{
	// Each key is a "Valhalla|HUD Style" FLinearColor property (and the
	// FValhallaUserUISettings::Colours key); the options menu lists them in
	// this order.
	static const TArray<FValhallaStyleColourKey> Keys = {
		{ TEXT("HpHighColour"),      TEXT("HP, over half") },
		{ TEXT("HpMidColour"),       TEXT("HP, over a quarter") },
		{ TEXT("HpLowColour"),       TEXT("HP, low (and hostile nameplates)") },
		{ TEXT("ManaColour"),        TEXT("Mana") },
		{ TEXT("EnergyColour"),      TEXT("Energy") },
		{ TEXT("CastBarColour"),     TEXT("Cast bar") },
		{ TEXT("HighlightColour"),   TEXT("Highlight (selected cells, edit outlines)") },
		{ TEXT("KeyLabelColour"),    TEXT("Action bar key labels") },
		{ TEXT("LabelColour"),       TEXT("Labels") },
		{ TEXT("ValueColour"),       TEXT("Names and values") },
		{ TEXT("ChatGeneralColour"), TEXT("Chat: general") },
		{ TEXT("ChatWorldColour"),   TEXT("Chat: world") },
		{ TEXT("ChatWhisperColour"), TEXT("Chat: whisper") },
		{ TEXT("ChatPartyColour"),   TEXT("Chat: party") },
		{ TEXT("ChatSystemColour"),  TEXT("Chat: system") },
		{ TEXT("PanelTintColour"),   TEXT("Panel tint") },
	};
	return Keys;
}

const FValhallaStyleColourKey* UValhallaGameHUDWidget::FindStyleColourKey(FName Key)
{
	return GetStyleColourKeys().FindByPredicate([Key](const FValhallaStyleColourKey& Info) { return Info.Key == Key; });
}

FLinearColor UValhallaGameHUDWidget::GetDefaultColour(FName Key) const
{
	// The property itself: it is never written at runtime, so it is the
	// class's default (WBP_GameHUD's Class Defaults).
	const FStructProperty* Property = FindFProperty<FStructProperty>(GetClass(), Key);
	if (Property && Property->Struct == TBaseStructure<FLinearColor>::Get())
	{
		return *Property->ContainerPtrToValuePtr<FLinearColor>(this);
	}
	return FLinearColor(1.f, 0.f, 1.f, 1.f);
}

FLinearColor UValhallaGameHUDWidget::GetEffectiveColour(FName Key) const
{
	const FLinearColor* Override = ColourOverrides.Find(Key);
	return Override ? *Override : GetDefaultColour(Key);
}

FLinearColor UValhallaGameHUDWidget::StyleColour(FName Key, const FLinearColor& Default) const
{
	const FLinearColor* Override = ColourOverrides.Find(Key);
	return Override ? *Override : Default;
}

FLinearColor UValhallaGameHUDWidget::EffectiveHpHighColour() const    { return StyleColour(TEXT("HpHighColour"), HpHighColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveHpMidColour() const     { return StyleColour(TEXT("HpMidColour"), HpMidColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveHpLowColour() const     { return StyleColour(TEXT("HpLowColour"), HpLowColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveManaColour() const      { return StyleColour(TEXT("ManaColour"), ManaColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveEnergyColour() const    { return StyleColour(TEXT("EnergyColour"), EnergyColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveCastBarColour() const   { return StyleColour(TEXT("CastBarColour"), CastBarColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveHighlightColour() const { return StyleColour(TEXT("HighlightColour"), HighlightColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveKeyLabelColour() const  { return StyleColour(TEXT("KeyLabelColour"), KeyLabelColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveLabelColour() const     { return StyleColour(TEXT("LabelColour"), LabelColour); }
FLinearColor UValhallaGameHUDWidget::EffectiveValueColour() const     { return StyleColour(TEXT("ValueColour"), ValueColour); }
FLinearColor UValhallaGameHUDWidget::EffectivePanelTint() const       { return StyleColour(TEXT("PanelTintColour"), PanelTintColour); }

FLinearColor UValhallaGameHUDWidget::HpColourFor(float Fraction) const
{
	// GameScene.ts:2156 — green over half, orange over a quarter, red below.
	return Fraction > 0.5f ? EffectiveHpHighColour() : (Fraction > 0.25f ? EffectiveHpMidColour() : EffectiveHpLowColour());
}

int32 UValhallaGameHUDWidget::EffectiveChatFontSize() const
{
	return UserChatFontSize > 0 ? FMath::Clamp(UserChatFontSize, 6, 24) : ChatFontSize;
}

int32 UValhallaGameHUDWidget::EffectiveChatVisibleLines() const
{
	return UserChatVisibleLines > 0 ? UserChatVisibleLines : Config.Chat.VisibleLines;
}

int32 UValhallaGameHUDWidget::EffectiveNameplateFontPx() const
{
	return UserNameplateFontSize > 0 ? UserNameplateFontSize : Config.Nameplates.FontSize;
}

void UValhallaGameHUDWidget::RestyleNameplates()
{
	const int32 Px = EffectiveNameplateFontPx();
	if (Px == AppliedNameplateFontPx)
	{
		return;
	}
	AppliedNameplateFontPx = Px;
	const FValhallaUIConfig::FNameplates& NP = Config.Nameplates;
	for (FPlate& Plate : Plates)
	{
		if (Plate.Name)
		{
			Plate.Name->SetFont(HudFont(PxToSlate(Px), NP.FontWeight.Equals(TEXT("bold"), ESearchCase::IgnoreCase), NP.StrokeThickness * 0.5f));
		}
	}
}

void UValhallaGameHUDWidget::ApplyUserStyle(const FValhallaUserUISettings& Settings)
{
	// Record what is in force: the Effective* getters read these.
	ColourOverrides.Reset();
	for (const FValhallaStyleColourKey& Info : GetStyleColourKeys())
	{
		if (const FLinearColor* Colour = Settings.Colours.Find(Info.Key))
		{
			ColourOverrides.Add(Info.Key, *Colour);
		}
	}
	UserChatFontSize = Settings.ChatFontSize;
	UserChatVisibleLines = Settings.ChatVisibleLines;
	bChatTimestamps = Settings.bChatTimestamps;
	bShowNpcNameplates = Settings.bShowNpcNameplates;
	bShowPlayerNameplates = Settings.bShowPlayerNameplates;
	bShowFloatingText = Settings.bFloatingCombatText;
	UserNameplateFontSize = Settings.NameplateFontSize;
	UserPanelBorder = Settings.PanelBorder;

	// The combat log's filters: what the player saved (a key not saved is shown).
	for (const FName Key : GetLogFilterKeys())
	{
		const bool* bSaved = Settings.LogFilters.Find(Key);
		const bool bOn = !bSaved || *bSaved;
		bool& Current = LogFilters.FindOrAdd(Key);
		if (Current != bOn)
		{
			Current = bOn;
			bCombatLogDirty = true;
		}
	}

	if (!RootCanvas)
	{
		return;
	}

	// Live widgets. Bars and nameplates take their colours on the next tick.
	const FLinearColor Highlight = EffectiveHighlightColour();
	const FLinearColor KeyLabel = EffectiveKeyLabelColour();
	for (UValhallaHUDSlotWidget* Cell : ActionCells)
	{
		Cell->SetHighlightColour(Highlight);
		Cell->SetKeyLabel(FString::FromInt(Cell->Index), KeyLabel);
	}
	for (const TArray<TObjectPtr<UValhallaHUDSlotWidget>>* Cells : { &InventoryCells, &EquipCells, &LootCells })
	{
		for (UValhallaHUDSlotWidget* Cell : *Cells)
		{
			Cell->SetHighlightColour(Highlight);
		}
	}
	SkillsBuiltForClass = NAME_None; // the skills pane's rows are remade with the new colours
	if (CastBar)
	{
		CastBar->SetFillColour(EffectiveCastBarColour());
	}
	for (UTextBlock* Name : PartyNames)
	{
		Name->SetColorAndOpacity(FSlateColor(EffectiveValueColour()));
	}
	ApplyPanelBackgrounds();
	ApplyPanelBorders();
	for (const TPair<FName, TObjectPtr<UValhallaPanelEditOverlay>>& Entry : EditOverlays)
	{
		if (Entry.Value)
		{
			Entry.Value->SetHighlight(Highlight);
		}
	}

	// The chat is redrawn only when something it shows changed.
	const FString ChatStyle = FString::Printf(TEXT("%d|%d|%d|%s|%s|%s|%s|%s"), EffectiveChatFontSize(), EffectiveChatVisibleLines(), bChatTimestamps ? 1 : 0,
		*FValhallaUserUISettings::ColourToHex(ChatColour(EValhallaChatChannel::General)), *FValhallaUserUISettings::ColourToHex(ChatColour(EValhallaChatChannel::World)),
		*FValhallaUserUISettings::ColourToHex(ChatColour(EValhallaChatChannel::Whisper)), *FValhallaUserUISettings::ColourToHex(ChatColour(EValhallaChatChannel::Party)),
		*FValhallaUserUISettings::ColourToHex(ChatColour(EValhallaChatChannel::System)));
	if (ChatStyle != AppliedChatStyle)
	{
		AppliedChatStyle = ChatStyle;
		if (bBuilt)
		{
			RefreshChatLines();
		}
		if (bChatOpen && ChatChannelText)
		{
			ChatChannelText->SetColorAndOpacity(FSlateColor(ChatColour(ChatChannel)));
		}
	}

	RestyleNameplates();
	if (!bShowFloatingText)
	{
		for (FFloater& Floater : Floaters)
		{
			Floater.StartedAt = -1.0;
			if (Floater.Text)
			{
				Floater.Text->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  B-21 step 3 — edit mode: the overlays, drag, resize, re-anchor
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaPanelEditOverlay::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureTree();
}

void UValhallaPanelEditOverlay::EnsureTree()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("EditOverlayTree"));
	}
	if (bTreeReady)
	{
		return;
	}
	bTreeReady = true;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	WidgetTree->RootWidget = Root;
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// The body: a faint outline over the whole panel. Visible = it takes the press.
	Outline = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Outline->SetVisibility(ESlateVisibility::Visible);
	UOverlaySlot* BodySlot = Root->AddChildToOverlay(Outline);
	BodySlot->SetHorizontalAlignment(HAlign_Fill);
	BodySlot->SetVerticalAlignment(VAlign_Fill);

	NameLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	NameLabel->SetFont(HudFont(7, true, 1.f));
	NameLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
	UOverlaySlot* LabelSlot = Root->AddChildToOverlay(NameLabel);
	LabelSlot->SetHorizontalAlignment(HAlign_Left);
	LabelSlot->SetVerticalAlignment(VAlign_Top);
	LabelSlot->SetPadding(FMargin(4.f, 2.f));

	// The grip, bottom right: always takes the press (resize).
	USizeBox* GripBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	GripBox->SetWidthOverride(GripSize);
	GripBox->SetHeightOverride(GripSize);
	GripBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Grip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Grip->SetVisibility(ESlateVisibility::Visible);
	Grip->SetToolTipText(AsText(TEXT("Drag to resize")));
	GripBox->SetContent(Grip);
	UOverlaySlot* GripSlot = Root->AddChildToOverlay(GripBox);
	GripSlot->SetHorizontalAlignment(HAlign_Right);
	GripSlot->SetVerticalAlignment(VAlign_Bottom);

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UValhallaPanelEditOverlay::Setup(FName InKey, const FLinearColor& Highlight)
{
	EnsureTree();
	PanelKey = InKey;
	NameLabel->SetText(AsText(InKey.ToString()));
	SetHighlight(Highlight);
}

void UValhallaPanelEditOverlay::SetHighlight(const FLinearColor& Highlight)
{
	EnsureTree();
	// The Highlight colour at half alpha round a barely-there wash.
	Outline->SetBrush(FSlateRoundedBoxBrush(WithAlpha(Highlight, 0.06f), 3.f, WithAlpha(Highlight, 0.5f), 1.5f));
	Outline->SetBrushColor(FLinearColor::White);
	Grip->SetBrush(FSlateRoundedBoxBrush(WithAlpha(Highlight, 0.85f), 2.f, FLinearColor(0.f, 0.f, 0.f, 0.8f), 1.f));
	Grip->SetBrushColor(FLinearColor::White);
	NameLabel->SetColorAndOpacity(FSlateColor(WithAlpha(Highlight, 0.8f)));
}

void UValhallaPanelEditOverlay::SetBodyHitTestable(bool bHitTestable)
{
	bBodyHitTestable = bHitTestable;
	if (Outline)
	{
		Outline->SetVisibility(bHitTestable ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	}
}

FReply UValhallaPanelEditOverlay::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	UValhallaGameHUDWidget* Owner = Hud.Get();
	if (Owner && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// The grip's hit area is a few px larger than it draws.
		const FVector2D Local = FVector2D(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
		const FVector2D Size = FVector2D(InGeometry.GetLocalSize());
		const bool bGrip = Local.X >= Size.X - GripSize - 4.f && Local.Y >= Size.Y - GripSize - 4.f;
		if (Owner->BeginPanelDrag(PanelKey, Owner->AbsoluteToCanvas(InMouseEvent.GetScreenSpacePosition()), bGrip))
		{
			bDragging = true;
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
		return FReply::Handled();
	}
	// Right click falls through to the HUD (the combat log's filter menu).
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UValhallaPanelEditOverlay::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	UValhallaGameHUDWidget* Owner = Hud.Get();
	if (bDragging && Owner && HasMouseCapture())
	{
		Owner->UpdatePanelDrag(Owner->AbsoluteToCanvas(InMouseEvent.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UValhallaPanelEditOverlay::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		if (UValhallaGameHUDWidget* Owner = Hud.Get())
		{
			Owner->UpdatePanelDrag(Owner->AbsoluteToCanvas(InMouseEvent.GetScreenSpacePosition()));
			Owner->EndPanelDrag(true);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UValhallaPanelEditOverlay::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// The panel's scroll box is under the overlay; pass the wheel on.
	if (UValhallaGameHUDWidget* Owner = Hud.Get())
	{
		Owner->ScrollPanelAt(PanelKey, InMouseEvent.GetScreenSpacePosition(), InMouseEvent.GetWheelDelta());
	}
	return FReply::Handled();
}

void UValhallaPanelEditOverlay::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
	if (bDragging)
	{
		// Lost mid-drag (alt-tab, the window closing): keep where it got to.
		bDragging = false;
		if (UValhallaGameHUDWidget* Owner = Hud.Get())
		{
			Owner->EndPanelDrag(true);
		}
	}
}

FVector2D UValhallaGameHUDWidget::ChooseAnchor(const FSlateRect& Rect, const FVector2D& Viewport)
{
	// Per axis: the near edge (0) when the panel's near edge is closest to it,
	// the far edge (1) likewise, the centre (0.5) when the panel's centre is
	// closest to the screen's centre (the tie goes to the centre).
	auto Axis = [](double Low, double High, double Extent) -> double
	{
		const double ToNear = FMath::Abs(Low);
		const double ToFar = FMath::Abs(Extent - High);
		const double ToCentre = FMath::Abs((Low + High) * 0.5 - Extent * 0.5);
		if (ToCentre <= ToNear && ToCentre <= ToFar)
		{
			return 0.5;
		}
		return ToNear <= ToFar ? 0.0 : 1.0;
	};
	return FVector2D(Axis(Rect.Left, Rect.Right, Viewport.X), Axis(Rect.Top, Rect.Bottom, Viewport.Y));
}

FVector2D UValhallaGameHUDWidget::SnapPanelPosition(const FVector2D& TopLeft, const FVector2D& Size, const FVector2D& Viewport, float Grid, float EdgeSnap)
{
	auto Axis = [Grid, EdgeSnap](double Pos, double Extent, double Canvas) -> double
	{
		double Out = Grid > 0.f ? FMath::RoundToDouble(Pos / Grid) * Grid : Pos;
		// The edges win over the grid.
		if (FMath::Abs(Pos) <= EdgeSnap)
		{
			Out = 0.0;
		}
		else if (FMath::Abs(Canvas - (Pos + Extent)) <= EdgeSnap)
		{
			Out = Canvas - Extent;
		}
		// Never off the canvas (a panel wider than it keeps its near edge visible... or its far one).
		return Extent <= Canvas ? FMath::Clamp(Out, 0.0, Canvas - Extent) : FMath::Clamp(Out, Canvas - Extent, 0.0);
	};
	return FVector2D(Axis(TopLeft.X, Size.X, Viewport.X), Axis(TopLeft.Y, Size.Y, Viewport.Y));
}

FValhallaPanelLayout UValhallaGameHUDWidget::AnchorLayoutForRect(const FSlateRect& Rect, const FVector2D& Viewport, float UiScale)
{
	const FVector2D Anchor = ChooseAnchor(Rect, Viewport);
	const FVector2D TopLeft(Rect.Left, Rect.Top);
	const FVector2D Size = FVector2D(Rect.GetSize());
	const float Scale = FMath::IsFinite(UiScale) ? FMath::Max(0.01f, UiScale) : 1.f;

	FValhallaPanelLayout Out;
	Out.AnchorMin = Out.AnchorMax = Anchor;
	Out.Alignment = Anchor;
	// The drawn rectangle's alignment point sits at anchor x canvas + slot
	// position, whatever the render scale about that point; the slot position
	// is Position x UiScale (ResolvePanelLayout).
	Out.Position = (TopLeft + Anchor * Size - Anchor * Viewport) / Scale;
	Out.Scale = 1.f;
	Out.bVisible = true;
	Out.bSet = true;
	return Out;
}

bool UValhallaGameHUDWidget::IsInteractiveChild(const UWidget* Widget)
{
	// Cells and scroll boxes are not: in edit mode a drag on them moves the
	// panel (they fill the inventory, chat, log and skills panels, which would
	// otherwise have almost nothing to grab).
	return Widget && (Widget->IsA<UButton>() || Widget->IsA<UCheckBox>() || Widget->IsA<USlider>() || Widget->IsA<USpinBox>()
		|| Widget->IsA<UEditableTextBox>() || Widget->IsA<UEditableText>() || Widget->IsA<UMultiLineEditableTextBox>()
		|| Widget->IsA<UMultiLineEditableText>() || Widget->IsA<UComboBoxString>());
}

FVector2D UValhallaGameHUDWidget::AbsoluteToCanvas(const FVector2D& Absolute) const
{
	return RootCanvas ? FVector2D(RootCanvas->GetCachedGeometry().AbsoluteToLocal(Absolute)) : Absolute;
}

FVector2D UValhallaGameHUDWidget::GetCanvasSize() const
{
	return RootCanvas ? FVector2D(RootCanvas->GetCachedGeometry().GetLocalSize()) : FVector2D::ZeroVector;
}

FSlateRect UValhallaGameHUDWidget::GetPanelCanvasRect(FName Key) const
{
	const FPanelState* State = PanelStates.Find(Key);
	const UWidget* Widget = State ? State->Widget.Get() : nullptr;
	if (!Widget || !RootCanvas)
	{
		return FSlateRect(0.f, 0.f, 0.f, 0.f);
	}
	const FGeometry& Geometry = Widget->GetCachedGeometry();
	const FGeometry& CanvasGeometry = RootCanvas->GetCachedGeometry();
	const FVector2D Size = FVector2D(Geometry.GetLocalSize());
	if (Size.X < 1.0 || Size.Y < 1.0)
	{
		return FSlateRect(0.f, 0.f, 0.f, 0.f);
	}
	// Both corners through the accumulated render transform (UiScale x the panel's scale).
	const FVector2D A = FVector2D(CanvasGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector)));
	const FVector2D B = FVector2D(CanvasGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(Size)));
	return FSlateRect(static_cast<float>(FMath::Min(A.X, B.X)), static_cast<float>(FMath::Min(A.Y, B.Y)),
		static_cast<float>(FMath::Max(A.X, B.X)), static_cast<float>(FMath::Max(A.Y, B.Y)));
}

FVector2D UValhallaGameHUDWidget::CurrentBoxSize(FName Key) const
{
	const FPanelState* State = PanelStates.Find(Key);
	const USizeBox* Box = State ? State->SizeBox.Get() : nullptr;
	const FValhallaMovablePanel* Info = FindMovablePanel(Key);
	if (!Box || !Info || Info->Sizing == EValhallaPanelSizing::Scaled)
	{
		return FVector2D::ZeroVector;
	}
	FVector2D Out(BoxWidth(Box), Info->Sizing == EValhallaPanelSizing::FlowingBox ? BoxHeight(Box) : BoxMaxHeight(Box));
	// An axis the designer left to its content: what it measures now.
	if (Out.X <= 0.0) { Out.X = Box->GetDesiredSize().X; }
	if (Out.Y <= 0.0) { Out.Y = Box->GetDesiredSize().Y; }
	return Out;
}

FVector2D UValhallaGameHUDWidget::MinFlowingSize(FName Key)
{
	if (Key == TEXT("Chat"))      { return FVector2D(200.0, 60.0); }
	if (Key == TEXT("CombatLog")) { return FVector2D(160.0, 80.0); }
	if (Key == TEXT("Skills"))    { return FVector2D(220.0, 120.0); }
	return FVector2D(120.0, 60.0);
}

bool UValhallaGameHUDWidget::IsPanelWantedByGame(FName Key, const UWidget* Widget) const
{
	// Windows the player opens: their own state. The rest (always-on parts,
	// and the ones the Tick code shows and hides every frame: cast bar, target,
	// party): what the Tick code has just set.
	if (Key == TEXT("Loot"))                              { return LootBag.IsValid(); }
	if (Key == TEXT("Skills"))                            { return bSkillsOpen; }
	if (Key == TEXT("Character") || Key == TEXT("Inventory")) { return bInventoryOpen; }
	// The cast bar's canvas child is its frame (CastBarSize), which stays up;
	// TickCastBar shows and hides the bar inside it. Wanted = a spell is casting
	// (not the edit-mode placeholder).
	if (Key == TEXT("CastBar"))
	{
		const AValhallaCharacter* Pawn = GetValhallaPawn();
		const UValhallaSkillComponent* Skills = Pawn ? Pawn->GetSkillComponent() : nullptr;
		return Skills && !Skills->CastingSkillId.IsNone();
	}
	return Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed && Widget->GetVisibility() != ESlateVisibility::Hidden;
}

void UValhallaGameHUDWidget::SetEditMode(bool bOn)
{
	if (!bLayoutFromBlueprint || !RootCanvas)
	{
		bEditMode = false;
		return;
	}
	if (bOn == bEditMode)
	{
		return;
	}
	bEditMode = bOn;

	if (bOn)
	{
		for (TPair<FName, FPanelState>& Entry : PanelStates)
		{
			UWidget* Widget = Entry.Value.Widget.Get();
			if (!Widget)
			{
				continue;
			}
			Entry.Value.Interactive.Reset();
			UWidgetTree::ForWidgetAndChildren(Widget, [&Entry](UWidget* Child)
			{
				if (IsInteractiveChild(Child))
				{
					Entry.Value.Interactive.Add(Child);
				}
			});

			UValhallaPanelEditOverlay* Overlay = WidgetTree->ConstructWidget<UValhallaPanelEditOverlay>(UValhallaPanelEditOverlay::StaticClass());
			Overlay->Hud = this;
			Overlay->Setup(Entry.Key, EffectiveHighlightColour());
			UCanvasPanelSlot* OverlaySlot = RootCanvas->AddChildToCanvas(Overlay);
			OverlaySlot->SetAnchors(FAnchors(0.f, 0.f));
			OverlaySlot->SetAlignment(FVector2D::ZeroVector);
			OverlaySlot->SetAutoSize(false);
			// Over every panel and the tooltip (50), under the options menu (60).
			OverlaySlot->SetZOrder(55);
			const FSlateRect Rect = GetPanelCanvasRect(Entry.Key);
			OverlaySlot->SetPosition(FVector2D(Rect.Left, Rect.Top));
			OverlaySlot->SetSize(FVector2D(Rect.GetSize()));
			EditOverlays.Add(Entry.Key, Overlay);
		}
		HideTooltip();
		UE_LOG(LogValhallaHUD, Log, TEXT("game HUD: unlocked (edit mode): %d panel overlay(s)."), EditOverlays.Num());
		return;
	}

	if (IsDraggingPanel())
	{
		EndPanelDrag(true);
	}
	for (const TPair<FName, TObjectPtr<UValhallaPanelEditOverlay>>& Entry : EditOverlays)
	{
		if (Entry.Value)
		{
			Entry.Value->RemoveFromParent();
		}
	}
	EditOverlays.Reset();
	for (TPair<FName, FPanelState>& Entry : PanelStates)
	{
		FPanelState& State = Entry.Value;
		UWidget* Widget = State.Widget.Get();
		State.Interactive.Reset();
		if (Widget && State.bGhosted)
		{
			// Back to what the game (or the player) wants: the Tick code shows
			// the cast bar, target and party again if they are needed.
			Widget->SetRenderOpacity(1.f);
			if (Entry.Key == TEXT("CastBar"))
			{
				// Collapse the bar, not its frame: TickCastBar only ever shows the
				// bar again, so a collapsed frame would hide it for good. A frame
				// the player hid stays hidden (EnforceUserHiddenPanels).
				if (State.bUserHidden)
				{
					Widget->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
			else if (State.bUserHidden || !IsPanelWantedByGame(Entry.Key, Widget)
				|| Entry.Key == TEXT("TargetFrame") || Entry.Key == TEXT("Party"))
			{
				Widget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		State.bGhosted = false;
		// The full height edit mode gave the max-height panels.
		const FValhallaMovablePanel* Info = FindMovablePanel(Entry.Key);
		if (USizeBox* Box = State.SizeBox.Get(); Box && Info && Info->Sizing == EValhallaPanelSizing::FlowingMaxHeight)
		{
			if (Entry.Key == TEXT("Chat") && bChatOpen && ChatOpenHeight > 0.f)
			{
				Box->SetHeightOverride(ChatOpenHeight);
			}
			else
			{
				Box->ClearHeightOverride();
			}
		}
	}
	// The edit-mode placeholder goes; TickCastBar shows the real bar during a cast.
	if (CastBarRoot)
	{
		CastBarRoot->SetVisibility(ESlateVisibility::Collapsed);
	}
	EnforceUserHiddenPanels();
	UE_LOG(LogValhallaHUD, Log, TEXT("game HUD: locked."));
}

void UValhallaGameHUDWidget::TickEditMode()
{
	if (PendingCommitTicks > 0 && --PendingCommitTicks == 0)
	{
		CommitPanelDrag();
	}
	if (!bEditMode)
	{
		return;
	}

	const FVector2D CursorAt = FSlateApplication::IsInitialized() ? FVector2D(FSlateApplication::Get().GetCursorPos()) : FVector2D(-1.0, -1.0);
	for (TPair<FName, FPanelState>& Entry : PanelStates)
	{
		FPanelState& State = Entry.Value;
		UWidget* Widget = State.Widget.Get();
		if (!Widget)
		{
			continue;
		}

		// Hidden panels (by the game or the player) show faintly so they can be placed.
		const bool bShown = !State.bUserHidden && IsPanelWantedByGame(Entry.Key, Widget);
		if (bShown)
		{
			if (State.bGhosted)
			{
				Widget->SetRenderOpacity(1.f);
				State.bGhosted = false;
			}
		}
		else
		{
			const ESlateVisibility Current = Widget->GetVisibility();
			if (Current == ESlateVisibility::Collapsed || Current == ESlateVisibility::Hidden)
			{
				const ESlateVisibility Designer = State.DesignerVisibility;
				Widget->SetVisibility(Designer == ESlateVisibility::Collapsed || Designer == ESlateVisibility::Hidden
					? ESlateVisibility::SelfHitTestInvisible : Designer);
			}
			if (!State.bGhosted)
			{
				Widget->SetRenderOpacity(0.4f);
				State.bGhosted = true;
			}
		}

		// Max-height panels (chat, skills) at their full height, so the whole box can be placed and sized.
		const FValhallaMovablePanel* Info = FindMovablePanel(Entry.Key);
		if (USizeBox* Box = State.SizeBox.Get(); Box && Info && Info->Sizing == EValhallaPanelSizing::FlowingMaxHeight)
		{
			const float MaxHeight = BoxMaxHeight(Box);
			if (MaxHeight > 0.f && !FMath::IsNearlyEqual(BoxHeight(Box), MaxHeight))
			{
				Box->SetHeightOverride(MaxHeight);
			}
		}

		// The overlay follows what the panel draws.
		UValhallaPanelEditOverlay* Overlay = EditOverlays.FindRef(Entry.Key);
		UCanvasPanelSlot* OverlaySlot = Overlay ? Cast<UCanvasPanelSlot>(Overlay->Slot) : nullptr;
		if (!OverlaySlot)
		{
			continue;
		}
		const FSlateRect Rect = GetPanelCanvasRect(Entry.Key);
		if (Rect.GetSize().X < 2.0 || Rect.GetSize().Y < 2.0)
		{
			Overlay->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		OverlaySlot->SetPosition(FVector2D(Rect.Left, Rect.Top));
		OverlaySlot->SetSize(FVector2D(Rect.GetSize()));
		Overlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		// A button, check box, slider or text box under the cursor gets the press.
		bool bOverInteractive = false;
		if (DragKey != Entry.Key)
		{
			for (const TWeakObjectPtr<UWidget>& Weak : State.Interactive)
			{
				const UWidget* Child = Weak.Get();
				if (Child && Child->IsVisible() && Child->GetCachedGeometry().IsUnderLocation(CursorAt))
				{
					bOverInteractive = true;
					break;
				}
			}
		}
		if (Overlay->IsBodyHitTestable() == bOverInteractive)
		{
			Overlay->SetBodyHitTestable(!bOverInteractive);
		}
	}
}

bool UValhallaGameHUDWidget::BeginPanelDrag(FName Key, const FVector2D& CanvasPoint, bool bResize)
{
	FPanelState* State = PanelStates.Find(Key);
	UWidget* Widget = State ? State->Widget.Get() : nullptr;
	UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
	if (!bEditMode || !CanvasSlot)
	{
		return false;
	}
	const FSlateRect Rect = GetPanelCanvasRect(Key);
	if (Rect.GetSize().X < 2.0 || Rect.GetSize().Y < 2.0)
	{
		return false;
	}
	if (IsDraggingPanel())
	{
		EndPanelDrag(true);
	}
	if (PendingCommitTicks > 0)
	{
		// The previous release has not been saved yet: save it now.
		PendingCommitTicks = 0;
		CommitPanelDrag();
	}

	DragKey = Key;
	bDragResize = bResize && FindMovablePanel(Key) != nullptr;
	bDragMoved = false;
	DragStartPoint = CanvasPoint;
	DragStartRect = Rect;
	DragTopLeft = FVector2D(Rect.Left, Rect.Top);
	DragStartRenderScale = FMath::Max(0.01f, State->AppliedScale);
	DragPanelScale = DragStartRenderScale / FMath::Max(0.01f, AppliedUiScale);
	DragStartBox = DragBox = CurrentBoxSize(Key);

	// Pinned by its drawn top-left while it moves (the render scale then grows
	// it from that corner); CommitPanelDrag re-anchors it.
	CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
	CanvasSlot->SetAlignment(FVector2D::ZeroVector);
	CanvasSlot->SetPosition(DragTopLeft);
	Widget->SetRenderTransformPivot(FVector2D::ZeroVector);
	UE_LOG(LogValhallaHUD, Log, TEXT("edit mode: %s %s from (%.0f, %.0f), %.0f x %.0f"), bDragResize ? TEXT("resize") : TEXT("move"),
		*Key.ToString(), Rect.Left, Rect.Top, Rect.GetSize().X, Rect.GetSize().Y);
	return true;
}

void UValhallaGameHUDWidget::UpdatePanelDrag(const FVector2D& CanvasPoint)
{
	FPanelState* State = DragKey.IsNone() ? nullptr : PanelStates.Find(DragKey);
	UWidget* Widget = State ? State->Widget.Get() : nullptr;
	UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
	const FValhallaMovablePanel* Info = FindMovablePanel(DragKey);
	if (!CanvasSlot || !Info)
	{
		return;
	}
	const FVector2D Delta = CanvasPoint - DragStartPoint;
	if (!bDragMoved && Delta.Size() < 3.0)
	{
		return; // a click, not a drag (yet)
	}
	bDragMoved = true;
	const FVector2D Canvas = GetCanvasSize();
	const FVector2D StartSize = FVector2D(DragStartRect.GetSize());

	if (!bDragResize)
	{
		DragTopLeft = SnapPanelPosition(FVector2D(DragStartRect.Left, DragStartRect.Top) + Delta, StartSize, Canvas, SnapGrid, EdgeSnapDistance);
		CanvasSlot->SetPosition(DragTopLeft);
		return;
	}

	if (Info->Sizing == EValhallaPanelSizing::Scaled)
	{
		// Uniform: the pointer's diagonal against the panel's, from its top-left.
		const FVector2D Wanted = StartSize + Delta;
		const double Ratio = FVector2D::DotProduct(Wanted, StartSize) / FMath::Max(1.0, StartSize.SizeSquared());
		const float StartPanelScale = DragStartRenderScale / FMath::Max(0.01f, AppliedUiScale);
		DragPanelScale = FMath::Clamp(FMath::RoundToFloat(static_cast<float>(StartPanelScale * Ratio) * 20.f) / 20.f, MinPanelScale, MaxPanelScale);
		FWidgetTransform Transform = Widget->GetRenderTransform();
		Transform.Scale = FVector2D(AppliedUiScale * DragPanelScale, AppliedUiScale * DragPanelScale);
		Widget->SetRenderTransform(Transform);
		State->AppliedScale = AppliedUiScale * DragPanelScale;
		return;
	}

	USizeBox* Box = State->SizeBox.Get();
	if (!Box)
	{
		return;
	}
	// The pointer moves in canvas units; the box is sized before the render scale.
	const FVector2D Min = MinFlowingSize(DragKey);
	const FVector2D Max = Canvas / DragStartRenderScale;
	FVector2D Size = DragStartBox + Delta / DragStartRenderScale;
	Size.X = FMath::Clamp(FMath::RoundToDouble(Size.X / SnapGrid) * SnapGrid, Min.X, FMath::Max(Min.X, Max.X));
	Size.Y = FMath::Clamp(FMath::RoundToDouble(Size.Y / SnapGrid) * SnapGrid, Min.Y, FMath::Max(Min.Y, Max.Y));
	DragBox = Size;
	SetWidthOrClear(Box, Size.X);
	if (Info->Sizing == EValhallaPanelSizing::FlowingBox)
	{
		SetHeightOrClear(Box, Size.Y);
	}
	else
	{
		SetMaxHeightOrClear(Box, Size.Y);
		Box->SetHeightOverride(Size.Y); // edit mode shows it at its full height
	}
	if (DragKey == TEXT("Chat"))
	{
		ChatDesignWidth = Size.X;
		ChatOpenHeight = Size.Y;
	}
}

void UValhallaGameHUDWidget::EndPanelDrag(bool bCommit)
{
	if (DragKey.IsNone())
	{
		return;
	}
	const FName Key = DragKey;
	DragKey = NAME_None;
	if (!bCommit || !bDragMoved)
	{
		// Nothing moved (a click) or cancelled: back where the settings have it.
		if (const UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this))
		{
			ApplyUserLayout(UserSettings->Get());
		}
		return;
	}
	// Measured once Slate has laid it out at its new place / size.
	PendingCommitKey = Key;
	bPendingResize = bDragResize;
	PendingCommitTicks = 2;
}

void UValhallaGameHUDWidget::CommitPanelDrag()
{
	const FName Key = PendingCommitKey;
	PendingCommitKey = NAME_None;
	const FPanelState* State = PanelStates.Find(Key);
	const FValhallaMovablePanel* Info = FindMovablePanel(Key);
	UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this);
	if (!State || !Info || !UserSettings)
	{
		return;
	}
	const FSlateRect Rect = GetPanelCanvasRect(Key);
	const FVector2D Canvas = GetCanvasSize();
	if (Rect.GetSize().X < 2.0 || Canvas.X < 2.0)
	{
		ApplyUserLayout(UserSettings->Get());
		return;
	}

	const FValhallaPanelLayout Placed = AnchorLayoutForRect(Rect, Canvas, AppliedUiScale);
	const FValhallaPanelLayout Designer = State->Designer;
	const FVector2D Box = CurrentBoxSize(Key);
	const float PanelScale = FMath::RoundToFloat(State->AppliedScale / FMath::Max(0.01f, AppliedUiScale) * 100.f) / 100.f;
	const bool bScaled = Info->Sizing == EValhallaPanelSizing::Scaled;
	UserSettings->Mutate([&](FValhallaUserUISettings& S)
	{
		const FValhallaPanelLayout* Existing = S.FindSetPanel(Key);
		FValhallaPanelLayout Layout = Existing ? *Existing : Designer;
		Layout.AnchorMin = Placed.AnchorMin;
		Layout.AnchorMax = Placed.AnchorMax;
		Layout.Alignment = Placed.Alignment;
		Layout.Position = Placed.Position;
		if (bScaled)
		{
			Layout.Scale = PanelScale;
		}
		else
		{
			// The box as it is now (TickLayout may have narrowed the chat), so a move never resizes it.
			if (Box.X > 0.0) { Layout.Size.X = Box.X; }
			if (Box.Y > 0.0) { Layout.Size.Y = Box.Y; }
		}
		Layout.bSet = true;
		S.Panels.Add(Key, Layout);
	});
	UE_LOG(LogValhallaHUD, Log, TEXT("edit mode: %s %s: drawn (%.0f, %.0f) %.0f x %.0f on %.0f x %.0f -> anchor (%.1f, %.1f) position (%.1f, %.1f)%s"),
		*Key.ToString(), bPendingResize ? TEXT("resized") : TEXT("moved"), Rect.Left, Rect.Top, Rect.GetSize().X, Rect.GetSize().Y, Canvas.X, Canvas.Y,
		Placed.AnchorMin.X, Placed.AnchorMin.Y, Placed.Position.X, Placed.Position.Y,
		*(bScaled ? FString::Printf(TEXT(", scale %.2f"), PanelScale) : FString::Printf(TEXT(", size %.0f x %.0f"), Box.X, Box.Y)));
}

void UValhallaGameHUDWidget::ScrollPanelAt(FName Key, const FVector2D& ScreenPosition, float WheelDelta)
{
	const FPanelState* State = PanelStates.Find(Key);
	UWidget* Widget = State ? State->Widget.Get() : nullptr;
	if (!Widget)
	{
		return;
	}
	UScrollBox* Under = nullptr;
	UWidgetTree::ForWidgetAndChildren(Widget, [&Under, &ScreenPosition](UWidget* Child)
	{
		if (UScrollBox* Scroll = Cast<UScrollBox>(Child); Scroll && Scroll->IsVisible() && Scroll->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			Under = Scroll;
		}
	});
	if (Under)
	{
		Under->SetScrollOffset(FMath::Clamp(Under->GetScrollOffset() - WheelDelta * 30.f, 0.f, Under->GetScrollOffsetOfEnd()));
	}
}

bool UValhallaGameHUDWidget::DragPanelForTest(FName Key, const FVector2D& Delta, bool bResize)
{
	if (!bEditMode)
	{
		UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI dragtest: the HUD is locked (valhalla.UI lock 0 first)."));
		return false;
	}
	const FSlateRect Rect = GetPanelCanvasRect(Key);
	const FVector2D Start = bResize ? FVector2D(Rect.Right - 4.0, Rect.Bottom - 4.0) : FVector2D(Rect.GetCenter());
	if (!BeginPanelDrag(Key, Start, bResize))
	{
		UE_LOG(LogValhallaHUD, Warning, TEXT("valhalla.UI dragtest: %s is not a movable panel on the canvas."), *Key.ToString());
		return false;
	}
	UpdatePanelDrag(Start + Delta * 0.5);
	UpdatePanelDrag(Start + Delta);
	EndPanelDrag(true);
	UE_LOG(LogValhallaHUD, Log, TEXT("valhalla.UI dragtest: %s %s by (%.0f, %.0f)."), bResize ? TEXT("resized") : TEXT("moved"), *Key.ToString(), Delta.X, Delta.Y);
	return true;
}

// ═════════════════════════════════════════════════════════════════════════════
//  B-21 step 4 — the options menu
// ═════════════════════════════════════════════════════════════════════════════

UValhallaOptionsMenuWidget* UValhallaGameHUDWidget::GetOptionsMenu() const
{
	return OptionsMenu;
}

UClass* UValhallaGameHUDWidget::GetOptionsMenuClass() const
{
	return OptionsMenuClass.Get();
}

bool UValhallaGameHUDWidget::IsOptionsOpen() const
{
	return OptionsMenu && OptionsMenu->GetVisibility() != ESlateVisibility::Collapsed;
}

void UValhallaGameHUDWidget::OpenOptions()
{
	if (!RootCanvas || !bLayoutFromBlueprint)
	{
		UE_LOG(LogValhallaHUD, Warning, TEXT("options menu: this HUD has no layout."));
		return;
	}
	if (!OptionsMenu)
	{
		UClass* MenuClass = OptionsMenuClass.Get() ? OptionsMenuClass.Get() : UValhallaOptionsMenuWidget::StaticClass();
		OptionsMenu = WidgetTree->ConstructWidget<UValhallaOptionsMenuWidget>(MenuClass);
		UCanvasPanelSlot* MenuSlot = RootCanvas->AddChildToCanvas(OptionsMenu);
		MenuSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		MenuSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		MenuSlot->SetPosition(FVector2D::ZeroVector);
		MenuSlot->SetAutoSize(true);
		MenuSlot->SetZOrder(60);
		OptionsMenu->Setup(this);
		ApplyPanelBorders(); // its frame too
		UE_LOG(LogValhallaHUD, Log, TEXT("options menu made (%s)."), *MenuClass->GetName());
	}
	if (const UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this))
	{
		OptionsMenu->SyncFromSettings(UserSettings->Get());
	}
	// It eats clicks on itself (a Visible border); the input mode stays GameAndUI.
	OptionsMenu->SetVisibility(ESlateVisibility::Visible);
	HideTooltip();
	UE_LOG(LogValhallaHUD, Log, TEXT("options menu open"));
}

void UValhallaGameHUDWidget::CloseOptions()
{
	if (!OptionsMenu)
	{
		return;
	}
	OptionsMenu->CloseSubPanel();
	OptionsMenu->SetVisibility(ESlateVisibility::Collapsed);
	UE_LOG(LogValhallaHUD, Log, TEXT("options menu closed"));
}

void UValhallaGameHUDWidget::ToggleOptions()
{
	if (IsOptionsOpen())
	{
		CloseOptions();
	}
	else
	{
		OpenOptions();
	}
}
