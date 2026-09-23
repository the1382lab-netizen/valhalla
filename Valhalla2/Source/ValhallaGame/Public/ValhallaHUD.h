// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ValhallaHUD.generated.h"

struct FValhallaCombatEvent;
class AValhallaNPC;
class AValhallaLootBag;
class UValhallaGameHUDWidget;

/**
 * The HUD actor. Since Phase 8b it does two things:
 *
 *  - It owns the real game HUD, `UValhallaGameHUDWidget` (UMG, laid out from
 *    ui-config.json), creating it for the local player in BeginPlay.
 *  - It draws the clickable "Loot (n)" labels over loot bags (Phase 9) on the
 *    canvas, because they are world-anchored and hit-tested by the controller.
 *
 * The Phase 2 canvas debug HUD — stat block, inventory list, party block, target
 * pane, cast bar, action bar, chat, floaters — is still here, behind
 * `valhalla.DebugHud 1` (and as a fallback if the widget could not be made). It
 * reads replicated state only and decides nothing.
 */
UCLASS()
class VALHALLAGAME_API AValhallaHUD : public AHUD
{
	GENERATED_BODY()

public:
	AValhallaHUD();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	//~ Begin AHUD interface
	virtual void DrawHUD() override;
	//~ End AHUD interface

	/** The UMG game HUD, or null before BeginPlay / on a server. */
	UValhallaGameHUDWidget* GetGameHUD() const { return GameHUD; }

	/** Set false to hide the debug block without unsetting the HUD class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|Debug")
	bool bShowDebugBlock = true;

	/**
	 * The bag whose "Loot (n)" label, drawn last frame, is under a screen
	 * point, or null. The labels are drawn over the world, so a bag hidden
	 * behind an NPC or its own corpse can still be clicked through its label.
	 */
	AValhallaLootBag* HitTestLootLabel(const FVector2D& ScreenPoint) const;

private:
	/** See GetGameHUD. */
	UPROPERTY(Transient)
	TObjectPtr<UValhallaGameHUDWidget> GameHUD;

	/** Draw one line and advance the cursor. */
	void DrawLine(const FString& Text, const FLinearColor& Colour, float& CursorY);

	/** The Phase 2a stat block, top-left. */
	void DrawDebugBlock();

	/** Name, level and an HP bar for whatever is selected, top-centre. */
	void DrawTargetPane();

	/** A progress bar and the skill's name while casting, above the action bar. */
	void DrawCastBar();

	/** Eight boxes with a skill abbreviation and the seconds left on each. */
	void DrawActionBar();

	/** Nameplates over every living NPC on screen. */
	void DrawNPCNameplates();

	/**
	 * Equipment and inventory, top-left under the debug block.
	 *
	 * The nine equip slots are always listed, empty ones included, because the
	 * point of the block is to prove that a slot changed. The inventory is
	 * listed densely, index and all, because that index is what
	 * `valhalla.DebugEquip` takes.
	 */
	void DrawInventoryBlock();

	/** The party roster, top-right. Nothing at all when not in a party. */
	void DrawPartyBlock();

	/** The last ChatLogMaxLines lines, bottom-left, coloured by channel. */
	void DrawChatLog();

	/** A "Loot (n)" label over every bag on screen, brighter when within reach. */
	void DrawLootBagLabels();

	/** Last frame's clickable bag labels, rebuilt every DrawHUD. */
	TArray<TPair<FBox2D, TWeakObjectPtr<AValhallaLootBag>>> LootLabelRects;

	/**
	 * The last second of combat events, projected to screen space and drawn
	 * where they happened: damage in white, crits in orange, heals in green,
	 * misses and dodges as words.
	 */
	void DrawFloatingCombatText();

	/** Colour and text for one event. Returns false when it should not be drawn. */
	static bool DescribeEvent(const FValhallaCombatEvent& Event, FString& OutText, FLinearColor& OutColour);

	/** Left margin, px. */
	static constexpr float MarginX = 24.f;

	/** Top margin, px. */
	static constexpr float MarginY = 24.f;

	/** Line height, px. */
	static constexpr float LineHeight = 18.f;

	/** Action bar slot size, px. */
	static constexpr float SlotSize = 44.f;

	/** Gap between action bar slots, px. */
	static constexpr float SlotGap = 6.f;

	/** Distance of the action bar from the bottom of the screen, px. */
	static constexpr float ActionBarBottomMargin = 40.f;

	/** How far a floating number rises over its lifetime, px. */
	static constexpr float FloaterRiseDistance = 48.f;

	/** How high above an actor's origin a nameplate or floater starts, cm. */
	static constexpr float NameplateWorldHeight = 110.f;

	/** A loot bag is small; its label sits lower than a character's nameplate. */
	static constexpr float BagLabelWorldHeight = 40.f;

	/** Where the inventory block starts, px from the top. Clear of the debug block. */
	static constexpr float InventoryBlockTop = 216.f;

	/** Right margin for the party block, px. */
	static constexpr float MarginRight = 24.f;

	/** Distance of the chat log's last line from the bottom of the screen, px. */
	static constexpr float ChatBottomMargin = 24.f;
};
