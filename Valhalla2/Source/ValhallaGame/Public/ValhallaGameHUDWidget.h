// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8b: the game HUD. One UUserWidget, built in C++ from WidgetTree like
// Phase 7b's front end and for the same reason — every binding is reviewable
// source — laid out from `ui-config.json` through FValhallaUIConfig.
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
#include "Components/Button.h"
#include "ValhallaGameTypes.h"
#include "ValhallaInventoryTypes.h"
#include "ValhallaUIConfig.h"
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
 * check; the designer tree's own sizes and colours are left alone (Setup only
 * styles the code-built cell).
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

	/** Build the cell. Size in px, colours from ui-config. Call once, before AddChild. */
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
	void SetSkillIcon(const FString& Code, const FLinearColor& CategoryColour, const FLinearColor& SkillColour);
	void SetKeyLabel(const FString& Text, const FLinearColor& Colour);
	void SetQuantity(int32 Quantity);
	/** 0 hides the overlay; `Text` is the seconds left. */
	void SetCooldown(float Fraction, const FString& Text, const FLinearColor& Colour);
	void SetDimmed(bool bDimmed);
	void SetSelected(bool bSelected);
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
	FLinearColor BorderColour = FLinearColor::Gray;
	FLinearColor HighlightColour = FLinearColor::Yellow;
	bool bPressed = false;
	bool bTreeReady = false;
	bool bDesignerTree = false;
	/** Designer tree: whether Stack is the designer's own (else SetDimmed dims the whole cell). */
	bool bDesignerStack = false;
	/** Designer tree: Frame's own colour, restored when deselected. */
	FLinearColor DesignerFrameColour = FLinearColor::White;
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
 * ## Layout
 *
 * Everything is placed on one canvas from FValhallaUIConfig — see
 * BuildVitals / BuildActionBar / … for which fields feed which panel. The
 * whole tree is thrown away and rebuilt by Rebuild(), which is what
 * `valhalla.ReloadUI` and the ui-config.json timestamp poll call, so a UI
 * Layout editor save shows up in a running PIE session.
 *
 * ## Layout from a Widget Blueprint (B-07 step 2)
 *
 * A Widget Blueprint child (WBP_GameHUD, set as Project Settings > Valhalla >
 * UI > Game HUD Class) may own the layout instead: its root must be a Canvas
 * Panel, and its widgets bind by name to the `meta = (BindWidget)` /
 * `(BindWidgetOptional)` members below (VitalsPanel, HpBar, ActionBarRow, ...;
 * the list is in the header, grouped by panel). C++ still fills and runs every
 * panel: it adds the action, inventory, equipment, loot and party cells and
 * the buff tokens to the designer's containers (leave those empty in the
 * designer), writes every text and bar, toggles panel visibility, and builds
 * the nameplate / floater layer and the combat-log filter menu itself.
 * Buttons are UValhallaHUDButton widgets with their Action set in the
 * designer. A missing optional part is a hidden stand-in (a warning, once) so
 * that feature just does not show. With no Blueprint, an empty one, or one
 * missing a required widget, the HUD builds itself in code exactly as before
 * (the last with an error in the log).
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
	/** Designer path: stand-ins for missing optional parts, hook up designer buttons, cells and the chat box. */
	void BindDesignerPanels();
	/** Designer path: fill the designer's containers (cells, rows, tokens) and build the world layer / filter menu. */
	void PopulateDesignerPanels();
	/** Designer path: layout panels, and panels with nothing clickable, stop eating clicks. Returns true when `Widget` is interactive. */
	bool ApplyClickThrough(UWidget* Widget);
	/** Hidden, unparented stand-in for an optional designer part the Blueprint lacks; warns once per name. */
	template <typename T>
	void EnsureDesignerPart(TObjectPtr<T>& Member, const TCHAR* Name, UClass* ConcreteClass = nullptr);
	/** Null every designer-bound member (the code path's fallback when a Blueprint is incomplete). */
	void ResetPanelPointers();
	// Shared by both paths: the runtime-made children of a panel.
	void AddActionCells(UPanelWidget* Row);
	void AddTargetBuffTokens(UPanelWidget* Row);
	void AddPartyRows(UPanelWidget* Column);
	void AddLootCells(UPanelWidget* Grid);
	void AddEquipRows(UPanelWidget* Column);
	void AddInventoryCells(UUniformGridPanel* Grid);
	void BuildLogFilterMenu(const FVector2D& Position);
	void SetInventoryShown(bool bShown);
	void BuildVitals();
	void BuildActionBar();
	void BuildCastBar();
	void BuildTargetFrame();
	void BuildPartyFrame();
	void BuildCombatLog();
	void BuildChat();
	void BuildLootPanel();
	void BuildSkillsPane();
	void BuildInventoryPanel();
	void BuildTooltip();
	void BuildDropConfirm();
	void BuildDeathOverlay();
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
	 * Keep the chat box clear of the action bar. ui-config places the chat at
	 * the vitals' right edge with a fixed width, which on a narrow viewport (a
	 * 640 px PIE client, or any window under ~1450 Slate units) runs under the
	 * centred action bar. Narrow it to the gap, or lift it above the vitals
	 * when the gap is too small to read. Re-run only when the width changes.
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
	void ShowItemTooltip(FName ItemId, int32 Quantity);
	void HideTooltip();
	void RequestDrop(EValhallaHUDSlotKind Kind, int32 Index);
	FLinearColor ChatColour(EValhallaChatChannel Channel) const;
	FString FormatChatLine(const FValhallaChatMessage& Line) const;
	void SetInputForTyping(bool bTyping);

	/** A resource bar (B-07 step 2: a BarWidgetClass instance, Setup with these numbers). */
	UValhallaHUDBarWidget* MakeBar(float Width, float Height, const FLinearColor& Fill, const FLinearColor& Background, float BackgroundAlpha, bool bWithOverlay = false, int32 FontSize = 9);

	UTextBlock* MakeText(const FString& Content, int32 Size, const FLinearColor& Colour, bool bBold = false, float Outline = 1.f);
	UBorder* MakePanel(const FLinearColor& Colour, float Alpha, float PanelPadding);
	UValhallaHUDButton* MakeButton(const FString& Caption, EValhallaHUDButton Action, int32 Index, const FLinearColor& Tint);
	UValhallaHUDSlotWidget* MakeCell(EValhallaHUDSlotKind Kind, int32 Index, float Size);
	class UCanvasPanelSlot* Place(UWidget* Widget, const FVector2D& Anchor, const FVector2D& Alignment, const FVector2D& Position, int32 ZOrder = 0);

protected:
	// ── Classes C++ makes cells and bars from ──────────────────────────
	/** The action / inventory / equipment / loot / skill cell. A WBP_HUDSlot child restyles every cell. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD")
	TSubclassOf<UValhallaHUDSlotWidget> SlotWidgetClass;
	/** Every bar C++ makes (party, nameplates, and the whole code-built HUD's). A WBP_HUDBar child restyles them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Valhalla|HUD")
	TSubclassOf<UValhallaHUDBarWidget> BarWidgetClass;

	// ── Designer panels (B-07 step 2) ──────────────────────────────────
	// A WBP_GameHUD child binds its widgets to these by name. BindWidget is
	// required (the Blueprint will not compile without it); BindWidgetOptional
	// may be left out and that feature is hidden. The code-built path assigns
	// the same members, so everything below works off them either way.
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
	/** The chat box's background: transparent while idle, chat.bgColor while typing. Required. */
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
	/** The action bar's outer panel (code path) / ActionBarRow (designer path). */
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
	/** Code path only: the chat's width box (TickLayout narrows it). */
	UPROPERTY(Transient) TObjectPtr<USizeBox> ChatSizer;
	/** The canvas width TickLayout last laid the chat out for. */
	float LayoutForWidth = -1.f;
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
	/** What ToggleInventory shows: the character + inventory pair (code path) / InventoryPanel (designer path). */
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

	// config watcher
	double ConfigWatchAccumulator = 0.0;
	FDateTime ConfigTimestamp;

	FDelegateHandle CombatEventHandle;
	TWeakObjectPtr<class AValhallaGameState> BoundGameState;
	bool bBuilt = false;
	bool bInventoryOpen = false;
	bool bSkillsOpen = false;
};
