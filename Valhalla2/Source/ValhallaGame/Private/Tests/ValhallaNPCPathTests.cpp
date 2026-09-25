// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-16: the rules an NPC walks a nav-mesh path by (FValhallaNPCPath).
//
// A broken path follower fails quietly: an NPC that re-plans every step costs
// CPU nobody notices until a camp is pulled; one that never re-plans chases
// where you *were*; one that skips the live goal at the end stops a metre short
// and never swings; one whose stuck clock never resets warps home mid-chase.
// All of it is arithmetic on a struct, so it is pinned here without a world or
// a nav mesh. The nav queries themselves are checked in PIE (PLAN.md, B-16).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaNPCPath.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaNPCPathSteeringTest,
	"Valhalla.Game.NPC.PathSteering",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaNPCPathSteeringTest::RunTest(const FString& /*Parameters*/)
{
	using EMode = FValhallaNPCPath::EMode;

	// ── Re-planning ──────────────────────────────────────────────────────
	{
		FValhallaNPCPath Path;
		const FVector Goal(1000.0, 0.0, 0.0);
		TestTrue(TEXT("a fresh path needs a plan"), FValhallaNPCPath::NeedsReplan(Path, Goal, 10.0));

		FValhallaNPCPath::SetStraight(Path, EMode::Direct, Goal, 10.0);
		TestFalse(TEXT("same goal, same moment: no re-plan"), FValhallaNPCPath::NeedsReplan(Path, Goal, 10.0));
		TestFalse(TEXT("goal moved 40 cm: no re-plan"), FValhallaNPCPath::NeedsReplan(Path, Goal + FVector(40.0, 0.0, 0.0), 10.1));
		TestTrue(TEXT("goal moved 50 cm: re-plan"), FValhallaNPCPath::NeedsReplan(Path, Goal + FVector(0.0, 50.0, 0.0), 10.1));
		TestFalse(TEXT("height alone does not count"), FValhallaNPCPath::NeedsReplan(Path, Goal + FVector(0.0, 0.0, 400.0), 10.1));
		TestTrue(TEXT("half a second old: re-plan"), FValhallaNPCPath::NeedsReplan(Path, Goal, 10.5));
	}

	// ── Walking corners, then the live goal ──────────────────────────────
	{
		// Round the end of a wall along X: start (0,0), corner (0,300), corner
		// (500,300), goal (500,0).
		FValhallaNPCPath Path;
		const TArray<FVector> Points = { FVector(0, 0, 0), FVector(0, 300, 0), FVector(500, 300, 0), FVector(500, 0, 0) };
		FValhallaNPCPath::SetPath(Path, Points, /*bPartial=*/false, Points.Last(), 0.0);
		TestTrue(TEXT("a 4-point path is a path"), Path.Mode == EMode::Path);

		FVector Dir = FValhallaNPCPath::SteerDirection(Path, FVector(0, 0, 0), FVector(500, 0, 0));
		TestTrue(TEXT("first heads for corner 1 (+Y), not the goal (+X)"), Dir.Equals(FVector(0, 1, 0), 1e-4));

		Dir = FValhallaNPCPath::SteerDirection(Path, FVector(0, 280, 0), FVector(500, 0, 0));
		TestTrue(TEXT("within 30 cm of corner 1 it turns for corner 2 (+X)"), Dir.Equals(FVector(1, 0, 0), 0.05));
		TestEqual(TEXT("and has advanced to it"), Path.NextIndex, 2);

		// Past the last corner, the live goal wins over the planned end point:
		// the target has stepped 100 cm since the plan.
		Dir = FValhallaNPCPath::SteerDirection(Path, FVector(490, 300, 0), FVector(600, 0, 0));
		const FVector Expected = FVector(110, -300, 0).GetSafeNormal();
		TestTrue(TEXT("after the corners it steers at the live goal"), Dir.Equals(Expected, 1e-4));
		TestEqual(TEXT("steering is flat"), Dir.Z, 0.0);
	}

	// ── Direct and fallback both steer straight ──────────────────────────
	{
		FValhallaNPCPath Path;
		FValhallaNPCPath::SetStraight(Path, EMode::Direct, FVector(0, 0, 0), 0.0);
		TestTrue(TEXT("direct mode steers at the goal"),
			FValhallaNPCPath::SteerDirection(Path, FVector(0, 0, 0), FVector(0, -200, 50)).Equals(FVector(0, -1, 0), 1e-4));

		const TArray<FVector> One = { FVector(0, 0, 0) };
		FValhallaNPCPath::SetPath(Path, One, false, FVector(100, 0, 0), 0.0);
		TestTrue(TEXT("a one-point path is treated as no path"), Path.Mode == EMode::Fallback);
		TestTrue(TEXT("and still steers straight"),
			FValhallaNPCPath::SteerDirection(Path, FVector(0, 0, 0), FVector(100, 0, 0)).Equals(FVector(1, 0, 0), 1e-4));

		TestTrue(TEXT("on top of the goal there is no direction"),
			FValhallaNPCPath::SteerDirection(Path, FVector(100, 0, 0), FVector(100, 0, 0)).IsNearlyZero());
	}

	// ── Stuck detection ──────────────────────────────────────────────────
	{
		FValhallaNPCPath Path;
		TestFalse(TEXT("the first step starts the clock"), FValhallaNPCPath::UpdateStuck(Path, FVector(0, 0, 0), 0.0));
		TestFalse(TEXT("2.9 s without progress is not stuck"), FValhallaNPCPath::UpdateStuck(Path, FVector(10, 0, 0), 2.9));
		TestTrue(TEXT("3 s without 25 cm of progress is stuck"), FValhallaNPCPath::UpdateStuck(Path, FVector(20, 0, 0), 3.0));

		FValhallaNPCPath Moving;
		FValhallaNPCPath::UpdateStuck(Moving, FVector(0, 0, 0), 0.0);
		TestFalse(TEXT("25 cm of progress resets the clock"), FValhallaNPCPath::UpdateStuck(Moving, FVector(25, 0, 0), 2.0));
		TestFalse(TEXT("so 4 s after the start is not stuck"), FValhallaNPCPath::UpdateStuck(Moving, FVector(30, 0, 0), 4.0));

		FValhallaNPCPath::ClearStuck(Moving);
		TestFalse(TEXT("after ClearStuck the clock starts again"), FValhallaNPCPath::UpdateStuck(Moving, FVector(30, 0, 0), 9.0));
	}

	// ── Reset ────────────────────────────────────────────────────────────
	{
		FValhallaNPCPath Path;
		FValhallaNPCPath::SetStraight(Path, EMode::Direct, FVector(1, 2, 3), 5.0);
		Path.Reset();
		TestTrue(TEXT("Reset forgets the plan"), Path.Mode == EMode::None && Path.PlannedAt < 0.0 && Path.StuckSince < 0.0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
