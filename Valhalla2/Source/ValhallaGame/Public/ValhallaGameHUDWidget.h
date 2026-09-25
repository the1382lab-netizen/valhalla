// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8b: the game HUD. B-07: laid out by the WBP_GameHUD Widget Blueprint
// (Content/Valhalla/UI/HUD, a child of UValhallaGameHUDWidget, chosen in
// Project Settings > Valhalla > UI > Game HUD Class); C++ binds its widgets by
// name, fills them and runs them. The code-built layout was removed in B-07
// step 4; `ui-config.json` keeps only the inventory grid, the chat line counts
// and the nameplates (the nameplate / floating-text layer is still C++'s).
//
// The HUD reads replicated state every tick and never decides anything. Every
// button ends in the same Server RPC a key press or a console command does.
//
// Interaction model (see the class comment for the whole of it):
//   left click  = the slot's primary action (cast / equip / unequip / loot one)
//   right click = drop (inventory, equipment, with a confirm) / filter menu (log)
//   drag        = move (swap inventory, equip onto a slot, unequip onto a cell,
//                 skill or action onto an action slot, loot onto the bag)
//   click-select-then-click-target is the drag's keyboard-free twin for the
//   skills pane: click a skill (it is "armed"), click an action slot.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/Button.h"
#include "Layout/SlateRect.h"
#include "ValhallaGameTypes.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaUIConfig.h"
#include "ValhallaUserUISettings.h"
#include "ValhallaGameHUDWidget.generated.h"

class AValhallaCharacter;
class AValhallaLootBag;
class AValhallaPlayerController;
class AValhallaPlayerState;
class UBorder;
class UCanvasPanel;
class UEditableTextBox;
class UHorizontalBox;
class UImage;
class UOverlay;
class UPanelWidget;
class UProgressBar;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UUniformGridPanel;
class UVerticalBox;
class UValhallaGameHUDWidget;
class UValhallaHUDBarWidget;
class UValhallaOptionsMenuWidget;
class UValhallaPanelEditOverlay;

VALHALLAGAME_API DECLARE_LOG_CATEGORY_EXTERN(LogValhallaHUD, Log, All);

/** What a slot cell is a cell of. */
UENUM()
enum class EValhallaHUDSlotKind : uint8
{
	None,
	/** Action bar, Index 1..8. */
	Action,
	/** Inventory grid, Index 0..31 (cells past Inventory.Num() are empty). */
	Inventory,
	/** Equipment, Index = equip-slot index 0..8. */
	Equip,
	/** Open loot bag, Index = bag slot. */
	Loot,
	/** Skills pane, Id = skill id. */
	Skill,
	/** The combat log panel itself — right click opens the filter menu. */
	CombatLog,
};

/** What a text button does. BlueprintType so a designer can set it on a UValhallaHUDButton. */
UENUM(BlueprintType)
enum class EValhallaHUDButton : uint8
{
	None,
	LootAll,
	LootClose,
	PartyAccept,
	PartyDecline,
	DropConfirm,
	DropCancel,
	/** Index = combat-log filter index. */
	LogFilter,
	LogFilterClose,
	InventoryClose,
	SkillsClose,
	PartyLeave,
	/** B-21: the cog bottom right; toggles the options menu (as Escape with nothing open). */
	Options,
};

/**
 * One cell: a background, an icon or a two-letter abbreviation, a key label, a
 * quantity, a cooldown overlay and a selection ring. Clickable, right-clickable,
 * draggable and a drop target.
 *
 * A UUserWidget rather than a UButton because a button has no right click, no
 * drag and no drop, and all three are the point of an inventory cell.
 *
 * B-07 step 2: inheritable. A Widget Blueprint child (WBP_HUDSlot) may lay the
 * cell out itself: its designer widgets bind by name to the parts below (all
 * optional). With no designer tree the cell builds itself in code exactly as
 * before. Either way the HUD drives it through the same Set* calls. A part the
 * designer left out is a hidden stand-in, so the setters never need a null
 * check; the designer tree's own colours are left alone (Setup only styles
 * the code-built cell), but Setup sizes its Sizer, so one WBP_HUDSlot serves
 * every cell size (B-07 step 4).
 */
UCLASS(Blueprintable, BlueprintType)
class VALHALLAGAME_API UValhallaHUDSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	EValhallaHUDSlotKind Kind = EValhallaHUDSlotKind::None;
	int32 Index = INDEX_NONE;
	FName Id;
	TWeakObjectPtr<UValhallaGameHUDWidget> Hud;

	/**
	 * Size the cell (px, both ways; 0 leaves the size alone) and, for the
	 * code-built cell, colour it. A designer tree keeps its look but is sized
	 * too: its Sizer takes the size. Call once, before AddChild.
	 */
	void Setup(float Size, const FLinearColor& Background, const FLinearColor& Border, const FLinearColor& Highlight);

	/** Put an arbitrary widget over the cell (the combat log uses this to be one big cell). */
	void SetContent(UWidget* Content);

	void SetIcon(UTexture2D* Texture);
	void SetAbbrev(const FString& Text, const FLinearColor& Tint);
	/**
	 * A skill's generated icon: a rounded square in its category's colour with
	 * the two-letter code on it. `SkillColour` (skills.json `iconColor`) is the
	 * tile's rim, so two offensive skills still read apart.
	 */
	void SetSkillIcon(const FString& Code, const FLinearColor& CategoryColour, const FLinearColor& SkillColour, UTexture2D* Texture = nullptr);
	/**
	 * B-15 Wave 4 frame art: the cell draws `SlotTexture` (T_UI_Slot, a bronze
	 * rim round a dark well) as a nine-slice box instead of the flat ui-config
	 * border and fill. Selection then tints the rim with the highlight colour.
	 */
	void SetFrameArt(UTexture2D* SlotTexture, const FVector2D& MarginPx = FVector2D(10.f, 10.f), float BorderPx = 4.f, float FramePadding = 3.f);
	void SetKeyLabel(const FString& Text, const FLinearColor& Colour);
	void SetQuantity(int32 Quantity);
	/** 0 hides the overlay; `Text` is the seconds left. */
	void SetCooldown(float Fraction, const FString& Text, const FLinearColor& Colour);
	void SetDimmed(bool bDimmed);
	void SetSelected(bool bSelected);
	/** B-21: the selected / armed rim's colour (the player's Highlight colour); re-applied at once when selected. */
	void SetHighlightColour(const FLinearColor& Colour);
	void Clear();

	/** True when a Widget Blueprint child supplied the cell's layout (its designer tree). */
	bool HasDesignerTree() const { return bDesignerTree; }

	/** True when showing something (an item, a skill). */
	bool bFilled = false;

protected:
	//~ Begin UUserWidget interface
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	//~ End UUserWidget interface

	// ── Designer parts (BindWidgetOptional: a WBP child names its widgets so) ──
	/** The cell's size. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> Sizer;
	/** The rim / frame. Tinted with the highlight colour when selected and there is no SelectionRing. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Frame;
	/** The well behind the icon. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Fill;
	/** The layers over the well (icon, code, cooldown, labels). Dimmed when the cell is unusable. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> Stack;
	/** Item or painted skill icon. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Icon;
	/** The generated skill tile (category colour, rounded) behind the two-letter code. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SkillTile;
	/** Two-letter skill code, or an item's short name when it has no icon. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Abbrev;
	/** Cooldown sweep: a SizeBox whose height is the fraction left, holding CooldownFill. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> CooldownSizer;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> CooldownFill;
	/** Alternative cooldown sweep for a designer: a progress bar, percent = fraction left. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> CooldownBar;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CooldownText;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyLabel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> QuantityText;
	/** Shown while the cell is selected / armed (designer only; the code-built cell tints Frame). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SelectionRing;

private:
	/** Build the code tree, or (designer tree) stand in for the parts it lacks. Idempotent. */
	void EnsureTree();
	/** Designer tree: give every missing part a hidden, unparented stand-in. */
	void FillMissingParts();

	float CellSize = 44.f;
	bool bFrameArt = false;
	FLinearColor BorderColour = FLinearColor::Gray;
	FLinearColor HighlightColour = FLinearColor::Yellow;
	bool bPressed = false;
	bool bSelectedState = false;
	bool bTreeReady = false;
	bool bDesignerTree = false;
	/** Designer tree: whether Stack is the designer's own (else SetDimmed dims the whole cell). */
	bool bDesignerStack = false;
	/** Designer tree: Frame's own colour, restored when deselected. */
	FLinearColor DesignerFrameColour = FLinearColor::White;
};

/**
 * The drag a HUD cell starts. Its payload is the cell. A drag that ends where
 * nothing takes it (the world, a panel's background, anywhere off the cells)
 * is cancelled; for an action bar cell that means "take it off the bar"
 * (UValhallaGameHUDWidget::HandleSlotDraggedOff).
 */
UCLASS()
class VALHALLAGAME_API UValhallaHUDDragOperation : public UDragDropOperation
{
	GENERATED_BODY()

protected:
	virtual void DragCancelled_Implementation(const FPointerEvent& PointerEvent) override;
};

/**
 * B-07 step 2: one resource bar (HP, mana / energy, cast, target, party, XP,
 * nameplates) — what MakeBar used to build as loose widgets in the HUD's tree.
 *
 * Code-built (no designer tree): 1.0's bar, a 1 px half-white stroke (Frame)
 * round a background at bgAlpha (Background), a fixed-size box (Sizer) with the
 * fill (Fill inside FillSizer, whose width is the fraction), an optional wash
 * over it (OverlayFill inside OverlaySizer: Shield of Faith on the HP bar) and
 * a centred Label.
 *
 * A Widget Blueprint child (WBP_HUDBar) may lay it out itself; every part is
 * BindWidgetOptional. A designer can use a UProgressBar (FillBar / OverlayBar)
 * instead of the SizeBox pair; with the SizeBox pair the fill's full width is
 * BarWidth. The fraction, fill colour and label always come from C++.
 */
UCLASS(Blueprintable, BlueprintType)
class VALHALLAGAME_API UValhallaHUDBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Size and colour the code-built bar (Width x Height px inside the frame).
	 * On a designer tree only the fill colour is applied; sizes, background and
	 * font are the designer's.
	 */
	void Setup(float InWidth, float InHeight, const FLinearColor& FillColour, const FLinearColor& BackgroundColour, float BackgroundAlpha,
		bool bWithOverlay = false, int32 FontSize = 9);

	/**
	 * B-07 step 4: the bar's size inside its frame, designer tree or not: the
	 * Sizer takes Width x Height and a designer bar's BarWidth becomes Width.
	 * MakeBar calls it for the bars C++ makes (party 160x8, nameplates 60x4);
	 * the bars placed in WBP_GameHUD keep the designer's size.
	 */
	void SetBarSize(float InWidth, float InHeight);

	/** 0..1, clamped (NaN is 0). Hides the fill at 0. Safe before the tree exists. */
	void SetFraction(float InFraction);
	float GetFraction() const { return Fraction; }
	/** The wash over the fill (shield), 0..1; 0 collapses it. */
	void SetOverlayFraction(float InFraction);
	float GetOverlayFraction() const { return OverlayFraction; }
	void SetFillColour(const FLinearColor& Colour);
	void SetLabel(const FString& Text);
	void SetLabelColour(const FLinearColor& Colour);
	void SetLabelVisible(bool bVisible);

	/**
	 * B-15 Wave 4 frame art for the code-built bar: T_UI_BarFrame as a nine-slice
	 * frame and T_UI_BarFill as the fill (tinted by the fill colour). No-op for a
	 * null texture or a designer tree.
	 */
	void SetFrameArt(UTexture2D* FrameTexture, UTexture2D* FillTexture);

	bool HasDesignerTree() const { return bDesignerTree; }

	/** The clamp SetFraction applies: [0, 1], NaN -> 0. */
	static float ClampFraction(float InFraction);

	/** Designer tree with the SizeBox fill: the fill's width at fraction 1, px. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|HUD", meta = (ClampMin = "1"))
	float BarWidth = 200.f;

protected:
	//~ Begin UUserWidget interface
	virtual void NativeOnInitialized() override;
	//~ End UUserWidget interface

	// ── Designer parts (all BindWidgetOptional) ──
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Frame;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Background;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> Sizer;
	/** Fill as a SizeBox (width = fraction x BarWidth) holding the Fill border. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> FillSizer;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Fill;
	/** Fill as a progress bar (percent = fraction, fill colour = the bar colour). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> FillBar;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> OverlaySizer;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> OverlayFill;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> OverlayBar;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Label;

private:
	/** Build the code tree unless a designer tree exists. Idempotent. */
	void EnsureTree();
	float FillWidth() const;

	float Width = 200.f;
	float Fraction = 1.f;
	float OverlayFraction = 0.f;
	/** Setup's bWithOverlay: only the HP bar shows the shield wash. */
	bool bOverlayEnabled = false;
	bool bTreeReady = false;
	bool bDesignerTree = false;
};

/** A text button that knows which button it is. See UValhallaCharacterRowButton for why a subclass. */
UCLASS()
class VALHALLAGAME_API UValhallaHUDButton : public UButton
{
	GENERATED_BODY()

public:
	/** B-07 step 2: editable, so a Widget Blueprint HUD places its own Loot All / Close / Accept ... buttons. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|HUD")
	EValhallaHUDButton Action = EValhallaHUDButton::None;
	/** The combat-log filter's index for LogFilter; unused by the other actions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Valhalla|HUD")
	int32 Index = INDEX_NONE;
	TWeakObjectPtr<UValhallaGameHUDWidget> Hud;

	UFUNCTION()
	void HandleClicked();
};

/** B-21: how a movable panel takes a user size. */
enum class EValhallaPanelSizing : uint8
{
	/** Fixed content (cells, bars): the player scales it (render scale); Size is ignored. */
	Scaled,
	/** A Size Box with Width and Height Overrides takes Size (the combat log). */
	FlowingBox,
	/** A Size Box with a Width Override and a Max Desired Height takes Size (chat, skills). */
	FlowingMaxHeight,
};

/**
 * B-21: one panel the player may move, size, scale and hide — an entry of
 * UValhallaGameHUDWidget::GetMovablePanels. `Member` is the BindWidget(Optional)
 * member naming it; what moves is that widget's root-canvas child (the member
 * itself, or the designer's frame round it: ActionBarRow sits in
 * ActionBarFrame, CastBar in CastBarSize).
 */
struct FValhallaMovablePanel
{
	/** The settings key (FValhallaUserUISettings::Panels). */
	FName Key;
	FName Member;
	EValhallaPanelSizing Sizing = EValhallaPanelSizing::Scaled;
	/** The Size Box a flowing panel's Size goes to (a designer widget name). */
	FName SizeBox;
	/** Always-on HUD parts may be hidden; windows the player opens (loot, skills, I) may not. */
	bool bHideable = false;
};

/**
 * B-21 step 5: one colour the player may change (options menu, Colours tab).
 * `Key` is the "Valhalla|HUD Style" property it overrides and the
 * FValhallaUserUISettings::Colours key.
 */
struct FValhallaStyleColourKey
{
	FName Key;
	const TCHAR* Label = TEXT("");
};

/**
 * B-21 step 3: the edit-mode handle over one movable panel. While the HUD is
 * unlocked (FValhallaUserUISettings::bLocked false) the HUD puts one on its
 * root canvas over every movable panel, the size of what the panel draws: a
 * faint outline in the Highlight colour, the panel's name and a 14 x 14 grip
 * bottom right. A left press on the body starts a move, on the grip a resize;
 * both capture the mouse and report to the HUD (BeginPanelDrag /
 * UpdatePanelDrag / EndPanelDrag), which moves the panel live and saves it on
 * release. The body stops eating clicks while the cursor is over one of the
 * panel's buttons, check boxes, sliders or text boxes (IsInteractiveChild,
 * checked every tick), so those keep working; cells and scroll boxes are
 * dragged (the panel wins). Locked, there are no overlays at all.
 */
UCLASS()
class VALHALLAGAME_API UValhallaPanelEditOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	FName PanelKey;
	TWeakObjectPtr<UValhallaGameHUDWidget> Hud;

	/** Build the tree (outline, label, grip) in the given colour. */
	void Setup(FName InKey, const FLinearColor& Highlight);
	void SetHighlight(const FLinearColor& Highlight);
	/** False: the body lets clicks through to the panel (the grip still takes them). */
	void SetBodyHitTestable(bool bHitTestable);
	bool IsBodyHitTestable() const { return bBodyHitTestable; }

	/** The grip's side, px; the hit area is a little larger. */
	static constexpr float GripSize = 14.f;

protected:
	//~ Begin UUserWidget interface
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	//~ End UUserWidget interface

private:
	void EnsureTree();

	UPROPERTY(Transient) TObjectPtr<UBorder> Outline;
	UPROPERTY(Transient) TObjectPtr<UBorder> Grip;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
	bool bTreeReady = false;
	bool bDragging = false;
	bool bBodyHitTestable = true;
};

/** One combat-log line, already worded and coloured, with its 1.0 filter key. */
struct FValhallaCombatLogLine
{
	FString Text;
	FLinearColor Colour = FLinearColor::White;
	FName Filter;
};

/**
 * The Phase 8b HUD.
 *
 * ## Layout: a Widget Blueprint (B-07)
 *
 * A Widget Blueprint child (WBP_GameHUD, set as Project Settings > Valhalla >
 * UI > Game HUD Class) owns the layout: its root must be a Canvas
 * Panel, and its widgets bind by name to the `meta = (BindWidget)` /
 * `(BindWidgetOptional)` members below (VitalsPanel, HpBar, ActionBarRow, ...;
 * the list is in the header, grouped by panel). C++ still fills and runs every
 * panel: it adds the action, inventory, equipment, loot and party cells and
 * the buff tokens to the designer's containers (leave those empty in the
 * designer), writes every text and bar, toggles panel visibility, and builds
 * the nameplate / floater layer and the combat-log filter menu itself.
 * Buttons are UValhallaHUDButton widgets with their Action set in the
 * designer. A missing optional part is a hidden stand-in (a warning, once) so
 * that feature just does not show. The sizes of the cells C++ makes and the
 * colours C++ applies at runtime (HP thresholds, mana / energy, cooldowns,
 * chat channels, ...) are the "Valhalla|HUD Style" properties below: the
 * Blueprint's Class Defaults. The code-built layout is gone (B-07 step 4): with
 * no Blueprint, an empty one, or one missing a required widget, the HUD logs an
 * error saying to set the project setting and runs blank but for nameplates.
 *
 * Rebuild() empties what C++ added and fills the designer's containers again;
 * `valhalla.ReloadUI` and the ui-config.json timestamp poll call it, so a UI
 * Layout editor save (inventory grid, chat lines, nameplates) shows up in a
 * running PIE session.
 *
 * ## Player layout (B-21)
 *
 * The player's UI settings (UValhallaUserSettingsSubsystem, per character,
 * synced through the backend) go over the designer's layout. The movable
 * panels are GetMovablePanels(): each is a root-canvas child whose designer
 * slot (anchors, alignment, position), Size Box and background colours are
 * recorded once (CaptureDesignerDefaults, the Reset values), and
 * ApplyUserLayout sets them from the settings after every build and on every
 * change (OnChanged). Flowing panels (chat, combat log, skills) take a size on
 * their Size Box; the others scale. UiScale multiplies every panel's position
 * and render scale (about its alignment point), which is what a DPI change
 * does to point-anchored canvas children, without touching the engine's DPI
 * curve or the front end; PanelOpacity scales the backgrounds' brush alpha
 * only, so text stays readable. `valhalla.UI settings | resetlayout | panels |
 * movepanel ...` drive it from the console.
 *
 * Edit mode (B-21 step 3): with the settings unlocked (the options menu's
 * Lock box, `valhalla.UI lock 0`) every movable panel gets a
 * UValhallaPanelEditOverlay: drag the body to move it, the bottom-right grip
 * to resize it (flowing panels: their Size Box; the others: Scale 0.5 .. 2).
 * Panels the game or the player has hidden are shown faintly so they can be
 * placed. While the drag runs the panel is pinned by its drawn top-left,
 * snapped to a 4 px grid and to the canvas edges within 8 px; on release it is
 * re-anchored to the nearest of the nine anchor points (ChooseAnchor,
 * AnchorLayoutForRect), so it keeps its place when the window changes size,
 * and saved. Unlocked, the overlay wins over a panel's cells (no cell drag
 * and drop, no tooltips) and scroll boxes (the wheel is passed on); its
 * buttons, check boxes, sliders and text boxes still take clicks. Locked
 * (the default) there are no overlays and nothing changes.
 *
 * Options menu (B-21 step 4): UValhallaOptionsMenuWidget (WBP_OptionsMenu,
 * OptionsMenuClass), made once and centred on the root canvas; Escape opens it
 * when CloseTopmost had nothing to close and closes it first when it is open;
 * the OptionsButton cog toggles it. Style (step 5): ApplyUserStyle.
 *
 * ## Input
 *
 * The HUD canvas is SelfHitTestInvisible: a click on bare canvas falls through
 * to the viewport and reaches the world, a click on a panel does not. Opening
 * the chat box switches the controller to UI-only input with the text box
 * focused, which is what stops WASD walking the character while you type;
 * closing it switches back. A designer tree gets the same treatment from C++
 * after binding (ApplyClickThrough): layout panels and panels with nothing
 * clickable in them stop eating clicks, whatever the designer left set.
 */
UCLASS(Blueprintable, BlueprintType)
class VALHALLAGAME_API UValhallaGameHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UValhallaGameHUDWidget(const FObjectInitializer& ObjectInitializer);

	//~ Begin UUserWidget interface
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~ End UUserWidget interface

	/**
	 * Whether this instance lays out from its Widget Blueprint: a subclass (not
	 * this C++ class itself), a Canvas Panel root, and every required
	 * BindWidget member bound. False on the C++ class and its CDO.
	 */
	bool WantsLayoutFromBlueprint() const;
	/** What NativeOnInitialized decided (WantsLayoutFromBlueprint at that time). */
	bool IsLayoutFromBlueprint() const { return bLayoutFromBlueprint; }

	TSubclassOf<UValhallaHUDSlotWidget> GetSlotWidgetClass() const { return SlotWidgetClass; }
	TSubclassOf<UValhallaHUDBarWidget> GetBarWidgetClass() const { return BarWidgetClass; }

	/** The BindWidget (required) member names, for tests and the editor tools. */
	static const TArray<FName>& GetRequiredPanelNames();
	/** The BindWidgetOptional member names. */
	static const TArray<FName>& GetOptionalPanelNames();

	/** Re-read the config from the data subsystem and rebuild every panel. */
	void Rebuild();

	// ── Player layout (B-21) ────────────────────────────────────────────

	/** The panels the player may move: key, BindWidget member, sizing, hideable. */
	static const TArray<FValhallaMovablePanel>& GetMovablePanels();
	static const FValhallaMovablePanel* FindMovablePanel(FName Key);

	/**
	 * Lay the movable panels out from `Settings` over the designer's layout
	 * (CaptureDesignerDefaults): each bSet panel takes its anchors, alignment,
	 * position, size (flowing panels' Size Box) and visibility; every panel
	 * takes UiScale (position x UiScale, render scale UiScale x Scale about its
	 * alignment point: what a DPI change would do) and PanelOpacity (its
	 * background borders' brush alpha; text and icons untouched). Settings
	 * with nothing set put back exactly the designer's layout. Returns how many
	 * panels a bSet entry moved; 0 (and nothing touched) before a layout exists.
	 */
	int32 ApplyUserLayout(const FValhallaUserUISettings& Settings);

	/**
	 * B-21 step 5: the player's style, live, no rebuild. Colours (a key of
	 * GetStyleColourKeys with an override) replace the "Valhalla|HUD Style"
	 * defaults everywhere the HUD reads them (the Effective*Colour getters):
	 * bars and nameplates on their next tick, cells' highlight and key labels,
	 * party names, the cast bar and the chat lines (redrawn) at once;
	 * PanelTintColour multiplies the movable panels' backgrounds. Chat font
	 * size, idle lines and timestamps redraw the chat; LogFilters become the
	 * combat log's filters; the nameplate / player-plate / floating-text
	 * switches and the nameplate font size restyle the pooled world layer.
	 * Safe on a HUD with no layout (it only records the values then).
	 */
	void ApplyUserStyle(const FValhallaUserUISettings& Settings);

	/** The colours the options menu offers, in menu order. */
	static const TArray<FValhallaStyleColourKey>& GetStyleColourKeys();
	static const FValhallaStyleColourKey* FindStyleColourKey(FName Key);
	/** The HUD Style property `Key` (HpHighColour, ...) as this class's defaults have it; magenta for an unknown key. */
	FLinearColor GetDefaultColour(FName Key) const;
	/** The player's override for `Key` when ApplyUserStyle recorded one, else GetDefaultColour. */
	FLinearColor GetEffectiveColour(FName Key) const;

	// ── Edit mode (B-21 step 3) ─────────────────────────────────────────

	/** True while the player's settings say unlocked: the edit overlays are up. */
	bool IsEditMode() const { return bEditMode; }
	/**
	 * Start moving (or, `bResize`, resizing from the grip) the movable panel
	 * `Key` from `CanvasPoint` (root-canvas units). The panel is pinned by its
	 * drawn top-left while it moves. False when it is not on the canvas.
	 */
	bool BeginPanelDrag(FName Key, const FVector2D& CanvasPoint, bool bResize);
	/** The pointer is now at `CanvasPoint`: move (snapped) or resize the panel live. */
	void UpdatePanelDrag(const FVector2D& CanvasPoint);
	/**
	 * Release: two ticks later (once Slate has laid the panel out) its drawn
	 * rectangle is re-anchored to the nearest of the nine anchor points and
	 * saved as its FValhallaPanelLayout (bSet) through the settings subsystem.
	 * `bCommit` false, or a press that never moved, puts it back.
	 */
	void EndPanelDrag(bool bCommit = true);
	bool IsDraggingPanel() const { return !DragKey.IsNone(); }
	/** Mouse wheel over a panel in edit mode: scroll the scroll box under the cursor. */
	void ScrollPanelAt(FName Key, const FVector2D& ScreenPosition, float WheelDelta);
	/** `valhalla.UI dragtest`: press at the panel's centre (or its grip), move by `Delta` canvas units, release. */
	bool DragPanelForTest(FName Key, const FVector2D& Delta, bool bResize);
	/** Absolute (desktop) pixels -> root-canvas units. */
	FVector2D AbsoluteToCanvas(const FVector2D& Absolute) const;
	/** The movable panel's drawn rectangle in root-canvas units (render scale included); empty when unknown. */
	FSlateRect GetPanelCanvasRect(FName Key) const;
	FVector2D GetCanvasSize() const;

	/**
	 * The anchor point (and alignment: the same) a panel drawn at `Rect` on a
	 * `Viewport`-sized canvas is kept at: per axis, whichever of the near edge,
	 * the centre and the far edge its own edge / centre is closest to. Pure.
	 */
	static FVector2D ChooseAnchor(const FSlateRect& Rect, const FVector2D& Viewport);
	/**
	 * Where a panel of `Size` dragged to `TopLeft` lands: on a `Grid` px grid,
	 * against a canvas edge when within `EdgeSnap` px of it, and kept on the
	 * canvas. Pure.
	 */
	static FVector2D SnapPanelPosition(const FVector2D& TopLeft, const FVector2D& Size, const FVector2D& Viewport,
		float Grid = 4.f, float EdgeSnap = 8.f);
	/**
	 * The layout (anchors = alignment = ChooseAnchor, Position at UiScale 1,
	 * bSet) that ResolvePanelLayout + the canvas put back at exactly `Rect`
	 * whatever its render scale: the alignment point of the drawn rectangle is
	 * `anchor x canvas + Position x UiScale`. Pure.
	 */
	static FValhallaPanelLayout AnchorLayoutForRect(const FSlateRect& Rect, const FVector2D& Viewport, float UiScale);
	/** Widgets a press must reach even while the HUD is unlocked (buttons, check boxes, sliders, text boxes, combos). */
	static bool IsInteractiveChild(const UWidget* Widget);

	static constexpr float SnapGrid = 4.f;
	static constexpr float EdgeSnapDistance = 8.f;
	static constexpr float MinPanelScale = 0.5f;
	static constexpr float MaxPanelScale = 2.f;

	// ── Options menu (B-21 step 4) ──────────────────────────────────────

	/** Create the menu once (OptionsMenuClass), centred on the root canvas; show it. Input mode is unchanged. */
	void OpenOptions();
	void CloseOptions();
	void ToggleOptions();
	bool IsOptionsOpen() const;
	UValhallaOptionsMenuWidget* GetOptionsMenu() const;
	UClass* GetOptionsMenuClass() const;

	/** A combat-log filter's menu label ("Your damage", ...), or the key. */
	static FString GetLogFilterLabel(FName Key);
	bool IsLogFilterOn(FName Key) const;
	/** Set one combat-log filter and save it (LogFilters). False when the key is unknown. */
	bool SetLogFilter(FName Key, bool bOn);

	/**
	 * The canvas values a panel is given: the designer's (`Designer`), or the
	 * player's (`User`, when non-null and bSet); Position is multiplied by
	 * UiScale, Scale is UiScale x the panel's own, a zero Size axis keeps the
	 * designer's. Pure, for ApplyUserLayout and its test.
	 */
	static FValhallaPanelLayout ResolvePanelLayout(const FValhallaPanelLayout& Designer, const FValhallaPanelLayout* User, float UiScale);

	/** The designer's layout of a movable panel (bSet false), or null before CaptureDesignerDefaults. */
	const FValhallaPanelLayout* GetDesignerLayout(FName Key) const;

	/** `valhalla.UI panels`: each movable panel's canvas widget, slot and desired size, to the log. */
	void LogPanelGeometry() const;

	/** Re-read ui-config.json from disk, then Rebuild. `valhalla.ReloadUI`. */
	void ReloadFromDisk();

	// ── Panel toggles (input actions, console) ──────────────────────────
	void ToggleInventory();
	void ToggleSkills();
	void OpenChat(const FString& Prefill = FString());
	void CloseChat();
	bool IsChatOpen() const { return bChatOpen; }
	/** Escape: close the topmost thing that is open. True when something closed. */
	bool CloseTopmost();
	void OpenLootPanel(AValhallaLootBag* Bag);
	void CloseLootPanel();
	bool IsLootPanelOpen() const;

	/** Submit a line exactly as pressing Enter in the chat box does. */
	void SubmitChatLine(const FString& Line);

	/** Arm a skill for click-to-assign, as clicking it in the skills pane does. */
	void ArmSkill(FName SkillId);

	/** Toggle one combat-log filter by its 1.0 key ("outDmg", "misses", …). False when unknown. */
	bool ToggleLogFilter(FName Key);

	/** Pin a tooltip for an inventory index, for the gate screenshot. */
	void PinInventoryTooltip(int32 InventoryIndex);

	// ── Called by the cells and buttons ─────────────────────────────────
	void HandleSlotClicked(UValhallaHUDSlotWidget* Cell);
	void HandleSlotRightClicked(UValhallaHUDSlotWidget* Cell, const FVector2D& ScreenPosition);
	void HandleSlotHovered(UValhallaHUDSlotWidget* Cell, bool bHovered);
	void HandleSlotDropped(UValhallaHUDSlotWidget* Source, UValhallaHUDSlotWidget* Target);
	/** An action bar cell dragged off the bar and let go anywhere but another bar cell: the slot is cleared. */
	void HandleSlotDraggedOff(UValhallaHUDSlotWidget* Source);
	/** True when a screen point is over the action bar row; a drag let go there (between cells) is not off the bar. */
	bool IsOverActionBar(const FVector2D& ScreenPosition) const;
	/** The system line for a skill that cannot go on the bar: "You must be level N to use <skill>." */
	void ExplainSkillNotPlaceable(FName SkillId);
	void HandleButton(EValhallaHUDButton Action, int32 Index);

	/** The 1.0 combat-log filter keys, in menu order. */
	static const TArray<FName>& GetLogFilterKeys();

	/** How many lines the combat log keeps. GameScene's CL_MAX. */
	static constexpr int32 CombatLogMaxLines = 50;

private:
	// ── Context ─────────────────────────────────────────────────────────
	AValhallaPlayerController* GetValhallaController() const;
	AValhallaPlayerState* GetValhallaPlayerState() const;
	AValhallaCharacter* GetValhallaPawn() const;
	const class UValhallaDataSubsystem* GetData() const;

	// ── Build ───────────────────────────────────────────────────────────
	void BuildAll();
	/** Stand-ins for missing parts, hook up designer buttons, cells and the chat box. */
	void BindDesignerPanels();
	/** Fill the designer's containers (cells, rows, tokens) and build the world layer / filter menu. */
	void PopulateDesignerPanels();
	/** B-21: once per HUD, record each movable panel's designer canvas slot, size box and backgrounds (the Reset values). */
	void CaptureDesignerDefaults();
	/** B-21: collapse the panels the player hid, after the Tick code has set its own visibility. */
	void EnforceUserHiddenPanels();
	/** B-21: the settings subsystem's OnChanged. */
	void HandleUserSettingsChanged(const FValhallaUserUISettings& Settings);
	/** ChatOpenBackground at the player's PanelOpacity. */
	FLinearColor ChatOpenBrush() const;
	/** B-21: the movable panels' backgrounds: designer colour x PanelTintColour, alpha x PanelOpacity. */
	void ApplyPanelBackgrounds();
	/** B-21 step 3: put up (unlocked) or take down (locked) the edit overlays. */
	void SetEditMode(bool bOn);
	/** B-21 step 3: every tick: a pending drag's commit; unlocked, the overlays' rectangles and hit-testing, ghosted panels. */
	void TickEditMode();
	/** Whether the game itself would show this panel now (edit mode shows the rest as ghosts). */
	bool IsPanelWantedByGame(FName Key, const UWidget* Widget) const;
	/** The drag's result, saved: re-anchored layout, size or scale. */
	void CommitPanelDrag();
	/** The Size Box values a flowing panel is showing (width, height or max height). */
	FVector2D CurrentBoxSize(FName Key) const;
	/** Minimum content size of a flowing panel while it is resized. */
	static FVector2D MinFlowingSize(FName Key);

	// B-21 step 5: the colours the HUD applies (the player's override, else the class default).
	FLinearColor StyleColour(FName Key, const FLinearColor& Default) const;
	FLinearColor EffectiveHpHighColour() const;
	FLinearColor EffectiveHpMidColour() const;
	FLinearColor EffectiveHpLowColour() const;
	FLinearColor EffectiveManaColour() const;
	FLinearColor EffectiveEnergyColour() const;
	FLinearColor EffectiveCastBarColour() const;
	FLinearColor EffectiveHighlightColour() const;
	FLinearColor EffectiveKeyLabelColour() const;
	FLinearColor EffectiveLabelColour() const;
	FLinearColor EffectiveValueColour() const;
	FLinearColor EffectivePanelTint() const;
	/** HP bar fill by fraction: high over half, mid over a quarter, low below. */
	FLinearColor HpColourFor(float Fraction) const;
	int32 EffectiveChatFontSize() const;
	int32 EffectiveChatVisibleLines() const;
	int32 EffectiveNameplateFontPx() const;
	/** Nameplate font (ui-config's weight and stroke) at EffectiveNameplateFontPx. */
	void RestyleNameplates();
	/** Layout panels, and panels with nothing clickable, stop eating clicks. Returns true when `Widget` is interactive. */
	bool ApplyClickThrough(UWidget* Widget);
	/** Hidden, unparented stand-in for a designer part the Blueprint lacks; warns once per name (not without a layout). */
	template <typename T>
	void EnsureDesignerPart(TObjectPtr<T>& Member, const TCHAR* Name, UClass* ConcreteClass = nullptr);
	/** Null every designer-bound member (an incomplete Blueprint's tree is dropped). */
	void ResetPanelPointers();
	// The runtime-made children of a designer panel.
	void AddActionCells(UPanelWidget* Row);
	void AddTargetBuffTokens(UPanelWidget* Row);
	void AddPartyRows(UPanelWidget* Column);
	void AddLootCells(UPanelWidget* Grid);
	void AddEquipRows(UPanelWidget* Column);
	void AddInventoryCells(UUniformGridPanel* Grid);
	void BuildLogFilterMenu(const FVector2D& Position);
	void SetInventoryShown(bool bShown);
	/** Nameplates and floating text: C++'s own layer under the designer's panels, styled from ui-config `nameplates`. */
	void BuildWorldLayer();

	// ── Tick ────────────────────────────────────────────────────────────
	void TickVitals();
	void TickActionBar();
	void TickCastBar();
	void TickTargetFrame();
	void TickPartyFrame();
	void TickChat(float DeltaTime);
	void TickLootPanel();
	void TickInventoryPanel();
	void TickTooltip();
	void TickDeathOverlay();
	void TickWorldLayer(float DeltaTime);
	void TickConfigWatcher(float DeltaTime);
	/**
	 * Keep the chat box clear of the action bar. WBP_GameHUD places the chat at
	 * the vitals' right edge with a fixed width, which on a narrow viewport (a
	 * 640 px PIE client, or any window under ~1450 Slate units) runs under the
	 * centred action bar. Narrow its Size Box to the gap, or lift it above the
	 * vitals when the gap is too small to read. Re-run only when the width
	 * changes; only for a bottom-left anchored chat and vitals. B-21: not at
	 * all once the player has placed the chat (Panels.Chat.bSet); the sizes it
	 * measures are taken at the panels' applied render scale.
	 */
	void TickLayout();

	// ── Events ──────────────────────────────────────────────────────────
	void BindCombatEvents();
	void HandleCombatEvent(const FValhallaCombatEvent& Event);
	void PushCombatLog(const FString& Text, const FLinearColor& Colour, FName Filter);
	void RefreshCombatLog();
	void SpawnFloater(const FValhallaCombatEvent& Event);
	void RefreshSkillsPane();
	void RefreshChatLines();

	UFUNCTION()
	void HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	// ── Helpers ─────────────────────────────────────────────────────────
	UTexture2D* FindItemIcon(FName ItemId);
	/** Import/UI/Icons/Skills/<id>.png (imported: /Game/Valhalla/UI/Icons/Skills). Null keeps the generated tile. */
	UTexture2D* FindSkillIcon(FName SkillId);
	/** A HUD frame texture (T_UI_Panel, T_UI_Slot...): /Game/Valhalla/UI/Frames, else Import/UI/Frames on disk. */
	UTexture2D* FindUiTexture(const FString& Name);
	UTexture2D* LoadUiTexture(const FString& AssetFolder, const FString& DiskFolder, const FString& Name, bool bPixelArtFilter);
	void ShowItemTooltip(FName ItemId, int32 Quantity);
	void HideTooltip();
	void RequestDrop(EValhallaHUDSlotKind Kind, int32 Index);
	FLinearColor ChatColour(EValhallaChatChannel Channel) const;
	FString FormatChatLine(const FValhallaChatMessage& Line) const;
	void SetInputForTyping(bool bTyping);

	/** A resource bar: a BarWidgetClass instance, Setup and SetBarSize with these numbers. */
	UValhallaHUDBarWidget* MakeBar(float Width, float Height, const FLinearColor& Fill, const FLinearColor& Background, float BackgroundAlpha, bool bWithOverlay = false, int32 FontSize = 9);

	UTextBlock* MakeText(const FString& Content, int32 Size, const FLinearColor& Colour, bool bBold = false, float Outline = 1.f);
	/** `bFramed`: draw T_UI_Panel (leather and bronze) when it exists and the panel is visible (Alpha > 0). */
	UBorder* MakePanel(const FLinearColor& Colour, float Alpha, float PanelPadding, bool bFramed = true);
	UValhallaHUDButton* MakeButton(const FString& Caption, EValhallaHUDButton Action, int32 Index, const FLinearColor& Tint);
	UValhallaHUDSlotWidget* MakeCell(EValhallaHUDSlotKind Kind, int32 Index, float Size);
	class UCanvasPanelSlot* Place(UWidget* Widget, const FVector2D& Anchor, const FVector2D& Alignment, const FVector2D& Position, int32 ZOrder = 0);

protected:
	// ── Classes C++ makes cells and bars from ──────────────────────────
	/** The action / inventory / equipment / loot / skill cell. A WBP_HUDSlot child restyles every cell. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD")
	TSubclassOf<UValhallaHUDSlotWidget> SlotWidgetClass;
	/** Every bar C++ makes (party, nameplates). A WBP_HUDBar child restyles them; SetBarSize sizes them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD")
	TSubclassOf<UValhallaHUDBarWidget> BarWidgetClass;

	// ── Style (B-07 step 4: these were ui-config.json fields) ──────────
	// The Blueprint's Class Defaults: sizes of the cells C++ makes and the
	// colours C++ applies while the game runs. Defaults are 1.0's values.

	/** Action bar cells, px square (was actionBar.slotSize). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style", meta = (ClampMin = "8"))
	float ActionSlotSize = 44.f;
	/** Space between action bar cells, px (was actionBar.slotGap). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style", meta = (ClampMin = "0"))
	float ActionSlotGap = 4.f;
	/** Inventory and loot cells, px square (was inventory.slotSize). The grids' spacing is the designer's Slot Padding. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style", meta = (ClampMin = "8"))
	float InventorySlotSize = 48.f;
	/** Equipment slots on the character panel, px square. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style", meta = (ClampMin = "8"))
	float EquipSlotSize = 26.f;
	/** Skill icons in the skills pane, px square. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style", meta = (ClampMin = "8"))
	float SkillSlotSize = 36.f;

	/** HP fill over half (HP, target, party, friendly nameplates; was hud.hpBar.colors). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor HpHighColour;
	/** HP fill over a quarter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor HpMidColour;
	/** HP fill below a quarter, and hostile nameplates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor HpLowColour;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor ManaColour;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor EnergyColour;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor CastBarColour;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor CastBarTextColour;
	/** The cooldown sweep over an action cell, alpha included. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor CooldownColour;
	/** The action cells' 1-8 key labels. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor KeyLabelColour;
	/** A selected / armed cell's rim. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor HighlightColour;
	/** Secondary text C++ writes: equipment slot names, skill details. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor LabelColour;
	/** Primary text C++ writes: party names, skill names. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor ValueColour;
	/** Chat lines, Slate points (ui-config's 11px). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style", meta = (ClampMin = "6"))
	int32 ChatFontSize = 8;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor ChatGeneralColour;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor ChatWorldColour;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor ChatWhisperColour;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor ChatPartyColour;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor ChatSystemColour;
	/** ChatPanel's background while typing (idle it is clear), alpha included. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor ChatOpenBackground;
	/** B-21: multiplies the movable panels' background brushes (white = the designer's colours). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD Style")
	FLinearColor PanelTintColour;

	/** B-21 step 4: the options menu (Escape with nothing open, the cog). WBP_OptionsMenu in WBP_GameHUD's Class Defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD")
	TSubclassOf<UValhallaOptionsMenuWidget> OptionsMenuClass;

	// ── Designer panels (B-07) ─────────────────────────────────────────
	// A WBP_GameHUD child binds its widgets to these by name. BindWidget is
	// required (the Blueprint will not compile without it); BindWidgetOptional
	// may be left out and that feature is hidden.
	// "leave empty": C++ adds that container's children at runtime.

	// vitals (required)
	/** The vitals block (HP, mana/energy, class line). Required. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidget))
	TObjectPtr<UPanelWidget> VitalsPanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidget))
	TObjectPtr<UValhallaHUDBarWidget> HpBar;
	/** Mana, or energy for an energy class; collapsed for neither. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidget))
	TObjectPtr<UValhallaHUDBarWidget> ManaBar;
	/** "Name Lv.n Class  HP: ...". */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ClassText;

	// action bar (required)
	/** The eight action cells go in here (a Horizontal Box; leave empty). Required. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidget))
	TObjectPtr<UPanelWidget> ActionBarRow;

	// cast bar
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UValhallaHUDBarWidget> CastBar;

	// target frame
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> TargetFramePanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TargetName;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UValhallaHUDBarWidget> TargetHpBar;
	/** Eight buff tokens go in here (a Horizontal Box; leave empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> TargetBuffs;

	// party
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> PartyPanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PartyTitle;
	/** One row per member (name + HP bar) goes in here (a Vertical Box; leave empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> PartyList;
	/** The "x invites you" prompt; Accept / Decline are UValhallaHUDButtons. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> InvitePanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InviteText;

	// combat log
	/** Right-click inside it opens the filter menu. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CombatLogPanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CombatLogTitle;
	/** The log lines go in here (leave empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> CombatLogScroll;

	// chat (required)
	/** The chat box's background: transparent while idle, ChatOpenBackground while typing. Holds a Size Box (TickLayout, OpenChat). Required. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidget))
	TObjectPtr<UBorder> ChatPanel;
	/** The chat lines go in here (leave empty). Required. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidget))
	TObjectPtr<UScrollBox> ChatScroll;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidget))
	TObjectPtr<UEditableTextBox> ChatInput;
	/** "[General]" while typing. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ChatChannelText;

	// loot
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> LootPanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LootTitle;
	/** Twelve loot cells go in here (a Uniform Grid Panel, 4 wide, or any panel; leave empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> LootGrid;

	// skills
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SkillsPanel;
	/** One row per class skill goes in here (leave empty; inside a Scroll Box). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> SkillsList;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SkillsHint;

	// character + inventory (I)
	/** Shown and hidden with InventoryPanel. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CharacterPanel;
	/** One row per equipment slot (cell + label) goes in here (a Vertical Box; leave empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> EquipmentPanel;
	/** B-07: "Level n   XP x / y (z%)" above XpBar, then CharacterStats below it. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CharacterLevel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UValhallaHUDBarWidget> XpBar;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CharacterStats;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> InventoryPanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InventoryTitle;
	/** inventory.cols x rows cells go in here (leave empty). A Uniform Grid Panel: cell i at row i / cols, column i % cols. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UUniformGridPanel> InventoryGrid;

	// tooltip, drop confirm, death
	/** Must sit directly on the root canvas: C++ moves it to the cursor. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> TooltipPanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TooltipName;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TooltipBody;
	/** "Drop x on the ground?"; Drop / Cancel are UValhallaHUDButtons. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> DropConfirmPanel;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DropText;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> DeathOverlay;
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DeathText;

	// options (B-21 step 4)
	/** The cog bottom right: a UValhallaHUDButton with Action = Options (toggles the options menu). */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UValhallaHUDButton> OptionsButton;

private:
	// ── State ───────────────────────────────────────────────────────────
	FValhallaUIConfig Config;

	/** Set once in NativeOnInitialized: the designer tree lays the HUD out. */
	bool bLayoutFromBlueprint = false;
	/** Optional designer parts already warned about. */
	TSet<FName> WarnedMissingParts;

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> WorldLayer;

	// action bar
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDSlotWidget>> ActionCells;
	/** ActionBarRow (TickLayout measures the canvas panel holding it). */
	UPROPERTY(Transient) TObjectPtr<UWidget> ActionBarRoot;

	// cast bar
	UPROPERTY(Transient) TObjectPtr<UWidget> CastBarRoot;

	// target frame
	UPROPERTY(Transient) TObjectPtr<UWidget> TargetRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> TargetBuffTokens;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TargetBuffTexts;

	// party
	UPROPERTY(Transient) TObjectPtr<UWidget> PartyRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PartyRows;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PartyNames;
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDBarWidget>> PartyBars;
	UPROPERTY(Transient) TObjectPtr<UWidget> InviteRoot;

	// combat log
	UPROPERTY(Transient) TObjectPtr<UWidget> FilterMenu;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> FilterLabels;
	TArray<FValhallaCombatLogLine> CombatLogLines;
	TMap<FName, bool> LogFilters;
	bool bCombatLogDirty = true;

	// chat
	/** ChatPanel's Size Box: TickLayout narrows it, OpenChat sets its height. */
	UPROPERTY(Transient) TObjectPtr<USizeBox> ChatSizer;
	/** The canvas width TickLayout last laid the chat out for. */
	float LayoutForWidth = -1.f;
	/** The designer's chat placement, read once (TickLayout moves it later). */
	bool bChatDesignKnown = false;
	FVector2D ChatDesignPosition = FVector2D::ZeroVector;
	float ChatDesignWidth = 0.f;
	/** The chat Size Box's Max Desired Height: its height while typing (0 = grow with the lines). */
	float ChatOpenHeight = 0.f;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ChatLineWidgets;
	EValhallaChatChannel ChatChannel = EValhallaChatChannel::General;
	bool bChatOpen = false;
	int32 ChatSeenCount = -1;
	/** Local arrival time of each ChatLog line, for the idle fade. */
	TArray<double> ChatArrivalTimes;
	int32 ChatLastLogNum = 0;
	FValhallaChatMessage ChatLastLine;

	// loot
	UPROPERTY(Transient) TObjectPtr<UWidget> LootRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDSlotWidget>> LootCells;
	TWeakObjectPtr<AValhallaLootBag> LootBag;

	// skills
	UPROPERTY(Transient) TObjectPtr<UWidget> SkillsRoot;
	FName ArmedSkill;
	FName SkillsBuiltForClass;
	int32 SkillsBuiltForLevel = -1;

	// inventory
	/** What ToggleInventory shows: InventoryPanel (CharacterPanel follows it). */
	UPROPERTY(Transient) TObjectPtr<UWidget> InventoryRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDSlotWidget>> InventoryCells;
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDSlotWidget>> EquipCells;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EquipLabels;

	// tooltip
	UPROPERTY(Transient) TObjectPtr<UWidget> TooltipRoot;
	bool bTooltipPinned = false;

	// drop confirm
	UPROPERTY(Transient) TObjectPtr<UWidget> DropRoot;
	EValhallaHUDSlotKind DropKind = EValhallaHUDSlotKind::None;
	int32 DropIndex = INDEX_NONE;

	// death
	UPROPERTY(Transient) TObjectPtr<UWidget> DeathRoot;
	bool bWasAlive = true;
	double DeathStartedAt = 0.0;

	// world layer: nameplates and floaters, pooled
	struct FPlate
	{
		TObjectPtr<UWidget> Root = nullptr;
		TObjectPtr<UTextBlock> Name = nullptr;
		TObjectPtr<UValhallaHUDBarWidget> Bar = nullptr;
	};
	TArray<FPlate> Plates;

	struct FFloater
	{
		TObjectPtr<UTextBlock> Text = nullptr;
		TWeakObjectPtr<AActor> Anchor;
		FVector Location = FVector::ZeroVector;
		double StartedAt = -1.0;
		float Lifetime = 1.2f;
		float XJitter = 0.f;
	};
	TArray<FFloater> Floaters;
	int32 NextFloater = 0;

	/** UPROPERTY holder for the pooled world-layer widgets, so GC sees them. */
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PooledWidgets;

	/** Client-side buff view for player targets, from buffApplied / buffRemoved. */
	struct FSeenBuff
	{
		FName SkillId;
		double ExpiresAt = 0.0;
	};
	TMap<TWeakObjectPtr<AActor>, TArray<FSeenBuff>> SeenBuffs;

	// pending party invite
	FString PendingInviter;

	// icons
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UTexture2D>> IconCache;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UTexture2D>> UiTextureCache;

	// config watcher
	double ConfigWatchAccumulator = 0.0;
	FDateTime ConfigTimestamp;

	// player layout (B-21)
	struct FPanelState
	{
		/** The root-canvas child that moves. */
		TWeakObjectPtr<UWidget> Widget;
		FValhallaPanelLayout Designer;
		ESlateVisibility DesignerVisibility = ESlateVisibility::SelfHitTestInvisible;
		FWidgetTransform DesignerTransform;
		FVector2D DesignerPivot = FVector2D(0.5f, 0.5f);
		TWeakObjectPtr<USizeBox> SizeBox;
		/** The panel's background borders and their designer brush colours (PanelOpacity scales the alpha). */
		TArray<TPair<TWeakObjectPtr<UBorder>, FLinearColor>> Backgrounds;
		bool bUserSet = false;
		bool bUserHidden = false;
		/** Edit mode shows this hidden panel faintly so it can be placed. */
		bool bGhosted = false;
		/** The render scale applied (UiScale x the panel's). */
		float AppliedScale = 1.f;
		/** Edit mode: the panel's buttons, check boxes, ... (the overlay lets presses through to them). */
		TArray<TWeakObjectPtr<UWidget>> Interactive;
	};
	TMap<FName, FPanelState> PanelStates;

	// edit mode (B-21 step 3)
	bool bEditMode = false;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UValhallaPanelEditOverlay>> EditOverlays;
	/** The panel being dragged (None: no drag). */
	FName DragKey;
	bool bDragResize = false;
	bool bDragMoved = false;
	FVector2D DragStartPoint = FVector2D::ZeroVector;
	FSlateRect DragStartRect;
	FVector2D DragTopLeft = FVector2D::ZeroVector;
	/** Render scale at the start (UiScale x the panel's) and the panel's own scale now. */
	float DragStartRenderScale = 1.f;
	float DragPanelScale = 1.f;
	FVector2D DragStartBox = FVector2D::ZeroVector;
	FVector2D DragBox = FVector2D::ZeroVector;
	/** A released drag waits this many ticks for Slate's layout, then CommitPanelDrag. */
	int32 PendingCommitTicks = 0;
	FName PendingCommitKey;
	bool bPendingResize = false;

	// options menu (B-21 step 4)
	UPROPERTY(Transient) TObjectPtr<UValhallaOptionsMenuWidget> OptionsMenu;

	// style (B-21 step 5): the player's overrides, as ApplyUserStyle last recorded them
	TMap<FName, FLinearColor> ColourOverrides;
	int32 UserChatFontSize = 0;
	int32 UserChatVisibleLines = 0;
	bool bChatTimestamps = false;
	bool bShowNpcNameplates = true;
	bool bShowPlayerNameplates = true;
	bool bShowFloatingText = true;
	int32 UserNameplateFontSize = 0;
	/** What the chat / nameplates were last drawn with, so a change elsewhere does not redraw them. */
	FString AppliedChatStyle;
	/** ApplyUserLayout's last log line (a slider drag applies every frame). */
	FString LastLayoutLog;
	int32 AppliedNameplateFontPx = -1;
	/** Wall-clock arrival of each ChatLog line (parallel to ChatArrivalTimes); MinValue = before this HUD. */
	TArray<FDateTime> ChatArrivalClock;
	bool bDesignerDefaultsCaptured = false;
	float AppliedUiScale = 1.f;
	float AppliedPanelOpacity = 1.f;
	/** The chat Size Box's designer width and max height (ChatDesignWidth / ChatOpenHeight when the player has not sized it). */
	FVector2D ChatDesignerSize = FVector2D::ZeroVector;
	FDelegateHandle UserSettingsHandle;

	FDelegateHandle CombatEventHandle;
	TWeakObjectPtr<class AValhallaGameState> BoundGameState;
	bool bBuilt = false;
	bool bInventoryOpen = false;
	bool bSkillsOpen = false;
};
