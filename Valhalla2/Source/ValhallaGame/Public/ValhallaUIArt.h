// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-08a: the "Gilded Hall" look shared by the front end (login, character
// select) and the party pane: palette, fonts, frame art and class icons.
//
// One place so the three screens agree, and so a change of colour or font is
// one edit. Everything here loads the imported asset first and falls back to
// the source file beside the project (Import/UI/...), so new art shows in the
// editor before anyone runs import_ui_icons.py — the same rule the HUD's icon
// loader follows. A packaged build only ever has the imported assets
// (/Game/Valhalla/UI is always cooked).

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"

class UObject;
class UTexture2D;

namespace ValhallaUIArt
{
	// ── Palette (Docs/branding/class-icons/README.md, "Gilded Hall") ───────

	/** Page background, the darkest bronze-black. */
	VALHALLAGAME_API extern const FLinearColor Background;
	/** The lighter glow at the centre of the page. */
	VALHALLAGAME_API extern const FLinearColor BackgroundGlow;
	/** Panel fill inside the gold frame. */
	VALHALLAGAME_API extern const FLinearColor Panel;
	/** A list row, and the row under the mouse. */
	VALHALLAGAME_API extern const FLinearColor Row;
	VALHALLAGAME_API extern const FLinearColor RowHover;
	/** The selected row's fill. */
	VALHALLAGAME_API extern const FLinearColor RowSelected;
	/** Gold: frames, primary buttons. */
	VALHALLAGAME_API extern const FLinearColor Gold;
	/** Light gold: headings, links, the selected row's edge. */
	VALHALLAGAME_API extern const FLinearColor GoldLight;
	/** Dark bronze line: row borders, input borders. */
	VALHALLAGAME_API extern const FLinearColor Line;
	/** Text on dark, and its quieter shades. */
	VALHALLAGAME_API extern const FLinearColor Ink;
	VALHALLAGAME_API extern const FLinearColor InkSoft;
	VALHALLAGAME_API extern const FLinearColor InkDim;
	/** Text on a gold button. */
	VALHALLAGAME_API extern const FLinearColor InkOnGold;
	/** Delete, errors. */
	VALHALLAGAME_API extern const FLinearColor Danger;
	/** "Server online". */
	VALHALLAGAME_API extern const FLinearColor Online;

	// ── Fonts ─────────────────────────────────────────────────────────────

	/**
	 * Cinzel (headings, buttons, names) at Size, from
	 * /Game/Valhalla/UI/Fonts/F_Cinzel; the engine's Roboto Bold when the font
	 * has not been imported yet. Typeface "Regular" or "Bold".
	 */
	VALHALLAGAME_API FSlateFontInfo DisplayFont(int32 Size, FName Typeface = TEXT("Regular"));

	/** EB Garamond (body text, labels, inputs) at Size, from F_EBGaramond; Roboto otherwise. */
	VALHALLAGAME_API FSlateFontInfo BodyFont(int32 Size, FName Typeface = TEXT("Regular"));

	// ── Art ───────────────────────────────────────────────────────────────

	/**
	 * A UI texture by name: /Game/Valhalla/UI/<Folder>/<Name>, then
	 * Import/UI/<Folder>/<Name>.png off disk. Null when neither exists.
	 * Cached; textures made from disk are rooted so the cache stays valid.
	 */
	VALHALLAGAME_API UTexture2D* LoadUiTexture(const FString& Folder, const FString& Name);

	/** The HUD's gold panel frame (T_UI_Panel) as a box brush with its 24 px trim. */
	VALHALLAGAME_API FSlateBrush PanelFrameBrush();

	/** A plain rounded-rectangle brush in one colour (fills, outlines). */
	VALHALLAGAME_API FSlateBrush SolidBrush(const FLinearColor& Colour);

	/** A box outline of the given thickness (no fill). */
	VALHALLAGAME_API FSlateBrush OutlineBrush(const FLinearColor& Colour, float Thickness);

	// ── Class icons (B-08a kit) ─────────────────────────────────────────────

	/**
	 * The icon file name for a class, without the extension: the class's own
	 * `icon` (classes.json) when it names one, else T_ClassIcon_<ClassId> with the
	 * id's first letter upper-cased. Mirrors shared/src/classes.ts classIconFileFor.
	 */
	VALHALLAGAME_API FString ClassIconName(FName ClassId, const FString& IconField);

	/**
	 * The class icon texture: the named icon, else the plain coin
	 * (T_ClassIcon_Default), else null. WorldContext finds the game data.
	 */
	VALHALLAGAME_API UTexture2D* ClassIconFor(const UObject* WorldContext, FName ClassId);
}
