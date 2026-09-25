// Copyright Valhalla 2.0. All Rights Reserved.
//
// Which locomotion cycle a ground speed plays (UValhallaAnimComponent::
// ChooseGait): walk below 200 cm/s, jog at 200 and above, no third gait.
//
// The rule fails quietly in play: a threshold off by one comparison makes
// every class (baseSpeed 200) walk instead of jog, and a missing hysteresis
// makes a body hovering at the threshold flicker between the two cycles every
// frame. The world half (the clip swap, the play rate, NPCs) is checked in
// PIE with `valhalla.Speed`; see PLAN.md, Locomotion gaits.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaAnimComponent.h"
#include "ValhallaLocomotionSettings.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaGaitTest,
	"Valhalla.Game.Anim.Gait",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaGaitTest::RunTest(const FString& /*Parameters*/)
{
	constexpr float Moving = 10.f;
	constexpr float Jog = 200.f;
	constexpr float Hysteresis = 10.f;

	auto Gait = [&](float Speed, EValhallaGait Previous)
	{
		return UValhallaAnimComponent::ChooseGait(Speed, Previous, Moving, Jog, Hysteresis);
	};
	auto Check = [this](const TCHAR* What, EValhallaGait Actual, EValhallaGait Expected)
	{
		TestEqual(What, FString(LexToString(Actual)), FString(LexToString(Expected)));
	};

	// From a standstill (and so from a walk, which takes the same branch).
	Check(TEXT("0 is idle"),       Gait(0.f, EValhallaGait::Idle),     EValhallaGait::Idle);
	Check(TEXT("150 walks"),       Gait(150.f, EValhallaGait::Idle),   EValhallaGait::Walk);
	Check(TEXT("199.9 walks"),     Gait(199.9f, EValhallaGait::Idle),  EValhallaGait::Walk);
	Check(TEXT("exactly 200 jogs"), Gait(200.f, EValhallaGait::Idle),  EValhallaGait::Jog);
	Check(TEXT("250 (rogue) jogs"), Gait(250.f, EValhallaGait::Idle),  EValhallaGait::Jog);
	Check(TEXT("399 jogs"),        Gait(399.f, EValhallaGait::Idle),   EValhallaGait::Jog);
	Check(TEXT("400 jogs (no run gait)"), Gait(400.f, EValhallaGait::Idle), EValhallaGait::Jog);
	Check(TEXT("450 jogs (no run gait)"), Gait(450.f, EValhallaGait::Walk), EValhallaGait::Jog);
	Check(TEXT("walk to 200 jogs at once"), Gait(200.f, EValhallaGait::Walk), EValhallaGait::Jog);

	// Hysteresis: only the way down is delayed.
	Check(TEXT("jogging at 195 keeps jogging"),  Gait(195.f, EValhallaGait::Jog),  EValhallaGait::Jog);
	Check(TEXT("jogging at 190 keeps jogging"),  Gait(190.f, EValhallaGait::Jog),  EValhallaGait::Jog);
	Check(TEXT("jogging at 189 walks"),          Gait(189.f, EValhallaGait::Jog),  EValhallaGait::Walk);
	Check(TEXT("walking at 195 keeps walking"),  Gait(195.f, EValhallaGait::Walk), EValhallaGait::Walk);
	Check(TEXT("jogging to a stop is idle"),     Gait(0.f, EValhallaGait::Jog),    EValhallaGait::Idle);

	// The idle cutoff.
	Check(TEXT("5 is idle"),   Gait(5.f, EValhallaGait::Walk),  EValhallaGait::Idle);
	Check(TEXT("10 walks"),    Gait(10.f, EValhallaGait::Idle), EValhallaGait::Walk);

	// A negative hysteresis is treated as none, never as an earlier drop.
	TestEqual(TEXT("negative hysteresis: jogging at 199 walks"),
		FString(LexToString(UValhallaAnimComponent::ChooseGait(199.f, EValhallaGait::Jog, Moving, Jog, -50.f))),
		FString(LexToString(EValhallaGait::Walk)));
	TestEqual(TEXT("negative hysteresis: jogging at 200 jogs"),
		FString(LexToString(UValhallaAnimComponent::ChooseGait(200.f, EValhallaGait::Jog, Moving, Jog, -50.f))),
		FString(LexToString(EValhallaGait::Jog)));

	// What the game actually reads (Project Settings > Valhalla > Locomotion).
	const UValhallaLocomotionSettings* Settings = GetDefault<UValhallaLocomotionSettings>();
	TestEqual(TEXT("JogSpeed defaults to 200"), Settings->JogSpeed, 200.f);
	TestEqual(TEXT("GaitHysteresis defaults to 10"), Settings->GaitHysteresis, 10.f);
	TestEqual(TEXT("MovingSpeed defaults to 10"), Settings->MovingSpeed, 10.f);
	TestTrue(TEXT("play rate matches speed by default"), Settings->bMatchPlayRateToSpeed);
	TestTrue(TEXT("play-rate range is 0.6..1.6"),
		FMath::IsNearlyEqual(Settings->MinPlayRate, 0.6f) && FMath::IsNearlyEqual(Settings->MaxPlayRate, 1.6f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
