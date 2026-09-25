// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-21 step 4: the in-game options menu. Escape opens it when nothing else is
// open (and closes it first when it is), the cog bottom right toggles it
// (UValhallaGameHUDWidget::OpenOptions / CloseOptions / ToggleOptions).
//
// Logic here, look in WBP_OptionsMenu (Content/Valhalla/UI/HUD, a child of
// this class, laid out by `hud_blueprints.layout_options_menu`, assigned as
// WBP_GameHUD's OptionsMenuClass). Every designer widget binds by name and is
// optional: a missing one hides that control (one warning listing them all).
//
// Every control writes the player's settings through
// UValhallaUserSettingsSubsystem::Mutate (saved 2 s later, per character,
// synced through the backend), and the HUD applies the change at once
// (ApplyUserLayout / ApplyUserStyle on OnChanged); the HUD then calls
// SyncFromSettings so the menu shows what is in force.
//
// Tabs (the Tabs widget switcher's children, in order):
//   0 Layout      LockCheck, UiScaleSlider (0.5-2), OpacitySlider (0.2-1),
//                 BorderSlider (1-12 px, the framed panels' trim),
//                 Show<Panel>Check for the hideable panels, ResetLayoutButton
//   1 Colours     ColourList (C++ adds a row per colour: name, swatch, Edit),
//                 ColourEditor (PresetGrid: 12 swatches, H / S / V sliders,
//                 EditSwatch, ColourOkButton / ColourCancelButton),
//                 ResetColoursButton
//   2 Chat & log  ChatFontSizeSlider (6-14), ChatLinesSlider (4-20),
//                 TimestampsCheck, LogFilterList (C++ adds a check box per
//                 combat-log filter), ResetChatButton
//   3 Nameplates  NpcNameplatesCheck, PlayerNameplatesCheck, FloatingTextCheck,
//                 NameplateFontSlider (8-20 px), ResetNameplatesButton
//   4 Controls    ControlsText, from the player controller's input mappings

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "ValhallaUserUISettings.h"
#include "ValhallaOptionsMenuWidget.generated.h"

class UBorder;
class UInputMappingContext;
class UPanelWidget;
class USlider;
class UTextBlock;
class UWidgetSwitcher;
class UValhallaGameHUDWidget;
class UValhallaOptionsMenuWidget;

/** What a button C++ adds to the menu does. */
UENUM()
enum class EValhallaOptionsButton : uint8
{
	None,
	/** Index = UValhallaGameHUDWidget::GetStyleColourKeys() index: open the colour editor on it. */
	EditColour,
	/** Index = GetPresetColours() index: put that colour in the editor. */
	Preset,
};

/** A button the menu makes (colour rows, preset swatches) that knows which one it is. Not focusable. */
UCLASS()
class VALHALLAGAME_API UValhallaOptionsIndexedButton : public UButton
{
	GENERATED_BODY()

public:
	UValhallaOptionsIndexedButton(const FObjectInitializer& ObjectInitializer);

	EValhallaOptionsButton Kind = EValhallaOptionsButton::None;
	int32 Index = INDEX_NONE;
	TWeakObjectPtr<UValhallaOptionsMenuWidget> Menu;

	UFUNCTION()
	void HandleClicked();
};

/** A check box the menu makes (combat-log filters): Index = GetLogFilterKeys() index. Not focusable. */
UCLASS()
class VALHALLAGAME_API UValhallaOptionsIndexedCheck : public UCheckBox
{
	GENERATED_BODY()

public:
	UValhallaOptionsIndexedCheck(const FObjectInitializer& ObjectInitializer);

	int32 Index = INDEX_NONE;
	TWeakObjectPtr<UValhallaOptionsMenuWidget> Menu;

	UFUNCTION()
	void HandleChanged(bool bIsChecked);
};

UCLASS(Blueprintable, BlueprintType)
class VALHALLAGAME_API UValhallaOptionsMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called once by the HUD that owns it: rows, handlers, ranges, the controls list. */
	void Setup(UValhallaGameHUDWidget* InHud);

	/** Show what `Settings` holds (no settings are written). */
	void SyncFromSettings(const FValhallaUserUISettings& Settings);

	void ShowTab(int32 TabIndex);
	int32 GetActiveTab() const;
	/** Close the colour editor if it is open (Escape does this before closing the menu). True when it closed. */
	bool CloseSubPanel();
	bool IsColourEditorOpen() const;

	void HandleIndexedButton(EValhallaOptionsButton Kind, int32 Index);
	void HandleIndexedCheck(int32 Index, bool bChecked);

	/** The BindWidgetOptional member names (tests, the editor tool). */
	static const TArray<FName>& GetOptionalWidgetNames();
	/** The panel keys that get a Show<Key>Check (the hideable movable panels). */
	static const TArray<FName>& GetShowPanelKeys();
	/** The colour editor's twelve presets, sRGB-picked, stored linear. */
	static const TArray<FLinearColor>& GetPresetColours();
	/** The Controls tab: one line per bound action (keys by display name), from the mapping context. */
	static FString BuildControlsText(const UInputMappingContext* Context);

	// Slider ranges.
	static constexpr float ChatFontMin = 6.f;
	static constexpr float ChatFontMax = 14.f;
	static constexpr float ChatLinesMin = 4.f;
	static constexpr float ChatLinesMax = 20.f;
	static constexpr float NameplateFontMin = 8.f;
	static constexpr float NameplateFontMax = 20.f;

protected:
	//~ Begin UUserWidget interface
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~ End UUserWidget interface

	// ── Frame ──
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UWidgetSwitcher> Tabs;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> CloseButton;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> LayoutTabButton;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ColoursTabButton;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ChatLogTabButton;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> NameplatesTabButton;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ControlsTabButton;

	// ── Layout tab ──
	/** Checked = locked (the default): panels cannot be dragged. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> LockCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> UiScaleSlider;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> UiScaleText;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> OpacitySlider;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> OpacityText;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> BorderSlider;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BorderText;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> ShowVitalsCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> ShowActionBarCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> ShowCastBarCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> ShowTargetFrameCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> ShowPartyCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> ShowCombatLogCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> ShowChatCheck;
	/** Panel positions, sizes, scales and visibility, UI scale and opacity back to the defaults (not the lock). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ResetLayoutButton;

	// ── Colours tab ──
	/** C++ adds one row per GetStyleColourKeys entry (leave empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UPanelWidget> ColourList;
	/** The editor sub-panel, collapsed until a row's Edit. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UWidget> ColourEditor;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> EditTitle;
	/** C++ adds the twelve preset swatches (a Uniform Grid Panel: 6 x 2; leave empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UPanelWidget> PresetGrid;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> HueSlider;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> SaturationSlider;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> ValueSlider;
	/** The colour being edited, live. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UBorder> EditSwatch;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ColourOkButton;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ColourCancelButton;
	/** Every colour back to the HUD's default. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ResetColoursButton;

	// ── Chat & log tab ──
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> ChatFontSizeSlider;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChatFontSizeText;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> ChatLinesSlider;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChatLinesText;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> TimestampsCheck;
	/** C++ adds a check box per combat-log filter (leave empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UPanelWidget> LogFilterList;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ResetChatButton;

	// ── Nameplates tab ──
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> NpcNameplatesCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> PlayerNameplatesCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> FloatingTextCheck;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<USlider> NameplateFontSlider;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NameplateFontText;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UButton> ResetNameplatesButton;

	// ── Controls tab ──
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|Options", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ControlsText;

private:
	class UValhallaUserSettingsSubsystem* GetUserSettings() const;
	/** Mutate the settings unless SyncFromSettings is writing the controls. */
	void Edit(TFunctionRef<void(FValhallaUserUISettings&)> Change);
	void SetPanelShown(FName Key, bool bShown);
	void BuildColourRows();
	void BuildPresetSwatches();
	void BuildLogFilterChecks();
	void RefreshControlsText();
	void OpenColourEditor(int32 KeyIndex);
	/** The editor's swatch from the H / S / V sliders. */
	void UpdateEditSwatch();
	void SetEditorColour(const FLinearColor& Colour);
	FLinearColor EditorColour() const;
	void StyleTabButtons();

	UFUNCTION() void OnClose();
	UFUNCTION() void OnLayoutTab();
	UFUNCTION() void OnColoursTab();
	UFUNCTION() void OnChatLogTab();
	UFUNCTION() void OnNameplatesTab();
	UFUNCTION() void OnControlsTab();
	UFUNCTION() void OnLockChanged(bool bChecked);
	UFUNCTION() void OnUiScaleChanged(float Value);
	UFUNCTION() void OnOpacityChanged(float Value);
	UFUNCTION() void OnBorderChanged(float Value);
	UFUNCTION() void OnShowVitalsChanged(bool bChecked);
	UFUNCTION() void OnShowActionBarChanged(bool bChecked);
	UFUNCTION() void OnShowCastBarChanged(bool bChecked);
	UFUNCTION() void OnShowTargetFrameChanged(bool bChecked);
	UFUNCTION() void OnShowPartyChanged(bool bChecked);
	UFUNCTION() void OnShowCombatLogChanged(bool bChecked);
	UFUNCTION() void OnShowChatChanged(bool bChecked);
	UFUNCTION() void OnResetLayout();
	UFUNCTION() void OnHsvChanged(float Value);
	UFUNCTION() void OnColourOk();
	UFUNCTION() void OnColourCancel();
	UFUNCTION() void OnResetColours();
	UFUNCTION() void OnChatFontChanged(float Value);
	UFUNCTION() void OnChatLinesChanged(float Value);
	UFUNCTION() void OnTimestampsChanged(bool bChecked);
	UFUNCTION() void OnResetChat();
	UFUNCTION() void OnNpcNameplatesChanged(bool bChecked);
	UFUNCTION() void OnPlayerNameplatesChanged(bool bChecked);
	UFUNCTION() void OnFloatingTextChanged(bool bChecked);
	UFUNCTION() void OnNameplateFontChanged(float Value);
	UFUNCTION() void OnResetNameplates();

	TWeakObjectPtr<UValhallaGameHUDWidget> Hud;
	bool bSetUp = false;
	bool bSyncing = false;
	/** The colour key being edited, or INDEX_NONE. */
	int32 EditingKey = INDEX_NONE;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> ColourSwatches;
	UPROPERTY(Transient) TArray<TObjectPtr<UCheckBox>> LogFilterChecks;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TabButtons;
};
