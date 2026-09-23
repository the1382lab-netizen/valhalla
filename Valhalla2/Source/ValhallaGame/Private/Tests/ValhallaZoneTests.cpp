// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 3 zone tests.
//
// Two rules, picked on the same principle the Phase 2b and Phase 5 tests were:
// each one fails *quietly*.
//
// A broken overlay parser does not crash. It produces a file with fewer points
// in it than it should, and the symptom is "the desert feels empty" — three
// hours of looking at spawner code before anyone checks whether the loader
// read the file at all. Worse, the failure mode that actually matters is the
// one where it reads a file it should have refused: a 1.0-format overlay, whose
// coordinates are map pixels relative to the map, parsed as if they were
// zone-local centimetres, puts every enemy in roughly the right place and
// slightly wrong, which is indistinguishable from bad level design.
//
// A broken point-in-zone lookup does not crash either. `PlayerState::ZoneId`
// stops following the pawn, and what breaks is `general` chat and the party XP
// split — two features whose failure looks like a netcode problem from every
// angle except the one that would find it.
//
// Both run on plain data: `ParseOverlay` is static and takes a string, and
// `Contains2D` is arithmetic on a box, so neither needs a world, a level or a
// net driver and both run in the commandlet context.
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
	/**
	 * A sample 2.0 overlay, written out in full rather than loaded from disk.
	 *
	 * Inline on purpose: a test that reads `maps/overlays-2.0/grasslands.json`
	 * would pass or fail depending on what the level builder last wrote there,
	 * which makes it a test of the builder and not of the format. This string
	 * is the format, and it is the thing the Phase 6 editor has to agree with.
	 *
	 * It deliberately contains three things that must be *tolerated* — a point
	 * with no `type`, a point with no `id`, and an `enemy_spawn` with no
	 * `templateId` — because "never fatal on bad data" is the rule
	 * `UValhallaDataSubsystem` set in Phase 1a and the overlay loader inherits
	 * it. Six good points, three bad, and the six must survive.
	 */
	const TCHAR* const SampleOverlay = TEXT(R"JSON(
{
  "version": "2.0",
  "units": "cm",
  "zoneId": "grasslands",
  "spawnPoints": [
    { "id": "player_spawn_main", "type": "player_spawn", "x": 1984, "y": 1024, "label": "Town square" },
    { "id": "enemy_field_w", "type": "enemy_spawn", "x": 896, "y": 2560,
      "templateId": "npc_1771431708366", "count": 3, "radius": 320, "label": "West field" },
    { "id": "enemy_ruin", "type": "enemy_spawn", "x": 3264, "y": 3392,
      "templateId": "npc_1771431708366", "count": 2, "radius": 256 },
    { "id": "npc_merchant", "type": "npc_spawn", "x": 2048, "y": 960,
      "templateId": "npc_1771709765831", "count": 1, "radius": 0, "label": "Bjorn the Trader" },
    { "id": "portal_to_desert", "type": "portal", "x": 3968, "y": 2048,
      "targetZone": "desert", "targetEntry": "entry_from_grasslands", "label": "Scorched Desert" },
    { "id": "entry_from_desert", "type": "zone_entry", "x": 3776, "y": 2048,
      "fromZone": "desert", "label": "From Scorched Desert" },
    { "id": "no_type_here", "x": 100, "y": 100 },
    { "type": "enemy_spawn", "x": 200, "y": 200, "templateId": "npc_1771431708366" },
    { "id": "enemy_without_template", "type": "enemy_spawn", "x": 300, "y": 300 }
  ]
}
)JSON");

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
//  Valhalla.Game.Zones.OverlayParse
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaZonesOverlayParseTest,
	"Valhalla.Game.Zones.OverlayParse",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaZonesOverlayParseTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaZoneTests;

	// ── The good file ────────────────────────────────────────────────────
	{
		FValhallaZoneOverlay Overlay;
		TArray<FString> Errors;
		TestTrue(TEXT("a well-formed 2.0 overlay parses"),
			UValhallaZoneSubsystem::ParseOverlay(SampleOverlay, Overlay, Errors));

		TestEqual(TEXT("version"), Overlay.Version, FString(TEXT("2.0")));
		TestEqual(TEXT("units"), Overlay.Units, FString(TEXT("cm")));
		TestEqual(TEXT("zoneId"), Overlay.ZoneId, FName(TEXT("grasslands")));

		// Six good points out of nine entries; the three malformed ones are
		// skipped and each one leaves a line in Errors.
		TestEqual(TEXT("six good points survive"), Overlay.SpawnPoints.Num(), 6);
		TestEqual(TEXT("three malformed points are reported, not fatal"), Errors.Num(), 3);

		// ── Per-type counts ──────────────────────────────────────────────
		TestEqual(TEXT("one player_spawn"),
			Overlay.PointsOfType(EValhallaOverlayPointType::PlayerSpawn).Num(), 1);
		TestEqual(TEXT("two enemy_spawns"),
			Overlay.PointsOfType(EValhallaOverlayPointType::EnemySpawn).Num(), 2);
		TestEqual(TEXT("one npc_spawn"),
			Overlay.PointsOfType(EValhallaOverlayPointType::NpcSpawn).Num(), 1);
		TestEqual(TEXT("one portal"),
			Overlay.PointsOfType(EValhallaOverlayPointType::Portal).Num(), 1);
		TestEqual(TEXT("one zone_entry"),
			Overlay.PointsOfType(EValhallaOverlayPointType::ZoneEntry).Num(), 1);

		// ── The fields a spawner is built from ───────────────────────────
		const TArray<const FValhallaOverlayPoint*> Enemies =
			Overlay.PointsOfType(EValhallaOverlayPointType::EnemySpawn);
		if (Enemies.Num() == 2)
		{
			TestEqual(TEXT("first enemy id"), Enemies[0]->Id, FName(TEXT("enemy_field_w")));
			TestEqual(TEXT("first enemy template"), Enemies[0]->TemplateId, FName(TEXT("npc_1771431708366")));
			TestEqual(TEXT("first enemy count"), Enemies[0]->Count, 3);
			TestEqual(TEXT("first enemy radius"), Enemies[0]->Radius, 320.0);
			TestEqual(TEXT("first enemy x, zone-local cm"), Enemies[0]->X, 896.0);
			TestEqual(TEXT("first enemy y, zone-local cm"), Enemies[0]->Y, 2560.0);
			TestEqual(TEXT("first enemy label"), Enemies[0]->Label, FString(TEXT("West field")));

			// `count` and `radius` are optional and default rather than zeroing.
			TestEqual(TEXT("second enemy count"), Enemies[1]->Count, 2);
		}

		// ── The two fields that replaced 1.0's overloaded templateId ─────
		const TArray<const FValhallaOverlayPoint*> Portals =
			Overlay.PointsOfType(EValhallaOverlayPointType::Portal);
		if (Portals.Num() == 1)
		{
			TestEqual(TEXT("portal targetZone"), Portals[0]->TargetZone, FName(TEXT("desert")));
			TestEqual(TEXT("portal targetEntry"), Portals[0]->TargetEntry, FName(TEXT("entry_from_grasslands")));
			// And it does *not* get a templateId, because 2.0 stopped using one
			// for this. A parser that silently accepted the 1.0 spelling would
			// hide a whole class of stale-file bug.
			TestEqual(TEXT("portal has no templateId"), Portals[0]->TemplateId, FName(NAME_None));
		}

		const TArray<const FValhallaOverlayPoint*> Entries =
			Overlay.PointsOfType(EValhallaOverlayPointType::ZoneEntry);
		if (Entries.Num() == 1)
		{
			TestEqual(TEXT("entry fromZone"), Entries[0]->FromZone, FName(TEXT("desert")));
			TestEqual(TEXT("entry id is what a portal names"), Entries[0]->Id, FName(TEXT("entry_from_desert")));
		}
	}

	// ── Refusals: the three cases where carrying on would misplace everything

	{
		// A 1.0 overlay. It is valid JSON with a `spawnPoints` array, and its
		// coordinates are map pixels — so accepting it would put enemies
		// plausibly, and wrongly, all over the zone.
		const TCHAR* const OneDotZero = TEXT(R"JSON(
{ "version": "1.0.0",
  "spawnPoints": [ { "id": "s1", "type": "player_spawn", "x": 319, "y": 928 } ] }
)JSON");

		FValhallaZoneOverlay Overlay;
		TArray<FString> Errors;
		TestFalse(TEXT("a 1.0 overlay is refused, not reinterpreted"),
			UValhallaZoneSubsystem::ParseOverlay(OneDotZero, Overlay, Errors));
		TestEqual(TEXT("refusing it keeps no points"), Overlay.SpawnPoints.Num(), 0);
		TestTrue(TEXT("and says why"), Errors.Num() > 0);
	}

	{
		const TCHAR* const WrongUnits = TEXT(R"JSON(
{ "version": "2.0", "units": "px", "zoneId": "desert", "spawnPoints": [] }
)JSON");

		FValhallaZoneOverlay Overlay;
		TArray<FString> Errors;
		TestFalse(TEXT("units other than cm are refused"),
			UValhallaZoneSubsystem::ParseOverlay(WrongUnits, Overlay, Errors));
	}

	{
		FValhallaZoneOverlay Overlay;
		TArray<FString> Errors;
		TestFalse(TEXT("garbage is refused without crashing"),
			UValhallaZoneSubsystem::ParseOverlay(TEXT("not json at all"), Overlay, Errors));
	}

	// ── Tolerated: absent `units` means cm, and no points is legitimate ──
	{
		const TCHAR* const Minimal = TEXT(R"JSON(
{ "version": "2.0", "zoneId": "desert", "spawnPoints": [] }
)JSON");

		FValhallaZoneOverlay Overlay;
		TArray<FString> Errors;
		TestTrue(TEXT("absent units is accepted and means cm"),
			UValhallaZoneSubsystem::ParseOverlay(Minimal, Overlay, Errors));
		TestEqual(TEXT("an empty zone is a legitimate thing to author"), Overlay.SpawnPoints.Num(), 0);
	}

	return true;
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
	// The min corner is the overlay's origin, so a point at exactly (0, 0) is
	// the one coordinate the format guarantees is inside. An exclusive test
	// there would make every overlay's first tile homeless.
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
	// This is the whole of the overlay's coordinate contract: an overlay
	// coordinate is centimetres from the box's min corner, so the same numbers
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
