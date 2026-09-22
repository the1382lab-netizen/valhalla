// Copyright Valhalla 2.0. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ValhallaHUD.generated.h"

struct FValhallaCombatEvent;
class AValhallaNPC;

/**
 * A canvas HUD: a debug block, a target pane, a cast bar, an action bar and
 * floating combat numbers. All of it drawn with DrawText and DrawRect.
 *
 * Its job is to make the server visible. Phase 2a proved class resolution and
 * replication by printing a stat block; Phase 2b has to prove that a swing
 * connected, that a cooldown is running and that an NPC is losing HP, and it has
 * to do that without a single UMG asset existing — because Phase 8 is where the
 * real UI is designed, and building it twice would be a waste.
 *
 * Everything here reads replicated state or AValhallaGameState's event list. The
 * HUD never decides anything, and nothing should come to depend on it.
 */
UCLASS()
class VALHALLAGAME_API AValhallaHUD : public AHUD
{
	GENERATED_BODY()

public:
	AValhallaHUD();

	//~ Begin AHUD interface
	virtual void DrawHUD() override;
	//~ End AHUD interface

	/** Set false to hide the debug block without unsetting the HUD class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|Debug")
	bool bShowDebugBlock = true;

private:
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
