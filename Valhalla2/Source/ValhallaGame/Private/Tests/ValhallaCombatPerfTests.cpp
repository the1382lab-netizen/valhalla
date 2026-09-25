// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-27 Phase 2, the combat presentation's cost rules:
//
//   Valhalla.Game.CombatPerf.LogAppend — FValhallaLogAppendPlan, what the
//     combat log and the chat do with new lines: reuse the oldest line widgets
//     once full, create the rest, rebuild only when the new lines alone fill
//     the list (before B-27 every line rebuilt all of it).
//   Valhalla.Game.CombatPerf.EffectRange — UValhallaVfxLibrary::
//     IsWithinEffectRange, the distance past which a one-shot effect is not
//     spawned, and the per-frame budget's constants.
//
// The widget and Niagara side (the lines on screen, pooled components going
// back to the pool) is checked in the combat benchmark (Tools/perf).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaGameHUDWidget.h"
#include "ValhallaVfxLibrary.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaCombatPerfLogAppendTest, "Valhalla.Game.CombatPerf.LogAppend", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaCombatPerfLogAppendTest::RunTest(const FString& /*Parameters*/)
{
	auto Check = [this](int32 Shown, int32 New, int32 Max, int32 Recycle, int32 Create, bool bRebuild)
	{
		const FValhallaLogAppendPlan Plan = FValhallaLogAppendPlan::Make(Shown, New, Max);
		const FString What = FString::Printf(TEXT("shown %d + new %d (max %d)"), Shown, New, Max);
		TestEqual(*(What + TEXT(": recycle")), Plan.Recycle, Recycle);
		TestEqual(*(What + TEXT(": create")), Plan.Create, Create);
		TestEqual(*(What + TEXT(": rebuild")), Plan.bRebuild, bRebuild);
	};
	Check(0, 1, 50, 0, 1, false);    // first line
	Check(10, 3, 50, 0, 3, false);   // room for all three
	Check(50, 1, 50, 1, 0, false);   // full: the oldest line comes back as the new one
	Check(49, 3, 50, 2, 1, false);   // one free slot, two reused
	Check(20, 50, 50, 0, 0, true);   // the new lines alone fill it: rebuild
	Check(60, 1, 50, 0, 0, true);    // more shown than allowed (the limit shrank): rebuild
	Check(10, 0, 50, 0, 0, false);   // nothing new: nothing to do
	Check(8, 2, 8, 2, 0, false);     // idle chat, 8 lines, full
	Check(3, 2, 0, 0, 0, false);     // a list that shows nothing
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaCombatPerfEffectRangeTest, "Valhalla.Game.CombatPerf.EffectRange", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaCombatPerfEffectRangeTest::RunTest(const FString& /*Parameters*/)
{
	const FVector Viewer(1000.0, 1000.0, 100.0);
	const float Vision = 1200.f; // a warrior's 12 m
	TestTrue(TEXT("next to the viewer"), UValhallaVfxLibrary::IsWithinEffectRange(Viewer + FVector(100.0, 0.0, 0.0), Viewer, Vision));
	TestTrue(TEXT("at the edge of vision"), UValhallaVfxLibrary::IsWithinEffectRange(Viewer + FVector(1200.0, 0.0, 0.0), Viewer, Vision));
	TestTrue(TEXT("just past vision, inside the margin"), UValhallaVfxLibrary::IsWithinEffectRange(Viewer + FVector(0.0, 1400.0, 0.0), Viewer, Vision));
	TestFalse(TEXT("well past vision"), UValhallaVfxLibrary::IsWithinEffectRange(Viewer + FVector(1600.0, 0.0, 0.0), Viewer, Vision));
	TestFalse(TEXT("another camp, 3.5 km away"), UValhallaVfxLibrary::IsWithinEffectRange(FVector(83050.0, 4760.0, 0.0), FVector(80480.0, 7300.0, 0.0), Vision));
	TestTrue(TEXT("height does not count"), UValhallaVfxLibrary::IsWithinEffectRange(Viewer + FVector(0.0, 0.0, 5000.0), Viewer, Vision));
	TestTrue(TEXT("no vision range: no limit"), UValhallaVfxLibrary::IsWithinEffectRange(FVector(1.0e6, 0.0, 0.0), Viewer, 0.f));
	TestTrue(TEXT("the per-frame budget allows a group fight's burst"), UValhallaVfxLibrary::MaxOneShotsPerFrame >= 8);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
