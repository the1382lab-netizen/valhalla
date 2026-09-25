// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 3 zone tests.
//
// One rule, picked on the same principle the Phase 2b and Phase 5 tests were:
// it fails *quietly*.
//
// A broken point-in-zone lookup does not crash. `PlayerState::ZoneId` stops
// following the pawn, and what breaks is `general` chat and the party XP split
// — two features whose failure looks like a netcode problem from every angle
// except the one that would find it.
//
// It runs on plain data: `Contains2D` is arithmetic on a box, so it needs no
// world, level or net driver and runs in the commandlet context.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaZoneSubsystem.h"
#include "ValhallaZoneTypes.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaZoneTests
{
	/** Two 4096 cm zones 40000 cm apart on X, as `L_World` loads them. */
	TArray<FValhallaZoneDef> MakeTwoZones()
	{
		TArray<FValhallaZoneDef> Zones;

		FValhallaZoneDef Grasslands;
		Grasslands.ZoneId = TEXT("grasslands");
		Grasslands.DisplayName = TEXT("Grasslands");
		Grasslands.Bounds = FBox(FVector(0.0, 0.0, 7.6), FVector(4096.0, 4096.0, 1007.6));
		Grasslands.DefaultSpawn = FVector(1984.0, 1024.0, 7.6);
		Zones.Add(Grasslands);

		FValhallaZoneDef Desert;
		Desert.ZoneId = TEXT("desert");
		Desert.DisplayName = TEXT("Scorched Desert");
		Desert.Bounds = FBox(FVector(40000.0, 0.0, 7.6), FVector(44096.0, 4096.0, 1007.6));
		Desert.DefaultSpawn = FVector(40192.0, 2048.0, 7.6);
		Zones.Add(Desert);

		return Zones;
	}

	/** What `UValhallaZoneSubsystem::GetZoneAt` does, over a plain array. */
	const FValhallaZoneDef* ZoneAt(const TArray<FValhallaZoneDef>& Zones, const FVector& Point)
	{
		for (const FValhallaZoneDef& Def : Zones)
		{
			if (Def.Contains2D(Point))
			{
				return &Def;
			}
		}
		return nullptr;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.Zones.LookupByPoint
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaZonesLookupByPointTest,
	"Valhalla.Game.Zones.LookupByPoint",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaZonesLookupByPointTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaZoneTests;

	const TArray<FValhallaZoneDef> Zones = MakeTwoZones();

	// ── Inside ───────────────────────────────────────────────────────────
	{
		const FValhallaZoneDef* Found = ZoneAt(Zones, FVector(2048.0, 2048.0, 90.0));
		if (TestNotNull(TEXT("the middle of the grasslands is in a zone"), Found))
		{
			TestEqual(TEXT("and it is the grasslands"), Found->ZoneId, FName(TEXT("grasslands")));
		}

		Found = ZoneAt(Zones, FVector(42000.0, 1000.0, 90.0));
		if (TestNotNull(TEXT("the middle of the desert is in a zone"), Found))
		{
			TestEqual(TEXT("and it is the desert"), Found->ZoneId, FName(TEXT("desert")));
		}
	}

	// ── Z is ignored ─────────────────────────────────────────────────────
	// A player knocked 50 m into the air, or standing on a roof above the
	// volume's top face, is still in the zone. Testing Z would drop them out of
	// `general` chat mid-fight.
	{
		const FValhallaZoneDef* High = ZoneAt(Zones, FVector(2048.0, 2048.0, 5000.0));
		if (TestNotNull(TEXT("a pawn above the volume is still in the zone"), High))
		{
			TestEqual(TEXT("still the grasslands"), High->ZoneId, FName(TEXT("grasslands")));
		}

		const FValhallaZoneDef* Low = ZoneAt(Zones, FVector(2048.0, 2048.0, -5000.0));
		TestNotNull(TEXT("and so is one below it"), Low);
	}

	// ── Edges are inclusive ──────────────────────────────────────────────
	// The min corner is the zone-local origin, so a point at exactly (0, 0) is
	// the one coordinate the admin API guarantees is inside. An exclusive test
	// there would make the zone's first tile homeless.
	{
		TestNotNull(TEXT("the min corner is inside"), ZoneAt(Zones, FVector(0.0, 0.0, 7.6)));
		TestNotNull(TEXT("the max corner is inside"), ZoneAt(Zones, FVector(4096.0, 4096.0, 7.6)));
	}

	// ── Outside, including the gap between the zones ─────────────────────
	// The 40000 cm offset exists precisely so this gap is empty. A lookup that
	// returned a zone here would mean the boxes had grown — which is the bug
	// `AValhallaZoneVolume::GetZoneBounds` avoids by reading the component's
	// own scaled extent instead of its padded render bounds.
	{
		TestNull(TEXT("the gap between the zones belongs to nobody"),
			ZoneAt(Zones, FVector(20000.0, 2048.0, 7.6)));
		TestNull(TEXT("just west of the grasslands is outside"),
			ZoneAt(Zones, FVector(-1.0, 2048.0, 7.6)));
		TestNull(TEXT("just north of the grasslands is outside"),
			ZoneAt(Zones, FVector(2048.0, 4097.0, 7.6)));
		TestNull(TEXT("beyond the desert is outside"),
			ZoneAt(Zones, FVector(44097.0, 2048.0, 7.6)));
	}

	// ── Zone-local round trip ────────────────────────────────────────────
	// This is the whole of the zone-local coordinate contract (the admin API
	// and the dashboard use it): a coordinate is centimetres from the box's
	// min corner, so the same numbers
	// mean the same tile in both zones however far apart the boxes are. Getting
	// this wrong by using the box *centre* as the origin would put everything
	// half a zone out — which is exactly the kind of error that looks like
	// sloppy level design rather than a bug.
	{
		const FValhallaZoneDef& Desert = Zones[1];

		const FVector World = Desert.FromZoneLocal(192.0, 2048.0, 0.0);
		TestEqual(TEXT("zone-local (192, 2048) in the desert is world X 40192"), World.X, 40192.0);
		TestEqual(TEXT("and world Y 2048"), World.Y, 2048.0);
		TestEqual(TEXT("and sits on the zone floor"), World.Z, 7.6);

		const FVector2D Local = Desert.ToZoneLocal(World);
		TestEqual(TEXT("round trip x"), Local.X, 192.0);
		TestEqual(TEXT("round trip y"), Local.Y, 2048.0);

		// The same local coordinate in the other zone is a different world
		// point, and that is the point.
		const FVector Grass = Zones[0].FromZoneLocal(192.0, 2048.0, 0.0);
		TestEqual(TEXT("the same local point in the grasslands is world X 192"), Grass.X, 192.0);
		TestTrue(TEXT("the two are 40000 cm apart"),
			FMath::IsNearlyEqual(World.X - Grass.X, 40000.0, 0.001));
	}

	// ── The fog renderer's rectangle ─────────────────────────────────────
	{
		const FBox2D Bounds = Zones[0].GetBounds2D();
		TestTrue(TEXT("the zone's 2D bounds are valid"), Bounds.bIsValid);
		TestEqual(TEXT("and 4096 cm wide"), Bounds.GetSize().X, 4096.0);
		TestEqual(TEXT("and 4096 cm tall"), Bounds.GetSize().Y, 4096.0);
	}

	// ── A degenerate zone is not a zone ──────────────────────────────────
	{
		FValhallaZoneDef Empty;
		TestFalse(TEXT("a zone with no id and no box is invalid"), Empty.IsValid());
		TestFalse(TEXT("and contains nothing"), Empty.Contains2D(FVector::ZeroVector));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
