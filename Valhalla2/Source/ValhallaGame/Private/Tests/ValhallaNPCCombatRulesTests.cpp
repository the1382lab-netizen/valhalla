// Copyright Valhalla 2.0. All Rights Reserved.
//
// The NPC fight rules (FValhallaNPCCombatRules): when to keep closing, when an
// attack may go off, when the leash sends it back and when it can be pulled
// again on the way.
//
// Each of these fails quietly in play: an archer that walks into melee, one
// that stops and starts on the edge of its range, an NPC that hits you with
// its back turned, or a leashed NPC that is pulled again while still beyond
// its leash and so flips between chasing and returning every step. The world
// half (the turn, the sight trace, the walk back) is checked in PIE; see
// PLAN.md, NPC leash, facing and ranged attacks.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaNPCCombatRules.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaNPCCombatRulesTest,
	"Valhalla.Game.NPC.CombatRules",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaNPCCombatRulesTest::RunTest(const FString& /*Parameters*/)
{
	using R = FValhallaNPCCombatRules;
	const EValhallaNPCAttackType Melee = EValhallaNPCAttackType::Melee;
	const EValhallaNPCAttackType Ranged = EValhallaNPCAttackType::Ranged;

	// ── Closing in ───────────────────────────────────────────────────────
	// Melee: stop 90 cm (30 + two 30 cm capsules), swing from 100.
	TestTrue(TEXT("melee closes from 95 cm"), R::ShouldChase(Melee, 95.0, 90.0, 100.0, true, false));
	TestFalse(TEXT("melee stops at 90 cm"), R::ShouldChase(Melee, 90.0, 90.0, 100.0, true, false));
	TestTrue(TEXT("melee ignores sight (the nav path handles walls)"), R::ShouldChase(Melee, 95.0, 90.0, 100.0, false, false));

	// Ranged: range 660 (600 + capsules).
	TestTrue(TEXT("ranged keeps walking in until 90% of its range"), R::ShouldChase(Ranged, 620.0, 90.0, 660.0, true, false));
	TestFalse(TEXT("then stops"), R::ShouldChase(Ranged, 594.0, 90.0, 660.0, true, false));
	TestFalse(TEXT("once holding, a target stepping back inside its range keeps it holding"), R::ShouldChase(Ranged, 650.0, 90.0, 660.0, true, true));
	TestTrue(TEXT("and one stepping out of range moves it again"), R::ShouldChase(Ranged, 661.0, 90.0, 660.0, true, true));
	TestTrue(TEXT("no sight: ranged walks on, even in range"), R::ShouldChase(Ranged, 300.0, 90.0, 660.0, false, true));
	TestFalse(TEXT("point blank: ranged never walks in to melee"), R::ShouldChase(Ranged, 60.0, 90.0, 660.0, true, false));

	// ── Attacking ────────────────────────────────────────────────────────
	TestTrue(TEXT("in range, in sight, facing, ready: attack"), R::CanAttack(100.0, 100.0, true, true, 5.0, 5.0));
	TestFalse(TEXT("not facing: no attack (the player rule)"), R::CanAttack(100.0, 100.0, true, false, 5.0, 5.0));
	TestFalse(TEXT("out of range: no attack"), R::CanAttack(101.0, 100.0, true, true, 5.0, 5.0));
	TestFalse(TEXT("no sight: no shot"), R::CanAttack(300.0, 660.0, false, true, 5.0, 5.0));
	TestFalse(TEXT("not ready: no attack"), R::CanAttack(100.0, 100.0, true, true, 4.9, 5.0));
	TestTrue(TEXT("point blank shot"), R::CanAttack(60.0, 660.0, true, true, 5.0, 5.0));

	// ── Leash and re-pull ────────────────────────────────────────────────
	TestFalse(TEXT("at the leash range: still fighting"), R::IsBeyondLeash(1200.0, 1200.0));
	TestTrue(TEXT("past it: walk back"), R::IsBeyondLeash(1200.5, 1200.0));
	TestFalse(TEXT("just back inside the leash: cannot be pulled yet"), R::CanRepull(1100.0, 1200.0));
	TestTrue(TEXT("within 75% of it: can be pulled again"), R::CanRepull(900.0, 1200.0));
	TestTrue(TEXT("the re-pull radius is inside the leash, so a re-pull never leashes at once"),
		R::RepullLeashFraction < 1.0 && !R::IsBeyondLeash(1200.0 * R::RepullLeashFraction, 1200.0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
