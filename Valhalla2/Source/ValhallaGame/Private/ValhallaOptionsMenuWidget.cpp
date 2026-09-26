// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaOptionsMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "ValhallaGameHUDWidget.h"
#include "ValhallaGraphicsSettingsSubsystem.h"
#include "ValhallaPlayerController.h"
#include "ValhallaUserSettingsSubsystem.h"

namespace
{
	FSlateFontInfo MenuFont(int32 Size, bool bBold)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
		Font.OutlineSettings.OutlineSize = 1;
		Font.OutlineSettings.OutlineColor = FLinearColor::Black;
		return Font;
	}

	FLinearColor MenuSrgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B));
	}

	/** Linear -> sRGB floats (what a colour picker's H / S / V read). */
	FLinearColor ToSrgbFloats(const FLinearColor& Linear)
	{
		const FColor Srgb = Linear.ToFColorSRGB();
		return FLinearColor(Srgb.R / 255.f, Srgb.G / 255.f, Srgb.B / 255.f, Linear.A);
	}

	FLinearColor FromSrgbFloats(const FLinearColor& Srgb)
	{
		FLinearColor Out = FLinearColor::FromSRGBColor(Srgb.ToFColor(false));
		Out.A = Srgb.A;
		return Out;
	}

	/** A swatch brush: the colour with a thin dark rim. */
	FSlateRoundedBoxBrush SwatchBrush(const FLinearColor& Colour)
	{
		return FSlateRoundedBoxBrush(Colour, 2.f, FLinearColor(0.f, 0.f, 0.f, 0.9f), 1.f);
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  Indexed buttons / checks (rows C++ adds)
// ═════════════════════════════════════════════════════════════════════════════

UValhallaOptionsIndexedButton::UValhallaOptionsIndexedButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Keyboard focus stays with the game viewport (WASD keeps walking).
	InitIsFocusable(false);
}

void UValhallaOptionsIndexedButton::HandleClicked()
{
	if (UValhallaOptionsMenuWidget* Owner = Menu.Get())
	{
		Owner->HandleIndexedButton(Kind, Index);
	}
}

UValhallaOptionsIndexedCheck::UValhallaOptionsIndexedCheck(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitIsFocusable(false);
}

void UValhallaOptionsIndexedCheck::HandleChanged(bool bIsChecked)
{
	if (UValhallaOptionsMenuWidget* Owner = Menu.Get())
	{
		Owner->HandleIndexedCheck(Index, bIsChecked);
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  Static lists
// ═════════════════════════════════════════════════════════════════════════════

const TArray<FName>& UValhallaOptionsMenuWidget::GetOptionalWidgetNames()
{
	// Keep in step with the BindWidgetOptional members in the header
	// (Valhalla.Game.UI.OptionsMenu checks, hud_blueprints.layout_options_menu makes them).
	static const TArray<FName> Names = {
		TEXT("Tabs"), TEXT("CloseButton"),
		TEXT("LayoutTabButton"), TEXT("ColoursTabButton"), TEXT("ChatLogTabButton"), TEXT("NameplatesTabButton"), TEXT("ControlsTabButton"),
		TEXT("LockCheck"), TEXT("UiScaleSlider"), TEXT("UiScaleText"), TEXT("OpacitySlider"), TEXT("OpacityText"),
		TEXT("BorderSlider"), TEXT("BorderText"),
		TEXT("ShowVitalsCheck"), TEXT("ShowActionBarCheck"), TEXT("ShowCastBarCheck"), TEXT("ShowTargetFrameCheck"),
		TEXT("ShowPartyCheck"), TEXT("ShowCombatLogCheck"), TEXT("ShowChatCheck"), TEXT("ResetLayoutButton"),
		TEXT("ColourList"), TEXT("ColourEditor"), TEXT("EditTitle"), TEXT("PresetGrid"),
		TEXT("HueSlider"), TEXT("SaturationSlider"), TEXT("ValueSlider"), TEXT("EditSwatch"),
		TEXT("ColourOkButton"), TEXT("ColourCancelButton"), TEXT("ResetColoursButton"),
		TEXT("ChatFontSizeSlider"), TEXT("ChatFontSizeText"), TEXT("ChatLinesSlider"), TEXT("ChatLinesText"),
		TEXT("TimestampsCheck"), TEXT("LogFilterList"), TEXT("ResetChatButton"),
		TEXT("NpcNameplatesCheck"), TEXT("PlayerNameplatesCheck"), TEXT("FloatingTextCheck"),
		TEXT("NameplateFontSlider"), TEXT("NameplateFontText"), TEXT("ResetNameplatesButton"),
		TEXT("ControlsText"),
		TEXT("GraphicsTabButton"), TEXT("PresetLowButton"), TEXT("PresetMediumButton"), TEXT("PresetHighButton"), TEXT("PresetEpicButton"),
		TEXT("PresetText"), TEXT("GlobalIlluminationCheck"), TEXT("ResolutionScaleSlider"), TEXT("ResolutionScaleText"),
		TEXT("FrameRateCapSlider"), TEXT("FrameRateCapText"), TEXT("VSyncCheck"), TEXT("MotionBlurCheck"), TEXT("ResetGraphicsButton"),
	};
	return Names;
}

const TArray<FName>& UValhallaOptionsMenuWidget::GetShowPanelKeys()
{
	// The hideable movable panels. Loot, Skills, Character and Inventory are
	// windows the player opens (a key or a click), so they have no switch.
	static const TArray<FName> Keys = {
		TEXT("Vitals"), TEXT("ActionBar"), TEXT("CastBar"), TEXT("TargetFrame"), TEXT("Party"), TEXT("CombatLog"), TEXT("Chat"),
	};
	return Keys;
}

const TArray<FLinearColor>& UValhallaOptionsMenuWidget::GetPresetColours()
{
	static const TArray<FLinearColor> Presets = {
		MenuSrgb(0xff, 0xff, 0xff), MenuSrgb(0xaa, 0xaa, 0xcc), MenuSrgb(0xff, 0x44, 0x44), MenuSrgb(0xff, 0xaa, 0x00),
		MenuSrgb(0xff, 0xdd, 0x00), MenuSrgb(0x44, 0xff, 0x44), MenuSrgb(0x2f, 0x9e, 0x55), MenuSrgb(0x5a, 0xd8, 0xff),
		MenuSrgb(0x44, 0x88, 0xff), MenuSrgb(0xa3, 0x35, 0xee), MenuSrgb(0xff, 0x88, 0xcc), MenuSrgb(0xc8, 0x96, 0x5a),
	};
	return Presets;
}

FString UValhallaOptionsMenuWidget::BuildControlsText(const UInputMappingContext* Context)
{
	if (!Context)
	{
		return TEXT("The controls are not available (no player input yet).");
	}

	// What each action is for; the keys come from the mapping context, so a
	// rebind shows up here. IA_ActionBar1..8 read as one line.
	auto Describe = [](const FString& ActionName) -> FString
	{
		if (ActionName == TEXT("IA_Move"))         { return TEXT("Move"); }
		if (ActionName.StartsWith(TEXT("IA_ActionBar"))) { return TEXT("Action bar slots"); }
		if (ActionName == TEXT("IA_ToggleSkills")) { return TEXT("Skills"); }
		if (ActionName == TEXT("IA_Inventory"))    { return TEXT("Character + inventory"); }
		if (ActionName == TEXT("IA_Chat"))         { return TEXT("Chat"); }
		if (ActionName == TEXT("IA_Escape"))       { return TEXT("Close the top window / options"); }
		if (ActionName == TEXT("IA_PrimaryClick")) { return TEXT("Target / use"); }
		if (ActionName == TEXT("IA_CameraOrbit"))  { return TEXT("Orbit the camera (hold, drag)"); }
		if (ActionName == TEXT("IA_CameraZoom"))   { return TEXT("Zoom"); }
		if (ActionName == TEXT("IA_CameraLook"))   { return FString(); } // the mouse axis behind the orbit
		return ActionName.StartsWith(TEXT("IA_")) ? ActionName.Mid(3) : ActionName;
	};

	TArray<FString> Order;
	TMap<FString, TArray<FString>> KeysFor;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (!Mapping.Action)
		{
			continue;
		}
		const FString Label = Describe(Mapping.Action->GetName());
		if (Label.IsEmpty())
		{
			continue;
		}
		if (!KeysFor.Contains(Label))
		{
			Order.Add(Label);
		}
		KeysFor.FindOrAdd(Label).AddUnique(Mapping.Key.GetDisplayName().ToString());
	}

	FString Out;
	for (const FString& Label : Order)
	{
		const TArray<FString>& Keys = KeysFor.FindChecked(Label);
		FString KeyText = FString::Join(Keys, TEXT(", "));
		if (Label == TEXT("Action bar slots") && Keys.Num() > 2)
		{
			KeyText = FString::Printf(TEXT("%s - %s"), *Keys[0], *Keys.Last());
		}
		Out += FString::Printf(TEXT("%s:   %s\n"), *Label, *KeyText);
	}
	Out += TEXT("\nWhile the HUD is unlocked (Layout tab), drag a panel to move it and its corner grip to resize it.");
	return Out;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaOptionsMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Missing parts only hide their control; say which, once per class.
	static TSet<FName> WarnedClasses;
	if (WidgetTree && WidgetTree->RootWidget && !WarnedClasses.Contains(GetClass()->GetFName()))
	{
		TArray<FString> Missing;
		for (const FName Name : GetOptionalWidgetNames())
		{
			const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaOptionsMenuWidget::StaticClass(), Name);
			if (!Property || !Property->GetObjectPropertyValue_InContainer(this))
			{
				Missing.Add(Name.ToString());
			}
		}
		if (Missing.Num() > 0)
		{
			WarnedClasses.Add(GetClass()->GetFName());
			UE_LOG(LogValhallaHUD, Warning, TEXT("options menu: %s has no %s; those controls are hidden."),
				*GetClass()->GetName(), *FString::Join(Missing, TEXT(", ")));
		}
	}
	else if (!WidgetTree || !WidgetTree->RootWidget)
	{
		UE_LOG(LogValhallaHUD, Warning, TEXT("options menu: %s has no layout (set WBP_GameHUD's Options Menu Class to WBP_OptionsMenu)."),
			*GetClass()->GetName());
	}
}

void UValhallaOptionsMenuWidget::Setup(UValhallaGameHUDWidget* InHud)
{
	Hud = InHud;
	if (bSetUp)
	{
		return;
	}
	bSetUp = true;

	if (CloseButton)         { CloseButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnClose); }
	if (LayoutTabButton)     { LayoutTabButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnLayoutTab); }
	if (ColoursTabButton)    { ColoursTabButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnColoursTab); }
	if (ChatLogTabButton)    { ChatLogTabButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnChatLogTab); }
	if (NameplatesTabButton) { NameplatesTabButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnNameplatesTab); }
	if (ControlsTabButton)   { ControlsTabButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnControlsTab); }
	if (GraphicsTabButton)   { GraphicsTabButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnGraphicsTab); }
	TabButtons = { LayoutTabButton, ColoursTabButton, ChatLogTabButton, NameplatesTabButton, ControlsTabButton, GraphicsTabButton };

	auto Range = [](USlider* Slider, float Min, float Max, float Step)
	{
		if (Slider)
		{
			Slider->SetMinValue(Min);
			Slider->SetMaxValue(Max);
			Slider->SetStepSize(Step);
		}
	};
	Range(UiScaleSlider, FValhallaUserUISettings::MinUiScale, FValhallaUserUISettings::MaxUiScale, 0.05f);
	Range(OpacitySlider, FValhallaUserUISettings::MinPanelOpacity, FValhallaUserUISettings::MaxPanelOpacity, 0.05f);
	Range(BorderSlider, FValhallaUserUISettings::MinPanelBorder, FValhallaUserUISettings::MaxPanelBorder, 1.f);
	Range(ChatFontSizeSlider, ChatFontMin, ChatFontMax, 1.f);
	Range(ChatLinesSlider, ChatLinesMin, ChatLinesMax, 1.f);
	Range(NameplateFontSlider, NameplateFontMin, NameplateFontMax, 1.f);
	Range(HueSlider, 0.f, 1.f, 0.005f);
	Range(SaturationSlider, 0.f, 1.f, 0.005f);
	Range(ValueSlider, 0.f, 1.f, 0.005f);
	Range(ResolutionScaleSlider, FValhallaGraphicsSettings::MinResolutionScale, FValhallaGraphicsSettings::MaxResolutionScale, 5.f);
	Range(FrameRateCapSlider, 0.f, static_cast<float>(FValhallaGraphicsSettings::GetFrameRateCapSteps().Num() - 1), 1.f);

	if (LockCheck)            { LockCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnLockChanged); }
	if (UiScaleSlider)        { UiScaleSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnUiScaleChanged); }
	if (OpacitySlider)        { OpacitySlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnOpacityChanged); }
	if (BorderSlider)         { BorderSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnBorderChanged); }
	if (ShowVitalsCheck)      { ShowVitalsCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnShowVitalsChanged); }
	if (ShowActionBarCheck)   { ShowActionBarCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnShowActionBarChanged); }
	if (ShowCastBarCheck)     { ShowCastBarCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnShowCastBarChanged); }
	if (ShowTargetFrameCheck) { ShowTargetFrameCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnShowTargetFrameChanged); }
	if (ShowPartyCheck)       { ShowPartyCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnShowPartyChanged); }
	if (ShowCombatLogCheck)   { ShowCombatLogCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnShowCombatLogChanged); }
	if (ShowChatCheck)        { ShowChatCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnShowChatChanged); }
	if (ResetLayoutButton)    { ResetLayoutButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnResetLayout); }

	if (HueSlider)            { HueSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnHsvChanged); }
	if (SaturationSlider)     { SaturationSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnHsvChanged); }
	if (ValueSlider)          { ValueSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnHsvChanged); }
	if (ColourOkButton)       { ColourOkButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnColourOk); }
	if (ColourCancelButton)   { ColourCancelButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnColourCancel); }
	if (ResetColoursButton)   { ResetColoursButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnResetColours); }

	if (ChatFontSizeSlider)   { ChatFontSizeSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnChatFontChanged); }
	if (ChatLinesSlider)      { ChatLinesSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnChatLinesChanged); }
	if (TimestampsCheck)      { TimestampsCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnTimestampsChanged); }
	if (ResetChatButton)      { ResetChatButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnResetChat); }

	if (NpcNameplatesCheck)   { NpcNameplatesCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnNpcNameplatesChanged); }
	if (PlayerNameplatesCheck){ PlayerNameplatesCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnPlayerNameplatesChanged); }
	if (FloatingTextCheck)    { FloatingTextCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnFloatingTextChanged); }
	if (NameplateFontSlider)  { NameplateFontSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnNameplateFontChanged); }
	if (ResetNameplatesButton){ ResetNameplatesButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnResetNameplates); }

	if (PresetLowButton)         { PresetLowButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnPresetLow); }
	if (PresetMediumButton)      { PresetMediumButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnPresetMedium); }
	if (PresetHighButton)        { PresetHighButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnPresetHigh); }
	if (PresetEpicButton)        { PresetEpicButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnPresetEpic); }
	PresetButtons = { PresetLowButton, PresetMediumButton, PresetHighButton, PresetEpicButton };
	if (GlobalIlluminationCheck) { GlobalIlluminationCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnGlobalIlluminationChanged); }
	if (ResolutionScaleSlider)   { ResolutionScaleSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnResolutionScaleChanged); }
	if (FrameRateCapSlider)      { FrameRateCapSlider->OnValueChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnFrameRateCapChanged); }
	if (VSyncCheck)              { VSyncCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnVSyncChanged); }
	if (MotionBlurCheck)         { MotionBlurCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnMotionBlurChanged); }
	if (ResetGraphicsButton)     { ResetGraphicsButton->OnClicked.AddUniqueDynamic(this, &UValhallaOptionsMenuWidget::OnResetGraphics); }

	BuildColourRows();
	BuildPresetSwatches();
	BuildLogFilterChecks();
	RefreshControlsText();
	if (ColourEditor)
	{
		ColourEditor->SetVisibility(ESlateVisibility::Collapsed);
	}
	ShowTab(0);

	if (const UValhallaUserSettingsSubsystem* UserSettings = GetUserSettings())
	{
		SyncFromSettings(UserSettings->Get());
	}
	// Graphics are the account's, not the character's: their own subsystem.
	if (UValhallaGraphicsSettingsSubsystem* Graphics = UValhallaGraphicsSettingsSubsystem::Get(this))
	{
		Graphics->OnChanged.AddUObject(this, &UValhallaOptionsMenuWidget::SyncFromGraphics);
		SyncFromGraphics(Graphics->Get());
	}
}

UValhallaUserSettingsSubsystem* UValhallaOptionsMenuWidget::GetUserSettings() const
{
	return UValhallaUserSettingsSubsystem::Get(this);
}

void UValhallaOptionsMenuWidget::Edit(TFunctionRef<void(FValhallaUserUISettings&)> Change)
{
	if (bSyncing)
	{
		return;
	}
	if (UValhallaUserSettingsSubsystem* UserSettings = GetUserSettings())
	{
		UserSettings->Mutate(Change);
	}
}

FReply UValhallaOptionsMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// A click on the menu's background is the menu's: it must not select or
	// walk to whatever stands behind it.
	Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	return FReply::Handled();
}

FReply UValhallaOptionsMenuWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Not the camera's zoom while the pointer is over the menu.
	Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
	return FReply::Handled();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Rows C++ adds
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaOptionsMenuWidget::BuildColourRows()
{
	if (!ColourList || !WidgetTree)
	{
		return;
	}
	ColourList->ClearChildren();
	ColourSwatches.Reset();
	const TArray<FValhallaStyleColourKey>& Keys = UValhallaGameHUDWidget::GetStyleColourKeys();
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Name->SetText(FText::FromString(Keys[Index].Label));
		Name->SetFont(MenuFont(8, false));
		Name->SetColorAndOpacity(FSlateColor(MenuSrgb(0xdd, 0xd6, 0xc4)));
		UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);

		USizeBox* SwatchSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SwatchSize->SetWidthOverride(40.f);
		SwatchSize->SetHeightOverride(14.f);
		UBorder* Swatch = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Swatch->SetBrush(SwatchBrush(FLinearColor::White));
		SwatchSize->SetContent(Swatch);
		UHorizontalBoxSlot* SwatchSlot = Row->AddChildToHorizontalBox(SwatchSize);
		SwatchSlot->SetVerticalAlignment(VAlign_Center);
		SwatchSlot->SetPadding(FMargin(6.f, 0.f));

		UValhallaOptionsIndexedButton* EditButton = WidgetTree->ConstructWidget<UValhallaOptionsIndexedButton>(UValhallaOptionsIndexedButton::StaticClass());
		EditButton->Kind = EValhallaOptionsButton::EditColour;
		EditButton->Index = Index;
		EditButton->Menu = this;
		EditButton->SetBackgroundColor(MenuSrgb(0x88, 0x88, 0xaa));
		UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Caption->SetText(FText::FromString(TEXT("Edit")));
		Caption->SetFont(MenuFont(7, true));
		EditButton->AddChild(Caption);
		EditButton->OnClicked.AddDynamic(EditButton, &UValhallaOptionsIndexedButton::HandleClicked);
		Row->AddChildToHorizontalBox(EditButton)->SetVerticalAlignment(VAlign_Center);

		if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(ColourList->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 1.f));
		}
		ColourSwatches.Add(Swatch);
	}
}

void UValhallaOptionsMenuWidget::BuildPresetSwatches()
{
	if (!PresetGrid || !WidgetTree)
	{
		return;
	}
	PresetGrid->ClearChildren();
	UUniformGridPanel* Grid = Cast<UUniformGridPanel>(PresetGrid);
	const TArray<FLinearColor>& Presets = GetPresetColours();
	for (int32 Index = 0; Index < Presets.Num(); ++Index)
	{
		UValhallaOptionsIndexedButton* Swatch = WidgetTree->ConstructWidget<UValhallaOptionsIndexedButton>(UValhallaOptionsIndexedButton::StaticClass());
		Swatch->Kind = EValhallaOptionsButton::Preset;
		Swatch->Index = Index;
		Swatch->Menu = this;
		FButtonStyle Style = Swatch->GetStyle();
		FSlateRoundedBoxBrush Normal = SwatchBrush(Presets[Index]);
		Style.SetNormal(Normal);
		Style.SetHovered(FSlateRoundedBoxBrush(Presets[Index], 2.f, FLinearColor::White, 1.5f));
		Style.SetPressed(Normal);
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Swatch->SetStyle(Style);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(22.f);
		Size->SetHeightOverride(16.f);
		Swatch->AddChild(Size);
		Swatch->OnClicked.AddDynamic(Swatch, &UValhallaOptionsIndexedButton::HandleClicked);
		if (Grid)
		{
			Grid->AddChildToUniformGrid(Swatch, Index / 6, Index % 6);
		}
		else
		{
			PresetGrid->AddChild(Swatch);
		}
	}
}

void UValhallaOptionsMenuWidget::BuildLogFilterChecks()
{
	if (!LogFilterList || !WidgetTree)
	{
		return;
	}
	LogFilterList->ClearChildren();
	LogFilterChecks.Reset();
	const TArray<FName>& Keys = UValhallaGameHUDWidget::GetLogFilterKeys();
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UValhallaOptionsIndexedCheck* Check = WidgetTree->ConstructWidget<UValhallaOptionsIndexedCheck>(UValhallaOptionsIndexedCheck::StaticClass());
		Check->Index = Index;
		Check->Menu = this;
		Check->OnCheckStateChanged.AddDynamic(Check, &UValhallaOptionsIndexedCheck::HandleChanged);
		Row->AddChildToHorizontalBox(Check)->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(UValhallaGameHUDWidget::GetLogFilterLabel(Keys[Index])));
		Label->SetFont(MenuFont(8, false));
		Label->SetColorAndOpacity(FSlateColor(MenuSrgb(0xdd, 0xd6, 0xc4)));
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		LogFilterList->AddChild(Row);
		LogFilterChecks.Add(Check);
	}
}

void UValhallaOptionsMenuWidget::RefreshControlsText()
{
	if (!ControlsText)
	{
		return;
	}
	const AValhallaPlayerController* PC = Cast<AValhallaPlayerController>(GetOwningPlayer());
	ControlsText->SetText(FText::FromString(BuildControlsText(PC ? PC->GetInputMappingContext() : nullptr)));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Settings -> controls
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaOptionsMenuWidget::SyncFromSettings(const FValhallaUserUISettings& Settings)
{
	TGuardValue<bool> Guard(bSyncing, true);

	if (LockCheck)     { LockCheck->SetIsChecked(Settings.bLocked); }
	if (UiScaleSlider) { UiScaleSlider->SetValue(Settings.UiScale); }
	if (UiScaleText)   { UiScaleText->SetText(FText::FromString(FString::Printf(TEXT("%.2fx"), Settings.UiScale))); }
	if (OpacitySlider) { OpacitySlider->SetValue(Settings.PanelOpacity); }
	if (OpacityText)   { OpacityText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Settings.PanelOpacity * 100.f))); }
	{
		// 0 in the settings = the HUD's default thickness, which is what the slider shows.
		const UValhallaGameHUDWidget* Owner = Hud.Get();
		const float Border = UValhallaGameHUDWidget::ResolvePanelBorder(Settings.PanelBorder,
			Owner ? Owner->GetPanelBorderThickness() : GetDefault<UValhallaGameHUDWidget>()->GetPanelBorderThickness());
		if (BorderSlider) { BorderSlider->SetValue(Border); }
		if (BorderText)   { BorderText->SetText(FText::FromString(FString::Printf(TEXT("%.0f px"), Border))); }
	}

	UCheckBox* const ShowChecks[] = { ShowVitalsCheck, ShowActionBarCheck, ShowCastBarCheck, ShowTargetFrameCheck, ShowPartyCheck, ShowCombatLogCheck, ShowChatCheck };
	const TArray<FName>& ShowKeys = GetShowPanelKeys();
	constexpr int32 ShowCheckCount = UE_ARRAY_COUNT(ShowChecks);
	for (int32 Index = 0; Index < ShowKeys.Num() && Index < ShowCheckCount; ++Index)
	{
		if (ShowChecks[Index])
		{
			const FValhallaPanelLayout* Layout = Settings.FindSetPanel(ShowKeys[Index]);
			ShowChecks[Index]->SetIsChecked(!Layout || Layout->bVisible);
		}
	}

	const UValhallaGameHUDWidget* Owner = Hud.Get();
	const TArray<FValhallaStyleColourKey>& Keys = UValhallaGameHUDWidget::GetStyleColourKeys();
	for (int32 Index = 0; Index < ColourSwatches.Num() && Index < Keys.Num(); ++Index)
	{
		const FLinearColor* Override = Settings.Colours.Find(Keys[Index].Key);
		const FLinearColor Colour = Override ? *Override : (Owner ? Owner->GetDefaultColour(Keys[Index].Key) : FLinearColor::White);
		ColourSwatches[Index]->SetBrush(SwatchBrush(Colour));
	}

	const int32 ChatFont = Settings.ChatFontSize > 0 ? Settings.ChatFontSize : 8;
	if (ChatFontSizeSlider) { ChatFontSizeSlider->SetValue(static_cast<float>(ChatFont)); }
	if (ChatFontSizeText)   { ChatFontSizeText->SetText(FText::FromString(FString::Printf(TEXT("%d pt%s"), ChatFont, Settings.ChatFontSize > 0 ? TEXT("") : TEXT(" (default)")))); }
	const int32 ChatLines = Settings.ChatVisibleLines > 0 ? Settings.ChatVisibleLines : 9;
	if (ChatLinesSlider)    { ChatLinesSlider->SetValue(static_cast<float>(ChatLines)); }
	if (ChatLinesText)      { ChatLinesText->SetText(FText::FromString(FString::Printf(TEXT("%d%s"), ChatLines, Settings.ChatVisibleLines > 0 ? TEXT("") : TEXT(" (default)")))); }
	if (TimestampsCheck)    { TimestampsCheck->SetIsChecked(Settings.bChatTimestamps); }
	const TArray<FName>& Filters = UValhallaGameHUDWidget::GetLogFilterKeys();
	for (int32 Index = 0; Index < LogFilterChecks.Num() && Index < Filters.Num(); ++Index)
	{
		const bool* bShown = Settings.LogFilters.Find(Filters[Index]);
		LogFilterChecks[Index]->SetIsChecked(!bShown || *bShown);
	}

	if (NpcNameplatesCheck)    { NpcNameplatesCheck->SetIsChecked(Settings.bShowNpcNameplates); }
	if (PlayerNameplatesCheck) { PlayerNameplatesCheck->SetIsChecked(Settings.bShowPlayerNameplates); }
	if (FloatingTextCheck)     { FloatingTextCheck->SetIsChecked(Settings.bFloatingCombatText); }
	const int32 PlateFont = Settings.NameplateFontSize > 0 ? Settings.NameplateFontSize : 12;
	if (NameplateFontSlider)   { NameplateFontSlider->SetValue(static_cast<float>(PlateFont)); }
	if (NameplateFontText)     { NameplateFontText->SetText(FText::FromString(FString::Printf(TEXT("%d px%s"), PlateFont, Settings.NameplateFontSize > 0 ? TEXT("") : TEXT(" (default)")))); }
}

void UValhallaOptionsMenuWidget::SyncFromGraphics(const FValhallaGraphicsSettings& Graphics)
{
	TGuardValue<bool> Guard(bSyncing, true);

	for (int32 Index = 0; Index < PresetButtons.Num(); ++Index)
	{
		if (UButton* Button = PresetButtons[Index])
		{
			// The preset in force lit (still lit under Custom: it is what Custom starts from).
			Button->SetBackgroundColor(Index == static_cast<int32>(Graphics.Quality) ? FLinearColor(1.15f, 1.05f, 0.85f) : FLinearColor(0.6f, 0.58f, 0.55f));
		}
	}
	if (PresetText)
	{
		FString Base = FValhallaGraphicsSettings::QualityToString(Graphics.Quality);
		Base[0] = FChar::ToUpper(Base[0]);
		PresetText->SetText(FText::FromString(Graphics.IsCustom()
			? FString::Printf(TEXT("Custom (%s, global illumination %s)"), *Base, Graphics.bGlobalIllumination ? TEXT("on") : TEXT("off"))
			: Base));
	}
	if (GlobalIlluminationCheck) { GlobalIlluminationCheck->SetIsChecked(Graphics.bGlobalIllumination); }
	if (ResolutionScaleSlider)   { ResolutionScaleSlider->SetValue(static_cast<float>(Graphics.ResolutionScale)); }
	if (ResolutionScaleText)     { ResolutionScaleText->SetText(FText::FromString(FString::Printf(TEXT("%d %%"), Graphics.ResolutionScale))); }
	const TArray<int32>& Caps = FValhallaGraphicsSettings::GetFrameRateCapSteps();
	int32 CapIndex = 0;
	for (int32 Index = 0; Index < Caps.Num(); ++Index)
	{
		// The step nearest a saved cap (a cap typed into the file may be off the steps).
		if (FMath::Abs(Caps[Index] - Graphics.FrameRateCap) < FMath::Abs(Caps[CapIndex] - Graphics.FrameRateCap))
		{
			CapIndex = Index;
		}
	}
	if (FrameRateCapSlider) { FrameRateCapSlider->SetValue(static_cast<float>(CapIndex)); }
	if (FrameRateCapText)   { FrameRateCapText->SetText(FText::FromString(Graphics.FrameRateCap > 0 ? FString::Printf(TEXT("%d fps"), Graphics.FrameRateCap) : FString(TEXT("None")))); }
	if (VSyncCheck)         { VSyncCheck->SetIsChecked(Graphics.bVSync); }
	if (MotionBlurCheck)    { MotionBlurCheck->SetIsChecked(Graphics.bMotionBlur); }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Tabs
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaOptionsMenuWidget::ShowTab(int32 TabIndex)
{
	if (Tabs && Tabs->GetNumWidgets() > 0)
	{
		Tabs->SetActiveWidgetIndex(FMath::Clamp(TabIndex, 0, Tabs->GetNumWidgets() - 1));
	}
	if (TabIndex != 1)
	{
		CloseSubPanel();
	}
	StyleTabButtons();
	if (TabIndex == 4)
	{
		RefreshControlsText();
	}
}

int32 UValhallaOptionsMenuWidget::GetActiveTab() const
{
	return Tabs ? Tabs->GetActiveWidgetIndex() : 0;
}

void UValhallaOptionsMenuWidget::StyleTabButtons()
{
	const int32 Active = GetActiveTab();
	for (int32 Index = 0; Index < TabButtons.Num(); ++Index)
	{
		if (UButton* Button = TabButtons[Index])
		{
			// The open tab's plate lit, the others dimmed.
			Button->SetBackgroundColor(Index == Active ? FLinearColor(1.15f, 1.05f, 0.85f) : FLinearColor(0.6f, 0.58f, 0.55f));
		}
	}
}

void UValhallaOptionsMenuWidget::OnClose()
{
	if (UValhallaGameHUDWidget* Owner = Hud.Get())
	{
		Owner->CloseOptions();
	}
}

void UValhallaOptionsMenuWidget::OnLayoutTab()     { ShowTab(0); }
void UValhallaOptionsMenuWidget::OnColoursTab()    { ShowTab(1); }
void UValhallaOptionsMenuWidget::OnChatLogTab()    { ShowTab(2); }
void UValhallaOptionsMenuWidget::OnNameplatesTab() { ShowTab(3); }
void UValhallaOptionsMenuWidget::OnControlsTab()   { ShowTab(4); }
void UValhallaOptionsMenuWidget::OnGraphicsTab()   { ShowTab(5); }

// ═════════════════════════════════════════════════════════════════════════════
//  Layout tab
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaOptionsMenuWidget::OnLockChanged(bool bChecked)
{
	Edit([bChecked](FValhallaUserUISettings& S) { S.bLocked = bChecked; });
	UE_LOG(LogValhallaHUD, Log, TEXT("options: HUD %s"), bChecked ? TEXT("locked") : TEXT("unlocked"));
}

void UValhallaOptionsMenuWidget::OnUiScaleChanged(float Value)
{
	Edit([Value](FValhallaUserUISettings& S) { S.UiScale = FMath::RoundToFloat(Value * 20.f) / 20.f; });
}

void UValhallaOptionsMenuWidget::OnOpacityChanged(float Value)
{
	Edit([Value](FValhallaUserUISettings& S) { S.PanelOpacity = FMath::RoundToFloat(Value * 20.f) / 20.f; });
}

void UValhallaOptionsMenuWidget::OnBorderChanged(float Value)
{
	Edit([Value](FValhallaUserUISettings& S)
	{
		S.PanelBorder = FMath::Clamp(FMath::RoundToFloat(Value), FValhallaUserUISettings::MinPanelBorder, FValhallaUserUISettings::MaxPanelBorder);
	});
}

void UValhallaOptionsMenuWidget::SetPanelShown(FName Key, bool bShown)
{
	const UValhallaGameHUDWidget* Owner = Hud.Get();
	const FValhallaPanelLayout* Designer = Owner ? Owner->GetDesignerLayout(Key) : nullptr;
	if (!Designer)
	{
		UE_LOG(LogValhallaHUD, Warning, TEXT("options: %s is not on this HUD's canvas."), *Key.ToString());
		return;
	}
	const FValhallaPanelLayout Base = *Designer;
	Edit([Key, bShown, &Base](FValhallaUserUISettings& S)
	{
		const FValhallaPanelLayout* Existing = S.FindSetPanel(Key);
		FValhallaPanelLayout Layout = Existing ? *Existing : Base;
		Layout.bSet = true;
		Layout.bVisible = bShown;
		S.Panels.Add(Key, Layout);
	});
}

void UValhallaOptionsMenuWidget::OnShowVitalsChanged(bool bChecked)      { SetPanelShown(TEXT("Vitals"), bChecked); }
void UValhallaOptionsMenuWidget::OnShowActionBarChanged(bool bChecked)   { SetPanelShown(TEXT("ActionBar"), bChecked); }
void UValhallaOptionsMenuWidget::OnShowCastBarChanged(bool bChecked)     { SetPanelShown(TEXT("CastBar"), bChecked); }
void UValhallaOptionsMenuWidget::OnShowTargetFrameChanged(bool bChecked) { SetPanelShown(TEXT("TargetFrame"), bChecked); }
void UValhallaOptionsMenuWidget::OnShowPartyChanged(bool bChecked)       { SetPanelShown(TEXT("Party"), bChecked); }
void UValhallaOptionsMenuWidget::OnShowCombatLogChanged(bool bChecked)   { SetPanelShown(TEXT("CombatLog"), bChecked); }
void UValhallaOptionsMenuWidget::OnShowChatChanged(bool bChecked)        { SetPanelShown(TEXT("Chat"), bChecked); }

void UValhallaOptionsMenuWidget::OnResetLayout()
{
	// Everything on this tab but the lock: where the panels are, and how big.
	Edit([](FValhallaUserUISettings& S)
	{
		S.ResetSection(EValhallaUISettingsSection::Layout);
		S.UiScale = 1.f;
		S.PanelOpacity = 1.f;
		S.PanelBorder = 0.f; // the HUD's default
	});
	UE_LOG(LogValhallaHUD, Log, TEXT("options: layout reset"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Colours tab
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaOptionsMenuWidget::HandleIndexedButton(EValhallaOptionsButton Kind, int32 Index)
{
	if (Kind == EValhallaOptionsButton::EditColour)
	{
		OpenColourEditor(Index);
	}
	else if (Kind == EValhallaOptionsButton::Preset && GetPresetColours().IsValidIndex(Index))
	{
		SetEditorColour(GetPresetColours()[Index]);
	}
}

bool UValhallaOptionsMenuWidget::IsColourEditorOpen() const
{
	return ColourEditor && ColourEditor->GetVisibility() != ESlateVisibility::Collapsed;
}

bool UValhallaOptionsMenuWidget::CloseSubPanel()
{
	const bool bWasOpen = IsColourEditorOpen();
	if (ColourEditor)
	{
		ColourEditor->SetVisibility(ESlateVisibility::Collapsed);
	}
	EditingKey = INDEX_NONE;
	return bWasOpen;
}

void UValhallaOptionsMenuWidget::OpenColourEditor(int32 KeyIndex)
{
	const TArray<FValhallaStyleColourKey>& Keys = UValhallaGameHUDWidget::GetStyleColourKeys();
	const UValhallaGameHUDWidget* Owner = Hud.Get();
	if (!Keys.IsValidIndex(KeyIndex) || !Owner || !ColourEditor)
	{
		return;
	}
	EditingKey = KeyIndex;
	if (EditTitle)
	{
		EditTitle->SetText(FText::FromString(FString::Printf(TEXT("Colour: %s"), Keys[KeyIndex].Label)));
	}
	SetEditorColour(Owner->GetEffectiveColour(Keys[KeyIndex].Key));
	ColourEditor->SetVisibility(ESlateVisibility::Visible);
}

void UValhallaOptionsMenuWidget::SetEditorColour(const FLinearColor& Colour)
{
	const FLinearColor Hsv = ToSrgbFloats(Colour).LinearRGBToHSV();
	{
		TGuardValue<bool> Guard(bSyncing, true);
		if (HueSlider)        { HueSlider->SetValue(Hsv.R / 360.f); }
		if (SaturationSlider) { SaturationSlider->SetValue(Hsv.G); }
		if (ValueSlider)      { ValueSlider->SetValue(Hsv.B); }
	}
	if (EditSwatch)
	{
		EditSwatch->SetBrush(SwatchBrush(FLinearColor(Colour.R, Colour.G, Colour.B, 1.f)));
	}
}

FLinearColor UValhallaOptionsMenuWidget::EditorColour() const
{
	const float H = HueSlider ? HueSlider->GetValue() * 360.f : 0.f;
	const float S = SaturationSlider ? SaturationSlider->GetValue() : 0.f;
	const float V = ValueSlider ? ValueSlider->GetValue() : 1.f;
	return FromSrgbFloats(FLinearColor(H, S, V, 1.f).HSVToLinearRGB());
}

void UValhallaOptionsMenuWidget::UpdateEditSwatch()
{
	if (EditSwatch)
	{
		EditSwatch->SetBrush(SwatchBrush(EditorColour()));
	}
}

void UValhallaOptionsMenuWidget::OnHsvChanged(float /*Value*/)
{
	if (!bSyncing)
	{
		UpdateEditSwatch();
	}
}

void UValhallaOptionsMenuWidget::OnColourOk()
{
	const TArray<FValhallaStyleColourKey>& Keys = UValhallaGameHUDWidget::GetStyleColourKeys();
	if (Keys.IsValidIndex(EditingKey))
	{
		const FName Key = Keys[EditingKey].Key;
		const FLinearColor Colour = EditorColour();
		Edit([Key, Colour](FValhallaUserUISettings& S) { S.Colours.Add(Key, Colour); });
		UE_LOG(LogValhallaHUD, Log, TEXT("options: colour %s -> %s"), *Key.ToString(), *FValhallaUserUISettings::ColourToHex(Colour));
	}
	CloseSubPanel();
}

void UValhallaOptionsMenuWidget::OnColourCancel()
{
	CloseSubPanel();
}

void UValhallaOptionsMenuWidget::OnResetColours()
{
	Edit([](FValhallaUserUISettings& S) { S.Colours.Reset(); });
	CloseSubPanel();
	UE_LOG(LogValhallaHUD, Log, TEXT("options: colours reset"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Chat & log, nameplates
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaOptionsMenuWidget::OnChatFontChanged(float Value)
{
	const int32 Size = FMath::RoundToInt(FMath::Clamp(Value, ChatFontMin, ChatFontMax));
	Edit([Size](FValhallaUserUISettings& S) { S.ChatFontSize = Size; });
}

void UValhallaOptionsMenuWidget::OnChatLinesChanged(float Value)
{
	const int32 Lines = FMath::RoundToInt(FMath::Clamp(Value, ChatLinesMin, ChatLinesMax));
	Edit([Lines](FValhallaUserUISettings& S) { S.ChatVisibleLines = Lines; });
}

void UValhallaOptionsMenuWidget::OnTimestampsChanged(bool bChecked)
{
	Edit([bChecked](FValhallaUserUISettings& S) { S.bChatTimestamps = bChecked; });
}

void UValhallaOptionsMenuWidget::HandleIndexedCheck(int32 Index, bool bChecked)
{
	const TArray<FName>& Keys = UValhallaGameHUDWidget::GetLogFilterKeys();
	UValhallaGameHUDWidget* Owner = Hud.Get();
	if (!bSyncing && Owner && Keys.IsValidIndex(Index))
	{
		// Through the HUD, so the log redraws and the filter menu agrees.
		Owner->SetLogFilter(Keys[Index], bChecked);
	}
}

void UValhallaOptionsMenuWidget::OnResetChat()
{
	if (UValhallaUserSettingsSubsystem* UserSettings = GetUserSettings())
	{
		UserSettings->ResetToDefaults(EValhallaUISettingsSection::Chat);
	}
}

void UValhallaOptionsMenuWidget::OnNpcNameplatesChanged(bool bChecked)
{
	Edit([bChecked](FValhallaUserUISettings& S) { S.bShowNpcNameplates = bChecked; });
}

void UValhallaOptionsMenuWidget::OnPlayerNameplatesChanged(bool bChecked)
{
	Edit([bChecked](FValhallaUserUISettings& S) { S.bShowPlayerNameplates = bChecked; });
}

void UValhallaOptionsMenuWidget::OnFloatingTextChanged(bool bChecked)
{
	Edit([bChecked](FValhallaUserUISettings& S) { S.bFloatingCombatText = bChecked; });
}

void UValhallaOptionsMenuWidget::OnNameplateFontChanged(float Value)
{
	const int32 Size = FMath::RoundToInt(FMath::Clamp(Value, NameplateFontMin, NameplateFontMax));
	Edit([Size](FValhallaUserUISettings& S) { S.NameplateFontSize = Size; });
}

void UValhallaOptionsMenuWidget::OnResetNameplates()
{
	if (UValhallaUserSettingsSubsystem* UserSettings = GetUserSettings())
	{
		UserSettings->ResetToDefaults(EValhallaUISettingsSection::Nameplates);
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  Graphics tab (B-27, per account)
// ═════════════════════════════════════════════════════════════════════════════

void UValhallaOptionsMenuWidget::EditGraphics(TFunctionRef<void(FValhallaGraphicsSettings&)> Change)
{
	if (bSyncing)
	{
		return;
	}
	if (UValhallaGraphicsSettingsSubsystem* Graphics = UValhallaGraphicsSettingsSubsystem::Get(this))
	{
		Graphics->Mutate(Change);
	}
}

void UValhallaOptionsMenuWidget::SelectPreset(EValhallaGraphicsQuality Quality)
{
	EditGraphics([Quality](FValhallaGraphicsSettings& S) { S.SetPreset(Quality); });
	UE_LOG(LogValhallaHUD, Log, TEXT("options: graphics preset %s"), FValhallaGraphicsSettings::QualityToString(Quality));
}

void UValhallaOptionsMenuWidget::OnPresetLow()    { SelectPreset(EValhallaGraphicsQuality::Low); }
void UValhallaOptionsMenuWidget::OnPresetMedium() { SelectPreset(EValhallaGraphicsQuality::Medium); }
void UValhallaOptionsMenuWidget::OnPresetHigh()   { SelectPreset(EValhallaGraphicsQuality::High); }
void UValhallaOptionsMenuWidget::OnPresetEpic()   { SelectPreset(EValhallaGraphicsQuality::Epic); }

void UValhallaOptionsMenuWidget::OnGlobalIlluminationChanged(bool bChecked)
{
	EditGraphics([bChecked](FValhallaGraphicsSettings& S) { S.bGlobalIllumination = bChecked; });
}

void UValhallaOptionsMenuWidget::OnResolutionScaleChanged(float Value)
{
	const int32 Scale = FMath::RoundToInt(Value / 5.f) * 5;
	EditGraphics([Scale](FValhallaGraphicsSettings& S) { S.ResolutionScale = Scale; });
}

void UValhallaOptionsMenuWidget::OnFrameRateCapChanged(float Value)
{
	const TArray<int32>& Caps = FValhallaGraphicsSettings::GetFrameRateCapSteps();
	const int32 Cap = Caps[FMath::Clamp(FMath::RoundToInt(Value), 0, Caps.Num() - 1)];
	EditGraphics([Cap](FValhallaGraphicsSettings& S) { S.FrameRateCap = Cap; });
}

void UValhallaOptionsMenuWidget::OnVSyncChanged(bool bChecked)
{
	EditGraphics([bChecked](FValhallaGraphicsSettings& S) { S.bVSync = bChecked; });
}

void UValhallaOptionsMenuWidget::OnMotionBlurChanged(bool bChecked)
{
	EditGraphics([bChecked](FValhallaGraphicsSettings& S) { S.bMotionBlur = bChecked; });
}

void UValhallaOptionsMenuWidget::OnResetGraphics()
{
	if (UValhallaGraphicsSettingsSubsystem* Graphics = UValhallaGraphicsSettingsSubsystem::Get(this))
	{
		Graphics->ResetToDefaults();
	}
}
