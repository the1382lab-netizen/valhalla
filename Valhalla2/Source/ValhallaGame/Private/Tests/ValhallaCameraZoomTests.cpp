// Copyright Valhalla 2.0. All Rights Reserved.
//
//   Valhalla.Game.Camera.StartZoom — AValhallaZoneAtmosphere::StartArmLengthFor:
//     a player logs in and enters every zone fully zoomed out, at the zone's
//     camera limit, or the character's own maximum when the zone sets none
//     (Kevin, 2026-09-25). When it happens (a new pawn, a zone change) is
//     checked in PIE: the "camera: zoomed out to" log line.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaCharacter.h"
#include "ValhallaZoneAtmosphere.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaCameraStartZoomTest, "Valhalla.Game.Camera.StartZoom", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaCameraStartZoomTest::RunTest(const FString& /*Parameters*/)
{
	TestEqual(TEXT("no zone limit: the character's maximum"), AValhallaZoneAtmosphere::StartArmLengthFor(0.f), AValhallaCharacter::CameraArmMax);
	TestEqual(TEXT("a zone limit: that limit"), AValhallaZoneAtmosphere::StartArmLengthFor(1800.f), 1800.f);
	TestEqual(TEXT("a limit past the character's maximum is capped"), AValhallaZoneAtmosphere::StartArmLengthFor(9000.f), AValhallaCharacter::CameraArmMax);
	TestEqual(TEXT("a limit under the minimum is raised to it"), AValhallaZoneAtmosphere::StartArmLengthFor(100.f), AValhallaCharacter::CameraArmMin);
	TestTrue(TEXT("fully out is further than the old 1500 cm start"), AValhallaZoneAtmosphere::StartArmLengthFor(0.f) > 1500.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
