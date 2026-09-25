// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-10: who answers an NPC's call for help (FValhallaSocialAggro::ShouldAnswer).
//
// Social aggro fails in two opposite ways, both quietly: a rule that is too
// loose turns every pull into a train (a whole camp, or the camp behind the
// wall); one that is too tight makes the checkbox do nothing. Each condition
// is pinned on its own here. The world half (the NPC loop, the sight trace,
// one call per fight) is checked in PIE; see PLAN.md, B-10 social aggro.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaSocialAggro.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaSocialAggroTest,
	"Valhalla.Game.NPC.SocialAggro",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaSocialAggroTest::RunTest(const FString& /*Parameters*/)
{
	using FCandidate = FValhallaSocialAggro::FCandidate;
	const FName Bandits(TEXT("bandits"));
	const double Range = 400.0;

	auto Neighbour = [&]()
	{
		FCandidate C;
		C.Group = Bandits;
		C.DistanceSq = 300.0 * 300.0;
		return C;
	};

	TestTrue(TEXT("a free same-group neighbour in range and in sight answers"),
		FValhallaSocialAggro::ShouldAnswer(Bandits, Range, Neighbour()));

	FCandidate C = Neighbour();
	C.Group = FName(TEXT("cultists"));
	TestFalse(TEXT("another group does not"), FValhallaSocialAggro::ShouldAnswer(Bandits, Range, C));

	C = Neighbour();
	C.DistanceSq = 401.0 * 401.0;
	TestFalse(TEXT("just out of range does not"), FValhallaSocialAggro::ShouldAnswer(Bandits, Range, C));
	C.DistanceSq = 400.0 * 400.0;
	TestTrue(TEXT("exactly at range does"), FValhallaSocialAggro::ShouldAnswer(Bandits, Range, C));

	C = Neighbour();
	C.bLineOfSight = false;
	TestFalse(TEXT("behind a sight blocker does not"), FValhallaSocialAggro::ShouldAnswer(Bandits, Range, C));

	C = Neighbour();
	C.bEngaged = true;
	TestFalse(TEXT("one already fighting does not (which also ends every chain)"), FValhallaSocialAggro::ShouldAnswer(Bandits, Range, C));

	C = Neighbour();
	C.bAlive = false;
	TestFalse(TEXT("a corpse does not"), FValhallaSocialAggro::ShouldAnswer(Bandits, Range, C));

	C = Neighbour();
	C.bCanAggro = false;
	TestFalse(TEXT("one that can never aggro (friendly, or canAggro off) does not"), FValhallaSocialAggro::ShouldAnswer(Bandits, Range, C));

	TestFalse(TEXT("a zero range reaches nobody"), FValhallaSocialAggro::ShouldAnswer(Bandits, 0.0, Neighbour()));
	TestFalse(TEXT("a caller with no group calls nobody"), FValhallaSocialAggro::ShouldAnswer(NAME_None, Range, Neighbour()));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
