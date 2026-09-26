// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-27 Phase 4: the player's graphics settings, per account (decision 3: they
// follow the player's login, not the character or the PC). Synced through the
// backend (`GET/PUT /api/account/settings`, stored verbatim in
// `account_settings.graphics_json`) and cached on disk:
//
//   Saved/Settings/graphics_<userId>.json   the account's copy
//   Saved/Settings/graphics_local.json      what this PC applied last (applied at
//                                           start, before anyone logs in; the only
//                                           copy for a dev session with no login)
//
// UValhallaGraphicsSettingsSubsystem (ValhallaGame) owns the live copy and
// applies it through UGameUserSettings.
//
// ## JSON format (version 1)
//
// One flat object, every field optional (a missing or wrong-typed field keeps
// its default, unknown fields are kept and written back):
//
//   {
//     "Version": 1,
//     "UpdatedAt": "2026-09-26T10:00:00.000Z",  // ISO 8601 UTC, stamped on every change
//     "Quality": "high",                        // low | medium | high | epic: the scalability preset
//     "GlobalIllumination": true,               // Lumen GI; the preset's own choice unless changed ("Custom")
//     "ResolutionScale": 100,                   // 50 .. 100, % of the window's pixels rendered (TSR upscales)
//     "FrameRateCap": 0,                        // 0 = none, else 30 .. 360 fps
//     "VSync": false,
//     "MotionBlur": false
//   }
//
// Decision 1: High, anti-aliasing history 100 % (the High preset's) and motion
// blur off by default; decision 8: VSync off and no frame-rate cap on a first
// launch.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** The scalability preset (sg.* 0..3). */
enum class EValhallaGraphicsQuality : uint8
{
	Low = 0,
	Medium = 1,
	High = 2,
	Epic = 3,
};

struct VALHALLACORE_API FValhallaGraphicsSettings
{
	static constexpr int32 CurrentVersion = 1;
	static constexpr int32 MinResolutionScale = 50;
	static constexpr int32 MaxResolutionScale = 100;
	static constexpr int32 MinFrameRateCap = 30;
	static constexpr int32 MaxFrameRateCap = 360;

	int32 Version = CurrentVersion;
	FString UpdatedAt;

	EValhallaGraphicsQuality Quality = EValhallaGraphicsQuality::High;
	bool bGlobalIllumination = true;
	int32 ResolutionScale = 100;
	/** 0 = no cap. */
	int32 FrameRateCap = 0;
	bool bVSync = false;
	bool bMotionBlur = false;

	/** Fields this version does not know, written back unchanged. */
	TMap<FString, TSharedPtr<FJsonValue>> UnknownFields;

	/** The preset's own GI choice: off on Low (the engine's Low GI has no Lumen), on above. */
	static bool PresetGlobalIllumination(EValhallaGraphicsQuality InQuality) { return InQuality != EValhallaGraphicsQuality::Low; }

	/** True when a setting the preset decides has been changed from the preset's choice (the menu shows "Custom"). */
	bool IsCustom() const { return bGlobalIllumination != PresetGlobalIllumination(Quality); }

	/** Choose a preset: its quality and its own GI choice. */
	void SetPreset(EValhallaGraphicsQuality InQuality);

	/** The r.MotionBlurQuality to apply: 0 when off, else the preset's (3, 4 on Epic). */
	int32 GetMotionBlurQualityCVar() const;

	/** The sg.GlobalIlluminationQuality to apply: 0 when GI is off, else the preset's level (at least 1). */
	int32 GetGlobalIlluminationLevel() const;

	/** Clamp everything into range (a cap under MinFrameRateCap other than 0 becomes MinFrameRateCap). */
	void Sanitize();

	/** The frame-rate caps the menu offers, 0 first ("none"). */
	static const TArray<int32>& GetFrameRateCapSteps();

	static const TCHAR* QualityToString(EValhallaGraphicsQuality InQuality);
	static bool QualityFromString(const FString& Text, EValhallaGraphicsQuality& Out);
	/** "Low" .. "Epic", or "Custom" when IsCustom. */
	FString GetPresetDisplayName() const;

	TSharedRef<FJsonObject> ToJson() const;
	FString ToJsonString(bool bPretty = true) const;
	/** Tolerant: see the file comment. False (and the defaults) when Json is null. */
	static bool FromJson(const TSharedPtr<FJsonObject>& Json, FValhallaGraphicsSettings& Out, TArray<FString>* OutWarnings = nullptr);
	static bool FromJsonString(const FString& Text, FValhallaGraphicsSettings& Out, TArray<FString>* OutWarnings = nullptr);

	/** Stamp UpdatedAt with the current UTC time. */
	void Touch();
	/** UpdatedAt as a time; FDateTime::MinValue() when empty or unparsable. */
	FDateTime GetUpdatedAtTime() const;

	/** Same values, ignoring UpdatedAt and UnknownFields. */
	bool SameValues(const FValhallaGraphicsSettings& Other) const;
};
