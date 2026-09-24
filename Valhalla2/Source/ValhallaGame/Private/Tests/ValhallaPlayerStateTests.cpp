// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-07 player-state tests.
//
//   XpToNextLevel      the XP bar's two numbers: a wrong cap or an off-by-one
//                      level only shows up as a bar that never fills.
//   ClientStatsOwnerOnly  the owner's copy of its resolved stats must replicate
//                      to the owner and nobody else; dropping the condition
//                      would hand every client everyone's dodge chance (see
//                      AValhallaPlayerState's class comment).
//
// Neither needs a world: the XP maths is static, and the replication condition
// is read off the class default object the engine builds its RepLayout from.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Net/UnrealNetwork.h"
#include "ValhallaConstants.h"
#include "ValhallaPlayerState.h"
#include "ValhallaStats.h"

// Guarded because the other ValhallaGame test files define the same flags, and
// a unity build puts them in one translation unit.
#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.PlayerState.XpToNextLevel
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaPlayerStateXpToNextLevelTest,
	"Valhalla.Game.PlayerState.XpToNextLevel",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaPlayerStateXpToNextLevelTest::RunTest(const FString& /*Parameters*/)
{
	// stats.ts:199 — 100 * 2^(level - 1), and nothing past the cap.
	TestEqual(TEXT("level 1 needs 100"), AValhallaPlayerState::XpToNextLevelFor(1), 100);
	TestEqual(TEXT("level 2 needs 200"), AValhallaPlayerState::XpToNextLevelFor(2), 200);
	TestEqual(TEXT("level 5 needs 1600"), AValhallaPlayerState::XpToNextLevelFor(5), 1600);
	TestEqual(TEXT("level 24 (last before the cap) needs 100 * 2^23"),
		AValhallaPlayerState::XpToNextLevelFor(Valhalla::MaxLevel - 1), 838860800);
	TestEqual(TEXT("the cap has no next level"), AValhallaPlayerState::XpToNextLevelFor(Valhalla::MaxLevel), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("past the cap has no next level"), AValhallaPlayerState::XpToNextLevelFor(Valhalla::MaxLevel + 5), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("level 0 is treated as level 1"), AValhallaPlayerState::XpToNextLevelFor(0), 100);

	// Agrees with the core function the server levels up with, at every level.
	for (int32 Level = 1; Level < Valhalla::MaxLevel; ++Level)
	{
		TestEqual(*FString::Printf(TEXT("level %d matches Valhalla::Stats::XpRequiredForLevel"), Level),
			AValhallaPlayerState::XpToNextLevelFor(Level),
			FMath::RoundToInt32(Valhalla::Stats::XpRequiredForLevel(Level)));
	}

	// The bar: Xp is within-level, so it is Xp / needed, clamped.
	constexpr float Tolerance = 1e-6f;
	TestEqual(TEXT("empty bar"), AValhallaPlayerState::XpFractionFor(1, 0), 0.f, Tolerance);
	TestEqual(TEXT("half bar"), AValhallaPlayerState::XpFractionFor(1, 50), 0.5f, Tolerance);
	TestEqual(TEXT("level 3: 100 of 400"), AValhallaPlayerState::XpFractionFor(3, 100), 0.25f, Tolerance);
	TestEqual(TEXT("full bar"), AValhallaPlayerState::XpFractionFor(1, 100), 1.f, Tolerance);
	TestEqual(TEXT("over-full clamps to 1"), AValhallaPlayerState::XpFractionFor(1, 150), 1.f, Tolerance);
	TestEqual(TEXT("negative clamps to 0"), AValhallaPlayerState::XpFractionFor(1, -5), 0.f, Tolerance);
	TestEqual(TEXT("the cap reads as a full bar"), AValhallaPlayerState::XpFractionFor(Valhalla::MaxLevel, 0), 1.f, Tolerance);

	// The instance getters read the replicated Level / Xp (defaults 1 / 0).
	const AValhallaPlayerState* Defaults = GetDefault<AValhallaPlayerState>();
	TestEqual(TEXT("default GetXpToNextLevel"), Defaults->GetXpToNextLevel(), 100);
	TestEqual(TEXT("default GetXpFraction"), Defaults->GetXpFraction(), 0.f, Tolerance);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.PlayerState.ClientStatsOwnerOnly
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaPlayerStateClientStatsOwnerOnlyTest,
	"Valhalla.Game.PlayerState.ClientStatsOwnerOnly",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaPlayerStateClientStatsOwnerOnlyTest::RunTest(const FString& /*Parameters*/)
{
	const FProperty* Property = FindFProperty<FProperty>(AValhallaPlayerState::StaticClass(), TEXT("ClientStats"));
	if (!TestNotNull(TEXT("ClientStats is a UPROPERTY"), Property))
	{
		return false;
	}
	TestTrue(TEXT("ClientStats is marked Replicated"), Property->HasAnyPropertyFlags(CPF_Net));

	// The server-authoritative block must stay out of reflection entirely: a
	// UPROPERTY could be replicated by accident; a plain member cannot.
	TestNull(TEXT("Stats is not a UPROPERTY"), FindFProperty<FProperty>(AValhallaPlayerState::StaticClass(), TEXT("Stats")));

	// RepIndex is only assigned once the class's runtime replication data has
	// been set up, which the engine does lazily on first network use. Without
	// this every property reports RepIndex 0 and the registration below trips
	// the engine's duplicate-entry assert (bIsPushBased mismatch) and crashes
	// the editor.
	AValhallaPlayerState::StaticClass()->SetUpRuntimeReplicationData();

	TArray<FLifetimeProperty> Lifetime;
	GetDefault<AValhallaPlayerState>()->GetLifetimeReplicatedProps(Lifetime);

	const uint16 RepIndex = Property->RepIndex;
	TestTrue(TEXT("replication data was set up (ClientStats has a non-zero RepIndex)"), RepIndex != 0);
	const FLifetimeProperty* Entry = Lifetime.FindByPredicate([RepIndex](const FLifetimeProperty& Candidate)
	{
		return Candidate.RepIndex == RepIndex;
	});
	if (!TestNotNull(TEXT("ClientStats is registered in GetLifetimeReplicatedProps"), Entry))
	{
		return false;
	}
	TestEqual(TEXT("ClientStats replicates COND_OwnerOnly"),
		static_cast<int32>(Entry->Condition), static_cast<int32>(COND_OwnerOnly));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
