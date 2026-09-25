// Copyright Valhalla 2.0. All Rights Reserved.
//
// The idle-movement rules (FValhallaNPCPatrolRules): which stop comes next on
// a Loop or PingPong route, when a stop is reached, how long an NPC pauses,
// where a follower stands and how fast it walks, where a roam goes without a
// nav mesh, and whether a spawn cycle is the rare one.
//
// Each of these fails quietly in play: a PingPong guard that walks off the end
// of its route (an index past the last stop is a crash, or a walk to 0,0,0),
// a one-point route that never turns round, a follower that twitches on and
// off behind a slow leader, a pause range typed backwards that never pauses,
// or a 20% rare that is really 19% or 21%. The world half (the nav path, the
// resume after a fight, the pair walking together) is checked in PIE; see
// PLAN.md, B-10 part 2.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaNPCPatrolRules.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaNPCPatrolTest,
	"Valhalla.Game.NPC.Patrol",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaNPCPatrolTest::RunTest(const FString& /*Parameters*/)
{
	using R = FValhallaNPCPatrolRules;
	const EValhallaPatrolMode None = EValhallaPatrolMode::None;
	const EValhallaPatrolMode Loop = EValhallaPatrolMode::Loop;
	const EValhallaPatrolMode PingPong = EValhallaPatrolMode::PingPong;

	// Walk a route from stop 0 for Steps stops and write the stops down.
	auto Walk = [](EValhallaPatrolMode Mode, int32 StopCount, int32 Steps) -> FString
	{
		int32 Current = 0;
		int32 Direction = 1;
		FString Out = FString::FromInt(Current);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Current = R::NextStop(Mode, StopCount, Current, Direction);
			Out += FString::Printf(TEXT(",%d"), Current);
		}
		return Out;
	};

	// ── Next stop ────────────────────────────────────────────────────────
	TestEqual(TEXT("loop of four wraps to the spawn point"), Walk(Loop, 4, 6), FString(TEXT("0,1,2,3,0,1,2")));
	TestEqual(TEXT("ping-pong of four turns at both ends"), Walk(PingPong, 4, 8), FString(TEXT("0,1,2,3,2,1,0,1,2")));
	TestEqual(TEXT("one patrol point (two stops): loop is out and back"), Walk(Loop, 2, 4), FString(TEXT("0,1,0,1,0")));
	TestEqual(TEXT("one patrol point (two stops): ping-pong is out and back"), Walk(PingPong, 2, 4), FString(TEXT("0,1,0,1,0")));
	TestEqual(TEXT("no patrol points: stays at the spawn point"), Walk(PingPong, 1, 3), FString(TEXT("0,0,0,0")));
	TestEqual(TEXT("mode None: stays put"), Walk(None, 4, 3), FString(TEXT("0,0,0,0")));

	int32 Direction = 1;
	TestEqual(TEXT("no stops at all: INDEX_NONE"), R::NextStop(Loop, 0, 0, Direction), static_cast<int32>(INDEX_NONE));
	Direction = -1;
	TestEqual(TEXT("an index past a shrunken route restarts at 0"), R::NextStop(PingPong, 3, 7, Direction), 0);
	TestEqual(TEXT("...heading outward"), Direction, 1);
	Direction = 0;
	TestEqual(TEXT("a zero direction counts as outward"), R::NextStop(PingPong, 3, 1, Direction), 2);

	TestFalse(TEXT("mode None is no route"), R::HasRoute(None, 4));
	TestFalse(TEXT("the spawn point alone is no route"), R::HasRoute(Loop, 1));
	TestTrue(TEXT("spawn point plus one point is a route"), R::HasRoute(PingPong, 2));

	// ── Arrival (XY, height ignored) ─────────────────────────────────────
	TestTrue(TEXT("35 cm from the stop: arrived"), R::HasArrived(FVector(35.0, 0.0, 0.0), FVector::ZeroVector));
	TestFalse(TEXT("36 cm: not yet"), R::HasArrived(FVector(36.0, 0.0, 0.0), FVector::ZeroVector));
	TestTrue(TEXT("a capsule centre 90 cm above a floor stop has arrived"), R::HasArrived(FVector(10.0, 10.0, 90.0), FVector::ZeroVector));

	// ── Pauses ───────────────────────────────────────────────────────────
	TestEqual(TEXT("pause roll 0 is the minimum"), R::RollPause(5.0, 10.0, 0.0), 5.0);
	TestEqual(TEXT("pause roll 1 is the maximum"), R::RollPause(5.0, 10.0, 1.0), 10.0);
	TestEqual(TEXT("pause roll 0.5 is the middle"), R::RollPause(5.0, 10.0, 0.5), 7.5);
	TestEqual(TEXT("a max below the min pauses for the min"), R::RollPause(8.0, 3.0, 0.9), 8.0);
	TestEqual(TEXT("a negative min counts as 0"), R::RollPause(-2.0, 4.0, 0.0), 0.0);
	TestEqual(TEXT("a random number outside [0,1] is clamped"), R::RollPause(5.0, 10.0, 3.0), 10.0);

	// ── Followers ────────────────────────────────────────────────────────
	const FVector Behind = R::FollowSpot(FVector(1000.0, 500.0, 90.0), 0.0);
	TestTrue(TEXT("facing +X, the spot is 120 cm behind on -X"), Behind.Equals(FVector(880.0, 500.0, 90.0), 0.01));
	const FVector BehindNorth = R::FollowSpot(FVector(0.0, 0.0, 0.0), 90.0, 100.0);
	TestTrue(TEXT("facing +Y, the spot is behind on -Y"), BehindNorth.Equals(FVector(0.0, -100.0, 0.0), 0.01));

	TestTrue(TEXT("a walking follower keeps walking at 41 cm"), R::FollowerShouldMove(41.0, true));
	TestFalse(TEXT("and stops at 40"), R::FollowerShouldMove(40.0, true));
	TestFalse(TEXT("a standing follower stays put at 79 cm (no twitching)"), R::FollowerShouldMove(79.0, false));
	TestTrue(TEXT("and sets off beyond 80"), R::FollowerShouldMove(81.0, false));

	TestEqual(TEXT("keeping up: the leader's pace"), R::FollowSpeedScale(100.0, 0.5), 0.5);
	TestEqual(TEXT("halfway into the catch-up band: halfway to full speed"), R::FollowSpeedScale(300.0, 0.5), 0.75);
	TestEqual(TEXT("far behind: full speed"), R::FollowSpeedScale(1000.0, 0.5), 1.0);
	TestEqual(TEXT("a standing leader still leaves a walking pace"), R::FollowSpeedScale(100.0, 0.0), 0.1);
	TestEqual(TEXT("a faster leader is capped at full speed"), R::FollowSpeedScale(100.0, 1.7), 1.0);

	// ── Roaming without a nav mesh ───────────────────────────────────────
	const FVector Home(100.0, 200.0, 50.0);
	TestTrue(TEXT("distance 0 is home"), R::WanderPoint(Home, 800.0, 0.3, 0.0).Equals(Home, 0.01));
	TestTrue(TEXT("distance 1, angle 0 is the rim on +X"), R::WanderPoint(Home, 800.0, 0.0, 1.0).Equals(FVector(900.0, 200.0, 50.0), 0.01));
	bool bAllInside = true;
	for (int32 I = 0; I <= 10; ++I)
	{
		for (int32 J = 0; J <= 10; ++J)
		{
			const FVector P = R::WanderPoint(Home, 800.0, I / 10.0, J / 10.0);
			bAllInside &= FVector::Dist2D(P, Home) <= 800.0 + 0.01 && FMath::IsNearlyEqual(P.Z, Home.Z);
		}
	}
	TestTrue(TEXT("every roam goal is inside the radius, at home height"), bAllInside);

	// ── Rare spawns ──────────────────────────────────────────────────────
	TestTrue(TEXT("20%: a roll of 0.19 is rare"), R::RollsRare(0.2, 0.19));
	TestFalse(TEXT("20%: a roll of exactly 0.2 is not"), R::RollsRare(0.2, 0.2));
	TestFalse(TEXT("chance 0 is never rare, even on a roll of 0"), R::RollsRare(0.0, 0.0));
	TestTrue(TEXT("chance 1 is always rare, even on a roll of 1"), R::RollsRare(1.0, 1.0));
	int32 Rares = 0;
	for (int32 I = 0; I < 1000; ++I)
	{
		Rares += R::RollsRare(0.2, (I + 0.5) / 1000.0) ? 1 : 0;
	}
	TestEqual(TEXT("20% of evenly spread rolls are rare, exactly"), Rares, 200);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
