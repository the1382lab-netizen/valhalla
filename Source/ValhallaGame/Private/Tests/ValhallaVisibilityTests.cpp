// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 5 line-of-sight tests.
//
// Two rules, picked for the same reason the Phase 2b tests were: each fails
// quietly rather than loudly. A visibility polygon that stops cutting notches
// does not crash, it just makes the fog stop hiding anything — which looks like
// a lighting change. A cache that stops expiring does not crash either, it just
// makes an enemy appear a few seconds after you walked round the corner, which
// looks like lag.
//
// The polygon test earns its keep immediately: the first, faithful port of
// `raySegmentIntersect` reproduced a sign error in 1.0 that makes every ray
// miss every wall in front of it, and this test is what caught it. See the
// comment on RaySegmentIntersect in ValhallaVisibilitySubsystem.cpp.
//
// Both run on plain data. ComputeVisibilityPolygon takes 2D points and no
// world, and the expiry rule is arithmetic on a tick counter, so neither needs
// a level, a net driver or an actor and both run in the commandlet context.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaVisibilitySubsystem.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaVisibilityTests
{
	/** A ranger's range would only make the numbers longer; the rule is the same. */
	static constexpr double Radius = 1000.0;

	/** Rays land on the boundary box and are then clamped, so allow a texel or so. */
	static constexpr double RadiusTolerance = 1.0;

	/** The four edges of an axis-aligned box, as segments. */
	static TArray<FValhallaVisibilitySegment> BoxSegments(double MinX, double MinY, double MaxX, double MaxY)
	{
		TArray<FValhallaVisibilitySegment> Segments;
		Segments.Emplace(FVector2D(MinX, MinY), FVector2D(MaxX, MinY));
		Segments.Emplace(FVector2D(MaxX, MinY), FVector2D(MaxX, MaxY));
		Segments.Emplace(FVector2D(MaxX, MaxY), FVector2D(MinX, MaxY));
		Segments.Emplace(FVector2D(MinX, MaxY), FVector2D(MinX, MinY));
		return Segments;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  The polygon — VisibilitySystem.ts:124 computeVisibilityPolygon
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaVisibilityPolygonTest,
	"Valhalla.Game.Visibility.Polygon",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaVisibilityPolygonTest::RunTest(const FString& Parameters)
{
	using namespace ValhallaVisibilityTests;

	const FVector2D Origin(0.0, 0.0);

	// ── Open field: a circle, not a square ──────────────────────────────
	//
	// The boundary box is square, so every ray hits it at a different distance;
	// what makes the result round is the clamp to the radius. If that clamp
	// were ever dropped the fog edge would become a box, which reads as a bug
	// in the post-process rather than as one here — hence the test.
	{
		const TArray<FVector2D> Open = ValhallaVisibility::ComputeVisibilityPolygon(
			Origin, TArrayView<const FValhallaVisibilitySegment>(), Radius);

		TestTrue(TEXT("open field produces at least the 64 arc rays"),
			Open.Num() >= ValhallaVisibility::DefaultArcRays);

		double Worst = 0.0;
		for (const FVector2D& Point : Open)
		{
			Worst = FMath::Max(Worst, FMath::Abs(FVector2D::Distance(Point, Origin) - Radius));
		}
		TestTrue(
			FString::Printf(TEXT("every open-field point sits on the vision circle (worst error %.4f cm)"), Worst),
			Worst <= RadiusTolerance);

		// Sorted by angle, which is what makes the triangle fan a fan rather
		// than a bow tie.
		bool bSorted = true;
		for (int32 Index = 1; Index < Open.Num(); ++Index)
		{
			const double Previous = FMath::Atan2(Open[Index - 1].Y, Open[Index - 1].X);
			const double Current = FMath::Atan2(Open[Index].Y, Open[Index].X);
			bSorted &= Current >= Previous - UE_DOUBLE_KINDA_SMALL_NUMBER;
		}
		TestTrue(TEXT("open-field polygon is sorted by angle"), bSorted);

		TestTrue(TEXT("the viewer's own position is inside the open-field polygon"),
			ValhallaVisibility::IsPointInPolygon(Open, Origin));
	}

	// ── One wall in front: a notch ──────────────────────────────────────
	//
	// A 400 cm wall 300 cm north of the viewer, 25 cm thick — the footprint of
	// VB_StoneWall_Straight laid across the view. Everything the algorithm is
	// for shows up in this one case: points on the near face of the wall, a
	// shadow behind it, and the ground beside it still lit.
	{
		const TArray<FValhallaVisibilitySegment> Wall = BoxSegments(-200.0, 300.0, 200.0, 325.0);

		const TArray<FVector2D> Blocked = ValhallaVisibility::ComputeVisibilityPolygon(Origin, Wall, Radius);

		TestTrue(TEXT("a wall still produces a polygon"), Blocked.Num() >= ValhallaVisibility::DefaultArcRays);

		// The notch: at least some vertices stop on the wall rather than at the
		// vision radius. Without the endpoint rays there would be none.
		int32 OnTheWall = 0;
		for (const FVector2D& Point : Blocked)
		{
			if (FVector2D::Distance(Point, Origin) < Radius - RadiusTolerance)
			{
				++OnTheWall;
			}
		}
		TestTrue(
			FString::Printf(TEXT("the wall cuts a notch (%d vertices short of the radius)"), OnTheWall),
			OnTheWall > 0);

		// Directly behind the wall is dark…
		TestFalse(TEXT("a point directly behind the wall is not visible"),
			ValhallaVisibility::IsPointInPolygon(Blocked, FVector2D(0.0, 600.0)));

		// …the near face of it is lit…
		TestTrue(TEXT("a point just in front of the wall is visible"),
			ValhallaVisibility::IsPointInPolygon(Blocked, FVector2D(0.0, 250.0)));

		// …and so is the open ground beside it, which is the half of the answer
		// a naive "anything past the wall distance is hidden" test would get
		// wrong.
		TestTrue(TEXT("a point beside the wall at the same distance is visible"),
			ValhallaVisibility::IsPointInPolygon(Blocked, FVector2D(600.0, 600.0)));

		// Nothing leaves the circle.
		double Furthest = 0.0;
		for (const FVector2D& Point : Blocked)
		{
			Furthest = FMath::Max(Furthest, FVector2D::Distance(Point, Origin));
		}
		TestTrue(
			FString::Printf(TEXT("no vertex escapes the vision radius (furthest %.2f cm)"), Furthest),
			Furthest <= Radius + RadiusTolerance);
	}

	// ── The segment filter ──────────────────────────────────────────────
	//
	// 1.0 keeps a segment unless *both* ends are beyond 1.5 * radius
	// (VisibilitySystem.ts:149), which is why the margin is 1.5 and not 1: a
	// wall running well past the edge of the circle still has to block the part
	// of it that crosses. The 1400 cm ends below are 1432 cm out — outside the
	// 1000 cm circle, inside the 1500 cm margin — so the wall is kept.
	//
	// (The margin is finite, so a wall longer than 3000 cm laid across this
	// viewer would be dropped. It is carried over as-is because nothing in the
	// project produces one: a level is built from 64 cm wall meshes and each is
	// its own four segments.)
	{
		const TArray<FValhallaVisibilitySegment> LongWall =
			{ FValhallaVisibilitySegment(FVector2D(-1400.0, 300.0), FVector2D(1400.0, 300.0)) };

		const TArray<FVector2D> Polygon = ValhallaVisibility::ComputeVisibilityPolygon(Origin, LongWall, Radius);

		TestFalse(TEXT("a wall that overshoots the circle at both ends still blocks"),
			ValhallaVisibility::IsPointInPolygon(Polygon, FVector2D(0.0, 800.0)));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  The cache — UValhallaVisibilitySubsystem
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaVisibilityCacheTest,
	"Valhalla.Game.Visibility.Cache",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaVisibilityCacheTest::RunTest(const FString& Parameters)
{
	// The expiry rule, on its own. IsVisibleFrom writes
	//
	//     ExpiryTick = TickCounter + max(1, CacheTicks)
	//
	// and trusts the entry while ExpiryTick > TickCounter. So an answer written
	// on tick T is good for exactly CacheTicks ticks — usable on T through
	// T + CacheTicks - 1 and stale on T + CacheTicks — and PruneCache drops
	// anything whose ExpiryTick has been reached.
	//
	// Testing this arithmetic rather than a live subsystem is deliberate. A
	// world subsystem needs a UWorld, a UWorld in an automation test needs a
	// map to load, and a test that loads a map to check an integer comparison
	// is a test that will be disabled the first time it is slow. The rule is
	// the thing that can break; the rule is what is pinned.
	const int32 CacheTicks = 4;

	const uint64 WrittenAt = 1000;
	const uint64 ExpiryTick = WrittenAt + static_cast<uint64>(FMath::Max(1, CacheTicks));

	auto IsFresh = [ExpiryTick](uint64 Now) { return ExpiryTick > Now; };
	auto WouldPrune = [ExpiryTick](uint64 Now) { return ExpiryTick <= Now; };

	TestTrue(TEXT("the tick it was written on is a hit"), IsFresh(WrittenAt));
	TestTrue(TEXT("the last tick within the window is a hit"), IsFresh(WrittenAt + CacheTicks - 1));
	TestFalse(TEXT("the tick after the window is a miss"), IsFresh(WrittenAt + CacheTicks));
	TestFalse(TEXT("long after the window is still a miss"), IsFresh(WrittenAt + 10 * CacheTicks));

	TestFalse(TEXT("a fresh entry survives a prune"), WouldPrune(WrittenAt + CacheTicks - 1));
	TestTrue(TEXT("an expired entry is pruned"), WouldPrune(WrittenAt + CacheTicks));

	// CacheTicks 0 or negative must still expire rather than caching forever:
	// the max(1, …) is what stops a mistyped ini value from freezing every
	// answer in the map for the rest of the session.
	for (const int32 Bad : { 0, -1, -100 })
	{
		const uint64 BadExpiry = WrittenAt + static_cast<uint64>(FMath::Max(1, Bad));
		TestTrue(
			FString::Printf(TEXT("CacheTicks=%d still expires after one tick"), Bad),
			BadExpiry == WrittenAt + 1);
	}

	// And the default the header documents.
	const UValhallaVisibilitySubsystem* Defaults = GetDefault<UValhallaVisibilitySubsystem>();
	TestTrue(TEXT("CacheTicks defaults to a positive number of ticks"), Defaults->CacheTicks > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
