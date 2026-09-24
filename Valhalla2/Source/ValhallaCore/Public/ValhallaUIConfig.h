// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8b: the typed spelling of 1.0's `shared/data/ui-config.json`.
//
// The file has existed since 1.0's UI Layout editor was written and has never
// had a runtime consumer — 1.0's GameScene hard-codes the same numbers the file
// holds. `UValhallaGameHUDWidget` is its first reader, so this is the first
// time a designer's edit in the UI Layout editor changes a game.
//
// B-07 step 4 trimmed it to what the C++ HUD still reads: the HUD's layout
// moved into the WBP_GameHUD Widget Blueprint, so the panel positions, sizes and
// colours (hud, actionBar, castBar, deathOverlay, most of chat and inventory)
// left the file. What stays: the inventory grid's cols x rows, the chat's line
// counts, and the nameplates (the world layer is still built in C++).
//
// Mirrors `UIConfig` in shared/src/ui-config.ts field for field. Every field
// defaults to `DEFAULT_UI_CONFIG`, so a file with a section missing (or no file
// at all) still produces a usable layout; `bHas*` records which sections the
// file actually carried, for the test and for the load log.
//
// Colours are parsed from `#rrggbb` as sRGB and stored linear, the way UMG
// wants them. Font sizes are 1.0's CSS strings ("12px") reduced to the number.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct VALHALLACORE_API FValhallaUIConfig
{
	FString Version = TEXT("1.1.0");

	// B-07 step 4: the HUD's layout, sizes and panel colours live in the
	// WBP_GameHUD Widget Blueprint (and its Class Defaults); the file keeps
	// only what C++ builds or counts at runtime.

	// ── chat ────────────────────────────────────────────────────────────
	struct FChat
	{
		/** Lines listed while the chat box is open (the client keeps 50). */
		int32 MaxMessages = 50;
		/** Lines shown while it is idle (each fades 10 s after it arrived). */
		int32 VisibleLines = 9;
	} Chat;

	// ── inventory ───────────────────────────────────────────────────────
	struct FInventory
	{
		/** The grid C++ fills InventoryGrid with: cols x rows cells. */
		int32 Cols = 8;
		int32 Rows = 4;
	} Inventory;

	// ── nameplates (and the world layer: still built in C++) ────────────
	struct FNameplates
	{
		int32 FontSize = 12;
		FString FontWeight = TEXT("bold");
		FLinearColor Color;
		FLinearColor StrokeColor;
		float StrokeThickness = 3.f;
		FLinearColor BgColor;
		float BgAlpha = 0.5f;
		float YOffset = -44.f;
		float BgPaddingX = 4.f;
		float BgPaddingY = 2.f;
		float BgRadius = 3.f;
	} Nameplates;

	// ── Which sections the source JSON actually carried ─────────────────
	bool bHasChat = false;
	bool bHasInventory = false;
	bool bHasNameplates = false;

	/** `DEFAULT_UI_CONFIG`, colours and all. */
	FValhallaUIConfig();

	/** The number of sections the file carries (chat, inventory, nameplates). */
	static constexpr int32 SectionCount = 3;

	/** True when all three sections were present in the parsed file. */
	bool HasAllSections() const
	{
		return bHasChat && bHasInventory && bHasNameplates;
	}

	/**
	 * Fill from a parsed ui-config.json. Starts from the defaults, so a
	 * missing field keeps its default rather than becoming 0. Never fails;
	 * returns HasAllSections().
	 */
	static bool Parse(const TSharedPtr<FJsonObject>& Root, FValhallaUIConfig& Out);

	/** `#rrggbb` / `rrggbb` / `#rgb` as sRGB → linear. `Fallback` when it is none of those. */
	static FLinearColor ParseHexColor(const FString& Hex, const FLinearColor& Fallback);

	/** "12px" → 12. `Fallback` when there is no leading number. */
	static int32 ParsePixelSize(const FString& Css, int32 Fallback);
};
