// Copyright Valhalla 2.0. All Rights Reserved.
//
// First public test, bug 3: the tester's client was refused 105 times with
// "Invalid target" (auto-attack with nothing selected, mostly), shown only as
// a combat-log line.
//
//   Valhalla.Game.Combat.AttackRefusals — UValhallaCombatLibrary::
//     WhyNotAttackableFrom names each rule: no target, yourself, not a
//     combatant, dead, a friendly NPC by name, another player; empty for a
//     living enemy.
//   Valhalla.UI.FailureFloaterMerge — the HUD merges a repeated refusal within
//     a second into the floater already up; digits do not count (a cooldown's
//     countdown is the same refusal).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaGameHUDWidget.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaAttackRefusalsTest, "Valhalla.Game.Combat.AttackRefusals", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaAttackRefusalsTest::RunTest(const FString& /*Parameters*/)
{
	auto Why = [](TFunctionRef<void(FValhallaAttackFacts&)> Set)
	{
		// A player attacking a living, hostile NPC; each case changes one thing.
		FValhallaAttackFacts Facts;
		Facts.bHasTarget = true;
		Facts.bTargetIsCombatant = true;
		Facts.bTargetAlive = true;
		Facts.bHostile = true;
		Facts.bAttackerIsPlayer = true;
		Set(Facts);
		return UValhallaCombatLibrary::WhyNotAttackableFrom(Facts);
	};

	TestEqual(TEXT("a living enemy: allowed"), Why([](FValhallaAttackFacts&) {}), FString());
	TestEqual(TEXT("nothing selected"), Why([](FValhallaAttackFacts& F) { F.bHasTarget = false; }), FString(TEXT("No target selected")));
	TestEqual(TEXT("yourself"), Why([](FValhallaAttackFacts& F) { F.bTargetIsSelf = true; F.bHostile = false; }), FString(TEXT("You can't attack yourself")));
	TestEqual(TEXT("a loot bag or a prop"), Why([](FValhallaAttackFacts& F) { F.bTargetIsCombatant = false; F.bHostile = false; }), FString(TEXT("You can't attack that")));
	TestEqual(TEXT("dead"), Why([](FValhallaAttackFacts& F) { F.bTargetAlive = false; }), FString(TEXT("Target is dead")));
	TestEqual(TEXT("dead wins over not hostile"), Why([](FValhallaAttackFacts& F) { F.bTargetAlive = false; F.bHostile = false; F.bTargetIsFriendlyNpc = true; }), FString(TEXT("Target is dead")));
	TestEqual(TEXT("a friendly NPC, by name"),
		Why([](FValhallaAttackFacts& F) { F.bHostile = false; F.bTargetIsFriendlyNpc = true; F.TargetName = TEXT("Orrin Blackwater"); }),
		FString(TEXT("You can't attack Orrin Blackwater")));
	TestEqual(TEXT("a friendly NPC with no name"),
		Why([](FValhallaAttackFacts& F) { F.bHostile = false; F.bTargetIsFriendlyNpc = true; }), FString(TEXT("You can't attack that")));
	TestEqual(TEXT("another player"),
		Why([](FValhallaAttackFacts& F) { F.bHostile = false; F.bTargetIsPlayer = true; }), FString(TEXT("You can't attack other players")));
	TestEqual(TEXT("not hostile, no other reason"),
		Why([](FValhallaAttackFacts& F) { F.bHostile = false; F.bAttackerIsPlayer = false; }), FString(TEXT("You can't attack that")));

	// The live function on no actors at all.
	TestEqual(TEXT("live: null target"), UValhallaCombatLibrary::WhyNotAttackable(nullptr, nullptr), FString(TEXT("No target selected")));
	TestEqual(TEXT("live: help with no target"), UValhallaCombatLibrary::WhyNotHelpable(nullptr), FString(TEXT("No target selected")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaFailureFloaterMergeTest, "Valhalla.UI.FailureFloaterMerge", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaFailureFloaterMergeTest::RunTest(const FString& /*Parameters*/)
{
	using W = UValhallaGameHUDWidget;
	TestEqual(TEXT("digits dropped"), W::FailureMergeKey(TEXT("On cooldown (6s)")), FString(TEXT("On cooldown (s)")));
	TestEqual(TEXT("cooldown countdown is one refusal"), W::FailureMergeKey(TEXT("On cooldown (5s)")), W::FailureMergeKey(TEXT("On cooldown (6s)")));

	const FString Key = W::FailureMergeKey(TEXT("No target selected"));
	TestTrue(TEXT("same reason 0.15 s later merges"), W::ShouldMergeFailure(Key, 10.15, Key, 10.0));
	TestFalse(TEXT("same reason 1.2 s later is new"), W::ShouldMergeFailure(Key, 11.2, Key, 10.0));
	TestFalse(TEXT("another reason is new"), W::ShouldMergeFailure(W::FailureMergeKey(TEXT("Target is dead")), 10.1, Key, 10.0));
	TestFalse(TEXT("nothing shown yet"), W::ShouldMergeFailure(Key, 0.0, FString(), -1000.0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
