// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8b: the typed spelling of 1.0's `shared/data/ui-config.json`.
//
// The file has existed since 1.0's UI Layout editor was written and has never
// had a runtime consumer — 1.0's GameScene hard-codes the same numbers the file
// holds. `UValhallaGameHUDWidget` is its first reader, so this is the first
// time a designer's edit in the UI Layout editor changes a game.
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

/** One `{ color, bgColor, bgAlpha }` style resource bar. */
struct VALHALLACORE_API FValhallaUIBarConfig
{
	float Width = 200.f;
	float Height = 12.f;
	FLinearColor Color = FLinearColor::White;
	FLinearColor BgColor = FLinearColor::Black;
	float BgAlpha = 0.7f;
};

struct VALHALLACORE_API FValhallaUIConfig
{
	FString Version = TEXT("1.0.0");

	// ── hud ─────────────────────────────────────────────────────────────
	struct FHud
	{
		// hpBar
		float HpX = 16.f;
		float HpWidth = 200.f;
		float HpHeight = 12.f;
		float HpYOffsetFromBottom = 36.f;
		FLinearColor HpHigh;
		FLinearColor HpMid;
		FLinearColor HpLow;
		FLinearColor HpBg;
		float HpBgAlpha = 0.7f;

		// manaBar (gapAboveHp lives here in 1.0 and applies to energy too)
		FValhallaUIBarConfig Mana;
		float ManaGapAboveHp = 6.f;

		// energyBar
		FValhallaUIBarConfig Energy;

		// classText
		int32 ClassFontSize = 12;
		FString ClassFontFamily = TEXT("monospace");
		FLinearColor ClassColor = FLinearColor::White;
		FLinearColor ClassStrokeColor = FLinearColor::Black;
		float ClassStrokeThickness = 2.f;
	} Hud;

	// ── actionBar ───────────────────────────────────────────────────────
	struct FActionBar
	{
		float SlotSize = 44.f;
		float SlotGap = 4.f;
		float Padding = 6.f;
		float BottomMargin = 8.f;
		FLinearColor Bg;
		float BgAlpha = 0.95f;
		FLinearColor Border;
		FLinearColor CooldownOverlay;
		float CooldownOverlayAlpha = 0.6f;
		FLinearColor KeyLabelColor;
	} ActionBar;

	// ── chat ────────────────────────────────────────────────────────────
	struct FChat
	{
		float MaxWidth = 360.f;
		float Height = 170.f;
		float BottomMargin = 8.f;
		float LineHeight = 15.f;
		float Padding = 6.f;
		float InputHeight = 18.f;
		int32 MaxMessages = 50;
		int32 VisibleLines = 9;
		int32 FontSize = 11;
		FLinearColor BgColor;
		float BgAlpha = 0.72f;
		FLinearColor BorderColor;
		float BorderAlpha = 0.9f;
		FLinearColor General;
		FLinearColor World;
		FLinearColor Whisper;
		FLinearColor System;
	} Chat;

	// ── inventory ───────────────────────────────────────────────────────
	struct FInventory
	{
		int32 Cols = 8;
		int32 Rows = 4;
		float SlotSize = 48.f;
		float SlotGap = 4.f;
		float CharPanelWidth = 220.f;
		float PanelGap = 8.f;
		float PanelHeight = 340.f;
		FLinearColor Bg;
		float BgAlpha = 0.95f;
		FLinearColor Border;
		FLinearColor SlotBg;
		FLinearColor Highlight;
		FLinearColor TitleColor;
		FLinearColor LabelColor;
		FLinearColor ValueColor;
	} Inventory;

	// ── castBar ─────────────────────────────────────────────────────────
	struct FCastBar
	{
		float Width = 260.f;
		float Height = 16.f;
		float YAboveActionBar = 12.f;
		FLinearColor Color;
		FLinearColor BgColor;
		float BgAlpha = 0.6f;
		FLinearColor TextColor;
		int32 FontSize = 11;
	} CastBar;

	// ── nameplates ──────────────────────────────────────────────────────
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

	// ── deathOverlay ────────────────────────────────────────────────────
	struct FDeathOverlay
	{
		float BgAlpha = 0.7f;
		FLinearColor TextColor;
		int32 FontSize = 32;
	} DeathOverlay;

	// ── Which sections the source JSON actually carried ─────────────────
	bool bHasHud = false;
	bool bHasActionBar = false;
	bool bHasChat = false;
	bool bHasInventory = false;
	bool bHasCastBar = false;
	bool bHasNameplates = false;
	bool bHasDeathOverlay = false;

	/** `DEFAULT_UI_CONFIG`, colours and all. */
	FValhallaUIConfig();

	/** True when all seven sections were present in the parsed file. */
	bool HasAllSections() const
	{
		return bHasHud && bHasActionBar && bHasChat && bHasInventory
			&& bHasCastBar && bHasNameplates && bHasDeathOverlay;
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
