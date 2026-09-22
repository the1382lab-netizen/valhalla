// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 2b rules tests.
//
// These three rules are the ones most likely to be quietly broken by a later
// edit, because each is a small piece of bookkeeping whose failure looks like a
// balance problem rather than a bug: a cooldown group that stops being shared
// just makes a class feel stronger, a stack that replaces instead of stacking
// just makes a DoT feel weaker, and a falloff that reaches 0 at the edge instead
// of 0.35 just makes AoE feel bad at range. None of them would crash anything.
//
// Every function under test is a static on UValhallaCombatLibrary that takes no
// world, no actors and no subsystem, which is what lets these run in the
// commandlet context with nothing loaded.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaCombatLibrary.h"
#include "ValhallaConstants.h"
#include "ValhallaGameTypes.h"
#include "ValhallaTypes.h"

/** Shared flags. Same shape as the Phase 1 tests, for the same reasons. */
#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaGameTests
{
	static constexpr double Tolerance = 1e-6;

	/** A minimal skill template, since these rules only read a handful of fields. */
	static FValhallaSkillTemplate MakeSkill(FName Id, float CooldownMs, FName CooldownGroup = NAME_None)
	{
		FValhallaSkillTemplate Skill;
		Skill.Id = Id;
		Skill.Name = Id.ToString();
		Skill.CooldownMs = CooldownMs;
		Skill.CooldownGroup = CooldownGroup;
		return Skill;
	}

	/** A buff with the timings the stacking rules actually look at. */
	static FValhallaActiveBuff MakeBuff(FName SkillId, double AppliedAt, double DurationSeconds)
	{
		FValhallaActiveBuff Buff;
		Buff.SkillId = SkillId;
		Buff.Caster = nullptr;
		Buff.AppliedAt = AppliedAt;
		Buff.LastTickAt = AppliedAt;
		Buff.ExpiresAt = AppliedAt + DurationSeconds;
		Buff.Stacks = 1;
		return Buff;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cooldowns — SkillSystem.ts:889 applyCooldown / :716 validateCast
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaSkillCooldownsTest,
	"Valhalla.Game.Skills.Cooldowns",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaSkillCooldownsTest::RunTest(const FString& Parameters)
{
	using namespace ValhallaGameTests;

	// Two skills sharing a group, one that does not, and one with no cooldown.
	const FValhallaSkillTemplate Bash = MakeSkill(TEXT("warrior_shield_bash"), 6000.f, TEXT("warrior_shield"));
	const FValhallaSkillTemplate Wall = MakeSkill(TEXT("warrior_shield_wall"), 60000.f, TEXT("warrior_shield"));
	const FValhallaSkillTemplate Cleave = MakeSkill(TEXT("warrior_cleave"), 4000.f);
	const FValhallaSkillTemplate Melee = MakeSkill(TEXT("melee_attack"), 0.f);

	TMap<FName, FValhallaSkillTemplate> Catalog;
	Catalog.Add(Bash.Id, Bash);
	Catalog.Add(Wall.Id, Wall);
	Catalog.Add(Cleave.Id, Cleave);
	Catalog.Add(Melee.Id, Melee);

	const TArray<FName> ClassSkills = { Melee.Id, Bash.Id, Cleave.Id, Wall.Id };

	auto FindSkill = [&Catalog](FName Id) -> const FValhallaSkillTemplate*
	{
		return Catalog.Find(Id);
	};

	TMap<FName, double> Cooldowns;
	FName Blocking;
	constexpr double Now = 100.0;

	// ── A zero cooldown writes no entry at all ───────────────────────────
	// Not a zero-length cooldown: an entry equal to Now would make the skill
	// briefly un-castable on a client whose clock trailed the server's.
	UValhallaCombatLibrary::ApplyCooldown(Cooldowns, Melee, ClassSkills, FindSkill, Now);
	TestEqual(TEXT("A zero-cooldown skill writes no cooldown entry"), Cooldowns.Num(), 0);

	// ── Casting Shield Bash puts Shield Wall on cooldown too ─────────────
	UValhallaCombatLibrary::ApplyCooldown(Cooldowns, Bash, ClassSkills, FindSkill, Now);

	TestTrue(TEXT("Shield Bash is on cooldown"), Cooldowns.Contains(Bash.Id));
	TestTrue(TEXT("Shield Wall shares the group and is on cooldown"), Cooldowns.Contains(Wall.Id));
	TestFalse(TEXT("Cleave is in no group and is untouched"), Cooldowns.Contains(Cleave.Id));

	// The group member takes the *caster's* skill's expiry, not its own 60 s.
	// Getting this backwards would make a 6 s skill lock a 60 s one for a
	// minute, which is the single most likely way to get cooldown groups wrong.
	TestEqual(TEXT("Shield Wall takes Shield Bash's expiry, not its own 60 s"),
		Cooldowns[Wall.Id], Now + 6.0, Tolerance);
	TestEqual(TEXT("Shield Bash's own expiry is its own cooldown"),
		Cooldowns[Bash.Id], Now + 6.0, Tolerance);

	// ── Reading it back ──────────────────────────────────────────────────
	{
		const double Remaining = UValhallaCombatLibrary::GetCooldownRemaining(
			Cooldowns, Bash, ClassSkills, FindSkill, Now + 1.0, Blocking);
		TestEqual(TEXT("Shield Bash has 5 s left one second in"), Remaining, 5.0, Tolerance);
		TestEqual(TEXT("…and it is blocked by itself"), Blocking, Bash.Id);
	}

	{
		// Shield Wall was never cast, so its own entry is what the group wrote.
		const double Remaining = UValhallaCombatLibrary::GetCooldownRemaining(
			Cooldowns, Wall, ClassSkills, FindSkill, Now + 1.0, Blocking);
		TestEqual(TEXT("Shield Wall is blocked for the same 5 s"), Remaining, 5.0, Tolerance);
	}

	{
		const double Remaining = UValhallaCombatLibrary::GetCooldownRemaining(
			Cooldowns, Cleave, ClassSkills, FindSkill, Now + 1.0, Blocking);
		TestEqual(TEXT("Cleave is ready"), Remaining, 0.0, Tolerance);
		TestEqual(TEXT("…and nothing is blocking it"), Blocking, FName(NAME_None));
	}

	// ── Both come off cooldown together ──────────────────────────────────
	{
		const double BashRemaining = UValhallaCombatLibrary::GetCooldownRemaining(
			Cooldowns, Bash, ClassSkills, FindSkill, Now + 6.0, Blocking);
		const double WallRemaining = UValhallaCombatLibrary::GetCooldownRemaining(
			Cooldowns, Wall, ClassSkills, FindSkill, Now + 6.0, Blocking);

		TestEqual(TEXT("Shield Bash is ready at exactly its expiry"), BashRemaining, 0.0, Tolerance);
		TestEqual(TEXT("Shield Wall is ready at the same moment"), WallRemaining, 0.0, Tolerance);
	}

	// ── Casting the long skill locks the short one for the long time ─────
	Cooldowns.Reset();
	UValhallaCombatLibrary::ApplyCooldown(Cooldowns, Wall, ClassSkills, FindSkill, Now);

	TestEqual(TEXT("Shield Wall's own 60 s applies"), Cooldowns[Wall.Id], Now + 60.0, Tolerance);
	TestEqual(TEXT("Shield Bash inherits the full 60 s from the group"), Cooldowns[Bash.Id], Now + 60.0, Tolerance);

	{
		const double Remaining = UValhallaCombatLibrary::GetCooldownRemaining(
			Cooldowns, Bash, ClassSkills, FindSkill, Now + 10.0, Blocking);
		TestEqual(TEXT("Shield Bash still has 50 s left"), Remaining, 50.0, Tolerance);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Buff stacking — SkillEffectHandler.ts:226 applyBuff
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaBuffStackingTest,
	"Valhalla.Game.Skills.BuffStacking",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaBuffStackingTest::RunTest(const FString& Parameters)
{
	using namespace ValhallaGameTests;

	const FName PoisonId(TEXT("rogue_poison_blade"));

	// ── Replace ──────────────────────────────────────────────────────────
	{
		TArray<FValhallaActiveBuff> Buffs;

		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 0.0, 6.0), EValhallaStackingMode::Replace, 1);
		TestEqual(TEXT("Replace: the first application is a push"), Buffs.Num(), 1);
		TestEqual(TEXT("Replace: it expires at 6 s"), Buffs[0].ExpiresAt, 6.0, Tolerance);

		// Re-applied at 3 s with 6 s of duration: the old buff is dropped, so the
		// new expiry is 3 + 6 = 9, and the three seconds already elapsed are lost.
		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 3.0, 6.0), EValhallaStackingMode::Replace, 1);
		TestEqual(TEXT("Replace: still one buff"), Buffs.Num(), 1);
		TestEqual(TEXT("Replace: the expiry restarts from now"), Buffs[0].ExpiresAt, 9.0, Tolerance);
		TestEqual(TEXT("Replace: stacks stay at 1"), Buffs[0].Stacks, 1);
		TestEqual(TEXT("Replace: the tick clock restarts too"), Buffs[0].AppliedAt, 3.0, Tolerance);
	}

	// ── Stack ────────────────────────────────────────────────────────────
	{
		TArray<FValhallaActiveBuff> Buffs;
		constexpr int32 MaxStacks = 3;

		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 0.0, 6.0), EValhallaStackingMode::Stack, MaxStacks);
		TestEqual(TEXT("Stack: one buff at one stack"), Buffs[0].Stacks, 1);

		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 1.0, 6.0), EValhallaStackingMode::Stack, MaxStacks);
		TestEqual(TEXT("Stack: still one buff entry"), Buffs.Num(), 1);
		TestEqual(TEXT("Stack: two stacks"), Buffs[0].Stacks, 2);
		TestEqual(TEXT("Stack: the duration refreshes, it does not add"), Buffs[0].ExpiresAt, 7.0, Tolerance);

		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 2.0, 6.0), EValhallaStackingMode::Stack, MaxStacks);
		TestEqual(TEXT("Stack: three stacks"), Buffs[0].Stacks, 3);

		// The cap holds however many more times it is applied.
		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 3.0, 6.0), EValhallaStackingMode::Stack, MaxStacks);
		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 4.0, 6.0), EValhallaStackingMode::Stack, MaxStacks);
		TestEqual(TEXT("Stack: maxStacks is a hard cap"), Buffs[0].Stacks, MaxStacks);
		TestEqual(TEXT("Stack: the duration still refreshes at the cap"), Buffs[0].ExpiresAt, 10.0, Tolerance);
	}

	// ── Extend ───────────────────────────────────────────────────────────
	{
		TArray<FValhallaActiveBuff> Buffs;

		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 0.0, 6.0), EValhallaStackingMode::Extend, 1);
		TestEqual(TEXT("Extend: the first application expires at 6 s"), Buffs[0].ExpiresAt, 6.0, Tolerance);

		// Re-applied at 2 s: 4 s remain, and 6 s are added. 2 + 4 + 6 = 12.
		// This is the rule that a reimplementation gets wrong by writing
		// max(remaining, new), which would give 8 here.
		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 2.0, 6.0), EValhallaStackingMode::Extend, 1);
		TestEqual(TEXT("Extend: still one buff"), Buffs.Num(), 1);
		TestEqual(TEXT("Extend: remaining + new, not max(remaining, new)"), Buffs[0].ExpiresAt, 12.0, Tolerance);
		TestEqual(TEXT("Extend: stacks stay at 1"), Buffs[0].Stacks, 1);

		// Extending a buff that has already lapsed adds nothing to the past: the
		// remainder clamps at 0, so it is just a fresh duration from now.
		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 20.0, 6.0), EValhallaStackingMode::Extend, 1);
		TestEqual(TEXT("Extend: an expired buff extends from now, not from the past"),
			Buffs[0].ExpiresAt, 26.0, Tolerance);
	}

	// ── Different skills never interact ──────────────────────────────────
	{
		TArray<FValhallaActiveBuff> Buffs;
		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(PoisonId, 0.0, 6.0), EValhallaStackingMode::Replace, 1);
		UValhallaCombatLibrary::ApplyBuff(Buffs, MakeBuff(TEXT("cleric_shield_of_faith"), 0.0, 15.0), EValhallaStackingMode::Replace, 1);

		TestEqual(TEXT("Two different skills are two buffs"), Buffs.Num(), 2);
		TestNotNull(TEXT("The poison is still findable"), UValhallaCombatLibrary::FindBuff(Buffs, PoisonId, nullptr));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  AoE falloff — SpellProjectileSystem.ts:340 computeFalloff
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaCombatFalloffTest,
	"Valhalla.Game.Combat.Falloff",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaCombatFalloffTest::RunTest(const FString& Parameters)
{
	constexpr float FloatTolerance = 1e-5f;
	const float Min = static_cast<float>(Valhalla::FireballDamageFalloffMin);

	TestEqual(TEXT("The minimum is the ported 0.35"), Min, 0.35f, FloatTolerance);

	// Dead centre: no reduction at all.
	TestEqual(TEXT("Centre of the blast takes full damage"),
		UValhallaCombatLibrary::ComputeFalloff(0.f, 100.f), 1.f, FloatTolerance);

	// Exactly on the edge: the floor, not zero. A target standing at the rim of
	// a fireball still takes 35%, which is the whole point of the minimum.
	TestEqual(TEXT("The edge of the blast takes the 35% floor"),
		UValhallaCombatLibrary::ComputeFalloff(100.f, 100.f), Min, FloatTolerance);

	// Linear in between: halfway is halfway between 1.0 and 0.35.
	TestEqual(TEXT("Halfway out is halfway between 1.0 and the floor"),
		UValhallaCombatLibrary::ComputeFalloff(50.f, 100.f), 1.f - 0.5f * (1.f - Min), FloatTolerance);

	TestEqual(TEXT("A quarter out"),
		UValhallaCombatLibrary::ComputeFalloff(25.f, 100.f), 1.f - 0.25f * (1.f - Min), FloatTolerance);

	TestEqual(TEXT("Three quarters out"),
		UValhallaCombatLibrary::ComputeFalloff(75.f, 100.f), 1.f - 0.75f * (1.f - Min), FloatTolerance);

	// Past the edge: clamped at the floor rather than going negative. Callers
	// are supposed to range-check first, but a falloff that could return a
	// negative number would turn a near miss into a heal.
	TestEqual(TEXT("Beyond the radius clamps to the floor, it does not go negative"),
		UValhallaCombatLibrary::ComputeFalloff(500.f, 100.f), Min, FloatTolerance);

	// A zero or negative radius means "no falloff", not "divide by zero".
	TestEqual(TEXT("A zero radius means no falloff"),
		UValhallaCombatLibrary::ComputeFalloff(10.f, 0.f), 1.f, FloatTolerance);
	TestEqual(TEXT("A negative radius means no falloff"),
		UValhallaCombatLibrary::ComputeFalloff(10.f, -5.f), 1.f, FloatTolerance);

	// The real fireball radius, with the numbers a designer would recognise.
	{
		const float Radius = static_cast<float>(Valhalla::FireballAoeRadius);
		TestEqual(TEXT("Fireball: centre"), UValhallaCombatLibrary::ComputeFalloff(0.f, Radius), 1.f, FloatTolerance);
		TestEqual(TEXT("Fireball: edge"), UValhallaCombatLibrary::ComputeFalloff(Radius, Radius), Min, FloatTolerance);
		TestTrue(TEXT("Fireball: falloff decreases monotonically"),
			UValhallaCombatLibrary::ComputeFalloff(10.f, Radius) > UValhallaCombatLibrary::ComputeFalloff(40.f, Radius));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Facing — controls rework (2026-09-22)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaCombatFacingTest,
	"Valhalla.Game.Combat.Facing",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaCombatFacingTest::RunTest(const FString& Parameters)
{
	// A target 100 cm away at a given bearing (degrees) from an origin.
	auto At = [](const FVector& Origin, double BearingDeg, double Distance = 100.0)
	{
		const double Rad = FMath::DegreesToRadians(BearingDeg);
		return Origin + FVector(FMath::Cos(Rad) * Distance, FMath::Sin(Rad) * Distance, 0.0);
	};

	const FVector Origin(1000.0, -500.0, 90.0);

	TestEqual(TEXT("The half-angle is 60 degrees"), Valhalla::FacingHalfAngleDegrees, 60.0);

	// Facing +X (yaw 0).
	TestTrue(TEXT("Dead ahead is facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, 0.0)));
	TestTrue(TEXT("59 degrees off to the left is facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, 59.0)));
	TestTrue(TEXT("59 degrees off to the right is facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, -59.0)));
	TestFalse(TEXT("61 degrees off is not facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, 61.0)));
	TestFalse(TEXT("61 degrees off the other way is not facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, -61.0)));
	TestFalse(TEXT("Directly behind is not facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, 180.0)));
	TestFalse(TEXT("Square to the side is not facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, 90.0)));

	// A target on top of the actor has no direction to be wrong about.
	TestTrue(TEXT("A target on top of the actor counts as faced"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, Origin));
	TestTrue(TEXT("A target a fraction of a centimetre away counts as faced"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 123.f, Origin + FVector(-0.4, 0.3, 0.0)));

	// 2D only: height difference does not change the answer.
	TestTrue(TEXT("A target far above, dead ahead, is facing (2D)"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, 0.0) + FVector(0.0, 0.0, 5000.0)));

	// The wrap at +-180: facing yaw 170, target at bearing -170 is 20 degrees off.
	TestTrue(TEXT("Across the +-180 wrap, 20 degrees off is facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 170.f, At(Origin, -170.0)));
	TestFalse(TEXT("Across the wrap, behind is not facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 170.f, At(Origin, -10.0)));

	// Non-normalised yaws, as an actor rotation can carry.
	TestTrue(TEXT("A yaw of 360+45 faces a target at 45"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 405.f, At(Origin, 45.0)));

	// A custom half-angle is honoured.
	TestFalse(TEXT("30 degrees off with a 20-degree half-angle is not facing"),
		UValhallaCombatLibrary::IsFacingPoint(Origin, 0.f, At(Origin, 30.0), 20.f));

	// Null actors are never facing.
	TestFalse(TEXT("A null target is not faced"), UValhallaCombatLibrary::IsFacing(nullptr, nullptr));

	TestEqual(TEXT("The message is Kevin's wording"),
		FString(UValhallaCombatLibrary::NotFacingText()), FString(TEXT("You must be facing your target")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
