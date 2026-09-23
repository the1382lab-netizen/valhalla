// Copyright Valhalla 2.0. All Rights Reserved.
//
// The weapon damage roll (2.0, 2026-09-22): every auto-attack adds a uniform
// roll in the weapon's [minDamage, maxDamage] — or its flat attackDamage, or
// 1-3 unarmed — to the base constant before strength scaling.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaConstants.h"
#include "ValhallaStats.h"
#include "ValhallaTypes.h"

// Local copy of the suite's flags (each test file defines its own, and with
// unity builds off there is no shared definition to collide with).
#define VALHALLA_DAMAGE_ROLL_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaDamageRollTest, "Valhalla.Core.Stats.DamageRoll",
	VALHALLA_DAMAGE_ROLL_TEST_FLAGS)

bool FValhallaDamageRollTest::RunTest(const FString& /*Parameters*/)
{
	using namespace Valhalla::Stats;

	TestEqual(TEXT("roll 0 is the minimum"), RollWeaponDamage(3.0, 8.0, 0.0), 3.0);
	TestEqual(TEXT("roll 1 is the maximum"), RollWeaponDamage(3.0, 8.0, 1.0), 8.0);
	TestEqual(TEXT("roll 0.5 is the middle"), RollWeaponDamage(3.0, 8.0, 0.5), 5.5);
	TestEqual(TEXT("rolls are clamped"), RollWeaponDamage(3.0, 8.0, 7.0), 8.0);
	TestEqual(TEXT("a fixed weapon (min == max) never varies"), RollWeaponDamage(5.0, 5.0, 0.2), 5.0);
	TestEqual(TEXT("a reversed range is fixed at its max"), RollWeaponDamage(9.0, 4.0, 0.0), 4.0);

	FValhallaItemTemplate Mace;
	Mace.MinDamage = 3.f;
	Mace.MaxDamage = 8.f;
	float Min = 0.f, Max = 0.f;
	Mace.GetDamageRange(Min, Max);
	TestTrue(TEXT("a weapon with a range uses it"), Min == 3.f && Max == 8.f);

	FValhallaItemTemplate OldBow;
	OldBow.AttackDamage = 5.f;
	OldBow.GetDamageRange(Min, Max);
	TestTrue(TEXT("a 1.0 attackDamage becomes a fixed range"), Min == 5.f && Max == 5.f);

	FValhallaNPCTemplate Wolf;
	Wolf.Damage = 10.f;
	Wolf.GetDamageRange(Min, Max);
	TestTrue(TEXT("an NPC with only `damage` hits for it every time"), Min == 10.f && Max == 10.f);
	Wolf.MinDamage = 7.f;
	Wolf.MaxDamage = 13.f;
	Wolf.GetDamageRange(Min, Max);
	TestTrue(TEXT("an NPC with a range rolls in it"), Min == 7.f && Max == 13.f);

	// An armed NPC adds its weapon's roll to its own: Test Enemy 7–13 with an
	// Iron Sword 4–9 hits for 11–22 before mitigation.
	TestEqual(TEXT("armed NPC, both rolls low"), RollNPCMeleeDamage(7.0, 13.0, 0.0, true, 4.0, 9.0, 0.0), 11.0);
	TestEqual(TEXT("armed NPC, both rolls high"), RollNPCMeleeDamage(7.0, 13.0, 1.0, true, 4.0, 9.0, 1.0), 22.0);
	TestEqual(TEXT("the two rolls are independent"), RollNPCMeleeDamage(7.0, 13.0, 0.0, true, 4.0, 9.0, 1.0), 16.0);
	TestEqual(TEXT("unarmed NPC ignores the weapon numbers"), RollNPCMeleeDamage(7.0, 13.0, 1.0, false, 4.0, 9.0, 1.0), 13.0);
	TestEqual(TEXT("an NPC hit is never below 1"), RollNPCMeleeDamage(0.0, 0.0, 0.5, false, 0.0, 0.0, 0.5), 1.0);

	// The whole melee number for a level-1 warrior (strength 20) swinging the
	// Iron Mace (+1 strength): 10 + [3, 8] + 21 * 0.8 = 29.8 .. 34.8 before
	// defense and the floor, against 27.8 .. 29.8 bare-handed.
	const double MaceLow = ComputePhysicalDamage(Valhalla::BaseMeleeDamage + RollWeaponDamage(3.0, 8.0, 0.0), 21.0);
	const double MaceHigh = ComputePhysicalDamage(Valhalla::BaseMeleeDamage + RollWeaponDamage(3.0, 8.0, 1.0), 21.0);
	const double FistHigh = ComputePhysicalDamage(Valhalla::BaseMeleeDamage
		+ RollWeaponDamage(Valhalla::UnarmedMinDamage, Valhalla::UnarmedMaxDamage, 1.0), 20.0);
	TestTrue(TEXT("the mace's worst hit beats the best punch"), MaceLow > FistHigh);
	TestTrue(TEXT("the mace's range is 5 wide"), FMath::IsNearlyEqual(MaceHigh - MaceLow, 5.0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
