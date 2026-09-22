// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 6b admin API tests.
//
// Two rules, picked on the principle the Phase 3 and 5 tests were: each one
// fails *quietly*, and in a way whose symptom points somewhere else.
//
// A wrong key or a wrong type in the state document does not error. The React
// dashboard reads `zone.npcs` and gets `undefined`, renders an empty map, and
// looks exactly like a server with no enemies in it — so the bug gets filed
// against the spawner, or against replication, or against the zone the
// designer happened to be looking at. The wire format is a contract with
// software nobody is going to rebuild, and a contract that is not written down
// twice is not a contract.
//
// A wrong cm conversion does not error either, and this is the worse of the
// two. `L_Desert` is loaded at world X = +40000, so a conversion that dropped
// the zone offset would put every desert click forty metres outside the level
// — which is visible. But a conversion that dropped it in only *one* direction
// round-trips wrongly and quietly: the dashboard shows an NPC at the right
// place, you right-click two centimetres away to spawn another, and it lands
// in the grasslands. The round trip is the property that matters, so the round
// trip is what is tested.
//
// Neither needs a world, a level or a net driver: `BuildStateJson` takes a
// plain snapshot struct and `ToZoneLocalCm`/`FromZoneLocalCm` take a plain
// FBox, for exactly this reason.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "ValhallaAdminServer.h"
#include "ValhallaZoneTypes.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaAdminTests
{
	/**
	 * A snapshot with one of everything and one empty zone.
	 *
	 * Hand-built rather than captured from a running server, for the reason the
	 * Phase 3 overlay sample is inline: a fixture taken from the world would be
	 * a test of whatever the world happened to contain that day. This is the
	 * shape, and the shape is what `AdminDashboard.tsx` was written against.
	 *
	 * The fractional values are deliberate — 1.0 put every coordinate and vital
	 * through `Math.round` before serialising, and the dashboard prints them
	 * raw into tooltips, so the rounding is part of the contract.
	 */
	static FValhallaAdminSnapshot MakeSampleSnapshot()
	{
		FValhallaAdminSnapshot Snapshot;
		Snapshot.bOnline = true;

		FValhallaAdminZoneSnapshot Grasslands;

		FValhallaAdminPlayerInfo Player;
		Player.SessionId = TEXT("256");
		Player.Name      = TEXT("Gandalf");
		Player.ClassId   = TEXT("wizard");
		Player.Level     = 3;
		Player.X         = 672.4;   // rounds down
		Player.Y         = 1023.5;  // rounds up
		Player.Hp        = 87.6;
		Player.MaxHp     = 120.0;
		Player.Mana      = 45.2;
		Player.MaxMana   = 200.0;
		Player.bAlive    = true;
		Grasslands.Players.Add(Player);

		FValhallaAdminNpcInfo Npc;
		Npc.Id         = TEXT("ValhallaNPC_2");
		Npc.TemplateId = TEXT("npc_1771431708366");
		Npc.Name       = TEXT("Field Rat");
		Npc.NpcType    = TEXT("enemy");
		Npc.Level      = 2;
		Npc.X          = 896.0;
		Npc.Y          = 2560.0;
		Npc.Hp         = 0.0;
		Npc.MaxHp      = 40.0;
		Npc.bAlive     = false;
		Grasslands.Npcs.Add(Npc);

		FValhallaAdminLootBagInfo Bag;
		Bag.Id = TEXT("ValhallaLootBag_0");
		Bag.X  = 900.0;
		Bag.Y  = 2570.0;
		FValhallaAdminItemStack Sword;
		Sword.ItemId = TEXT("iron_sword");
		Sword.Quantity = 1;
		Bag.Items.Add(Sword);

		FValhallaAdminItemStack Potions;
		Potions.ItemId = TEXT("health_potion");
		Potions.Quantity = 5;
		Bag.Items.Add(Potions);
		Grasslands.LootBags.Add(Bag);

		FValhallaAdminSpawnPointInfo Point;
		Point.Id = TEXT("ValhallaNPCSpawner_3");
		Point.Label = TEXT("West field 1");
		Point.NpcClass = TEXT("BP_NPC_TestEnemy");
		Point.TemplateId = TEXT("npc_1771431708366");
		Point.X = 640.4;
		Point.Y = 1440.0;
		Point.RespawnSeconds = 45.0;
		Point.bRespawnOverride = true;
		Point.SecondsUntilRespawn = 12.34;
		Point.NpcId = TEXT("ValhallaNPC_2");
		Point.bNpcAlive = false;
		Grasslands.SpawnPoints.Add(Point);

		Snapshot.Zones.Add(TEXT("grasslands"), Grasslands);

		// A zone with nothing in it. 2.0 lists it; 1.0 would not have. The
		// dashboard's zone picker depends on it being here.
		Snapshot.Zones.Add(TEXT("desert"), FValhallaAdminZoneSnapshot());

		return Snapshot;
	}

	/** Fetch a nested object, adding an error and returning null if it is absent. */
	static const TSharedPtr<FJsonObject>* GetObject(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		if (!Parent.IsValid() || !Parent->TryGetObjectField(Field, Out) || !Out || !Out->IsValid())
		{
			Test.AddError(FString::Printf(TEXT("expected an object field '%s'."), Field));
			return nullptr;
		}
		return Out;
	}

	/** Fetch a nested array, adding an error and returning null if it is absent. */
	static const TArray<TSharedPtr<FJsonValue>>* GetArray(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		if (!Parent.IsValid() || !Parent->TryGetArrayField(Field, Out) || !Out)
		{
			Test.AddError(FString::Printf(TEXT("expected an array field '%s'."), Field));
			return nullptr;
		}
		return Out;
	}

	/** Every key the entry must have, and no test for keys it may also have. */
	static void CheckKeys(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Object, const FString& What, const TArray<FString>& Keys)
	{
		if (!Object.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s is not an object."), *What));
			return;
		}
		for (const FString& Key : Keys)
		{
			Test.TestTrue(FString::Printf(TEXT("%s has '%s'"), *What, *Key), Object->HasField(Key));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.Admin.StateJson — the GET /api/admin/state document
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaAdminStateJsonTest,
	"Valhalla.Game.Admin.StateJson",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaAdminStateJsonTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaAdminTests;

	const FValhallaAdminSnapshot Snapshot = MakeSampleSnapshot();
	const TSharedPtr<FJsonObject> Root = UValhallaAdminServer::BuildStateJson(Snapshot);

	// ── The five top-level fields ────────────────────────────────────────
	CheckKeys(*this, Root, TEXT("the state document"),
		{ TEXT("online"), TEXT("zones"), TEXT("totalPlayers"), TEXT("totalNpcs"), TEXT("totalLootBags") });

	bool bOnline = false;
	TestTrue(TEXT("online is a bool"), Root->TryGetBoolField(TEXT("online"), bOnline));
	TestTrue(TEXT("online is true for a live snapshot"), bOnline);

	// The totals are derived, not copied. A handler that summed the wrong
	// thing would still produce a document the dashboard renders, with the
	// header numbers disagreeing with the map — which reads as a replication
	// bug and is not one.
	double Total = 0.0;
	TestTrue(TEXT("totalPlayers is a number"), Root->TryGetNumberField(TEXT("totalPlayers"), Total));
	TestEqual(TEXT("totalPlayers counts every zone"), Total, 1.0);
	TestTrue(TEXT("totalNpcs is a number"), Root->TryGetNumberField(TEXT("totalNpcs"), Total));
	TestEqual(TEXT("totalNpcs counts every zone"), Total, 1.0);
	TestTrue(TEXT("totalLootBags is a number"), Root->TryGetNumberField(TEXT("totalLootBags"), Total));
	TestEqual(TEXT("totalLootBags counts every zone"), Total, 1.0);

	// ── zones ────────────────────────────────────────────────────────────
	const TSharedPtr<FJsonObject>* Zones = GetObject(*this, Root, TEXT("zones"));
	if (!Zones) { return false; }

	TestEqual(TEXT("both zones are listed, including the empty one"), (*Zones)->Values.Num(), 2);
	TestTrue(TEXT("the empty zone is present"), (*Zones)->HasField(TEXT("desert")));

	const TSharedPtr<FJsonObject>* Grasslands = GetObject(*this, *Zones, TEXT("grasslands"));
	if (!Grasslands) { return false; }

	CheckKeys(*this, *Grasslands, TEXT("a zone"), { TEXT("players"), TEXT("npcs"), TEXT("lootBags"), TEXT("spawnPoints") });

	// ── A spawn point entry (2.0 only) ───────────────────────────────────
	if (const TArray<TSharedPtr<FJsonValue>>* Points = GetArray(*this, *Grasslands, TEXT("spawnPoints")))
	{
		TestEqual(TEXT("one spawn point"), Points->Num(), 1);
		if (Points->Num() == 1)
		{
			const TSharedPtr<FJsonObject>& PointObj = (*Points)[0]->AsObject();
			CheckKeys(*this, PointObj, TEXT("a spawn point"),
				{ TEXT("id"), TEXT("label"), TEXT("npcClass"), TEXT("templateId"), TEXT("x"), TEXT("y"),
				  TEXT("respawnSeconds"), TEXT("respawnOverride"), TEXT("respawnIn"), TEXT("npcId"), TEXT("npcAlive") });
			double PointValue = 0.0;
			PointObj->TryGetNumberField(TEXT("x"), PointValue);
			TestEqual(TEXT("spawn point x is rounded"), PointValue, 640.0);
			PointObj->TryGetNumberField(TEXT("respawnIn"), PointValue);
			TestEqual(TEXT("respawnIn keeps one decimal"), PointValue, 12.3);
		}
	}

	// An empty zone must carry three empty *arrays*, not be an empty object:
	// `zone.npcs.map(...)` on undefined is a thrown TypeError and a blank panel.
	const TSharedPtr<FJsonObject>* Desert = GetObject(*this, *Zones, TEXT("desert"));
	if (Desert)
	{
		const TArray<TSharedPtr<FJsonValue>>* EmptyNpcs = GetArray(*this, *Desert, TEXT("npcs"));
		TestTrue(TEXT("an empty zone's npcs is an empty array, not absent"), EmptyNpcs && EmptyNpcs->Num() == 0);
		TestTrue(TEXT("an empty zone has players"), (*Desert)->HasField(TEXT("players")));
		TestTrue(TEXT("an empty zone has lootBags"), (*Desert)->HasField(TEXT("lootBags")));
	}

	// ── A player entry ───────────────────────────────────────────────────
	const TArray<TSharedPtr<FJsonValue>>* Players = GetArray(*this, *Grasslands, TEXT("players"));
	if (!Players || Players->Num() != 1)
	{
		AddError(TEXT("expected exactly one player in grasslands."));
		return false;
	}

	const TSharedPtr<FJsonObject>& PlayerObj = (*Players)[0]->AsObject();
	CheckKeys(*this, PlayerObj, TEXT("a player"),
		{ TEXT("sessionId"), TEXT("name"), TEXT("classId"), TEXT("level"),
		  TEXT("x"), TEXT("y"), TEXT("hp"), TEXT("maxHp"),
		  TEXT("mana"), TEXT("maxMana"), TEXT("alive"),
		  TEXT("zoneId"), TEXT("godMode"), TEXT("frozen"), TEXT("mutedSeconds"),
		  TEXT("account"), TEXT("userId") });

	// sessionId is a *string*, even though 2.0's is a number underneath.
	// The dashboard uses it as an object key and posts it back verbatim.
	FString SessionId;
	TestTrue(TEXT("sessionId is a string"), PlayerObj->TryGetStringField(TEXT("sessionId"), SessionId));
	TestEqual(TEXT("sessionId round-trips"), SessionId, FString(TEXT("256")));

	double Number = 0.0;
	PlayerObj->TryGetNumberField(TEXT("x"), Number);
	TestEqual(TEXT("x is rounded, as Math.round did"), Number, 672.0);
	PlayerObj->TryGetNumberField(TEXT("y"), Number);
	TestEqual(TEXT("y rounds half up, as Math.round did"), Number, 1024.0);
	PlayerObj->TryGetNumberField(TEXT("hp"), Number);
	TestEqual(TEXT("hp is rounded"), Number, 88.0);

	// ── An NPC entry ─────────────────────────────────────────────────────
	const TArray<TSharedPtr<FJsonValue>>* Npcs = GetArray(*this, *Grasslands, TEXT("npcs"));
	if (!Npcs || Npcs->Num() != 1)
	{
		AddError(TEXT("expected exactly one NPC in grasslands."));
		return false;
	}

	const TSharedPtr<FJsonObject>& NpcObj = (*Npcs)[0]->AsObject();
	CheckKeys(*this, NpcObj, TEXT("an NPC"),
		{ TEXT("id"), TEXT("templateId"), TEXT("name"), TEXT("npcType"),
		  TEXT("level"), TEXT("x"), TEXT("y"), TEXT("hp"), TEXT("maxHp"), TEXT("alive") });

	bool bAlive = true;
	TestTrue(TEXT("alive is a bool"), NpcObj->TryGetBoolField(TEXT("alive"), bAlive));
	TestFalse(TEXT("a dead NPC is still listed, marked dead"), bAlive);

	FString NpcType;
	NpcObj->TryGetStringField(TEXT("npcType"), NpcType);
	TestEqual(TEXT("npcType uses the 1.0 spelling"), NpcType, FString(TEXT("enemy")));

	// ── A loot bag entry ─────────────────────────────────────────────────
	const TArray<TSharedPtr<FJsonValue>>* Bags = GetArray(*this, *Grasslands, TEXT("lootBags"));
	if (!Bags || Bags->Num() != 1)
	{
		AddError(TEXT("expected exactly one loot bag in grasslands."));
		return false;
	}

	const TSharedPtr<FJsonObject>& BagObj = (*Bags)[0]->AsObject();
	CheckKeys(*this, BagObj, TEXT("a loot bag"),
		{ TEXT("id"), TEXT("x"), TEXT("y"), TEXT("itemCount"), TEXT("items") });

	BagObj->TryGetNumberField(TEXT("itemCount"), Number);
	TestEqual(TEXT("itemCount is the length of items, not the slot cap"), Number, 2.0);

	const TArray<TSharedPtr<FJsonValue>>* Items = GetArray(*this, BagObj, TEXT("items"));
	if (Items && Items->Num() == 2)
	{
		const TSharedPtr<FJsonObject>& ItemObj = (*Items)[0]->AsObject();
		CheckKeys(*this, ItemObj, TEXT("a bag item"), { TEXT("itemId"), TEXT("quantity") });

		FString ItemId;
		ItemObj->TryGetStringField(TEXT("itemId"), ItemId);
		TestEqual(TEXT("itemId"), ItemId, FString(TEXT("iron_sword")));

		const TSharedPtr<FJsonObject>& SecondObj = (*Items)[1]->AsObject();
		SecondObj->TryGetNumberField(TEXT("quantity"), Number);
		TestEqual(TEXT("quantity"), Number, 5.0);
	}
	else
	{
		AddError(TEXT("expected two items in the bag."));
	}

	// ── The offline document ─────────────────────────────────────────────
	//
	// The dashboard's "server offline" state reads `online === false` and then
	// still iterates `zones`, so an offline document has to be shaped the same
	// way with nothing in it — which is what 1.0 returned when there was no
	// room.
	{
		const FValhallaAdminSnapshot Offline;
		const TSharedPtr<FJsonObject> OfflineJson = UValhallaAdminServer::BuildStateJson(Offline);

		bool bOfflineFlag = true;
		TestTrue(TEXT("offline: online is present"), OfflineJson->TryGetBoolField(TEXT("online"), bOfflineFlag));
		TestFalse(TEXT("offline: online is false"), bOfflineFlag);

		const TSharedPtr<FJsonObject>* OfflineZones = GetObject(*this, OfflineJson, TEXT("zones"));
		TestTrue(TEXT("offline: zones is an empty object, not absent"), OfflineZones && (*OfflineZones)->Values.Num() == 0);

		double Zero = -1.0;
		OfflineJson->TryGetNumberField(TEXT("totalPlayers"), Zero);
		TestEqual(TEXT("offline: totalPlayers is 0"), Zero, 0.0);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.Admin.CmConversion — world <-> zone-local centimetres
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaAdminCmConversionTest,
	"Valhalla.Game.Admin.CmConversion",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaAdminCmConversionTest::RunTest(const FString& /*Parameters*/)
{
	constexpr double Tolerance = 1e-6;

	// The two real zones, as the level builders place them: 4096 cm square
	// (64 tiles of 64 cm — PLAN.md, Phase 3), the desert offset +40000 cm on X
	// so the two boxes cannot touch. The offset is the whole reason this test
	// exists, so it is the offset the test uses.
	FValhallaZoneDef Grasslands;
	Grasslands.ZoneId = TEXT("grasslands");
	Grasslands.Bounds = FBox(FVector(0.0, 0.0, 0.0), FVector(4096.0, 4096.0, 512.0));

	FValhallaZoneDef Desert;
	Desert.ZoneId = TEXT("desert");
	Desert.Bounds = FBox(FVector(40000.0, 0.0, 0.0), FVector(44096.0, 4096.0, 512.0));

	// ── The zone origin is the box's min corner ──────────────────────────
	{
		const FVector2D Origin = UValhallaAdminServer::ToZoneLocalCm(Grasslands, FVector(0.0, 0.0, 0.0));
		TestEqual(TEXT("grasslands min corner is local (0, 0) in X"), Origin.X, 0.0);
		TestEqual(TEXT("grasslands min corner is local (0, 0) in Y"), Origin.Y, 0.0);

		const FVector2D DesertOrigin = UValhallaAdminServer::ToZoneLocalCm(Desert, FVector(40000.0, 0.0, 0.0));
		TestEqual(TEXT("desert min corner is local (0, 0) in X, offset removed"), DesertOrigin.X, 0.0);
		TestEqual(TEXT("desert min corner is local (0, 0) in Y"), DesertOrigin.Y, 0.0);
	}

	// ── The far corner is the zone's size, in both zones ─────────────────
	{
		const FVector2D Far = UValhallaAdminServer::ToZoneLocalCm(Desert, FVector(44096.0, 4096.0, 0.0));
		TestEqual(TEXT("desert max corner is local (4096, 4096) in X"), Far.X, 4096.0);
		TestEqual(TEXT("desert max corner is local (4096, 4096) in Y"), Far.Y, 4096.0);
	}

	// ── The same local coordinate means two different world points ───────
	//
	// The property a single-zone test cannot see, and the one that breaks when
	// a handler forgets which zone it was given.
	{
		const FVector InGrass = UValhallaAdminServer::FromZoneLocalCm(Grasslands, 672.0, 672.0, 0.0);
		const FVector InDesert = UValhallaAdminServer::FromZoneLocalCm(Desert, 672.0, 672.0, 0.0);

		TestEqual(TEXT("grasslands (672, 672) is world X 672"), InGrass.X, 672.0);
		TestEqual(TEXT("desert (672, 672) is world X 40672"), InDesert.X, 40672.0);
		TestEqual(TEXT("both are world Y 672"), InGrass.Y, InDesert.Y);
		TestTrue(TEXT("40000 cm apart, which is the streaming offset"),
			FMath::IsNearlyEqual(InDesert.X - InGrass.X, 40000.0, Tolerance));
	}

	// ── The round trip, which is the actual contract ─────────────────────
	//
	// Every admin coordinate makes this trip: the dashboard is *sent* zone-local
	// cm by /state and *sends back* zone-local cm to spawn-npc. A conversion
	// that is wrong in one direction only is invisible until something is
	// placed, and then it is placed in the wrong zone.
	{
		const TArray<FVector2D> Samples = {
			FVector2D(0.0, 0.0),
			FVector2D(672.0, 672.0),
			FVector2D(4096.0, 4096.0),
			FVector2D(1.5, 4094.25),
			// Outside the box on purpose: the conversion is arithmetic, not a
			// clamp, and a handler that silently clamped would move a spawn
			// request rather than reporting it.
			FVector2D(-250.0, 5000.0),
		};

		for (const FVector2D& Local : Samples)
		{
			for (const FValhallaZoneDef* Zone : { &Grasslands, &Desert })
			{
				const FVector World = UValhallaAdminServer::FromZoneLocalCm(*Zone, Local.X, Local.Y, 8.0);
				const FVector2D Back = UValhallaAdminServer::ToZoneLocalCm(*Zone, World);

				TestTrue(
					FString::Printf(TEXT("%s: (%.2f, %.2f) survives local -> world -> local in X"),
						*Zone->ZoneId.ToString(), Local.X, Local.Y),
					FMath::IsNearlyEqual(Back.X, Local.X, Tolerance));

				TestTrue(
					FString::Printf(TEXT("%s: (%.2f, %.2f) survives local -> world -> local in Y"),
						*Zone->ZoneId.ToString(), Local.X, Local.Y),
					FMath::IsNearlyEqual(Back.Y, Local.Y, Tolerance));

				// The Z offset is the *only* thing that comes out of nowhere,
				// and it is measured from the box floor, not from zero.
				TestTrue(
					FString::Printf(TEXT("%s: Z is the zone floor plus the offset"), *Zone->ZoneId.ToString()),
					FMath::IsNearlyEqual(World.Z, Zone->Bounds.Min.Z + 8.0, Tolerance));
			}
		}
	}

	// ── World -> local -> world, the other way round ─────────────────────
	{
		const FVector WorldPoint(41234.5, 2345.75, 90.0);
		const FVector2D Local = UValhallaAdminServer::ToZoneLocalCm(Desert, WorldPoint);
		const FVector Back = UValhallaAdminServer::FromZoneLocalCm(Desert, Local.X, Local.Y, WorldPoint.Z - Desert.Bounds.Min.Z);

		TestTrue(TEXT("world -> local -> world preserves X"), FMath::IsNearlyEqual(Back.X, WorldPoint.X, Tolerance));
		TestTrue(TEXT("world -> local -> world preserves Y"), FMath::IsNearlyEqual(Back.Y, WorldPoint.Y, Tolerance));
		TestTrue(TEXT("world -> local -> world preserves Z"), FMath::IsNearlyEqual(Back.Z, WorldPoint.Z, Tolerance));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
