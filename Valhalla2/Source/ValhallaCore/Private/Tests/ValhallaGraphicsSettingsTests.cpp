// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-27 Phase 4, the per-account graphics settings document (FValhallaGraphicsSettings):
//
//   Valhalla.Core.GraphicsSettings.Defaults — decision 1 and 8's first launch:
//     High, global illumination on, 100 %, no cap, VSync and motion blur off.
//   Valhalla.Core.GraphicsSettings.Json — a round trip keeps every field and
//     unknown ones; a missing field keeps its default, a wrong one is warned
//     about; out-of-range values are clamped; not an object -> the defaults.
//   Valhalla.Core.GraphicsSettings.Presets — the preset sets its GI choice,
//     changing GI makes it "Custom", the engine values applied (GI level,
//     r.MotionBlurQuality).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaGraphicsSettings.h"

#ifndef VALHALLA_CORE_TEST_FLAGS
#define VALHALLA_CORE_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaGraphicsDefaultsTest, "Valhalla.Core.GraphicsSettings.Defaults", VALHALLA_CORE_TEST_FLAGS)

bool FValhallaGraphicsDefaultsTest::RunTest(const FString& /*Parameters*/)
{
	const FValhallaGraphicsSettings S;
	TestEqual(TEXT("High"), static_cast<int32>(S.Quality), static_cast<int32>(EValhallaGraphicsQuality::High));
	TestTrue(TEXT("global illumination on"), S.bGlobalIllumination);
	TestEqual(TEXT("100 % resolution"), S.ResolutionScale, 100);
	TestEqual(TEXT("no frame-rate cap"), S.FrameRateCap, 0);
	TestFalse(TEXT("VSync off"), S.bVSync);
	TestFalse(TEXT("motion blur off"), S.bMotionBlur);
	TestFalse(TEXT("not custom"), S.IsCustom());
	TestEqual(TEXT("shown as High"), S.GetPresetDisplayName(), FString(TEXT("High")));
	TestEqual(TEXT("motion blur off -> r.MotionBlurQuality 0"), S.GetMotionBlurQualityCVar(), 0);
	TestEqual(TEXT("GI level = the preset's"), S.GetGlobalIlluminationLevel(), 2);
	TestTrue(TEXT("no UpdatedAt until a change"), S.UpdatedAt.IsEmpty());
	TestEqual(TEXT("an empty UpdatedAt is the oldest time"), S.GetUpdatedAtTime(), FDateTime::MinValue());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaGraphicsJsonTest, "Valhalla.Core.GraphicsSettings.Json", VALHALLA_CORE_TEST_FLAGS)

bool FValhallaGraphicsJsonTest::RunTest(const FString& /*Parameters*/)
{
	FValhallaGraphicsSettings S;
	S.SetPreset(EValhallaGraphicsQuality::Epic);
	S.bGlobalIllumination = false;
	S.ResolutionScale = 75;
	S.FrameRateCap = 144;
	S.bVSync = true;
	S.bMotionBlur = true;
	S.Touch();
	S.UnknownFields.Add(TEXT("FutureField"), MakeShared<FJsonValueString>(TEXT("kept")));

	FValhallaGraphicsSettings Back;
	TArray<FString> Warnings;
	TestTrue(TEXT("round trip parses"), FValhallaGraphicsSettings::FromJsonString(S.ToJsonString(), Back, &Warnings));
	TestEqual(TEXT("no warnings"), Warnings.Num(), 0);
	TestTrue(TEXT("same values"), Back.SameValues(S));
	TestEqual(TEXT("UpdatedAt kept"), Back.UpdatedAt, S.UpdatedAt);
	TestTrue(TEXT("UpdatedAt parses"), Back.GetUpdatedAtTime() > FDateTime(2026, 1, 1));
	TestTrue(TEXT("unknown field kept"), Back.UnknownFields.Contains(TEXT("FutureField")));
	TestTrue(TEXT("written as lower-case quality"), S.ToJsonString(false).Contains(TEXT("\"Quality\":\"epic\"")));

	// Missing fields keep their defaults; Low without a GI field gets Low's GI (off).
	FValhallaGraphicsSettings Partial;
	TestTrue(TEXT("partial parses"), FValhallaGraphicsSettings::FromJsonString(TEXT("{\"Quality\":\"LOW\",\"FrameRateCap\":60}"), Partial));
	TestEqual(TEXT("quality is case-insensitive"), static_cast<int32>(Partial.Quality), static_cast<int32>(EValhallaGraphicsQuality::Low));
	TestFalse(TEXT("Low's own GI choice"), Partial.bGlobalIllumination);
	TestFalse(TEXT("so not custom"), Partial.IsCustom());
	TestEqual(TEXT("cap read"), Partial.FrameRateCap, 60);
	TestEqual(TEXT("missing scale = default"), Partial.ResolutionScale, 100);

	// Wrong types warn and keep the default; out of range is clamped.
	FValhallaGraphicsSettings Bad;
	Warnings.Reset();
	FValhallaGraphicsSettings::FromJsonString(TEXT("{\"Quality\":\"ultra\",\"VSync\":\"yes\",\"ResolutionScale\":10,\"FrameRateCap\":5}"), Bad, &Warnings);
	TestEqual(TEXT("unknown quality -> High"), static_cast<int32>(Bad.Quality), static_cast<int32>(EValhallaGraphicsQuality::High));
	TestFalse(TEXT("VSync of the wrong type -> default"), Bad.bVSync);
	TestEqual(TEXT("scale clamped up to 50"), Bad.ResolutionScale, FValhallaGraphicsSettings::MinResolutionScale);
	TestEqual(TEXT("a cap under 30 becomes 30"), Bad.FrameRateCap, FValhallaGraphicsSettings::MinFrameRateCap);
	TestTrue(TEXT("warned"), Warnings.Num() >= 3);

	FValhallaGraphicsSettings Garbage;
	TestFalse(TEXT("not an object -> false"), FValhallaGraphicsSettings::FromJsonString(TEXT("[1,2]"), Garbage));
	TestTrue(TEXT("and the defaults"), Garbage.SameValues(FValhallaGraphicsSettings()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaGraphicsPresetsTest, "Valhalla.Core.GraphicsSettings.Presets", VALHALLA_CORE_TEST_FLAGS)

bool FValhallaGraphicsPresetsTest::RunTest(const FString& /*Parameters*/)
{
	FValhallaGraphicsSettings S;
	S.SetPreset(EValhallaGraphicsQuality::Low);
	TestFalse(TEXT("Low: GI off"), S.bGlobalIllumination);
	TestEqual(TEXT("Low: GI level 0"), S.GetGlobalIlluminationLevel(), 0);
	S.bGlobalIllumination = true;
	TestTrue(TEXT("Low with GI is custom"), S.IsCustom());
	TestEqual(TEXT("shown as Custom"), S.GetPresetDisplayName(), FString(TEXT("Custom")));
	TestEqual(TEXT("Low with GI: at least level 1 (Lumen)"), S.GetGlobalIlluminationLevel(), 1);

	S.SetPreset(EValhallaGraphicsQuality::Epic);
	TestTrue(TEXT("a preset resets GI to its own"), S.bGlobalIllumination);
	TestFalse(TEXT("so not custom"), S.IsCustom());
	TestEqual(TEXT("Epic GI level 3"), S.GetGlobalIlluminationLevel(), 3);
	S.bMotionBlur = true;
	TestEqual(TEXT("Epic motion blur -> 4"), S.GetMotionBlurQualityCVar(), 4);
	S.SetPreset(EValhallaGraphicsQuality::Medium);
	TestEqual(TEXT("Medium motion blur -> 3"), S.GetMotionBlurQualityCVar(), 3);
	S.bGlobalIllumination = false;
	TestTrue(TEXT("Medium without GI is custom"), S.IsCustom());
	TestEqual(TEXT("GI off -> level 0"), S.GetGlobalIlluminationLevel(), 0);

	const TArray<int32>& Caps = FValhallaGraphicsSettings::GetFrameRateCapSteps();
	TestEqual(TEXT("the first cap step is none"), Caps[0], 0);
	for (int32 Index = 1; Index < Caps.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("cap step %d is in range and rising"), Caps[Index]),
			Caps[Index] > Caps[Index - 1] && Caps[Index] >= FValhallaGraphicsSettings::MinFrameRateCap && Caps[Index] <= FValhallaGraphicsSettings::MaxFrameRateCap);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
