// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-27 Phase 5, the vision fog's cheaper path (AValhallaFogRenderer):
//
//   Valhalla.Game.Fog.Recompute — NeedsRecompute: nothing to do while the pawn
//     stands still; a move of RecomputeDistanceCm, a new range, new bounds (a
//     zone change), new walls (a level streamed) or a movable blocker in range
//     recompute.
//   Valhalla.Game.Fog.BlockerCache — MakeBoxSegments gives a rotated wall its
//     own footprint, not its bounding square; SelectSegmentsInRange keeps the
//     walls within range (3D, as the old overlap sphere) and flags movable ones.
//
// The cache being built per level and the explored mask per zone are checked
// in PIE: the "fog: N vision blockers in M levels" and "fog bounds -> zone ...
// its / a new explored mask" log lines.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaFogRenderer.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaFogRecomputeTest, "Valhalla.Game.Fog.Recompute", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaFogRecomputeTest::RunTest(const FString& /*Parameters*/)
{
	AValhallaFogRenderer::FFogKey Last;
	Last.Origin = FVector2D(1000.0, 2000.0);
	Last.Range = 1200.f;
	Last.Bounds = FBox2D(FVector2D(0.0, 0.0), FVector2D(10000.0, 10000.0));
	Last.BlockerGeneration = 3;

	auto With = [&Last](TFunctionRef<void(AValhallaFogRenderer::FFogKey&)> Change)
	{
		AValhallaFogRenderer::FFogKey Key = Last;
		Change(Key);
		return Key;
	};

	TestTrue(TEXT("nothing computed yet"), AValhallaFogRenderer::NeedsRecompute(nullptr, Last, false));
	TestFalse(TEXT("standing still"), AValhallaFogRenderer::NeedsRecompute(&Last, Last, false));
	TestFalse(TEXT("a 5 cm shuffle"), AValhallaFogRenderer::NeedsRecompute(&Last, With([](auto& K) { K.Origin.X += 5.0; }), false));
	TestTrue(TEXT("a 10 cm step"), AValhallaFogRenderer::NeedsRecompute(&Last, With([](auto& K) { K.Origin.Y += AValhallaFogRenderer::RecomputeDistanceCm; }), false));
	TestTrue(TEXT("a new vision range"), AValhallaFogRenderer::NeedsRecompute(&Last, With([](auto& K) { K.Range = 1800.f; }), false));
	TestTrue(TEXT("a zone change (new bounds)"), AValhallaFogRenderer::NeedsRecompute(&Last, With([](auto& K) { K.Bounds = FBox2D(FVector2D(40000.0, 0.0), FVector2D(50000.0, 10000.0)); }), false));
	TestTrue(TEXT("walls changed (a level streamed)"), AValhallaFogRenderer::NeedsRecompute(&Last, With([](auto& K) { ++K.BlockerGeneration; }), false));
	TestTrue(TEXT("a movable blocker in range"), AValhallaFogRenderer::NeedsRecompute(&Last, Last, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaFogBlockerCacheTest, "Valhalla.Game.Fog.BlockerCache", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaFogBlockerCacheTest::RunTest(const FString& /*Parameters*/)
{
	// A 64 x 25 wall turned 90 degrees at (500, 0): its footprint is 25 wide in X, 64 long in Y.
	const FBox Local(FVector(-32.0, -12.5, 0.0), FVector(32.0, 12.5, 300.0));
	const FTransform Turned(FRotator(0.0, 90.0, 0.0), FVector(500.0, 0.0, 0.0));
	FValhallaVisibilitySegment Edges[4];
	AValhallaFogRenderer::MakeBoxSegments(Local, Turned, Edges);
	FBox2D Footprint(ForceInit);
	for (const FValhallaVisibilitySegment& Edge : Edges)
	{
		Footprint += Edge.A;
		Footprint += Edge.B;
	}
	TestTrue(TEXT("rotated wall: 25 cm across X"), FMath::IsNearlyEqual(Footprint.GetSize().X, 25.0, 0.01));
	TestTrue(TEXT("rotated wall: 64 cm along Y"), FMath::IsNearlyEqual(Footprint.GetSize().Y, 64.0, 0.01));
	TestTrue(TEXT("rotated wall: centred on its actor"), Footprint.GetCenter().Equals(FVector2D(500.0, 0.0), 0.01));
	TestTrue(TEXT("the four edges close"), Edges[3].B.Equals(Edges[0].A, 0.01));

	auto Entry = [](const FVector& Centre, bool bMovable)
	{
		AValhallaFogRenderer::FBlockerEntry Out;
		Out.Bounds = FBox(Centre - FVector(50.0), Centre + FVector(50.0));
		Out.bMovable = bMovable;
		const FTransform At(Centre);
		AValhallaFogRenderer::MakeBoxSegments(FBox(FVector(-50.0), FVector(50.0)), At, Out.Segments);
		return Out;
	};
	TArray<AValhallaFogRenderer::FBlockerEntry> Entries;
	Entries.Add(Entry(FVector(500.0, 0.0, 0.0), false));      // near
	Entries.Add(Entry(FVector(5000.0, 0.0, 0.0), false));     // far
	Entries.Add(Entry(FVector(0.0, 0.0, 4000.0), false));     // right above, out of the 3D range
	Entries.Add(Entry(FVector(0.0, 1250.0, 0.0), false));     // its box reaches into range

	TArray<FValhallaVisibilitySegment> Segments;
	bool bMovable = true;
	AValhallaFogRenderer::SelectSegmentsInRange(Entries, FVector::ZeroVector, 1200.f, Segments, bMovable);
	TestEqual(TEXT("two walls in range, four edges each"), Segments.Num(), 8);
	TestFalse(TEXT("no movable blocker"), bMovable);

	// A movable entry without a component is skipped (nothing to read) and flags nothing.
	Entries.Add(Entry(FVector(300.0, 0.0, 0.0), true));
	AValhallaFogRenderer::SelectSegmentsInRange(Entries, FVector::ZeroVector, 1200.f, Segments, bMovable);
	TestEqual(TEXT("a movable entry whose component is gone adds nothing"), Segments.Num(), 8);

	AValhallaFogRenderer::SelectSegmentsInRange(TArray<AValhallaFogRenderer::FBlockerEntry>(), FVector::ZeroVector, 1200.f, Segments, bMovable);
	TestEqual(TEXT("no walls: no segments"), Segments.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
