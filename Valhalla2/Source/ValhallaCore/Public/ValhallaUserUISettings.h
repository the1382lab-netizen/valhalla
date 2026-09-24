// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-21: the player's own UI settings, per character, synced through the
// backend (`GET/PUT /api/characters/:id/settings`, stored verbatim in
// `character_settings.ui_json`) and cached on disk at
// `Saved/UI/settings_<characterId>.json` (`settings_offline.json` without a
// character, e.g. an offline PIE on L_World). UValhallaUserSettingsSubsystem
// (ValhallaGame) owns the live copy; UValhallaGameHUDWidget applies it.
//
// ## JSON format (version 1)
//
// One flat object. Every field is optional: a missing field keeps its default,
// a field of the wrong type is ignored (with a warning), unknown fields are
// kept and written back unchanged (so an older client never erases what a
// newer one saved), and a `Version` newer than CurrentVersion still loads
// every field this version knows. Text that is not a JSON object at all loads
// as the defaults.
//
//   {
//     "Version": 1,
//     "UpdatedAt": "2026-09-24T17:10:17.252Z",   // ISO 8601 UTC, stamped on every change
//     "bLocked": true,                          // panels cannot be dragged
//     "UiScale": 1.0,                           // 0.5 .. 2
//     "PanelOpacity": 1.0,                      // 0.2 .. 1, panel backgrounds only
//     "Panels": {                               // key = a movable panel (UValhallaGameHUDWidget::GetMovablePanels)
//       "Chat": {
//         "AnchorMin": [0, 1], "AnchorMax": [0, 1], "Alignment": [0, 1],
//         "Position": [300, -8],                // canvas offset, Slate units at UiScale 1
//         "Size": [0, 0],                       // content size of a flowing panel; [0, 0] = the designer's
//         "Scale": 1.0,                         // on top of UiScale
//         "bVisible": true,
//         "bSet": true                          // false = the entry is ignored (designer layout)
//       }
//     },
//     "Colours": { "HpHighColour": "#44ff44ff" },   // key = a "Valhalla|HUD Style" property; sRGB #rrggbbaa (or #rrggbb)
//     "ChatFontSize": 0,                        // 0 = the HUD's default
//     "ChatVisibleLines": 0,                    // 0 = ui-config's
//     "bChatTimestamps": false,
//     "LogFilters": { "misses": false },        // combat-log filter key -> shown; a missing key is shown
//     "bShowNpcNameplates": true,
//     "bShowPlayerNameplates": true,
//     "bFloatingCombatText": true,
//     "NameplateFontSize": 0                    // 0 = ui-config's
//   }
//
// Vectors are [x, y] arrays; colours are sRGB hex strings (stored linear here,
// the way UMG wants them; 8 bits per channel on disk).

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ValhallaUserUISettings.generated.h"

/** Which part of the settings a reset clears. */
UENUM(BlueprintType)
enum class EValhallaUISettingsSection : uint8
{
	/** Everything, the lock included. */
	All,
	/** Panel positions, sizes, scales and visibility (Panels). */
	Layout,
	/** UiScale, PanelOpacity and Colours. */
	Style,
	/** ChatFontSize, ChatVisibleLines, bChatTimestamps, LogFilters. */
	Chat,
	/** The nameplate and floating-text switches and font size. */
	Nameplates,
};

/** Where one HUD panel sits on the HUD's root canvas. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaPanelLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	FVector2D AnchorMin = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	FVector2D AnchorMax = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	FVector2D Alignment = FVector2D::ZeroVector;

	/** The canvas slot's position (offset from the anchor), Slate units at UiScale 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	FVector2D Position = FVector2D::ZeroVector;

	/**
	 * A flowing panel's content size (its Size Box: chat width and open height,
	 * combat log width and height, skills width and max height). (0, 0), or a
	 * zero axis, keeps the designer's. Ignored for scaled panels.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	FVector2D Size = FVector2D::ZeroVector;

	/** Render scale on top of UiScale, about the panel's alignment point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings", meta = (ClampMin = "0.25", ClampMax = "4"))
	float Scale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	bool bVisible = true;

	/** False: this entry is ignored and the panel keeps the designer's layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	bool bSet = false;

	static constexpr float MinScale = 0.25f;
	static constexpr float MaxScale = 4.f;

	bool Equals(const FValhallaPanelLayout& Other, float Tolerance = 0.01f) const;
};

/** The player's UI settings for one character. See the file comment for the JSON. */
USTRUCT(BlueprintType)
struct VALHALLACORE_API FValhallaUserUISettings
{
	GENERATED_BODY()

	/** The format this build writes. */
	static constexpr int32 CurrentVersion = 1;

	static constexpr float MinUiScale = 0.5f;
	static constexpr float MaxUiScale = 2.f;
	static constexpr float MinPanelOpacity = 0.2f;
	static constexpr float MaxPanelOpacity = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|UI Settings")
	int32 Version = CurrentVersion;

	/** ISO 8601 UTC of the last change; empty = never changed (the defaults). Newer wins between the backend and the disk cache. */
	UPROPERTY(BlueprintReadOnly, Category = "Valhalla|UI Settings")
	FString UpdatedAt;

	/** Panels cannot be dragged or resized (edit mode unlocks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	bool bLocked = true;

	/** The whole HUD's scale, 0.5 .. 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings", meta = (ClampMin = "0.5", ClampMax = "2"))
	float UiScale = 1.f;

	/** Panel backgrounds' opacity, 0.2 .. 1 (text and icons stay opaque). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings", meta = (ClampMin = "0.2", ClampMax = "1"))
	float PanelOpacity = 1.f;

	/** Movable panel key (Vitals, ActionBar, Chat, ...) -> layout. Only bSet entries take effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	TMap<FName, FValhallaPanelLayout> Panels;

	/** "Valhalla|HUD Style" colour property name (HpHighColour, ChatWorldColour, ...) -> colour, linear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	TMap<FName, FLinearColor> Colours;

	/** Chat line font size, Slate points; 0 = the HUD's ChatFontSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings", meta = (ClampMin = "0"))
	int32 ChatFontSize = 0;

	/** Idle chat lines; 0 = ui-config's chat.visibleLines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings", meta = (ClampMin = "0"))
	int32 ChatVisibleLines = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	bool bChatTimestamps = false;

	/** Combat-log filter key ("outDmg", "misses", ...) -> shown. A key not here is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	TMap<FName, bool> LogFilters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	bool bShowNpcNameplates = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	bool bShowPlayerNameplates = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings")
	bool bFloatingCombatText = true;

	/** Nameplate font size, px as ui-config's; 0 = ui-config's nameplates.fontSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Valhalla|UI Settings", meta = (ClampMin = "0"))
	int32 NameplateFontSize = 0;

	/** Top-level fields this version does not know, kept to be written back unchanged. Not a UPROPERTY. */
	TMap<FString, TSharedPtr<FJsonValue>> UnknownFields;

	// ── JSON ────────────────────────────────────────────────────────────

	TSharedRef<FJsonObject> ToJson() const;
	FString ToJsonString(bool bPretty = true) const;

	/**
	 * Fill `Out` from a parsed document, starting from the defaults. Never
	 * fails on content: wrong-typed fields are skipped (a line each in
	 * `OutWarnings`), values are clamped (Sanitize). False only for a null
	 * object, in which case Out is the defaults.
	 */
	static bool FromJson(const TSharedPtr<FJsonObject>& Json, FValhallaUserUISettings& Out, TArray<FString>* OutWarnings = nullptr);

	/** FromJson on text. Not a JSON object (empty, corrupt, an array): false and the defaults. */
	static bool FromJsonString(const FString& Text, FValhallaUserUISettings& Out, TArray<FString>* OutWarnings = nullptr);

	// ── Edits ───────────────────────────────────────────────────────────

	/** Clamp UiScale, PanelOpacity, panel scales and sizes, font sizes; NaN -> default. */
	void Sanitize();

	/** Back to the defaults for one section (UpdatedAt is the caller's to stamp). */
	void ResetSection(EValhallaUISettingsSection Section);

	/** Stamp UpdatedAt with the current UTC time. */
	void Touch();

	/** The panel's layout when the player set one (bSet), else null. */
	const FValhallaPanelLayout* FindSetPanel(FName Key) const;

	/** UpdatedAt as a time; FDateTime::MinValue() when empty or unparsable. */
	FDateTime GetUpdatedAtTime() const;

	/** Same values (colours to 8 bits, floats to 1e-3), ignoring UpdatedAt and UnknownFields. */
	bool EquivalentTo(const FValhallaUserUISettings& Other) const;

	// ── Helpers (public for the tests) ─────────────────────────────────

	/** Linear -> sRGB "#rrggbbaa", lower case. */
	static FString ColourToHex(const FLinearColor& Colour);
	/** "#rrggbbaa" / "#rrggbb" / without '#', sRGB -> linear. False (Out untouched) for anything else. */
	static bool ColourFromHex(const FString& Hex, FLinearColor& Out);
};
