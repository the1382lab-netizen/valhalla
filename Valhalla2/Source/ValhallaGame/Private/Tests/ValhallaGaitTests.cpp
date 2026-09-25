// Copyright Valhalla 2.0. All Rights Reserved.
//
// Which locomotion cycle a ground speed plays (UValhallaAnimComponent::
// ChooseGait): walk below 200 cm/s, jog at 200 and above, no third gait.
// 200 is measured a hair under 200 (MaxWalkSpeed 200 reads 199.x), so the
// jog starts JogTolerance (4) under it: 196.
//
// The rule fails quietly in play: a threshold off by one comparison makes
// every class (baseSpeed 200) walk instead of jog, and a missing hysteresis
// makes a body hovering at the threshold flicker between the two cycles every
// frame. Another client's view of a body (a simulated proxy) jitters by more
// than GaitHysteresis, so it uses ProxyHysteresis on a smoothed speed. The world half (the clip swap, the play rate, NPCs) is checked in
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
	constexpr float Tolerance = 4.f;
	constexpr float ProxyHysteresis = 40.f;

	auto Gait = [&](float Speed, EValhallaGait Previous)
	{
		return UValhallaAnimComponent::ChooseGait(Speed, Previous, Moving, Jog, Hysteresis, Tolerance);
	};
	auto Check = [this](const TCHAR* What, EValhallaGait Actual, EValhallaGait Expected)
	{
		TestEqual(What, FString(LexToString(Actual)), FString(LexToString(Expected)));
	};

	// From a standstill (and so from a walk, which takes the same branch).
	Check(TEXT("0 is idle"),       Gait(0.f, EValhallaGait::Idle),     EValhallaGait::Idle);
	Check(TEXT("150 walks"),       Gait(150.f, EValhallaGait::Idle),   EValhallaGait::Walk);
	Check(TEXT("195 walks"),       Gait(195.f, EValhallaGait::Idle),   EValhallaGait::Walk);
	Check(TEXT("195.9 walks"),     Gait(195.9f, EValhallaGait::Idle),  EValhallaGait::Walk);
	Check(TEXT("196 jogs (tolerance)"), Gait(196.f, EValhallaGait::Idle), EValhallaGait::Jog);
	Check(TEXT("199.9 (MaxWalkSpeed 200 as measured) jogs"), Gait(199.9f, EValhallaGait::Idle), EValhallaGait::Jog);
	Check(TEXT("exactly 200 jogs"), Gait(200.f, EValhallaGait::Idle),  EValhallaGait::Jog);
	Check(TEXT("250 (rogue) jogs"), Gait(250.f, EValhallaGait::Idle),  EValhallaGait::Jog);
	Check(TEXT("399 jogs"),        Gait(399.f, EValhallaGait::Idle),   EValhallaGait::Jog);
	Check(TEXT("400 jogs (no run gait)"), Gait(400.f, EValhallaGait::Idle), EValhallaGait::Jog);
	Check(TEXT("450 jogs (no run gait)"), Gait(450.f, EValhallaGait::Walk), EValhallaGait::Jog);
	Check(TEXT("walk to 200 jogs at once"), Gait(200.f, EValhallaGait::Walk), EValhallaGait::Jog);
	Check(TEXT("walk to 196 jogs at once"), Gait(196.f, EValhallaGait::Walk), EValhallaGait::Jog);

	// Hysteresis: only the way down is delayed, from the tolerant threshold
	// (196): a jog walks below 196 - 10 = 186.
	Check(TEXT("jogging at 195 keeps jogging"),  Gait(195.f, EValhallaGait::Jog),  EValhallaGait::Jog);
	Check(TEXT("jogging at 190 keeps jogging"),  Gait(190.f, EValhallaGait::Jog),  EValhallaGait::Jog);
	Check(TEXT("jogging at 187 keeps jogging"),  Gait(187.f, EValhallaGait::Jog),  EValhallaGait::Jog);
	Check(TEXT("jogging at 186 keeps jogging"),  Gait(186.f, EValhallaGait::Jog),  EValhallaGait::Jog);
	Check(TEXT("jogging at 185 walks"),          Gait(185.f, EValhallaGait::Jog),  EValhallaGait::Walk);
	Check(TEXT("walking at 195 keeps walking"),  Gait(195.f, EValhallaGait::Walk), EValhallaGait::Walk);
	Check(TEXT("jogging to a stop is idle"),     Gait(0.f, EValhallaGait::Jog),    EValhallaGait::Idle);

	// The idle cutoff.
	Check(TEXT("5 is idle"),   Gait(5.f, EValhallaGait::Walk),  EValhallaGait::Idle);
	Check(TEXT("10 walks"),    Gait(10.f, EValhallaGait::Idle), EValhallaGait::Walk);

	// A negative hysteresis is treated as none, never as an earlier drop
	// (no tolerance here, so the threshold is exactly JogSpeed).
	Check(TEXT("negative hysteresis: jogging at 199 walks"),
		UValhallaAnimComponent::ChooseGait(199.f, EValhallaGait::Jog, Moving, Jog, -50.f, 0.f), EValhallaGait::Walk);
	Check(TEXT("negative hysteresis: jogging at 200 jogs"),
		UValhallaAnimComponent::ChooseGait(200.f, EValhallaGait::Jog, Moving, Jog, -50.f, 0.f), EValhallaGait::Jog);
	// A negative tolerance is none too, never a jog threshold above JogSpeed.
	Check(TEXT("negative tolerance: 200 jogs"),
		UValhallaAnimComponent::ChooseGait(200.f, EValhallaGait::Walk, Moving, Jog, Hysteresis, -50.f), EValhallaGait::Jog);
	Check(TEXT("negative tolerance: 199 walks"),
		UValhallaAnimComponent::ChooseGait(199.f, EValhallaGait::Walk, Moving, Jog, Hysteresis, -50.f), EValhallaGait::Walk);

	// The proxy average (SmoothProxySpeed): one time constant covers 1 - 1/e
	// of the gap; no time, no change; no time constant, the raw speed.
	constexpr float Tau = UValhallaAnimComponent::ProxySpeedTimeConstant;
	TestTrue(TEXT("proxy smoothing time constant is ~0.15 s"), FMath::IsNearlyEqual(Tau, 0.15f));
	TestTrue(TEXT("smoothing: one time constant is 63% of the way"),
		FMath::IsNearlyEqual(UValhallaAnimComponent::SmoothProxySpeed(0.f, 200.f, Tau, Tau), 200.f * (1.f - FMath::Exp(-1.f)), 0.01f));
	TestTrue(TEXT("smoothing: zero delta keeps the average"),
		FMath::IsNearlyEqual(UValhallaAnimComponent::SmoothProxySpeed(150.f, 200.f, 0.f, Tau), 150.f));
	TestTrue(TEXT("smoothing: zero time constant is the raw speed"),
		FMath::IsNearlyEqual(UValhallaAnimComponent::SmoothProxySpeed(150.f, 200.f, 1.f / 60.f, 0.f), 200.f));

	// Another client's view of a body jogging at 200: its replicated speed
	// jitters 200 +- 15 (seen in PIE: Jog->Walk at 147 / 170 with 10 cm/s of
	// hysteresis). With ProxyHysteresis it never leaves the jog once in it,
	// raw or averaged as TickLocomotion does. The same stream with the local
	// GaitHysteresis does drop to a walk, so the stream is a real test.
	{
		FRandomStream Jitter(1382);
		TArray<float> Stream;
		for (int32 i = 0; i < 600; ++i)
		{
			// Uniform 185..215, with the two extremes forced in regularly.
			Stream.Add(i % 50 == 25 ? 185.f : i % 50 == 49 ? 215.f : 200.f + Jitter.FRandRange(-15.f, 15.f));
		}

		auto Leaves = [&](float StreamHysteresis, bool bSmooth, bool& bReachedJog)
		{
			EValhallaGait G = EValhallaGait::Idle;
			float Smoothed = 0.f;
			int32 Left = 0;
			bReachedJog = false;
			for (const float Raw : Stream)
			{
				Smoothed = (!bSmooth || G == EValhallaGait::Idle) ? Raw
					: UValhallaAnimComponent::SmoothProxySpeed(Smoothed, Raw, 1.f / 60.f, Tau);
				const EValhallaGait Next = UValhallaAnimComponent::ChooseGait(Smoothed, G, Moving, Jog, StreamHysteresis, Tolerance);
				if (G == EValhallaGait::Jog && Next != EValhallaGait::Jog)
				{
					++Left;
				}
				bReachedJog |= Next == EValhallaGait::Jog;
				G = Next;
			}
			return Left;
		};

		bool bReached = false;
		TestEqual(TEXT("proxy 200 +- 15, ProxyHysteresis, raw: never leaves the jog"), Leaves(ProxyHysteresis, false, bReached), 0);
		TestTrue(TEXT("proxy 200 +- 15, raw: reaches the jog"), bReached);
		TestEqual(TEXT("proxy 200 +- 15, ProxyHysteresis, smoothed: never leaves the jog"), Leaves(ProxyHysteresis, true, bReached), 0);
		TestTrue(TEXT("proxy 200 +- 15, smoothed: reaches the jog"), bReached);
		TestTrue(TEXT("the same stream with GaitHysteresis 10, raw, does flicker"), Leaves(Hysteresis, false, bReached) > 0);
	}

	// What the game actually reads (Project Settings > Valhalla > Locomotion).
	const UValhallaLocomotionSettings* Settings = GetDefault<UValhallaLocomotionSettings>();
	TestEqual(TEXT("JogSpeed defaults to 200"), Settings->JogSpeed, 200.f);
	TestEqual(TEXT("JogTolerance defaults to 4"), Settings->JogTolerance, 4.f);
	TestEqual(TEXT("GaitHysteresis defaults to 10"), Settings->GaitHysteresis, 10.f);
	TestEqual(TEXT("ProxyHysteresis defaults to 40"), Settings->ProxyHysteresis, 40.f);
	TestEqual(TEXT("MovingSpeed defaults to 10"), Settings->MovingSpeed, 10.f);
	TestTrue(TEXT("play rate matches speed by default"), Settings->bMatchPlayRateToSpeed);
	TestTrue(TEXT("play-rate range is 0.6..1.6"),
		FMath::IsNearlyEqual(Settings->MinPlayRate, 0.6f) && FMath::IsNearlyEqual(Settings->MaxPlayRate, 1.6f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
