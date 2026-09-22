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
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UUniformGridPanel;
class UVerticalBox;
class UValhallaGameHUDWidget;

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

/** What a text button does. */
UENUM()
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
 */
UCLASS()
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

private:
	void EnsureTree();

	UPROPERTY(Transient) TObjectPtr<USizeBox> Sizer;
	UPROPERTY(Transient) TObjectPtr<UBorder> Frame;
	UPROPERTY(Transient) TObjectPtr<UBorder> Fill;
	UPROPERTY(Transient) TObjectPtr<UOverlay> Stack;
	UPROPERTY(Transient) TObjectPtr<UImage> Icon;
	UPROPERTY(Transient) TObjectPtr<UBorder> SkillTile;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Abbrev;
	UPROPERTY(Transient) TObjectPtr<USizeBox> CooldownSizer;
	UPROPERTY(Transient) TObjectPtr<UBorder> CooldownFill;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CooldownText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> KeyLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> QuantityText;

	float CellSize = 44.f;
	FLinearColor BorderColour = FLinearColor::Gray;
	FLinearColor HighlightColour = FLinearColor::Yellow;
	bool bPressed = false;
};

/** A text button that knows which button it is. See UValhallaCharacterRowButton for why a subclass. */
UCLASS()
class VALHALLAGAME_API UValhallaHUDButton : public UButton
{
	GENERATED_BODY()

public:
	EValhallaHUDButton Action = EValhallaHUDButton::None;
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
 * ## Input
 *
 * The HUD canvas is SelfHitTestInvisible: a click on bare canvas falls through
 * to the viewport and reaches the world, a click on a panel does not. Opening
 * the chat box switches the controller to UI-only input with the text box
 * focused, which is what stops WASD walking the character while you type;
 * closing it switches back.
 */
UCLASS()
class VALHALLAGAME_API UValhallaGameHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ Begin UUserWidget interface
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End UUserWidget interface

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

	/** A resource bar: a background box and a fill whose width is set per tick. */
	struct FBar
	{
		TObjectPtr<USizeBox> FillSizer = nullptr;
		TObjectPtr<UBorder> FillBorder = nullptr;
		TObjectPtr<USizeBox> OverlaySizer = nullptr;
		TObjectPtr<UTextBlock> Label = nullptr;
		TObjectPtr<UWidget> Root = nullptr;
		float Width = 200.f;
		void Set(float Fraction) const;
		void SetOverlay(float Fraction) const;
	};
	FBar MakeBar(float Width, float Height, const FLinearColor& Fill, const FLinearColor& Background, float BackgroundAlpha, bool bWithOverlay = false, int32 FontSize = 9);

	UTextBlock* MakeText(const FString& Content, int32 Size, const FLinearColor& Colour, bool bBold = false, float Outline = 1.f);
	UBorder* MakePanel(const FLinearColor& Colour, float Alpha, float PanelPadding);
	UValhallaHUDButton* MakeButton(const FString& Caption, EValhallaHUDButton Action, int32 Index, const FLinearColor& Tint);
	UValhallaHUDSlotWidget* MakeCell(EValhallaHUDSlotKind Kind, int32 Index, float Size);
	class UCanvasPanelSlot* Place(UWidget* Widget, const FVector2D& Anchor, const FVector2D& Alignment, const FVector2D& Position, int32 ZOrder = 0);

	// ── State ───────────────────────────────────────────────────────────
	FValhallaUIConfig Config;

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> WorldLayer;

	// vitals
	FBar HpBar;
	FBar ResourceBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ClassText;

	// action bar
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDSlotWidget>> ActionCells;
	UPROPERTY(Transient) TObjectPtr<UWidget> ActionBarRoot;

	// cast bar
	UPROPERTY(Transient) TObjectPtr<UWidget> CastBarRoot;
	FBar CastBar;

	// target frame
	UPROPERTY(Transient) TObjectPtr<UWidget> TargetRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TargetName;
	FBar TargetHp;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> TargetBuffs;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> TargetBuffTokens;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TargetBuffTexts;

	// party
	UPROPERTY(Transient) TObjectPtr<UWidget> PartyRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PartyTitle;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PartyRows;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PartyNames;
	TArray<FBar> PartyBars;
	UPROPERTY(Transient) TObjectPtr<UWidget> InviteRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InviteText;

	// combat log
	UPROPERTY(Transient) TObjectPtr<UScrollBox> CombatLogScroll;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CombatLogTitle;
	UPROPERTY(Transient) TObjectPtr<UWidget> FilterMenu;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> FilterLabels;
	TArray<FValhallaCombatLogLine> CombatLogLines;
	TMap<FName, bool> LogFilters;
	bool bCombatLogDirty = true;

	// chat
	UPROPERTY(Transient) TObjectPtr<UBorder> ChatPanel;
	UPROPERTY(Transient) TObjectPtr<USizeBox> ChatSizer;
	/** The canvas width TickLayout last laid the chat out for. */
	float LayoutForWidth = -1.f;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChatChannelText;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> ChatScroll;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> ChatInput;
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
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LootTitle;
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDSlotWidget>> LootCells;
	TWeakObjectPtr<AValhallaLootBag> LootBag;

	// skills
	UPROPERTY(Transient) TObjectPtr<UWidget> SkillsRoot;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SkillsList;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SkillsHint;
	FName ArmedSkill;
	FName SkillsBuiltForClass;
	int32 SkillsBuiltForLevel = -1;

	// inventory
	UPROPERTY(Transient) TObjectPtr<UWidget> InventoryRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InventoryTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CharacterStats;
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDSlotWidget>> InventoryCells;
	UPROPERTY(Transient) TArray<TObjectPtr<UValhallaHUDSlotWidget>> EquipCells;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EquipLabels;

	// tooltip
	UPROPERTY(Transient) TObjectPtr<UWidget> TooltipRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TooltipName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TooltipBody;
	bool bTooltipPinned = false;

	// drop confirm
	UPROPERTY(Transient) TObjectPtr<UWidget> DropRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DropText;
	EValhallaHUDSlotKind DropKind = EValhallaHUDSlotKind::None;
	int32 DropIndex = INDEX_NONE;

	// death
	UPROPERTY(Transient) TObjectPtr<UWidget> DeathRoot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DeathText;
	bool bWasAlive = true;
	double DeathStartedAt = 0.0;

	// world layer: nameplates and floaters, pooled
	struct FPlate
	{
		TObjectPtr<UWidget> Root = nullptr;
		TObjectPtr<UTextBlock> Name = nullptr;
		FBar Bar;
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
